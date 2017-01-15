/*
 * source.cpp
 *
 *  Created on: Nov 20, 2016
 *      Author: erwin
 */

#define _DEBUG 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/sysinfo.h>
#include <unistd.h> //sysconf
//#include <openmpi/mpi.h>
#include <chrono>
#include "GoLMap.h"
#include "stepfunctions.h"

typedef unsigned long long int uint64;

/*
 *
 *
 */

struct systeminfo {
	float avgload1; //avg load last minute
	float avgload5; //avg load last 5 minutes
	uint64 freememInMB;
	uint64 freememByte;
	int8_t cores; //ncores
};

void getsysinfo(systeminfo *nfo) {
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

void printBinary64(uint64 &a) {
	for (int i = 0; i < 64; i++)
		if ((a<<i)&0b1000000000000000000000000000000000000000000000000000000000000000)
			printf("X");
		else
			printf("0");
}


uint64 countAlive(GoLMap &map) {
	uint64 count = 0;

	for(uint64 row = 0; row<map.getsy(); row++){
		for(uint64 cache = 0; cache < map.getCacheCount128(); cache++) {
			count += __builtin_popcountll(*map.get64(row, cache));
		}
	}

	return count;
}

int main(int nargs, char **args) {

	//get current info
	systeminfo info;
	getsysinfo(&info);


	uint64 mapx, mapy;
	unsigned int steps;

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
			break;
	}

	if (mapx < 3 || mapy < 3){
		printf("Map must be bigger than 2x2\n");
	}

	GoLMap * map,* newmap, *tmpmap;
	map = new GoLMap(mapx, mapy);
	newmap = new GoLMap(mapx, mapy);


	if (map->getsx() == 0 || newmap->getsx() == 0)
		return 1;

	auto start = std::chrono::high_resolution_clock::now();
	//Code goes here

	for (uint64 step = 0; step < steps; step++)
	{
		stepCMap(*map, *newmap);
		tmpmap = map;
		map = newmap;
		newmap = tmpmap;

	}

	//End of processing
	auto elapsed = std::chrono::high_resolution_clock::now() - start;
	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

	//Count result
	printf("Alive: %llu\n",countAlive(*map));

	//Clean up
	delete newmap;
	delete map;

	//Final result
	printf("Done in %llius (%.5fs)\n", microseconds, (double)microseconds/1000000.0);

	return 0;
}
