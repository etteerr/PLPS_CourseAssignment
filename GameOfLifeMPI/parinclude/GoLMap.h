/*
 * map
 *
 *  Created on: Jan 3, 2017
 *      Author: erwin
 */

#ifndef GOLMAP_H_
#define GOLMAP_H_

//set verbose
#ifndef GOLVERBOSE
	#define GOLVERBOSE 1
#endif

//inc
#include <stdio.h>
#include <emmintrin.h> //SSE2
//def
typedef unsigned long long uint64;
typedef unsigned char uchar;

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
} charui;

//GoLMap class
class GoLMap{

	//map size (in bits)
	uint64 sx, sy;
	//oversize of x (in bits)
	int oversize;
	uint64 sxo; //True size of x in bytes

	//map holder
	void *data;

public:
	//getters
	bool isAllocated()			{ return sx>0;					};
	uint64 getsx() 				{ return sx; 					};
	uint64 getsy() 				{ return sy; 					};
	int getOversize() 			{ return oversize; 				};
	uint64 getMapSizeInBytes() 	{ return sxo*sy; 				};
	uint64 getCacheCount64() 	{ return sxo/sizeof(uint64); 	};
	uint64 getCacheCount128() 	{ return sxo/sizeof(__m128i); 	};
	uint64 getCacheCount8() 	{ return sxo/sizeof(char); 		};
	static uint64 getEstMemoryUsageBytes(uint64 sx, uint64 sy) {
		uint64 oversize, sxo;
		if (sx % 128LL!=0) {
				oversize= 128 -(sx % 128);
				sxo = sx + oversize;
				sxo /= 8;
			}else{
				oversize = 0;
				sxo = sx/8;
			}
		return sxo*sy;
	}

	void set(uint64 x, uint64 y, char state) {
		if (x>=sx || y>=sy)
			return;

		charui * cache;
		cache->c = *get8(sy, x/8);
		switch(x%8) {
			case 0:
				cache->bits.a = state;
				break;
			case 1:
				cache->bits.b = state;
				break;
			case 2:
				cache->bits.c = state;
				break;
			case 3:
				cache->bits.d = state;
				break;
			case 4:
				cache->bits.e = state;
				break;
			case 5:
				cache->bits.f = state;
				break;
			case 6:
				cache->bits.g = state;
				break;
			case 7:
				cache->bits.h = state;
				break;
		}
	}

	/*
	 * Counts the alive cells using popcountll
	 */
	uint64 getAlive(){
		uint64 count = 0;

		for(uint64 row = 0; row<this->getsy(); row++){
			for(uint64 cache = 0; cache < this->getCacheCount128(); cache++) {
				count += __builtin_popcountll(*this->get64(row, cache));
			}
		}

		return count;
	}

	/*
	 * Request 64 bits cache located at cacheposition (sx multiple of 64)
	 */
	uint64 * get64(uint64 row, uint64 cacheposition) {
		//printf("Cache access64: %llu, %llu\n", row, cacheposition);
		return ((uint64*)data) + cacheposition + (row*sxo)/sizeof(uint64);
	}

	/*
	 * Requests the __m128i wide pointer located at cacheposition
	 * (sx multiple of 128) for use in SSE
	 */
	__m128i * get128(uint64 row, uint64 cacheposition) {
		//printf("Cache access128: %llu, %llu\n", row, cacheposition);
		return ((__m128i*)data) + cacheposition + (row*sxo)/sizeof(__m128i);
	}

	/*
	 * Request the smallest unit available.
	 * Used to solve the borders of caches and true borders
	 * Size is a byte
	 * cacheposition is bytewise as well
	 * ALERT: This will screw up with the wrong endian!
	 */
	uchar * get8(uint64 row, uint64 cacheposition) {
		//printf("Cache access8: %llu, %llu\n", row, cacheposition);
		//Little endian:
		//return ((char*)data) + cacheposition + (row*sxo)/sizeof(char);
		//Big endian:
		uchar * buff;
		buff = (uchar*)get64(row, cacheposition/8);
		return &buff[7-(cacheposition%8)];
	}

	/*
	 * Initialize map to x by y raw memory with zeros
	 * On error, allocation is freed and sx & sy will be zero
	 */
	GoLMap(uint64 x, uint64 y) {
		sx = x; sy = y;
		// Every cell is 1 bit
		// size (of x) must be multiple of 64 bits
		if (sx % 128LL!=0) {
			oversize= 128 -(sx % 128);
			sxo = sx + oversize;
			sxo /= 8;
		}else{
			oversize = 0;
			sxo = sx/8;
		}

		// field size is x*y (in bits)
		uint64 size = (sx+oversize)*y;

		//Allocate data
		// 128 bits (16 byte) allignment
		// of size/8 bytes
		data = aligned_alloc(128/8, size/8);

		if (data==0)
		{
			//Error on allocating, rollback and set failing conditions
			if (GOLVERBOSE) printf("Error while allocating memory.\nSize: %llu Bytes\nMap dimensions: %llux%llu\n", size/8, x,y);
			sx = sy = 0;
			oversize = 0;
			return;
		}

		if (GOLVERBOSE) printf("\nAllocation successful.\nSize: %llu Bytes\nSize: %.5f MBytes\nMap dimensions: %llux%llu\n", size/8, (double)size/(1024.0*1024.0*8.0), x,y);
		if (GOLVERBOSE) printf("True dimensions:%llux%llu\n", sxo*8, sy);
		if (GOLVERBOSE) printf("Oversize: %i\n", oversize);

		//Set map to 0
		for (uint64 row = 0; row < sy; row++)
			for (uint64 cpos = 0; cpos < (sxo/sizeof(uint64)); cpos++) {
				*(((uint64*)data) + row*(sxo/sizeof(uint64)) + cpos ) = 0LL;
			}
	}//map

	/*
	 * Sets the appended size for aligned allocation to 0;
	 */
	void resetFalseBorder() {
		if (!oversize)
			return;

		//fill mask with ones for oversized position and bitwise not
		uint64 mask = ~((1<<oversize)-1);
		//#pragma omp parallel for default(none) shared(mask)
		for (uint64 row = 0; row < sy; row++)
			*( ( (uint64*) data) + row*(sxo/sizeof(uint64)) + (sxo/sizeof(uint64))-1 ) &= mask;

	}

	/*
	 * Initialize map and point memory to the preallocated data given
	 * as _data.
	 * Then a check is done to see if all rows of data are of size y.
	 * If not, sx and sy are set to 0 and map has failed to initialize.
	 */
	GoLMap(uint64 x, uint64 y, int _oversize, void * _data) {
		//Assumption that data is of size x*y
		data = _data;
		oversize = _oversize;
		sx = x;
		sy = y;
		sxo = sx + oversize;
	}//map

	/*
	 * sets sx and sy to 0 and frees memory.
	 */
	~GoLMap() {
		if (GOLVERBOSE) printf("Freeing GoL map (%llu Mb)\n", sxo*sy/(1024*1024));
		free(data);
		data = 0;
	}

};



#endif /* GOLMAP_H_ */
