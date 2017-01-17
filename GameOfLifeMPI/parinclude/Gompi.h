/*
 * Gompi.h
 *
 *  Created on: Jan 16, 2017
 *      Author: erwin
 */

#ifndef PARINCLUDE_GOMPI_H_
#define PARINCLUDE_GOMPI_H_

//Settings
#define VERBOSE 0
#define GOLVERBOSE 0
#define OMPI_SKIP_MPICXX //C++ bindings not working with autocomplete... Lazy C functions work well
#define STEP_MP_THRESHOLD_XY 100 //Both X and Y must be bigger than, before MP can starts
#define GOMPI_SEED 1 //Seed for map generation


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
	int64 mapx, mapy;
	GoLMap *readMap = 0, *writeMap = 0;

	//Node info
	systeminfo sysnfo;


	//Functions
	/*
	 * TODO
	 */
	void init_mpi();

	/*
	 * While processing, if one node fails
	 * 	Random message is send to master node so it fails
	 *
	 * 	Master node:
	 * 		- Sends random message to all other nodes
	 * 		- Checks if they have received it after 5 seconds
	 * 		- Ends run();
	 */
	void cascadeError();

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
	void runSolo(int64 steps);

	/*
	 * TODO runMaster documentation
	 */
	void runMPI(int64 steps);

	/*
	 * stepEdge, give structures with pointers containing  byte sized rows
	 * 	Shift edge shifts the left edge x positions to accommodate for data padding
	 */
	void stepEdge(adjData left, uchar* resLeft, adjData right, uchar * resRight) {
		stepEdge(left, resLeft, right, resRight, 0);
	}
	void stepEdge(adjData left, uchar* resLeft, adjData right, uchar* resRight, char shiftEdge);
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
	void step64(int64 & rowchunk, int64 &rowabove, int64 & rowbelow, int64 & rowresult);
	/*
	 * step128, takes uint64 sized chunks and processes them.
	 * But leaving the edges of the 128bit sized chunk unprocessed (due to missing info)
	 * results are directly written to the address specified in _rowresult
	 */
	void step128(__m128i * _rowchunk, __m128i *_rowabove, __m128i * _rowbelow, __m128i * _rowresult);


	//flags for stepGeneral
#define FLAG_STEP_GHOSTROWS 0b00000001
#define FLAG_STEP_POLLGHOSTROWS_MPI 0b00000010
#define FLAG_STEP_PARALLELPROCESSING 0b00000100
	/****
	 * stepGeneral
	 * Steps the whole map. Different ways based on the flags
	 * Always warps
	 * Flags are orred together. Some flags may need other flags before they works (Like FLAG_STEP_POLLGHOSTROWS_MPI)
	 */
	void stepGeneral(GoLMap & map, GoLMap & newmap, char flags);

	/**
	 * createWorldSegment()
	 *
	 */
	void createWorldSegment(int64 & buffer);
	void createWorldSegment(int64 * buffer, int64 size);

	/*
	 * TODO: DOcumentation
	 */
	void getsysinfo(systeminfo *nfo);
public:
	/*
	 * TODO Gompi constructor
	 */
	Gompi(int64 x, int64 y);
	/*
	 * TODO gompi deconstructor
	 */
	~Gompi();
	/*
	 * TODO run fun
	 *
	 * returns status
	 */
	int run(int64 steps);

	//GEtters/setters
	int getStatus() { return status; };

	/*
	 * Gathers all alive counts
	 * 	Must be called on all nodes.
	 *
	 * 	Assumes manual reset of ghost rows IF not using MPI
	 */
	int64 getAlive();

	int getNameLength() const {
		return name_length;
	}

	const char* getProcessorName() const {
		return processor_name;
	}

	const systeminfo& getSysnfo() const {
		return sysnfo;
	}

	int getWorldRank() const {
		return world_rank;
	}

	int getWorldSize() const {
		return world_size;
	}

};




#endif /* PARINCLUDE_GOMPI_H_ */
