#include "../parinclude/Gompi.h"

unsigned long long sendCount = 0;
unsigned long long recvCount = 0;
unsigned long long isendCount= 0;
unsigned long long irecvCount= 0;
unsigned long long bcastCount= 0;
unsigned long long waitCount = 0;

double sendDuration = 0;
double recvDuration = 0;
double isendDuration= 0;
double irecvDuration= 0;
double bcastDuration= 0;
double waitDuration = 0;

double ttime = 0;

void printProfiling() {
	printf("Profiling report:\n");
	printf("\tsendCount: \t%llu\n", sendCount);
	printf("\trecvCount: \t%llu\n", recvCount);
	printf("\tisendCount:\t%llu\n", isendCount);
	printf("\tirecvCount:\t%llu\n", irecvCount);
	printf("\tbcastCount:\t%llu\n", bcastCount);
	printf("\twaitCount: \t%llu\n", waitCount);
	printf("Timings:\n");
	printf("\tsendDuration: \t%.10f\n", sendDuration);
	printf("\trecvDuration: \t%.10f\n", recvDuration);
	printf("\tisendDuration:\t%.10f\n", isendDuration);
	printf("\tirecvDuration:\t%.10f\n", irecvDuration);
	printf("\tbcastDuration:\t%.10f\n", bcastDuration);
	printf("\twaitDuration: \t%.10f\n", waitDuration);
}

int __EMPI_WAIT(MPI_Request * req, MPI_Status *stat) {
	waitCount++;
	ttime=MPI_Wtime();
	int r = MPI_Wait(req, stat);
	waitDuration +=MPI_Wtime()-ttime;
	return r;
}

void printBinary64(int64 &a) {
	for (int i = 0; i < 64; i++)
		if ((a<<i)&0b1000000000000000000000000000000000000000000000000000000000000000)
			printf("X");
		else
			printf("0");
	printf("\n");
}

void printBinary64(int64 *a, int64 c) {
	for (int64 i = 0; i < c; i++)
		printBinary64(a[i]);
}


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

int64 countOnes(int64 * p, int64 c) {
	int64 count = 0;

	for(int64 cache = 0; cache < c; cache++) {
		count += __builtin_popcountll(p[cache]);
	}

	return count;
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
	//Init seed
	srand(GOMPI_SEED);

	//SHared vars
	int64 * buffer;

	if (world_size==1) {
		readMap = new GoLMap(mapx, mapy);
		writeMap = new GoLMap(mapx, mapy);

		int64* buff;
		buff = readMap->get64(0,0);

		createWorldSegment(buff, readMap->getCacheCount64()*mapy);

		//printf("Node %i: Ones %lli", world_rank, countOnes(buff, readMap->getCacheCount64()*mapy));

		return 0;
	}

	//Check if memory is enough on each node
	status = checkMemory();
	if (status)
		return status;


	//Calculate rows per node (done on every node) (So its global)
	int64 rowCount = mapy/(int64)world_size;
	int64 * rowCounts = new int64[world_size];
	int64 rest = mapy%(int64)world_size;
	for (int64 i = 0; i < world_size; i++) {
		rowCounts[i] = rowCount;
		if (rest > 0) {
			rowCounts[i]++;
			rest--;
		}
	}

	//Start distribution
	int64 caches; //Amount of 64 bits per row
	{
		int64 oversize, sxo, sx;
		sx = mapx;
		if (sx % 128LL!=0) {
			oversize= 128 -(sx % 128);
			sxo = sx + oversize;
			//sxo /= 8;
		}else{
			oversize = 0;
			sxo = sx;
		}
		caches = sxo/64;
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
		for (int i = 1; i < world_size; i++) {
			//Send ok to recv
			MPI_Send(&status, 1, MPI_INT,i, ECOMM_STATE, MPI_COMM_WORLD);
			if (status==ESTATE_OK){
				if (VERBOSE) printf("Master: Creating segment... (%.5f MB)\n", (float)(rowCounts[i]*caches*8)/(1024.0*1024.0));
				//Allocate buffer
				buffer = new int64[rowCounts[i]*caches];
				//Create world
				createWorldSegment(buffer, rowCounts[i]*caches);
				//Send to waiting node
				if (VERBOSE) printf("Master: Sending data to node %i (%lli)\n", i, countOnes(buffer, rowCounts[i]*caches));
				MPI_Send(buffer,rowCounts[i]*caches,MPI_INT64_T, i, ECOMM_DATA, MPI_COMM_WORLD);
				//Get result (Recv directly to ensure no useless datatransfer)
				MPI_Recv(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				//Dealloc
				delete buffer;
			}

		}

		if (status) {
			printf("one or more nodes failed to receive data.\nError: %i\n", status);
			//Tell the bad news to the others....
			for(int i = 1; i < world_size; i++)
				MPI_Send(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD);

			return status;
		}

		//Create own data
		//THis is after sending data because the buffer Can be HUGE
		readMap = new GoLMap(mapx, rowCounts[0]+2);
		writeMap = new GoLMap(mapx, rowCounts[0]+2);

		if (VERBOSE) printf("Master: Creating own segment...\n");
		if (readMap->isAllocated() && writeMap->isAllocated()) {
			//Get a pointer to data of row 2 (row 1 is the row from warp, last row is the row from rank 1 machine)
			buffer = readMap->get64(1,0);
			//Fill this memory
			createWorldSegment(buffer, (rowCounts[0])*caches);
		}else
			status = ESTATE_ALLOCATIONERROR;

	}else{ //********************* SLAVE CODE ***********************

		//Init map
		readMap = new GoLMap(mapx, rowCounts[world_rank]+2);
		writeMap = new GoLMap(mapx, rowCounts[world_rank]+2);

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
			MPI_Recv(buffer, rowCounts[world_rank]*caches, MPI_INT64_T, COMM_MASTER, ECOMM_DATA, MPI_COMM_WORLD, &recvStatus);
			//Send status
			if (recvStatus.MPI_ERROR)
				status = ESTATE_DATARECVERROR;

			if (VERBOSE && !status) printf("Node %i: Data received. (%lli)\n", world_rank, countOnes(buffer, rowCounts[world_rank]*caches));

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
		for(int i = 1; i < world_size; i++) {
			if (VERBOSE) printf("Master sending OK message\n");
			MPI_Send(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD);
		}

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

	int64 rowCount = mapy/(int64)world_size;
	int64 masterExtra = mapy%(int64)world_size;

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

void Gompi::runSolo(int64 steps) {
	int64 stepCounter = 0;
	GoLMap * tmp;

	while(status==ESTATE_OK && stepCounter < steps) {
		//Step
		stepGeneral(*readMap, *writeMap, SOLO_PROCESSING_FLAGS);
		//Swap buffers
		tmp = readMap;
		readMap = writeMap;
		writeMap = tmp;
		//Inc step
		stepCounter++;
	}
}


void Gompi::cascadeError() {
	if (status) {
		printf("Node %i: Initiating cascade error (%i)\n", world_rank, status);
		int64 s = world_rank;
		//Let all nodes be errored >:)
		if (world_rank!=0) //Slave
			MPI_Send(&s, 1, MPI_UINT64_T, 0, ECOMM_STATE, MPI_COMM_WORLD);
		else { //Master
			MPI_Request * reqs = new MPI_Request[world_size];
			for (int i = 1; i < world_size; i++) {
				MPI_Isend(&s, 1, MPI_INT64_T, i, ECOMM_STATE, MPI_COMM_WORLD, &reqs[i]);
			}
			printf("Master: Waiting 5 seconds for nodes to respond to error...\n");
			sleep(5);
			int flag;
			for(int i = 1; i< world_size; i++){
				MPI_Test(&reqs[i], &flag, MPI_STATUS_IGNORE);
				if (flag) {
					printf("Master: Node %i received error.", i);
				}else{
					printf("Master: Node %i unreachable.", i);
				}
			}
			delete[] reqs;
		}
	}
}

void Gompi::runMPI(int64 steps) {
	int64 stepCounter = 0;
	GoLMap * tmp;

	while(status==ESTATE_OK && stepCounter < steps) {
		if (VERBOSE) printf("Node %i: Starting step %lli\n", world_rank, stepCounter);
		//Step
		stepGeneral(*readMap, *writeMap, MPI_PROCESSING_FLAGS);
		//Swap buffers
		tmp = readMap;
		readMap = writeMap;
		writeMap = tmp;
		//Inc step
		stepCounter++;
		//sync
		EMPI_Bcast(&stepCounter, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
	}

	//Cascade error
	//cascadeError();
}

Gompi::Gompi(int64 x, int64 y) {
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
	//Clean mpi
	MPI_Finalize();

	//Clean maps
	if (GOLVERBOSE && readMap) if (readMap->isAllocated()) printf("Node %i: ", world_rank);
	delete readMap;
	if (GOLVERBOSE && writeMap) if (writeMap->isAllocated()) printf("Node %i: ", world_rank);
	delete writeMap;
	if (VERBOSE) printf("Node %i: Bye bye! (%i)\n", world_rank, status);
}

void Gompi::stepEdge(adjData left, uchar* resLeft, adjData right,
		uchar* resRight, char shift) {
	uchar el, er;
	charuni l,r;
	l.c = *resLeft;
	r.c = *resRight;

	//Correct padding error
	l.c >>=shift;

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

	*resLeft =  l.c<<shift; //shift back padding correction
	*resRight = r.c;
}

void Gompi::step64(int64& rowchunk, int64& rowabove, int64& rowbelow,
		int64& rowresult) {
	int64 low, mid, high, c1,c2, a2, a3;
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

	left.up 	= ((uchar *)(((int64*)_rowabove)+0))+0;
	left.mid 	= ((uchar *)(((int64*)_rowchunk)+0))+0;
	left.down 	= ((uchar *)(((int64*)_rowbelow)+0))+0;

	/*
	 * the right part is one uint64 to the right.
	 * Due to little endian, its the last char chunk of the uint64.
	 * uint 64 has 8 char chunks
	 */
	right.up 	= ((uchar *)(((int64*)_rowabove)+1))+7;
	right.mid 	= ((uchar *)(((int64*)_rowchunk)+1))+7;
	right.down 	= ((uchar *)(((int64*)_rowbelow)+1))+7;

	//Repeat for target
	uchar * tl, * tr;
	tl = ((uchar*)((int64 *) _rowresult+0))+0;
	tr = ((uchar*)((int64 *) _rowresult+1))+7;

	//call stepEdge
	stepEdge(left, tl, right, tr);
}

void Gompi::stepGeneral(GoLMap& map, GoLMap& newmap, char flags) {
	//Set global flags variable
	bool parallelProcessing= false;
	MPI_Request requestStatus1, requestStatus2;
	MPI_Request	sendStatus1, sendStatus2;

	//set parallel processing
	//Does not affect parallel data recv
	if ((flags&FLAG_STEP_PARALLELPROCESSING)&&mapx>STEP_MP_THRESHOLD_XY&&mapy>STEP_MP_THRESHOLD_XY)
		parallelProcessing = true;


	{
		/*
		 * Structure:
		 * 	Flag handling
		 * 		- Toggle parallel
		 * 		if MPI
		 *	 		- Irecv and Isend TOP/BOT
		 *
		 * 	Process: (column wise threading if parallelProcessing == true)
		 * 		- Rows depending on Flags
		 *
		 * 	If MPI (WAS IN PARALLEL => SEGMENTATION FAULT)
		 * 		- Wait for Recv's to finish
		 * 		- Process them
		 *
		 * 	- Anneal 128 bits borders
		 *
		 * 	- Warp horizontal
		 *
		 * 	- Mask padding
		 */

		//If MPI (And thus ghost rows), start Irecv & send
		if ( (flags & (FLAG_STEP_GHOSTROWS|FLAG_STEP_POLLGHOSTROWS_MPI))
				== (FLAG_STEP_GHOSTROWS|FLAG_STEP_POLLGHOSTROWS_MPI))
		{
			if (VERBOSE) printf("Node %i: sending to ranks (%i) & (%i)\n", world_rank, ((world_rank-1)%world_size+world_size)%world_size, ((world_rank+1)%world_size+world_size)%world_size);

			//Share data with node 'above' and 'below'
			EMPI_Isend(readMap->get64(1,0), readMap->getCacheCount64(), MPI_INT64_T, ((world_rank-1)%world_size+world_size)%world_size, ECOMM_DATA_UP, MPI_COMM_WORLD, &sendStatus1);
			EMPI_Isend(readMap->get64(-2,0), readMap->getCacheCount64(), MPI_INT64_T, ((world_rank+1)%world_size+world_size)%world_size, ECOMM_DATA_DOWN, MPI_COMM_WORLD, &sendStatus2);
			//Recv shared data (parallel ofc)
			EMPI_Irecv(readMap->get64(0,0), readMap->getCacheCount64(), MPI_INT64_T, ((world_rank-1)%world_size+world_size)%world_size, ECOMM_DATA_DOWN, MPI_COMM_WORLD, &requestStatus1);
			EMPI_Irecv(readMap->get64(-1,0), readMap->getCacheCount64(), MPI_INT64_T, ((world_rank+1)%world_size+world_size)%world_size, ECOMM_DATA_UP, MPI_COMM_WORLD, &requestStatus2);

			//if (VERBOSE) printf("Node %i: Data received.\n", world_rank);
		}



		//Run 128
		/*
		 * Process map column wise in blocks of 128
		 * The pointers for below, current and above are assigned fifo style from bottom to top
		 * BLoks of 128 are ensured by map (data is appended to 128 bits multiple for every row)
		 */
		#pragma omp parallel for default(none) shared(map, newmap,parallelProcessing, flags) if(parallelProcessing)
		for (int64 cache = 0; cache < map.getCacheCount128(); cache++) {
			//Pointers
			__m128i *above, *below, *current, *target;

			//use flags
			int64 start, end; //Set per thread
			//********* BOOTSTRAPPING ***********
			if (flags&FLAG_STEP_GHOSTROWS)
			{
				if (flags & FLAG_STEP_POLLGHOSTROWS_MPI) {
					//Bootstrap parallel with ghosts and MPI
					//This mode skips the first real line and last real line
					//And continues processing while waiting for these lines from
					//other nodes
					start = 2;
					end = map.getsy()-2;
				}else {
					//Bootstrap parallel with ghosts
					//Note that the assumption is made that the ghost rows are up to date!
					start = 1; //Do not update ghost rows
					end = map.getsy()-1; //Do not update ghost rows
				}
			}else {
				//Bootstrap regular
				start = 0;
				end = map.getsy();
			}
			//Can do this: BUT, flags may change.... so... nah
			//start = flags & (FLAG_STEP_GHOSTROWS | FLAG_STEP_POLLGHOSTROWS_MPI);
			//end = map.getsy()-(flags & (FLAG_STEP_GHOSTROWS | FLAG_STEP_POLLGHOSTROWS_MPI));

			above = 0;
			current = map.get128(start-1, cache);
			below = map.get128(start,cache);
			//*********** END OF BOOTSTRAPPING ***********


			//Iterate to last
			for (int64 row = start; row < end; row++){
				//set read pointers
				above = current;	//current row is now previous row
				current = below; 	//Next row is now current row
				below = map.get128(row+1,cache);//Get next row (get128 warps row input)
				//Set target pointer
				target = newmap.get128(row, cache);

				//Process
				step128(current,above,below, target);
			}
		}//End parallel for



	}

	//**************Update MPI ghost row depended rows*******************
	if ( (flags & (FLAG_STEP_GHOSTROWS|FLAG_STEP_POLLGHOSTROWS_MPI))
					== (FLAG_STEP_GHOSTROWS|FLAG_STEP_POLLGHOSTROWS_MPI))
	{
		int sharedStat = status;
//			#pragma omp parallel sections default(none) shared(requestStatus1, requestStatus2, sharedStat, ompi_mpi_comm_world)
		{
//				#pragma omp section
			{
				__m128i *above, *below, *current, *target;
				MPI_Status stat;
				if (!EMPI_Wait(&requestStatus1, &stat))
					stat.MPI_ERROR = 0;

				if (stat.MPI_ERROR || stat.MPI_TAG != ECOMM_DATA_DOWN || stat.MPI_SOURCE != ((world_rank-1)%world_size+world_size)%world_size) {
					#pragma omp critical
					sharedStat = ESTATE_PROCESSINGERROR;
					printf("Node %i: Invalid message received:\n\tMPI_ERROR: %i\n\tMPI_TAG: %i\n\tMPI_SOURCE: %i\n\tExpected source: %i\n",
						world_rank,
						stat.MPI_ERROR,
						stat.MPI_TAG,
						stat.MPI_SOURCE,
						((world_rank-1)%world_size+world_size)%world_size);
					MPI_Abort(MPI_COMM_WORLD, sharedStat);
				}
				else { //Request answered
					if (VERBOSE) printf("Node %i: Top row received\n", world_rank );
					for (int64 i = 0; i < readMap->getCacheCount128(); i++) {
						//Get caches
						above  = 	readMap->get128(0,i);
						current=	readMap->get128(1,i);
						below  =	readMap->get128(2,i);
						target = 	writeMap->get128(1,i);
						//Process
						step128(current,above,below, target);
					}
				}
			}

//				#pragma omp section
			{
				__m128i *above, *below, *current, *target;
				MPI_Status stat;
				if (!EMPI_Wait(&requestStatus2, &stat))
					stat.MPI_ERROR = 0;

				if (stat.MPI_ERROR || stat.MPI_TAG != ECOMM_DATA_UP || stat.MPI_SOURCE != ((world_rank+1)%world_size+world_size)%world_size) {
					#pragma omp critical
					sharedStat = ESTATE_PROCESSINGERROR;
					printf("Node %i: Invalid message received:\n\tMPI_ERROR: %i\n\tMPI_TAG: %i\n\tMPI_SOURCE: %i\n\tExpected source: %i\n",
							world_rank,
							stat.MPI_ERROR,
							stat.MPI_TAG,
							stat.MPI_SOURCE,
							((world_rank+1)%world_size+world_size)%world_size);
					MPI_Abort(MPI_COMM_WORLD, sharedStat);
				}
				else { //Request answered
					if (VERBOSE) printf("Node %i: Bottom row received\n", world_rank );
					for (int64 i = 0; i < readMap->getCacheCount128(); i++) {
						//Get caches
						above  = 	readMap->get128(-3,i);
						current=	readMap->get128(-2,i);
						below  =	readMap->get128(-1,i);

						target =   writeMap->get128(-2,i);

						//Process
						step128(current,above,below, target);
					}
				}
			}
		}// End parallel sections
		status = sharedStat;
	}//End parallel Recv */


	//Settings for next two sections
	int64 start, end;
	if (flags&FLAG_STEP_GHOSTROWS) {
		start = 1;
		end = map.getsy()-1;
	}else{
		start = 0;
		end = map.getsy();
	}

	// ************* border annealing ************
	if (map.getCacheCount128()>1) {
		//Anneal all 128bits borders
		//128/8 = make steps of 128 in 8 bit cache (to get the 128 bits borders)
		#pragma omp parallel for default(none) shared(map, newmap, flags, start, end) if(parallelProcessing)
		for (int64 cache = 128/8; cache < map.getCacheCount8(); cache+=128/8) {
			uchar *ltarget;
			uchar *rtarget;
			adjData left, right;

			//********* BOOTSTRAPPING ***********
			//Bootstrap parallel with ghosts
			//Note that the assumption is made that the ghost rows are up to date!
			left.up = 0;
			left.mid = map.get8(start-1, cache);//Ghost row (will be up)
			left.down = map.get8(start,cache);

			right.up = 0;
			right.mid = map.get8(start-1, cache+1);
			right.down = map.get8(start,cache+1);
			//********** PROCCESSING ********
			//Process data
			for (int64 row = start; row < end; row++)
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
	}// ************* end of border annealing ************



	//**************** HORIZONTAL WARP ******************
	#pragma omp parallel for default(none) shared(map, newmap, flags, start, end) if(parallelProcessing)
	for(int64 row = start; row < end; row++) {
		uchar *ltarget;
		uchar *rtarget;
		adjData left, right;

		//Gather pointers
		right.up = 	map.get8(row-1, 0);
		right.mid = map.get8(row, 	0);
		right.down =map.get8(row+1,	0);

		left.up = 	map.get8(row-1, map.getsx()/8);
		left.mid = 	map.get8(row, 	map.getsx()/8);
		left.down = map.get8(row+1,	map.getsx()/8);

		ltarget = newmap.get8(row, map.getsx()/8);
		rtarget = newmap.get8(row, 0);

		//********** PROCCESSING ********
		char shift = 8-(map.getsx()%8);
		if (shift == 8)
			shift = 0;
		stepEdge(left, ltarget, right, rtarget, shift);
	}


	//Vertical border should already be interpreted as 0
	//reset the invalid datapoints
	newmap.resetFalseBorder();
}

void Gompi::createWorldSegment(int64 & seg) {
	seg = 0;
	//float j;
	//for(int i = 0; i < 64; i++) {
	//	j = (float)rand()/(float)(RAND_MAX);
	//	if (j>=0.5)
	//		seg+=0b1;
	//	seg <<= 1;
	//}
	seg = ((int64)(rand())<<32) + ((int64)(rand()));
	//printBinary64(seg);
}

void Gompi::createWorldSegment(int64* buffer, int64 size) {
	//Parallel screws over random generation
//#pragma omp parallel for default(none) shared(buffer, size)
	for (int64 i = 0; i < size; i++)
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

int Gompi::run(int64 steps) {
	if (world_size == 1)
		runSolo(steps);
	else
		runMPI(steps);

	return status;
}

int64 Gompi::getAlive()  {

	if (VERBOSE) printf("Node %i: Counting started...\n", world_rank);
	//Rest ghosts
	readMap->resetFalseBorder();
	if (world_size>1)
		for(int64 i = 0; i < readMap->getCacheCount64(); i++) {
			*readMap->get64(0, i) = 0ll;
			*readMap->get64(-1, i) = 0ll;
		}
	else {
		return readMap->getAlive();
	}

	//get alive
	int64 personalAlive = readMap->getAlive();

	if (world_rank==0) {
		int64 ali = 0;
		int64 * alive = new int64[world_size];
		//sleep(4);
		MPI_Gather(&personalAlive, 1, MPI_INT64_T, alive, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
		for (int64 i = 0; i < world_size; i++)
			ali += alive[i];
		if (VERBOSE) printf("Node %i: Counting gathered.\n", world_rank);
		return ali;
	}

	//Slave
	MPI_Gather(&personalAlive, 1, MPI_INT64_T, 0, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);

	if (VERBOSE) printf("Node %i: Counting send. (%lli)\n", world_rank, personalAlive);
	return 0;
}
