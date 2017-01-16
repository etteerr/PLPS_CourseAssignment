/*
 * A few help functions like system information and count alive :D
 *
 */
#ifndef _HELPFUN
#define _HELPFUN


typedef unsigned long long int uint64;

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


uint64 createWorldSegment() {
	uint64 seg = 0;
	float j;
	for(int i = 0; i < 64; i++) {
		j = (float)rand()/(float)(RAND_MAX);
		if (j>=0.5)
			seg+=0b1;
		seg <<= 1;
	}
	return seg;
}

void createWorldSegment(uint64 * buffer, uint64 size) {
	for (int i = 0; i < size; i++)
		buffer[i] = createWorldSegment();
}
#endif
