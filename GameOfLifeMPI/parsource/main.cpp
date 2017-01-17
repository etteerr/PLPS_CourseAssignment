/*
 * source.cpp
 *
 *  Created on: Nov 20, 2016
 *      Author: erwin
 */


#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include "../parinclude/Gompi.h"
#include <sys/time.h>



//Entry (Assumption is that all programs receive the same arguments)
int main(int nargs, char **args) {
	int64 steps, mapx, mapy;
	steps = mapx = mapy = 0;

	switch(nargs) {
		case 1:
			mapx = mapy = 8;
			steps = 10000;
			break;
		case 4:
			steps = atoll(args[3]);
			/* no break */
		case 3:
			mapy = atoll(args[2]);
			/* no break */
		case 2:
			mapx = atoll(args[1]);
			break;
		default:
			printf("Usage: par-gol [width] [height] [steps]\n");
	}

	if (mapx < 3 || mapy < 3){
		printf("Map must be bigger than 2x2\n");
		return 1;
	}

	if (steps < 1 ) {
		printf("Steps must be > 0");
		return 1;
	}

	// RUn program
	Gompi gameoflife(mapx, mapy);


	//print intial alive
	//if (gameoflife.getWorldRank()==0) printf("Alive: %lli\n", gameoflife.getAlive());
	//else gameoflife.getAlive();

	//Init timer
	timeval start, end;
	double duration;

	//Wait 2 seconds
	sleep(2);

	if (!gameoflife.getStatus()) {
		//printf("Node %i: Starting GoL\n",gameoflife.getWorldRank());
		//Start chrono
		if (gettimeofday(&start, 0))
			gameoflife.ABORT(667);

		//run
		gameoflife.run(steps);

		//end chrono
		if (gettimeofday(&end, 0))
			gameoflife.ABORT(667);
	}

	duration = (end.tv_sec + (end.tv_usec / 1000000.0)) -
			(start.tv_sec + (start.tv_usec / 1000000.0));


	//Print alive cells
	if (gameoflife.getWorldRank()==0) printf("Number of live cells = %d\n", gameoflife.getAlive());
	else gameoflife.getAlive();

	//Print duration
	if (gameoflife.getWorldRank()==0) printf("Computing time: %.5f\n", duration);

	//printf("Node %i: Exited.\n", gameoflife.getWorldRank());

	if (gameoflife.getWorldRank()==0) printProfiling();

	return gameoflife.getStatus();
}
