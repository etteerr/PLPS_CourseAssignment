/*
 * source.cpp
 *
 *  Created on: Nov 20, 2016
 *      Author: erwin
 */


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


//programs

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


	//Init personal node data
	systeminfo sysnfo;
	getsysinfo(&sysnfo);

	//Report operation
	printf("Node %i of %i (%s) reporting in\n"
		   "\tSystem usage: %.3f\n"
		   "\tFree memory : %i Mb\n",
		   world_rank, world_size, processor_name,
		   sysnfo.avgload1,
		   sysnfo.freememInMB);

	//Run program
	int res = -1;
	if (world_size==1)
		res = runSolo();
	else
		if(world_rank==0)
			res = runMaster();
		else
			res = runSlave();

	//Clean up
	MPI_Finalize();

	return res;
}
