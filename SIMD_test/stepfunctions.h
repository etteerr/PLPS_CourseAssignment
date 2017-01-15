/*
 * stepfunctions.h
 *
 *  Created on: Jan 4, 2017
 *      Author: erwin
 */

#ifndef STEPFUNCTIONS_H_
#define STEPFUNCTIONS_H_

typedef unsigned long long int uint64;
typedef unsigned char uchar;
#include <emmintrin.h> //SSE2
#include "GoLMap.h"

//Edge struct for stepEdge called adjacent data (adjData)
struct adjData {
	uchar *up;
	uchar *mid;
	uchar *down;
};
typedef union  {
	uchar c;
	struct s {
		unsigned a:1;
		unsigned b:1;
		unsigned c:1;
		unsigned d:1;
		unsigned e:1;
		unsigned f:1;
		unsigned g:1;
		unsigned h:1;
	} bits;
} charuni;
/*
 * stepEdge, give structures with pointers containing  byte sized rows
 */
void stepEdge(adjData left,uchar* resLeft, adjData right, uchar * resRight);
/*
 * step1, give pointers to most left cell of 3.
 * Thus the processed cell is pointer+1 in mid (result+1 is result)
 */
//void step1();

/*
 * step64, takes uint64 sized chunks and processes them.
 * But leaving the edges of the 64bit sized chunk unprocessed (due to missing info)
 * results are directly written to the address specified in _rowresult
 */
void step64(uint64 & rowchunk, uint64 &rowabove, uint64 & rowbelow, uint64 & rowresult);
/*
 * step128, takes uint64 sized chunks and processes them.
 * But leaving the edges of the 128bit sized chunk unprocessed (due to missing info)
 * results are directly written to the address specified in _rowresult
 */
void step128(__m128i * _rowchunk, __m128i *_rowabove, __m128i * _rowbelow, __m128i * _rowresult);


/****
 * stepCMap
 * Steps the whole map. This is done with borders as 0 (so no warp)
 *
 */
void stepCMap(GoLMap & map, GoLMap & newmap);

/****
 * stepCMapWarp
 * Steps the whole map. This is done with warp (up-down, left right, no iverse)
 *
 */
void stepCMapWarp(GoLMap & map, GoLMap & newmap);

/****
 * stepIMap
 * Steps the incomplete map, which is part of a bigger map.
 * This is done with borders as 0 (so no warp)
 *
 */
void stepIMap(GoLMap & map, GoLMap & newmap);
#endif /* STEPFUNCTIONS_H_ */
