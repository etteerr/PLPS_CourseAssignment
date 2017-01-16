/*
 * source.cpp
 *
 *  Created on: Nov 20, 2016
 *      Author: erwin
 */

#define VERBOSE 1

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
GoLMap *readMap, *writeMap;

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

	uint64 rowCount = mapy/(uint64)world_size;
	uint64 masterExtra = mapy%(uint64)world_size;

	bool terminate = false;

	//Get system info
	getsysinfo(&sysnfo);

	//Set status (0 is OK)
	int status = 0;
	if(world_rank)
		status = (int)(2*GoLMap::getEstMemoryUsageBytes(mapx, rowCount) > sysnfo.freememByte);
	else
		status = (int)(2*GoLMap::getEstMemoryUsageBytes(mapx, rowCount+masterExtra) > sysnfo.freememByte);

	//Print error status
	if (status)
		printf("Node %i reports not enough memory (free: %.5f MB, needed: %.5f MB)\n",
				world_rank,
				(float)sysnfo.freememByte/(1024.0*1024.0),
				(float)2*GoLMap::getEstMemoryUsageBytes(mapx, rowCount)/(1024.0*1024.0));

	//gather status
	int * buffer = 0;
	if (world_rank==0)
		buffer = new int[world_size];

	MPI_Gather(&status, 1, MPI_INT, buffer, 1, MPI_INT, 0, MPI_COMM_WORLD);

	//Check all statuses (as master)
	if (world_rank==0) {
		for (int i = 0; i < world_size; i++) {
			if (buffer[i])
				terminate = true;
		}
	}

	//receive statuses (or send if master)
	MPI_Bcast(&terminate, 1, MPI_CXX_BOOL, 0, MPI_COMM_WORLD);

	return terminate;

}

void reportUsage() {
	printf("Node %i of %i (%s) reporting in\n"
		   "\tSystem usage: %.3f\n"
		   "\tFree memory : %i Mb\n",
		   world_rank, world_size, processor_name,
		   sysnfo.avgload1,
		   sysnfo.freememInMB);
}

int generateAndDistribute() {

	if (world_size==1) {
		readMap = new GoLMap(mapx, mapy);
		writeMap = new GoLMap(mapx, mapy);

		uint64* buff;
		buff = readMap->get64(0,0);

		createWorldSegment(buff, readMap->getCacheCount64()*mapy);
		return 0;
	}

	//Check if memory is enough on each node
	int status;
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
	}
	uint64 * buffer;
	if (world_rank==0) {
		//Create own data
		readMap = new GoLMap(mapx, masterExtra+rowCount+2);
		writeMap = new GoLMap(mapx, masterExtra+rowCount+2);
		//Get a pointer to data of row 2 (row 1 is the row from warp, last row is the row from rank 1 machine)
		buffer = readMap->get64(1,0);
		//Fill this memory
		createWorldSegment(buffer, (masterExtra+rowCount)*caches);


		//create and send 'their'  data
		buffer = new uint64[rowCount*caches];
		for (int i = 1; i < world_size; i++) {
			createWorldSegment(buffer, rowCount*caches);
			MPI_Send(buffer,rowCount*caches,MPI_INT64_T, i, 0, MPI_COMM_WORLD);
		}
		delete buffer;

		//Check all nodes received data
		int status = 0;
		int * statusBuffer = new int[world_size];
		MPI_Gather(&status, 1, MPI_INT, statusBuffer, 1, MPI_INT, 0, MPI_COMM_WORLD);

		for(int i = 0; i < world_size; i++)
			if (statusBuffer[i])
				status = statusBuffer[i];

		delete statusBuffer;

		if (status) {
			printf("one or more nodes failed to receive data.\nError: %i\n", status);
			return status;
		}


	}else{
		//Init map
		readMap = new GoLMap(mapx, rowCount+2);
		writeMap = new GoLMap(mapx, rowCount+2);

		//Vars
		uint64 * buffer;
		MPI_Status recvStatus;

		//If allocation failed, cancel recv
		if (!readMap->isAllocated() || !writeMap->isAllocated()) {
			if (VERBOSE) printf("Node %i: Memory Allocation failed.\n", world_rank);
			//Create fake buffer
			buffer = new uint64[rowCount*caches];
			MPI_Recv(buffer, rowCount*caches, MPI_INT64_T, 0, 0, MPI_COMM_WORLD, &recvStatus);
			delete buffer;
			int error = 666;
			MPI_Gather(&error, 1, MPI_INT, 0, 1, MPI_INT, 0, MPI_COMM_WORLD);
			return error;
		}

		//Recieve data
		buffer = readMap->get64(1,0);
		MPI_Recv(buffer, rowCount*caches, MPI_INT64_T, 0, 0, MPI_COMM_WORLD, &recvStatus);

		//Send status
		MPI_Gather(&recvStatus.MPI_ERROR, 1, MPI_INT, 0, 1, MPI_INT, 0, MPI_COMM_WORLD);

		//Check for errors
		if (recvStatus.MPI_ERROR) {
			delete readMap;
			delete writeMap;
			if (VERBOSE) printf("Node %i: Error while receiving initial data (%i)\n", recvStatus.MPI_ERROR);
			return recvStatus.MPI_ERROR;
		}
	}

	return 0;
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
