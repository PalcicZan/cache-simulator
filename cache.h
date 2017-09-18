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
#define N_WAY_SET_ASSOCIATIVE_CACHE 4
#define SIZE_OF_CACHE_1 32768
#define SIZE_OF_CACHE_LINE 64
#define NUM_OF_SETS (32768 >> log2(SIZE_OF_CACHE_LINE)) >> log2(N_WAY_SET_ASSOCIATIVE_CACHE)

struct CacheLine
{
	//uchar data[64]; 
	uint tag;
	uint data[16]; // 64 bytes
	bool valid, dirty;
};

class Cache
{
public:
	Cache();
	~Cache();
	void ArtificialDelay();
	uint Read32bit(uint address);
	void Write32bit(uint address, uint value);
	float GetDummyValue() const { return dummy; }
private:
	void LoadLineFromMem(uint address, CacheLine& line);
	void WriteLineToMem(uint address, CacheLine& line);

	// Eviction policies
	uint LRU();
	uint MRU();
	uint RandomReplacement();

	CacheLine cache[NUM_OF_SETS][N_WAY_SET_ASSOCIATIVE_CACHE]; //32KB in 512 lines
	float dummy;
};