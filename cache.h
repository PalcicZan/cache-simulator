#pragma once

uint LOADINT(uint* address);
void STOREINT(uint* address, uint value);
float LOADFLOAT(float* a);
void STOREFLOAT(float* a, float f);
vec3 LOADVEC(vec3* a);
void STOREVEC(vec3* a, vec3 t);
void* LOADPTR(void* a);
void STOREPTR(void* a, void* p);

void CachePerformancePerFrame();

#define DELAY dummy = min( 10, dummy + sinf( (float)(address / 1789) ) ); // artificial delay

#define LEVEL1
#define LEVEL2
#define LEVEL3

#define LEVEL1_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL1_SIZE 32768
#define SIZE_OF_CACHE_LINE 64
#define NUM_OF_SETS (32768 >> 6) >> 2

#ifdef LEVEL2
#define LEVEL2_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL2_SIZE 32768
#endif
#ifdef LEVEL3
#define LEVEL3_N_WAY_SET_ASSOCIATIVE 4
#define LEVEL3_SIZE 32768
#endif


static const char* DEBUG_OFFSET[] = {"", "\t", "\t\t" };
//#define DEBUG_L2
//#define DEBUG
#define PERFORMANCE
#if defined(DEBUG)
#define debug(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#define debugL2(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#elif defined(DEBUG_L1)
#define debug(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#elif defined(DEBUG_L2)
#define debug(...) do{ } while ( false )
#define debugL2(...) do{ fprintf( stderr,__VA_ARGS__ ); } while( false )
#else
#define debug(...) do{ } while ( false )
#define debugL2(...) do{ } while ( false )
#endif

struct CacheLine
{
	uint data[16]; // 64 bytes
	uint tag;
	bool valid = false, dirty = false;
};

class Cache
{
public:
	Cache::Cache();
	Cache::Cache(const uint level, const uint size, const uint n);
	~Cache();

	uint Read32bit(uint address);
	void Write32bit(uint address, uint value);
	float GetDummyValue() const { return dummy; }

	// Cache performance
	int readMiss, writeMiss, readCount, writeCount;
private:
	const uint level, size, nWaySetAssociative;

	void LoadLine(uint address, CacheLine& line);
	void WriteLine(uint address, CacheLine& line);
	void GetLine(uint address, CacheLine& line);
	void SetLine(uint address, CacheLine& line);

	// Accessed only by last cache in hierarchy
	void LoadLineFromMem(uint address, CacheLine& line);
	void WriteLineToMem(uint address, CacheLine& line);
	
	// Eviction policies
	uint LRU();
	uint MRU();
	uint RandomReplacement();
	CacheLine **cache;// [NUM_OF_SETS][LEVEL1_N_WAY_SET_ASSOCIATIVE]; //32KB in 512 lines
	float dummy;
};