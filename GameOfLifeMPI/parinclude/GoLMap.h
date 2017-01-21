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
#include <stdio.h> //alligned_alloc
#include <emmintrin.h> //SSE2
//def
typedef unsigned long long int uint64;
typedef long long int int64;
typedef unsigned char uchar;
#define MASK_SIGNEDSHIFT 0x7FFFFFFFFFFFFFFF

inline __m128i shiftr128iAsUnsigned(__m128i * thing, int shift) {
	uint64 a[2];
	a[0] = *(uint64 *) thing;
	a[1] = *(((uint64 *) thing)+1);

	a[0] = (a[0]>>shift);
	a[1] = (a[1]>>shift);

	return *((__m128i *) &a);
}

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

struct systeminfo {
	float avgload1; //avg load last minute
	float avgload5; //avg load last 5 minutes
	uint64 freememInMB;
	uint64 freememByte;
	int8_t cores; //ncores
};

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
	bool isAllocated()			{ return sx>0 && data;			};
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

	void set(uint64 row, uint64 x , char state) {
		if (x>=sx)
			return;

		row = (row%((int64)sy)+((int64)sy))%((int64)sy);

		charuni cache;
		cache.c = *get8(row, x/8);
		switch(x%8) {
			case 0:
				cache.bits.h = state;
				break;
			case 1:
				cache.bits.g = state;
				break;
			case 2:
				cache.bits.f = state;
				break;
			case 3:
				cache.bits.e = state;
				break;
			case 4:
				cache.bits.d = state;
				break;
			case 5:
				cache.bits.c = state;
				break;
			case 6:
				cache.bits.b = state;
				break;
			case 7:
				cache.bits.a = state;
				break;
		}
		//Save
		*get8(row, x/8) = cache.c;
	}

	char get(int64 row, uint64 x) {
		if (x>=sx)
			return -1;

		row = (row%((int64)sy)+((int64)sy))%((int64)sy);

		charuni cache;
		cache.c = *get8(row, x/8);

		switch(x%8) {
			case 0:
				return cache.bits.h;
				break;
			case 1:
				return cache.bits.g;
				break;
			case 2:
				return cache.bits.f;
				break;
			case 3:
				return cache.bits.e;
				break;
			case 4:
				return cache.bits.d;
				break;
			case 5:
				return cache.bits.c;
				break;
			case 6:
				return cache.bits.b;
				break;
			case 7:
				return cache.bits.a;
				break;
		}

		return -1;
	}

	/*
	 * Counts the alive cells using popcountll
	 */
	uint64 getAlive(){
		uint64 count = 0;
		uint64 tmp = 0;

		for(uint64 row = 0; row<this->getsy(); row++){
			for(uint64 cache = 0; cache < this->getCacheCount64(); cache++) {
				tmp += __builtin_popcountll(*this->get64(row, cache));
			}
			//printf("Row %i: Count: %i\n", row, tmp);
			count += tmp;
			tmp = 0;
		}

		return count;
	}

	/*
	 * Request 64 bits cache located at cacheposition (sx multiple of 64)
	 * Warps row input to valid value
	 */
	uint64 * get64(int64 row, uint64 cacheposition) {
		//printf("Cache access64: %llu, %llu\n", row, cacheposition);
		//if (GOLVERBOSE) if (row < 0 || row >= sy) printf("Row128: %lli -> %lli (of %lli)\n", row, (row%sy+sy)%sy, sy);
		//if (GOLVERBOSE) if ((cacheposition < 0) || (cacheposition >= sxo/sizeof(int64))) printf("cache64: %lli -> %lli (of %lli)\n", row, (row%sy+sy)%sy, sy);
		row = (row%((int64)sy)+((int64)sy))%((int64)sy);
		return ((uint64*)data) + cacheposition + (row*sxo)/sizeof(uint64);
	}

	/*
	 * Requests the __m128i wide pointer located at cacheposition
	 * (sx multiple of 128) for use in SSE
	 * Warps row input to valid value
	 */
	__m128i * get128(int64 row, uint64 cacheposition) {
		//printf("Cache access128: %llu, %llu\n", row, cacheposition);
		//if (GOLVERBOSE) if (row < 0 || row >= sy) printf("Row128: %lli -> %lli (of %lli)\n", row, (row%sy+sy)%sy, sy);
		//if (GOLVERBOSE) if (cacheposition < 0 || cacheposition >= sxo/sizeof(__m128i)) printf("cache128: %lli -> %lli (of %lli)\n", row, (row%sy+sy)%sy, sy);
		row = (row%((int64)sy)+((int64)sy))%((int64)sy);
		return ((__m128i*)data) + cacheposition + (row*sxo)/sizeof(__m128i);
	}

	/*
	 * Request the smallest unit available.
	 * Used to solve the borders of caches and true borders
	 * Size is a byte
	 * cacheposition is bytewise as well
	 * ALERT: This will screw up with the wrong endian!
	 * Warps row input to valid value
	 */
	uchar * get8(int64 row, uint64 cacheposition) {
		//printf("Cache access8: %llu, %llu\n", row, cacheposition);
		//if (GOLVERBOSE) if (row < 0 || row >= sy) printf("Row128: %lli -> %lli (of %lli)\n", row, (row%sy+sy)%sy, sy);
		//if (GOLVERBOSE) if (cacheposition < 0 || cacheposition >= sxo/sizeof(char)) printf("cache8: %lli -> %lli (of %lli)\n", row, (row%sy+sy)%sy, sy);
		//Little endian:
		//return ((char*)data) + cacheposition + (row*sxo)/sizeof(char);
		//Big endian:
		row = (row%((int64)sy)+((int64)sy))%((int64)sy);
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
		data = 0;

		if (sx < 3 || sy < 3) {
			if (GOLVERBOSE) printf("Map dimentions smaller than 3! (%llu by %llu)\n", sx,sy);
			sx = sy = 0;
			return;
		}

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
		//int res = posix_memalign(&data,128/8, size/8); //das4 does not use c++11 compatible version of gcc

		if (data==0)// || res)
		{
			//Error on allocating, rollback and set failing conditions
			if (GOLVERBOSE) printf("Error while allocating memory.\nSize: %llu Bytes\nMap dimensions: %llux%llu\n", size/8, x,y);
			sx = sy = 0;
			oversize = 0;
			return;
		}

		if (GOLVERBOSE) printf("\nAllocation successful.\nSize: %llu Bytes\nSize: %.5f MBytes\nMap dimensions: %llux%llu\n", size/8, (double)size/(1024.0*1024.0*8.0), x,y);
		if (GOLVERBOSE) printf("True dimensions:%llux%llu\n", sxo*8, sy);
		if (GOLVERBOSE) printf("Oversize: %i\n\n", oversize);

		//Set map to 0
		for (uint64 row = 0; row < sy; row++)
			for (uint64 cpos = 0; cpos < (sxo/(uint64)sizeof(uint64)); cpos++) {
				*(((uint64*)data) + row*(sxo/sizeof(uint64)) + cpos ) = 0;
			}
	}//map

	/*
	 * Sets the appended size for aligned allocation to 0;
	 */
	void resetFalseBorder() {
		if (!oversize)
			return;


		//fill mask with ones for oversized position and bitwise not
		if (oversize>64){
			unsigned long long mask = ~((1ll<<((unsigned long long)oversize-64ll))-1ll);
			//#pragma omp parallel for default(none) shared(mask)
			for (uint64 row = 0; row < sy; row++) {
				*( ( (uint64*) data) + row*(sxo/sizeof(uint64)) + (sxo/sizeof(uint64))-1 ) = 0;
				*( ( (uint64*) data) + row*(sxo/sizeof(uint64)) + (sxo/sizeof(uint64))-2 ) &= mask;
			}
		}else{
			unsigned long long mask = ~((1ll<<((unsigned long long)oversize))-1ll);
			//the rest of the 128 bits
			for (uint64 row = 0; row < sy; row++)
				*( ( (uint64*) data) + row*(sxo/sizeof(uint64)) + (sxo/sizeof(uint64))-1 ) &= mask;
		}

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
		if (GOLVERBOSE && data) printf("Freeing GoL map (%.5f Mb)\n", (float)(sxo*sy)/(1024.0*1024.0));
		if (data) free(data);
		data = 0;
	}

};



#endif /* GOLMAP_H_ */
