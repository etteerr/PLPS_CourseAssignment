/*
 * Gompi.h
 *
 *  Created on: Jan 16, 2017
 *      Author: erwin
 */

#ifndef PARINCLUDE_GOMPI_H_
#define PARINCLUDE_GOMPI_H_



//Profiling
#define EProfiling

//flags for stepGeneral
#define FLAG_STEP_NOFLAG 				0b00000000
#define FLAG_STEP_GHOSTROWS 			0b00000001
#define FLAG_STEP_POLLGHOSTROWS_MPI 	0b00000010
#define FLAG_STEP_PARALLELPROCESSING 	0b00000100

//States
volatile const int ESTATE_OK 				= 0;
volatile const int ESTATE_LOWMEMORY 		= 1;
volatile const int ESTATE_ALLOCATIONERROR 	= 2;
volatile const int ESTATE_PROCESSINGERROR 	= 3;
volatile const int ESTATE_DATARECVERROR 	= 4;
volatile const int ESTATE_UNDEFINEDERROR 	= 666;

//External settings
#define OMPI_SKIP_MPICXX //C++ bindings not working with autocomplete... Lazy C functions work well (As they are extern 'c' declared)

//Settings
#define VERBOSE 			 0
#define GOLVERBOSE 			 0
#define STEP_MP_THRESHOLD_XY 100 	//Both X and Y must be bigger than, before MP can starts
#define GOMPI_SEED 			 1 		//Seed for map generation

//processing flags
#define SOLO_PROCESSING_FLAGS 	FLAG_STEP_NOFLAG
#define MPI_PROCESSING_FLAGS	FLAG_STEP_GHOSTROWS | FLAG_STEP_POLLGHOSTROWS_MPI


//inc
#include <sys/sysinfo.h>
#include <unistd.h> //sysconf
#include <mpi.h>
#include "../parinclude/GoLMap.h" //uint64/uchar


//Profiling functions
#ifndef PROFILINGTOOLSER
#define PROFILINGTOOLSER

extern unsigned long long sendCount;
extern unsigned long long recvCount;
extern unsigned long long isendCount;
extern unsigned long long irecvCount;
extern unsigned long long bcastCount;
extern unsigned long long waitCount;

extern double sendDuration;
extern double recvDuration;
extern double isendDuration;
extern double irecvDuration;
extern double bcastDuration;
extern double waitDuration;

extern double ttime;

extern int __EMPI_WAIT(MPI_Request * req, MPI_Status *stat);
extern void printProfiling();

#ifdef EProfiling
	#define EMPI_Send(BUF, COUNT, DATATYPE, DEST, TAG, COMM) 			sendCount++; 	ttime=MPI_Wtime(); MPI_Send(BUF, COUNT, DATATYPE, DEST, TAG, COMM); 		sendDuration +=MPI_Wtime()-ttime
	#define EMPI_Recv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, STAT) 	recvCount++; 	ttime=MPI_Wtime(); MPI_Recv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, STAT); recvDuration +=MPI_Wtime()-ttime
	#define EMPI_Isend(BUF, COUNT, DATATYPE, DEST, TAG, COMM, REQ) 		isendCount++; 	ttime=MPI_Wtime(); MPI_Isend(BUF, COUNT, DATATYPE, DEST, TAG, COMM, REQ); 	isendDuration+=MPI_Wtime()-ttime
	#define EMPI_Irecv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, REQ) 	irecvCount++; 	ttime=MPI_Wtime(); MPI_Irecv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, REQ); irecvDuration+=MPI_Wtime()-ttime
	#define EMPI_Bcast(BUF, COUNT, DATATYPE, SOURCE, COMM) 				bcastCount++; 	ttime=MPI_Wtime(); MPI_Bcast(BUF, COUNT, DATATYPE, SOURCE, COMM); 			bcastDuration+=MPI_Wtime()-ttime
	#define EMPI_Wait(REQ, STAT)										__EMPI_WAIT(REQ, STAT)
#else
	#define EMPI_Send(BUF, COUNT, DATATYPE, DEST, TAG, COMM) 			MPI_Send(BUF, COUNT, DATATYPE, DEST, TAG, COMM)
	#define EMPI_Recv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, STAT) 	MPI_Recv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, STAT)
	#define EMPI_Isend(BUF, COUNT, DATATYPE, DEST, TAG, COMM, REQ) 		MPI_Isend(BUF, COUNT, DATATYPE, DEST, TAG, COMM, REQ)
	#define EMPI_Irecv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, REQ) 	MPI_Irecv(BUF, COUNT, DATATYPE, SOURCE, TAG, COMM, REQ)
	#define EMPI_Bcast(BUF, COUNT, DATATYPE, SOURCE, COMM) 				MPI_Bcast(BUF, COUNT, DATATYPE, SOURCE, COMM)
	#define EMPI_Wait(REQ, STAT) 										MPI_Wait(REQ, STAT)
#endif

#endif

/* COMM labels
 * 	STATE: label for exchanging states
 * 	DATA: label for exchanging generic data
 * 	DATA_UP/DOWN: Specific direction data (COMM direction in world_rank from perspective of send)
 */
enum {
	ECOMM_STATE,
	ECOMM_OTHER,
	ECOMM_DATA=100,
	ECOMM_DATA_UP,
	ECOMM_DATA_DOWN
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
	uint64 caches; //Amount of 64 bits per row

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
	void runSolo(uint64 steps);

	/*
	 * TODO runMaster documentation
	 */
	void runMPI(uint64 steps);

	/*
	 * stepEdge, give structures with pointers containing  byte sized rows
	 * 	Shift edge shifts the left edge x positions to accommodate for data padding
	 */
	void stepEdge(GoLMap & map, GoLMap & resultMap,  int64 row);
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
	void createWorldSegment(uint64 & buffer, int);
	void createWorldSegment(uint64 * buffer, uint64 size);

	/*
	 * TODO: DOcumentation
	 */
	void getsysinfo(systeminfo *nfo);
public:

	void ABORT(int code) {
		MPI_Abort(MPI_COMM_WORLD, code);
	}
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
	 *
	 * returns status
	 */
	int run(uint64 steps);

	//GEtters/setters
	int getStatus() { return status; };

	/*
	 * Gathers all alive counts
	 * 	Must be called on all nodes.
	 *
	 * 	Assumes manual reset of ghost rows IF not using MPI
	 */
	uint64 getAlive();

	/*
	 * print
	 * 	Prints the GOL map in console
	 */
	void print();

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
