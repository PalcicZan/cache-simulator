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
#define N_WAY_SET_ASSOCIATIVE_CACHE 4
#define SIZE_OF_CACHE_1 32768
#define SIZE_OF_CACHE_LINE 64
#define NUM_OF_SETS (32768 >> 6) >> 2

//#define DEBUG
#ifdef DEBUG
#define debug(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#else
#define debug(...) do{ } while ( false )
#endif

struct CacheLine
{
	uint tag;
	uint data[16]; // 64 bytes
	bool valid = false, dirty = false;
};

class Cache
{
public:
	Cache();
	Cache(uint level, uint size, uint nWayAssociative);
	~Cache();
	void ArtificialDelay();
	uint Read32bit(uint address);
	void Write32bit(uint address, uint value);
	float GetDummyValue() const { return dummy; }

	// Cache performance
	int readMiss, writeMiss, readCount, writeCount;
private:
	uint level, size, nWayAssociative;
	void LoadLine(uint address, CacheLine& line);
	void WriteLine(uint address, CacheLine& line);
	void LoadLineFromMem(uint address, CacheLine& line);
	void WriteLineToMem(uint address, CacheLine& line);
	
	// Eviction policies
	uint LRU();
	uint MRU();
	uint RandomReplacement();

	CacheLine cache[NUM_OF_SETS][N_WAY_SET_ASSOCIATIVE_CACHE]; //32KB in 512 lines
	float dummy;
};