/*
 * Gompi.h
 *
 *  Created on: Jan 16, 2017
 *      Author: erwin
 */

#ifndef PARINCLUDE_GOMPI_H_
#define PARINCLUDE_GOMPI_H_

//Settings
#define VERBOSE 1
#define GOLVERBOSE 1


//inc
#include <sys/sysinfo.h>
#include <unistd.h> //sysconf
#include <openmpi/mpi.h>
#include "../parinclude/GoLMap.h" //uint64/uchar

//States
volatile const int ESTATE_OK 				= 0;
volatile const int ESTATE_LOWMEMORY 		= 1;
volatile const int ESTATE_ALLOCATIONERROR 	= 2;
volatile const int ESTATE_PROCESSINGERROR 	= 3;
volatile const int ESTATE_DATARECVERROR 	= 4;
volatile const int ESTATE_UNDEFINEDERROR 	= 666;

//COMM labels
enum {
	ECOMM_DATA,
	ECOMM_STATE,
	ECOMM_OTHER
};

//Handy define
enum { COMM_MASTER =  0}; //Pretty blue color hack :O


//data structs
struct adjData {
	uchar *up;
	uchar *mid;
	uchar *down;
};

class Gompi {
private:
	//Globals
	int status;

	//Init mpi data
	int world_size,
		world_rank,
		name_length;
	char processor_name[MPI_MAX_PROCESSOR_NAME];

	//Global game data
	uint64 mapx, mapy;
	GoLMap *readMap = 0, *writeMap = 0;

	//Node info
	systeminfo sysnfo;


	//Functions
	/*
	 * TODO
	 */
	void init_mpi();

	/*
	 * Requires global: map*, world_*, *Map
	 * generateAndDistribute()
	 * 	Generates a Game of Life map in one of the following ways:
	 * 	- world_size is 1 (local version)
	 * 		generates map
	 *	- world_size > 1
	 *		Distribution of data
	 *		Each nodes gets X rows + 2.
	 *		The latter are the ghost rows (information from the other nodes above and below)
	 *
	 * NOTE: Scatter is not used due to the memory limitations of one node.
	 */
	int generateAndDistribute();

	/*
	 * checkMemory
	 * 	For each node inc the master, memory capacity is checked.
	 * 	If memory is inadequate, checkMemory return 0 for all nodes.
	 * 	Note that multiple instances in the same node will not account for the fact
	 * 	other instances use the same memory
	 * return 0 if all is OK
	 */
	int checkMemory();

	/*
	 * waitForMessage(source,tag)
	 * 	Waits for a message to arive
	 * 	AND NOT HOG A CORE WHILE DOING IT
	 *
	 * For INIT part ONLY!!
	 * Do not use in computational part!!!!!
	 */
	void waitForMessage(int source, int tag, MPI_Comm comm);

	/*
	 * TODO runSolo documentation
	 */
	void runSolo(uint64 steps);

	/*
	 * TODO runSlave documentation
	 */
	void runSlave(uint64 steps);

	/*
	 * TODO runMaster documentation
	 */
	void runMaster(uint64 steps);

	/*
	 * stepEdge, give structures with pointers containing  byte sized rows
	 */
	void stepEdge(adjData left, uchar* resLeft, adjData right, uchar * resRight);
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

	/**
	 * createWorldSegment()
	 *
	 */
	void createWorldSegment(uint64 & buffer);
	void createWorldSegment(uint64 * buffer, uint64 size);

	/*
	 * TODO: DOcumentation
	 */
	void getsysinfo(systeminfo *nfo);
public:
	/*
	 * TODO Gompi constructor
	 */
	Gompi(uint64 x, uint64 y);
	/*
	 * TODO gompi deconstructor
	 */
	~Gompi();
	/*
	 * TODO run fun
	 */
	void run(uint64 steps);

	//GEtters/setters
	/*
	 * TODO documentation
	 *
	 */
	int getStatus() { return status; };

	/*
	 * TODO: documentation
	 */
	uint64 getAlive() { if (readMap) return readMap->getAlive(); return 0; };
};




#endif /* PARINCLUDE_GOMPI_H_ */
