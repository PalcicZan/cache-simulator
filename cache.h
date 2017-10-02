#pragma once

uint LOADINT(uint* address);
void STOREINT(uint* address, uint value);
float LOADFLOAT(float* a);
void STOREFLOAT(float* a, float f);
vec3 LOADVEC(vec3* a);
void STOREVEC(vec3* a, vec3 t);
void* LOADPTR(void* a);
void STOREPTR(void* a, void* p);
// ============================================================================
// DEFINES PREHIBIT TO CHANGE
// ============================================================================

#define DELAY dummy = min( 10, dummy + sinf( (float)(address / 1789) ) ); // artificial delay
#define L1 0
#define L2 1
#define L3 2

#define READ 0
#define WRITE 1
#define MISS 0
#define WRITE_LINE 1
#define ALL 2

#define PERFORMANCE_AMAT 0
#define LOCAL_MISS_RATE 1
#define GLOBAL_MISS_RATE 2
#define READ_MISS 3
#define WRITE_MISS 4
#define N_ACCESSES 5

#define EP_RR 0
#define EP_LRU 1
#define EP_FIFO 2
#define EP_MRU 3

#define CACHE_LINE_SIZE 64
// ============================================================================
// FREE TO CHANGE DEFINES
// ============================================================================

// Cache hierarchy
#define NUM_OF_LEVELS 3

#define LEVEL1_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL1_SIZE 32768
//4 //8 //32768 //262144
//4 16 //32768 //2097152
#if NUM_OF_LEVELS > 1
#define LEVEL2_N_WAY_SET_ASSOCIATIVE 8
#define LEVEL2_SIZE 262144
#endif
#if NUM_OF_LEVELS > 2
#define LEVEL3_N_WAY_SET_ASSOCIATIVE 16
#define LEVEL3_SIZE 2097152	
#endif

#define SUPPORT_WIDER_TYPES

// Select eviction policy
#define EVICTION_POLICY EP_LRU

// Define access penalties
#define RAM_PENALTY 100
#define L1_PENALTY 4
#if NUM_OF_LEVELS == 1
#define L2_PENALTY RAM_PENALTY
#define L3_PENALTY 0
#elif NUM_OF_LEVELS == 2
#define L2_PENALTY 11
#define L3_PENALTY RAM_PENALTY
#else
#define L2_PENALTY 11
#define L3_PENALTY 39
#endif 

// Debug and performance handles
#define REAL_TIME_SCRHEIGHT SCRHEIGHT/5

/*
SELECTED_PERFORMANCE can have these values
 - PERFORMANCE_AMAT - Average memory access time
 - LOCAL_MISS_RATE 1 - Local miss rate on individual cache level - if higher than 100% means it's because for each access you get more than one access to lower level.
 - GLOBAL_MISS_RATE 2 - Global miss rate on individual cache level - of all accesses to cache L1
 - READ_MISS 3
 - WRITE_MISS 4 
 - N_ACCESSES 5 - Number of accesses to each cache level
*/
#define SELECTED_PERFORMANCE PERFORMANCE_AMAT 
#define SHOW_PERFORMANCE

#ifdef SHOW_PERFORMANCE
#define SHOW_GRAPH
#define SAVE_PERFORMANCE
//#define BLEND_PERFORMANCE
// Types of graph performances 
#if SELECTED_PERFORMANCE != PERFORMANCE_AMAT
#define PERFORMANCE_PER_LEVEL // Include parts of every level in the graph
#endif
#endif
#define INCLUDE_DRAM SELECTED_PERFORMANCE == N_ACCESSES

static const char* FILE_NAME = "AMAT_LRU_64.csv";
static const char* LEVEL_OFFSET[] = { "", "\t", "\t\t" };
static const char* LEVEL_LABELS[] = { "L1", "L2", "L3", "DRAM", "AMAT" };
static const char* EVICTION_LABELS[] = { "Random Replacement (RR)", "Least recently used (LRU)", "First In First Out (FIFO)", "Most recently used (MRU)" };
static const char* PERFORMANCE_LABELS[] = { "AMAT", "LOCAL MISS RATE", "GLOBAL MISS RATE", "READ MISS", "WRITE MISS", "NUM OF ACCESSES" };
static const uint COLOR[5] = { 0x90AFC5, 0x336B87, 0x2A3132 , 0x763626, 0x90AFC5 };

// Logging
//#define ENABLE_LOG
#ifdef ENABLE_LOG
#define LOG(level, ...) do{fprintf(stderr,"%s", LEVEL_OFFSET[level]); fprintf(stderr,__VA_ARGS__ ); } while( false )
#else
#define LOG(...) do{ } while ( false )
#endif

class CacheLine
{
public:
	uint data[CACHE_LINE_SIZE / sizeof(uint)];
	uintptr_t tag;
	bool valid = false, dirty = false;
#if EVICTION_POLICY != EP_RR
	uint timestamp = 0;
#endif
private:
	void* operator new(size_t i)
	{
		return _aligned_malloc(i, CACHE_LINE_SIZE);
	}

	void operator delete(void* p)
	{
		_aligned_free(p);
	}
};

// A write-back cache with write allocation
class Cache
{
public:
	Cache::Cache();
	Cache::Cache(const uint level, const uint size, const uint n);
	~Cache();

	void Read(const uintptr_t address, const uint size, void* value);
	void Write(const uintptr_t address, const uint size, const void* value);

	float GetDummyValue() const { return dummy; }

	static void GetPerformancePerFrame(Surface* gameScreen, const uint nFrame);
	static void DrawGraphPerformance(Surface* gameScreen, const uint nFrame);
	static float performance[SCRWIDTH][NUM_OF_LEVELS + 1];
	static Surface realTimeSurface;
	static FILE* results;
private:
	// Initialize cache
	const uint level, size, nWaySetAssociative;
	void InitCache();
	// General interfaces
	void LoadLine(const uintptr_t address, CacheLine& line);
	void WriteLine(const uintptr_t address, const CacheLine& line);
	// Accessed only by last cache in hierarchy
	void LoadLineFromMem(const uintptr_t address, CacheLine& line);
	void WriteLineToMem(const uintptr_t address, CacheLine& line);


	// Eviction policies
	inline void UpdateEviction(uint set, uint slot, uintptr_t tag);
	inline uint Evict(uint set);
	// Helper functions
	void WriteDirtyLine(CacheLine& line, uint set, uint operation);
	// Performance
	void ResetPerformanceCounters();
	void GetPerformance();
	void UpdatePerformance();

	float measurement[6];
	int counters[2][3];
	// readMiss means simply not tag or valid in the cache
	// readMissWriteDirty - write dirty line to next cache level
	// readCount - all reads
	// readAccessToNextLevel - counts only if needs to access to next level (multiple per requests counts as one)
	CacheLine **cache;
	float dummy;
};