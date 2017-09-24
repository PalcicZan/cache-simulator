#pragma once

uint LOADINT(uint* address);
void STOREINT(uint* address, uint value);
float LOADFLOAT(float* a);
void STOREFLOAT(float* a, float f);
vec3 LOADVEC(vec3* a);
void STOREVEC(vec3* a, vec3 t);
void* LOADPTR(void* a);
void STOREPTR(void* a, void* p);

#define DELAY dummy = min( 10, dummy + sinf( (float)(address / 1789) ) ); // artificial delay

// Cache hierarchy
#define LEVEL1
#define LEVEL2
#define LEVEL3

#define LEVEL1_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL1_SIZE 32768
#define SIZE_OF_CACHE_LINE 64

#if defined(LEVEL2) || defined(LEVEL3)
#define LEVEL2_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL2_SIZE 32768
#endif
#ifdef LEVEL3
#define LEVEL3_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL3_SIZE 32768
#endif

// Eviction policy
#define EP_LRU
#define EP_RR
#define EP_FIFO

// Debug and performance handles
#define PERFORMANCE
#define CORRECTNESS

static const char* LEVEL_OFFSET[] = {"", "\t", "\t\t" };
//#define DEBUG_L2
//#define DEBUG
#if defined(DEBUG)
#define debug(...) do{printf("%s", LEVEL_OFFSET[level-1]); fprintf( stderr,__VA_ARGS__ ); } while( false )
#define debugL2(...) do{printf("%s", LEVEL_OFFSET[level-1]); fprintf( stderr,__VA_ARGS__ ); } while( false )
#elif defined(DEBUG_L1)
#define debug(...) do{printf("%s", LEVEL_OFFSET[level-1]); fprintf( stderr,__VA_ARGS__ ); } while( false )
#elif defined(DEBUG_L2)
#define debug(...) do{ } while ( false )
#define debugL2(level, ...) do{fprintf(stderr,"%s", LEVEL_OFFSET[level-1]); fprintf(stderr,__VA_ARGS__ ); } while( false )
#else
#define debug(...) do{ } while ( false )
#define debugL2(...) do{ } while ( false )
#endif

struct CacheLine
{
	uint data[16]; // 64 bytes
	uint tag;
	bool valid = false, dirty = false;
#ifdef EP_LRU
	uint timestamp;
#endif
};

// A write-back cache with write allocation
class Cache
{
public:
	Cache::Cache();
	Cache::Cache(const uint level, const uint size, const uint n);
	~Cache();

	uint Read32bit(uint address);
	void Write32bit(uint address, uint value);
	float GetDummyValue() const { return dummy; }

	static void GetPerformancePerFrame(uint nFrame);
	static void GetRealTimePerformance(Surface gameScreen, uint nFrame);
	static uint numOfAccessPerFrame, numOfAccess;
	static Surface realTimeSurface;


private:
	// Init cache
	const uint level, size, nWaySetAssociative;
	void InitCache();
	// General interfaces
	void LoadLine(uint address, CacheLine& line);
	void WriteLine(uint address, CacheLine& line);
	// Communications cache lines between levels
	void GetLine(uint address, CacheLine& line);
	void SetLine(uint address, CacheLine& line);
	// Accessed only by last cache in hierarchy
	void LoadLineFromMem(uint address, CacheLine& line);
	void WriteLineToMem(uint address, CacheLine& line);
	// Eviction policies
	void UpdateEviction();
	uint Evict();
	// Helper functions
	uint GetMatchinValidLine(CacheLine& set);
	void WriteDirtyLine(CacheLine& line, uint set);
	// Performance
	int readMiss, writeMiss, readCount, writeCount;
	void ResetPerformanceCounters();
	void GetPerormance();

	CacheLine **cache;	//[NUM_OF_SETS][LEVEL1_N_WAY_SET_ASSOCIATIVE];
	float dummy;
};