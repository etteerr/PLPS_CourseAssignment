/*
 * source.cpp
 *
 *  Created on: Nov 20, 2016
 *      Author: erwin
 */

#define VERBOSE 1
#define GOLVERBOSE 0

//States
volatile const int ESTATE_OK 				= 0;
volatile const int ESTATE_LOWMEMORY 		= 1;
volatile const int ESTATE_ALLOCATIONERROR 	= 2;
volatile const int ESTATE_PROCESSINGERROR 	= 3;
volatile const int ESTATE_DATARECVERROR 	= 4;
volatile const int ESTATE_UNDEFINEDERROR 	= 666;

//COMM labels
enum {
	ECOMM_DATA,
	ECOMM_STATE,
	ECOMM_OTHER
};

//Handy define
enum { COMM_MASTER =  0}; //Pretty blue color hack :O

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/sysinfo.h>
#include <unistd.h> //sysconf
#include <openmpi/mpi.h>
#include <chrono>
#include "../parinclude/GoLMap.h"
#include "../parinclude/stepfunctions.h"
#include "../parinclude/helpfun.h"


//Globals
//Init mpi data
int world_size,
	world_rank,
	name_length;
char processor_name[MPI_MAX_PROCESSOR_NAME];

//Global game data
uint64 mapx, mapy;
unsigned int steps;
GoLMap *readMap = 0, *writeMap = 0;

//Node info
systeminfo sysnfo;

//functions
/*
 * checkMemory
 * 	For each node inc the master, memory capacity is checked.
 * 	If memory is inadequate, checkMemory return 0 for all nodes.
 * 	Note that multiple instances in the same node will not account for the fact
 * 	other instances use the same memory
 * return 0 if all is OK
 */
int checkMemory() {
	/* Comm info:
	 * 	Shared code:
	 * 		- Gather (Master) (Gather statussus)
	 * 		- Bcast (Master)  (Broadbast masters decision)
	 */

	uint64 rowCount = mapy/(uint64)world_size;
	uint64 masterExtra = mapy%(uint64)world_size;

	int status = ESTATE_OK;

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

/*
 * Requires global: sysnfo, world_rank, world_size, processor_name
 * reportUsage()
 * 	Prints the node number and report system usage and free memory.
 */
void reportUsage() {
	printf("Node %i of %i (%s) reporting in\n"
		   "\tSystem usage: %.3f\n"
		   "\tFree memory : %i Mb\n",
		   world_rank, world_size, processor_name,
		   sysnfo.avgload1,
		   sysnfo.freememInMB);
}

/*
 * Requires global: map*, world_*, *Map
 * generateAndDistribute()
 * 	Generates a Game of Life map in one of the following ways:
 * 	- world_size is 1 (local version)
 * 		generates map
 *	- world_size > 1
 */
int generateAndDistribute() {
	/* Comunication info:
	 *
	 * Shared Code:
	 * 	None (checkMemory omitted)
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
	 * 		Bcast (Master state)
	 */

	//SHared vars
	uint64 * buffer;
	int status = ESTATE_OK;

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
		caches = sx/128;
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
				//Create world
				createWorldSegment(buffer, rowCount*caches);
				//Send to waiting node
				MPI_Send(buffer,rowCount*caches,MPI_INT64_T, i, ECOMM_DATA, MPI_COMM_WORLD);
				//Get result (Recv directly to ensure no useless datatransfer)
				MPI_Recv(&status, 1, MPI_INT, i, ECOMM_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			}

		}
		delete buffer;

		if (status) {
			printf("one or more nodes failed to receive data.\nError: %i\n", status);
			return status;
		}

		//Create own data
		//THis is after sending data because the buffer Can be HUGE
		readMap = new GoLMap(mapx, masterExtra+rowCount+2);
		writeMap = new GoLMap(mapx, masterExtra+rowCount+2);

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
			MPI_Gather((void*)&ESTATE_ALLOCATIONERROR, 1, MPI_INT,0, 1, MPI_INT, COMM_MASTER, MPI_COMM_WORLD);
		}else
			MPI_Gather((void*)&ESTATE_OK, 1, MPI_INT,0, 1, MPI_INT, COMM_MASTER, MPI_COMM_WORLD);

		//Recieve data
		buffer = readMap->get64(1,0);
		MPI_Recv(&status, 1, MPI_INT, COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		if (status == ESTATE_OK) {
			MPI_Recv(buffer, rowCount*caches, MPI_INT64_T, COMM_MASTER, ECOMM_DATA, MPI_COMM_WORLD, &recvStatus);
			//Send status
			if (recvStatus.MPI_ERROR)
				status = ESTATE_DATARECVERROR;
			MPI_Send(&status, 1, MPI_INT, COMM_MASTER, ECOMM_STATE, MPI_COMM_WORLD);
		}
	}

//**************** COMMON CODE ********************
	//Sync status with master
	MPI_Bcast(&status, 1, MPI_INT, COMM_MASTER, MPI_COMM_WORLD);

	//Check for errors
	if (status) {
		if (readMap)  delete readMap;
		if (writeMap) delete writeMap;
		if (VERBOSE) printf("Node %i: Error received while receiving data (%i)\n",world_rank,  status);
		return status;
	}

	return status;
}

//programsint

int runSolo() {

	return 0;
}

int runSlave() {


	return 0;
}


int runMaster() {


	return 0;
}

//Entry (Assumption is that all programs recieve the same arguments)
int main(int nargs, char **args) {

	steps = mapx = mapy = 0;

	switch(nargs) {
		case 1:
			mapx = mapy = 2560;
			steps = 10000;
			break;
		case 4:
			mapx = atoll(args[1]);
			mapy = atoll(args[2]);
			steps = atoi(args[3]);
			break;
		case 2:
			//TODO: mpi gather system info
			return 0;
			break;
	}

	if (mapx < 3 || mapy < 3){
		printf("Map must be bigger than 2x2\n");
	}

	//MPI init
	MPI_Init(0,0);

	//Get n processors
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	//Get rank
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

	//Get processor name
	MPI_Get_processor_name(processor_name, &name_length);

	//Report operation
	//reportUsage();

	int res = -1;
	//create and Exchange data
	res = generateAndDistribute();
	if (res)
		return res;
	//From this point on, we assume readMap and writeMap are always allocated

	//Run program
	if (world_size==1)
		res = runSolo();
	else
		if(world_rank==0)
			res = runMaster();
		else
			res = runSlave();

	//Clean up
	MPI_Finalize();
	delete readMap;
	delete writeMap;

	return res;
}
