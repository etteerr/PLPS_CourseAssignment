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

int main(int nargs, char **args) {

	//get current info
	systeminfo info;
	getsysinfo(&info);


	uint64 mapx, mapy;
	unsigned int steps;
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
			if (atoi(args[1])==-1) {
				printf("Avgl: %f\n",info.avgload1);
				printf("Avgl5:%f\n",info.avgload5);
				printf("Freemb: %llu\n",info.freememInMB);
				printf("Freebyte: %llu\n",info.freememByte);
				printf("N-cores: %i\n",info.cores);
			}
			return 0;
			break;
	}

	if (mapx < 3 || mapy < 3){
		printf("Map must be bigger than 2x2\n");
	}

	auto start = std::chrono::high_resolution_clock::now();
	//Code goes here


	//End of processing
	auto elapsed = std::chrono::high_resolution_clock::now() - start;
	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

	//Count result


	//Clean up


	//Final result
	printf("Done in %llius (%.5fs)\n", microseconds, (double)microseconds/1000000.0);

	return 0;
}
