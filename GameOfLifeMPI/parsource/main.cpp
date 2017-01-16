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




//Entry (Assumption is that all programs receive the same arguments)
int main(int nargs, char **args) {
	uint64 steps, mapx, mapy;
	steps = mapx = mapy = 0;

	switch(nargs) {
		case 1:
			mapx = mapy = 2560;
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
	}

	// RUn program
	Gompi gameoflife(mapx, mapy);

	if (!gameoflife.getStatus()) {
		//Start chrono

		//run
		gameoflife.run(steps);

		//end chrono

	}


	return gameoflife.getStatus();
}
