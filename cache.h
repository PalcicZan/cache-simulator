#pragma once

uint LOADINT(uint* address);
void STOREINT(uint* address, uint value);
float LOADFLOAT(float* a);
void STOREFLOAT(float* a, float f);
vec3 LOADVEC(vec3* a);
void STOREVEC(vec3* a, vec3 t);
void* LOADPTR(void* a);
void STOREPTR(void* a, void* p);

void* LOAD64PTR(void* a);
void STORE64PTR(void* a, void* p);
vec3 LOAD128VEC(vec3* a);
void STORE128VEC(vec3* a, vec3 t);
// ============================================================================
// DEFINES NOT TO CHANGE
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

#define CACHE_LINE_SIZE 64
// ============================================================================
// FREE TO CHANGE DEFINES
// ============================================================================

// Cache hierarchy
#define NUM_OF_LEVELS 3

#define LEVEL1_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL1_SIZE 32768

#if NUM_OF_LEVELS > 1
#define LEVEL2_N_WAY_SET_ASSOCIATIVE 8	//4 //8
#define LEVEL2_SIZE 262144				//32768 //262144
#endif
#if NUM_OF_LEVELS > 2
#define LEVEL3_N_WAY_SET_ASSOCIATIVE 16 //4 16
#define LEVEL3_SIZE 2097152				//32768 //2097152
#endif

// Define eviction policy
#define EP_RR
//#define EP_LRU
//#define EP_FIFO

// Define access penalties
#define L1_PENALTY 4
#define L2_PENALTY 11
#define L3_PENALTY 39
#define RAM_PENALTY 100

// Debug and performance handles
#define REAL_TIME_SCRHEIGHT SCRHEIGHT/4
//#define PERFORMANCE
#ifdef PERFORMANCE
// Types of graph perfomances 
//#define PERFORMANCE_AMAT
#define PERFORMANCE_PENALTY
//#define PERFORMANCE_PER_LEVEL // Include parts of every level in the graph
//#define INCLUDE_DRAM
#endif

static const uint COLOR[4] = { 0x00FF00, 0x0000FF, 0x00FFFF , 0xFF0000 };
static const char* LEVEL_OFFSET[] = { "", "\t", "\t\t" };

//#define DEBUG_L2
//#define DEBUG
#if defined(DEBUG)
#define debug(...) do{printf("%s", LEVEL_OFFSET[level]); fprintf( stderr,__VA_ARGS__ ); } while( false )
#define debugL2(...) do{printf("%s", LEVEL_OFFSET[level]); fprintf( stderr,__VA_ARGS__ ); } while( false )
#elif defined(DEBUG_L1)
#define debug(...) do{printf("%s", LEVEL_OFFSET[level]); fprintf( stderr,__VA_ARGS__ ); } while( false )
#elif defined(DEBUG_L2)
#define debug(...) do{ } while ( false )
#define debugL2(level, ...) do{fprintf(stderr,"%s", LEVEL_OFFSET[level]); fprintf(stderr,__VA_ARGS__ ); } while( false )
#else
#define debug(...) do{ } while ( false )
#define debugL2(...) do{ } while ( false )
#endif

class CacheLine
{
public:
	uint data[CACHE_LINE_SIZE/4];
	uintptr_t tag;
	bool valid = false, dirty = false;
#if defined(EP_LRU) || defined(EP_FIFO)
	uint timestamp = 0;
#endif
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

	static void GetPerformancePerFrame(Surface* gameScreen, const uint nFrame, const bool outprint);
	static void DrawGraphPerformance(Surface* gameScreen, const uint nFrame);
	static float performance[SCRWIDTH][NUM_OF_LEVELS + 1];
	static Surface realTimeSurface;
	static float AMAT;

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
	void UpdateEviction(uint set, uint slot, uintptr_t tag);
	uint Evict(uint set);
	// Helper functions
	void WriteDirtyLine(CacheLine& line, uint set, uint operation);
	// Performance
	void ResetPerformanceCounters();
	void GetPerformance();
	void UpdatePerformance();
	float localMissRate, globalMissRate, readMiss, writeMiss;
	int counters[2][3];
	// readMiss means simply not tag or valid in the cache
	// readMissWriteDirty - write dirty line to next cache level
	// readCount - all reads
	// readAccessToNextLevel - counts only if needs to access to next level (multiple per requests counts as one)
	CacheLine **cache;
	float dummy;
};