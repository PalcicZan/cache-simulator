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

#define L1 0
#define L2 1
#define L3 2
#define DRAM 3

// Cache hierarchy
#define NUM_OF_LEVELS 2

#define SIZE_OF_CACHE_LINE 64
#define LEVEL1_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL1_SIZE 32768

#if NUM_OF_LEVELS > 1
#define LEVEL2_N_WAY_SET_ASSOCIATIVE 8 //4
#define LEVEL2_SIZE 262144 //32768 //262144
#endif
#if NUM_OF_LEVELS > 2
#define LEVEL3_N_WAY_SET_ASSOCIATIVE 16 //4 16
#define LEVEL3_SIZE 2097152 //32768 //2097152
#endif

// Eviction policy
#define EP_LRU
#define EP_RR
#define EP_FIFO

// Debug and performance handles
#define REAL_TIME_SCRHEIGHT SCRHEIGHT/4
#define PERFORMANCE
#define READ 0
#define WRITE 1
#define MISS 0
#define WRITE_LINE 1
#define ALL 2

static const uint COLOR[4] = { 0x00FF00, 0x0000FF, 0x00FFFF , 0xFF0000};
static const char* LEVEL_OFFSET[] = {"", "\t", "\t\t"};
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
	uint timestamp = 0;
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
	static void GetRealTimePerformance(Surface* gameScreen, uint nFrame);
	static uint performance[SCRWIDTH][4];
	static uint numOfAccessPerFrame, numOfAccess;
	static Surface realTimeSurface;

private:
	// Initialize cache
	const uint level, size, nWaySetAssociative;
	void InitCache();
	// General interfaces
	void LoadLine(uint address, CacheLine& line);
	void WriteLine(uint address, CacheLine& line);
	// Communications between levels
	void GetLine(uint address, CacheLine& line);
	void SetLine(uint address, CacheLine& line);
	// Accessed only by last cache in hierarchy
	void LoadLineFromMem(uint address, CacheLine& line);
	void WriteLineToMem(uint address, CacheLine& line);
	// Eviction policies
	void UpdateEviction(uint set, uint slot);
	uint Evict(uint set);
	// Helper functions
	uint GetMatchinValidLine(CacheLine& set);
	void WriteDirtyLine(CacheLine& line, uint set, uint operation);
	// Performance
	void ResetPerformanceCounters();
	void GetPerformance(const bool withTitles, const bool withOffset);

	// readMiss means simply not tag or valid in the cache
	// readMissWriteDirty - write dirty line to next cache level
	// readCount - all reads
	// readAccessToNextLevel - counts only if needs to access to next level (multiple per requests counts as one)
	int counters[2][3];
	//int writeMiss, writeMissWriteDirty, writeCount;
	//int readMiss, readMissWriteDirty, readCount;
	//int readAccessToNextLevel, writeAccessToNextLevel;

	CacheLine **cache;	//[NUM_OF_SETS][LEVEL1_N_WAY_SET_ASSOCIATIVE];
	float dummy;
};