#include "stepfunctions.h"
#include <omp.h>
/*
 * step1, give pointers to most left cell of 3.
 * Thus the processed cell is pointer+1 in mid (result+1 is result)
 */

void stepEdge(adjData left, uchar* resLeft, adjData right, uchar * resRight) {
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

void step64(uint64 & rowchunk, uint64 &rowabove, uint64 & rowbelow, uint64 & rowresult) {
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


void step128(__m128i * _rowchunk, __m128i *_rowabove, __m128i * _rowbelow, __m128i * _rowresult) {
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
	//TODO: check if the following gets optimized out (should be)
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

/****
 * stepCMap
 * Steps the whole map. This is done with borders as 0 (so no warp)
 *
 */
void stepCMap(GoLMap & map, GoLMap & newmap) {
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

/****
 * stepCMapWarp
 * Steps the whole map. This is done with warp (up-down, left right, no iverse)
 *
 */
void stepCMapWarp(GoLMap & map){

}

/****
 * stepIMap
 * Steps the incomplete map, which is part of a bigger map.
 * This is done with borders as 0 (so no warp)
 *
 */
void stepIMap(GoLMap & map){

}
