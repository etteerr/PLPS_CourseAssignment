#include "../parinclude/Gompi.h"

void Gompi::init_mpi() {
	//MPI init
	int res = MPI_Init(0,0);

	if (res)
		status = ESTATE_UNDEFINEDERROR;

	//Get n processors
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	//Get rank
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

	//Get processor name
	MPI_Get_processor_name(processor_name, &name_length);
}

int Gompi::generateAndDistribute() {
	/* Comunication info:
	 *
	 * Shared Code:
	 * 	None (checkMemory omitted)
	 * 	If world_size==1
	 * 		Create map and exit
	 *
	 * COMM ORDER (MASTER):
	 * 		- GATHER
	 * 		for each node
	 * 			- Send (state)
	 * 			if no error
	 * 				- Send (data)
	 * 				- Recv (state)
	 *
	 * 	COMM ORDER SLAVE:
	 * 		- Gather
	 * 		- Recv (state)
	 * 			if no error
	 * 			- Recv (data)
	 * 			- Send (state)
	 *
	 * 	Shared code:
	 * 		send/recv (Master state) (Also before master alloc)
	 */

	//SHared vars
	uint64 * buffer;

	if (world_size==1) {
		readMap = new GoLMap(mapx, mapy);
		writeMap = new GoLMap(mapx, mapy);

		uint64* buff;
		buff = readMap->get64(0,0);

		createWorldSegment(buff, readMap->getCacheCount64()*mapy);

		return 0;
	}

	//Check if memory is enough on each node
	status = checkMemory();
	if (status)
		return status;


	//Calculate rows per node
	uint64 rowCount = mapy/(uint64)world_size;
	uint64 masterExtra = mapy%(uint64)world_size;

	//Start distribution
	uint64 caches; //Amount of 64 bits per row
	{
		uint64 oversize, sxo, sx;
		sx = mapx;
		if (sx % 128LL!=0) {
			oversize= 128 -(sx % 128);
			sxo = sx + oversize;
			//sxo /= 8;
		}else{
			oversize = 0;
			sxo = sx;
		}
		caches = sxo/128;
	} //Scope out to remove useless variables

	if (world_rank==0) { //********************* MASTER CODE **********************

		//Check if all other nodes have done their work Correctly
		int* sbuffer = new int[world_size]; //Status buffer
		MPI_Gather(&status, 1, MPI_INT, sbuffer, 1, MPI_INT, COMM_MASTER, MPI_COMM_WORLD);
		for(int i=0; i<world_size; i++){
			if (sbuffer[i]!=ESTATE_OK)
				status = ESTATE_ALLOCATIONERROR;
		}
		delete sbuffer;

		//create and send 'their'  data
		buffer = new uint64[rowCount*caches];
		for (int i = 1; i < world_size; i++) {
			//Send ok to recv
			MPI_Send(&status, 1, MPI_INT,i, ECOMM_STATE, MPI_COMM_WORLD);
			if (status==ESTATE_OK){
				if (VERBOSE) printf("Master: Creating segment... (%.5f MB)\n", (float)(rowCount*caches*8)/(1024.0*1024.0));
				//Create world
				createWorldSegment(buffer, rowCount*caches);
				//Send to waiting node
				if (VERBOSE) printf("Master: Sending data to node %i\n", i);
				MPI_Send(buffer,rowCount*caches,MPI_INT64_T, i, ECOMM_DATA, MPI_COMM_WORLD);
				//Get result (Recv directly to ensure no useless datatransfer)
				MPI_Recv(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			}

		}
		delete buffer;

		if (status) {
			printf("one or more nodes failed to receive data.\nError: %i\n", status);
			//Tell the bad news to the others....
			for(int i = 1; i < world_size; i++)
				MPI_Send(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD);

			return status;
		}

		//Create own data
		//THis is after sending data because the buffer Can be HUGE
		readMap = new GoLMap(mapx, masterExtra+rowCount+2);
		writeMap = new GoLMap(mapx, masterExtra+rowCount+2);

		if (VERBOSE) printf("Master: Creating own segment...\n");
		if (readMap->isAllocated() && writeMap->isAllocated()) {
			//Get a pointer to data of row 2 (row 1 is the row from warp, last row is the row from rank 1 machine)
			buffer = readMap->get64(1,0);
			//Fill this memory
			createWorldSegment(buffer, (masterExtra+rowCount)*caches);
		}else
			status = ESTATE_ALLOCATIONERROR;

	}else{ //********************* SLAVE CODE ***********************

		//Init map
		readMap = new GoLMap(mapx, rowCount+2);
		writeMap = new GoLMap(mapx, rowCount+2);

		//Vars
		MPI_Status recvStatus;

		//If allocation failed, cancel recv
		if (!readMap->isAllocated() || !writeMap->isAllocated()) {
			if (VERBOSE) printf("Node %i: Memory Allocation failed.\n", world_rank);
			//Send fail code
			status = ESTATE_ALLOCATIONERROR;
			if (!readMap->isAllocated()) 	readMap = 0;
			if (!writeMap->isAllocated()) 	writeMap = 0;
		}

		MPI_Gather(&status, 1, MPI_INT,0, 1, MPI_INT, COMM_MASTER, MPI_COMM_WORLD);


		//Recieve data
		buffer = readMap->get64(1,0);
		waitForMessage(COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD);
		MPI_Recv(&status, 1, MPI_INT, COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		if (status == ESTATE_OK) {
			waitForMessage(COMM_MASTER, ECOMM_DATA, MPI_COMM_WORLD);
			MPI_Recv(buffer, rowCount*caches, MPI_INT64_T, COMM_MASTER, ECOMM_DATA, MPI_COMM_WORLD, &recvStatus);
			//Send status
			if (recvStatus.MPI_ERROR)
				status = ESTATE_DATARECVERROR;

			if (VERBOSE && !status) printf("Node %i: Data received.\n", world_rank);

			MPI_Send(&status, 1, MPI_INT, COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD);
		}
	}

//**************** COMMON CODE ********************
	//Sync status with master
	//Note: Dropped Bcast for send/recv to be able to waitForMessage
	//      and reducing idle usage.
	if (world_rank) {
		waitForMessage(COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD);
		MPI_Recv(&status, 1, MPI_INT, COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	}else
		for(int i = 1; i < world_size; i++)
			MPI_Send(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD);

	//Check for errors
	if (status) {
		if (GOLVERBOSE) printf("Node %i: ", world_rank);
		if (readMap)  delete readMap;
		if (GOLVERBOSE) printf("Node %i: ", world_rank);
		if (writeMap) delete writeMap;
		if (VERBOSE) printf("Node %i: Error received while receiving data (%i)\n",world_rank,  status);
		return status;
	}

	return status;
}

int Gompi::checkMemory() {
	/* Comm info:
	 * 	Shared code:
	 * 		- Gather (Master) (Gather statussus)
	 * 		- Bcast (Master)  (Broadbast masters decision)
	 */

	uint64 rowCount = mapy/(uint64)world_size;
	uint64 masterExtra = mapy%(uint64)world_size;

	//Get system info
	getsysinfo(&sysnfo);

	//Set status (0 is OK)
	bool terminate = false;
	if(world_rank)
		terminate = (int)(2*GoLMap::getEstMemoryUsageBytes(mapx, rowCount) > sysnfo.freememByte);
	else
		terminate = (int)(2*GoLMap::getEstMemoryUsageBytes(mapx, rowCount+masterExtra) > sysnfo.freememByte);

	//Print error status
	if (terminate) {
		printf("Node %i reports not enough memory (free: %.5f MB, needed: %.5f MB)\n",
				world_rank,
				(float)sysnfo.freememByte/(1024.0*1024.0),
				(float)2*GoLMap::getEstMemoryUsageBytes(mapx, rowCount)/(1024.0*1024.0));
		status = ESTATE_LOWMEMORY;
	}

	//gather status
	int * buffer = 0;
	if (world_rank==0)
		buffer = new int[world_size];

	MPI_Gather(&status, 1, MPI_INT, buffer, 1, MPI_INT, COMM_MASTER, MPI_COMM_WORLD);

	//Check all statuses (as master)
	if (world_rank==0) {
		for (int i = 0; i < world_size; i++) {
			if (buffer[i])
				status = ESTATE_LOWMEMORY;
		}
	}

	//receive statuses (or send if master)
	MPI_Bcast(&status, 1, MPI_CXX_BOOL, COMM_MASTER, MPI_COMM_WORLD);

	return status;
}

void Gompi::waitForMessage(int source, int tag, MPI_Comm comm) {
	if (VERBOSE) printf("Node %i: Waiting for message.\n", world_rank);
	int flag = 0;
	while(true) {
		MPI_Iprobe(source, tag, comm, &flag, MPI_STATUS_IGNORE);
		if (flag)
			break;

		usleep(100000); //Sleep 100ms to reduce system usage
	}
	if (VERBOSE) printf("Node %i: Message received.\n", world_rank);
}

void Gompi::runSolo(uint64 steps) {
}

void Gompi::runSlave(uint64 steps) {
}

void Gompi::runMaster(uint64 steps) {
}

Gompi::Gompi(uint64 x, uint64 y) {
	readMap = writeMap = 0;
	mapx = x; mapy = y;
	status = ESTATE_OK;

	//init mpi
	init_mpi();

	//Check OK
	if (status)
		return;
	//Create and distribute GoL map
	generateAndDistribute();
}

Gompi::~Gompi() {
	MPI_Finalize();
	if (GOLVERBOSE && readMap) if (readMap->isAllocated()) printf("Node %i: ", world_rank);
	if (readMap) delete readMap;
	if (GOLVERBOSE && writeMap) if (writeMap->isAllocated()) printf("Node %i: ", world_rank);
	if (writeMap) delete writeMap;
	if (VERBOSE) printf("Node %i: Bye bye! (%i)\n", world_rank, status);
}

void Gompi::stepEdge(adjData left, uchar* resLeft, adjData right,
		uchar* resRight) {
	uchar el, er;
	charuni l,r;
	l.c = *resLeft;
	r.c = *resRight;

	el = er = 0;

	//This is now a stupid way of doing it (since i created the charunion)
	//But what ever, it works
	//Let us pray for compiler optimization, that it might optimize all >.<"
	el = (*left.up>>1 & 0b00000001)  + (*left.up & 0b00000001) + (*right.up>>7 & 0b00000001) +
		 (*left.mid>>1 & 0b00000001) +                         + (*right.mid>>7 & 0b00000001) +
		 (*left.down>>1 & 0b00000001)+ (*left.down & 0b00000001)+ (*right.down>>7 & 0b00000001);

	er = (*left.up & 0b00000001)  + (*right.up>>7 & 0b00000001)  + (*right.up>>6 & 0b00000001) +
		 (*left.mid & 0b00000001) +                         	 + (*right.mid>>6 & 0b00000001) +
		 (*left.down & 0b00000001)+ (*right.down>>7 & 0b00000001)+ (*right.down>>6 & 0b00000001);




	l.bits.a = ((l.bits.a & (el==2)) | (el==3));
	r.bits.h = ((r.bits.h & (er==2)) | (er==3));

	*resLeft =  l.c;
	*resRight = r.c;
}

void Gompi::step64(uint64& rowchunk, uint64& rowabove, uint64& rowbelow,
		uint64& rowresult) {
	uint64 low, mid, high, c1,c2, a2, a3;
	low=mid=high=0;

	//above
	c1 = low&rowabove; //Always 0
	low ^= rowabove;
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//below
	c1 = low&rowbelow;
	low ^= rowbelow;
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//upperleft
	c1 = low&(rowabove>>1);
	low ^= (rowabove>>1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//upperright
	c1 = low&(rowabove<<1);
	low ^= (rowabove<<1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//lowerleft
	c1 = low&(rowbelow>>1);
	low ^= (rowbelow>>1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//lowerright
	c1 = low&(rowbelow<<1);
	low ^= (rowbelow<<1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//midleft
	c1 = low&(rowchunk>>1);
	low ^= (rowchunk>>1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//midright
	c1 = low&(rowchunk<<1);
	low ^= (rowchunk<<1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;


	//result
	a2 = (~high)&mid&(~low);
	a3 = (~high)&mid&(low);

	/*
	 * if there are 2, nothing happens (if alive, stays alive, if dead stays dead.)
	 * if there are 3, new life or nothing
	 * Apply border mask
	 */
	rowresult = ((a2&rowchunk)|a3)&0b0111111111111111111111111111111111111111111111111111111111111110;
}

void Gompi::step128(__m128i* _rowchunk, __m128i* _rowabove, __m128i* _rowbelow, __m128i* _rowresult) {
	__m128i low, mid, high, c1,c2, a2, a3;
	__m128i rowchunk, rowabove, rowbelow, rowresult;
	low*=0;
	mid*=0;
	high*=0;

	//Load
	rowchunk = _mm_loadu_si128((__m128i*)_rowchunk);
	rowabove = _mm_loadu_si128((__m128i*)_rowabove);
	rowbelow = _mm_loadu_si128((__m128i*)_rowbelow);
	//rowresult = _mm_loadu_si128((__m128i*)_rowresult);

	//above
	c1 = low&rowabove; //Always 0
	low ^= rowabove;
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//below
	c1 = low&rowbelow;
	low ^= rowbelow;
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//upperleft
	c1 = low&(rowabove>>1);
	low ^= (rowabove>>1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//upperright
	c1 = low&(rowabove<<1);
	low ^= (rowabove<<1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//lowerleft
	c1 = low&(rowbelow>>1);
	low ^= (rowbelow>>1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//lowerright
	c1 = low&(rowbelow<<1);
	low ^= (rowbelow<<1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//midleft
	c1 = low&(rowchunk>>1);
	low ^= (rowchunk>>1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;

	//midright
	c1 = low&(rowchunk<<1);
	low ^= (rowchunk<<1);
	c2 = mid & c1;
	mid ^= c1;
	high |= c2;


	//result
	a2 = (~high)&mid&(~low);
	a3 = (~high)&mid&(low);

	/*
	 * if there are 2, nothing happens (if alive, stays alive, if dead stays dead.)
	 * if there are 3, new life or nothing
	 * Apply border mask (64 bit operation)
	 */
	rowresult = ((a2&rowchunk)|a3)&0b0111111111111111111111111111111111111111111111111111111111111110;

	//save result
	_mm_store_si128((__m128i*)_rowresult,rowresult);

	//resolve internal border
	adjData left, right;

	left.up 	= ((uchar *)(((uint64*)_rowabove)+0))+0;
	left.mid 	= ((uchar *)(((uint64*)_rowchunk)+0))+0;
	left.down 	= ((uchar *)(((uint64*)_rowbelow)+0))+0;

	/*
	 * the right part is one uint64 to the right.
	 * Due to little endian, its the last char chunk of the uint64.
	 * uint 64 has 8 char chunks
	 */
	right.up 	= ((uchar *)(((uint64*)_rowabove)+1))+7;
	right.mid 	= ((uchar *)(((uint64*)_rowchunk)+1))+7;
	right.down 	= ((uchar *)(((uint64*)_rowbelow)+1))+7;

	//Repeat for target
	uchar * tl, * tr;
	tl = ((uchar*)((uint64 *) _rowresult+0))+0;
	tr = ((uchar*)((uint64 *) _rowresult+1))+7;

	//call stepEdge
	stepEdge(left, tl, right, tr);
}

void Gompi::stepCMap(GoLMap& map, GoLMap& newmap) {
	{
		//Run 128
		/*
		 * Process map column wise in blocks of 128
		 * The pointers for below, current and above are assigned fifo style from bottom to top
		 * BLoks of 128 are ensured by map (data is appended to 128 bits multiple for every row)
		 */
		#pragma omp parallel for default(none) shared(map, newmap)
		for (uint64 cache = 0; cache < map.getCacheCount128(); cache++) {
			uint64 empty[2];
			empty[0] = 0;
			empty[1] = 0;
			__m128i *above, *below, *current, *target;
			//Bootstrap
			above = (__m128i*)&empty;
			current = (__m128i*)&empty;
			below = map.get128(0,cache);

			//Iterate to last

			for (uint64 row = 0; row < map.getsy()-1; row++){
				//Sort data pointers
				above = current;	//current row is now previous row
				current = below; 	//Next row is now current row
				below = map.get128(row+1,cache);//Get next row
				target = newmap.get128(row, cache); //Get target
				//Process
				step128(current,above,below, target);
			}

			//Last (load empty border)
			above = current;	//current row is now previous row
			current = below; 	//Next row is now current row
			below = (__m128i*)&empty;//Get next row
			target = newmap.get128(map.getsy()-1, cache); //Get target

			//Process final row
			step128(current,above,below, target);


		}
	}
	if (map.getCacheCount128()>1) {
		//Anneal all 128bits borders


		//128/8 = make steps of 128 in 8 bit cache (to get the 128 bits borders)
		#pragma omp parallel for default(none) shared(map, newmap)
		for (uint64 cache = 128/8; cache < map.getCacheCount8(); cache+=128/8) {
			uchar *ltarget;
			uchar *rtarget;
			adjData left, right;
			uchar empty = 0;
			//Bootstrap column
			left.up = &empty;
			left.mid = &empty;
			left.down = map.get8(0,cache);

			right.up = &empty;
			right.mid = &empty;
			right.down = map.get8(0,cache+1);

			for (uint64 row = 0; row < map.getsy()-1; row++)
			{
				//Set pointers
				left.up = left.mid;
				left.mid = left.down;
				left.down = map.get8(row+1, cache);

				right.up = right.mid;
				right.mid = right.down;
				right.down = map.get8(row+1, cache+1);

				ltarget = newmap.get8(row, cache);
				rtarget = newmap.get8(row, cache+1);

				//step
				stepEdge(left, ltarget, right, rtarget);

			}
		}
	}

	//Vertical border should already be interpreted as 0
	//reset the invalid datapoints
	newmap.resetFalseBorder();
}

void Gompi::createWorldSegment(uint64 & seg) {
	seg = 0;
	float j;
	for(int i = 0; i < 64; i++) {
		j = (float)rand()/(float)(RAND_MAX);
		if (j>=0.5)
			seg+=0b1;
		seg <<= 1;
	}
}

void Gompi::createWorldSegment(uint64* buffer, uint64 size) {
#pragma omp parallel for default(none) shared(buffer, size)
	for (uint64 i = 0; i < size; i++)
		createWorldSegment(buffer[i]);
}

void Gompi::getsysinfo(systeminfo* nfo) {
	struct sysinfo systemInfo;
	if (sysinfo(&systemInfo)!=0) {
		nfo=0;
		return;
	}
	nfo->avgload1 = (float)systemInfo.loads[0]/(1<<16);
	nfo->avgload5 = (float)systemInfo.loads[1]/(1<<16);
	nfo->freememInMB = systemInfo.freeram/(1024*1024);
	nfo->freememByte = systemInfo.freeram;
	nfo->cores = sysconf(_SC_NPROCESSORS_ONLN);
}

void Gompi::run(uint64 steps) {
	if (world_size == 1)
		runSolo(steps);
	else
	if (world_rank)
		runSlave(steps);
	else
		runMaster(steps);
}
