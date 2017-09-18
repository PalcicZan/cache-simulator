#include "precomp.h"

// instantiate the cache
Cache cache;

// helper functions; forward all requests to the cache
uint LOADINT(uint* address) { return cache.Read32bit((uint)address); }
void STOREINT(uint* address, uint value) { cache.Write32bit((uint)address, value); }
float LOADFLOAT(float* a) { uint v = LOADINT((uint*)a); return *(float*)&v; }
void STOREFLOAT(float* a, float f) { uint v = *(uint*)&f; STOREINT((uint*)a, v); }
vec3 LOADVEC(vec3* a) { vec3 r; for (int i = 0; i < 4; i++) r.cell[i] = LOADFLOAT((float*)a + i); return r; }
void STOREVEC(vec3* a, vec3 t) { for (int i = 0; i < 4; i++) { float v = t.cell[i]; STOREFLOAT((float*)a + i, v); } }
void* LOADPTR(void* a) { uint v = LOADINT((uint*)a); return *(void**)&v; }
void STOREPTR(void* a, void* p) { uint v = *(uint*)&p; STOREINT((uint*)a, v); }

// ============================================================================
// CACHE SIMULATOR IMPLEMENTATION
// ============================================================================

// cache constructor
Cache::Cache()
{
}

// cache destructor
Cache::~Cache()
{
	printf("%f\n", dummy); // prevent dead code elimination
}

// reading 32-bit values
uint Cache::Read32bit(uint address)
{
	// TODO: prevent reads from RAM using a cache
	// ... get set
	uint offset = address & 63; // for each byte of 64
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);

	// Get if valid and matching tag
	for (uint i = 0; i < N_WAY_SET_ASSOCIATIVE_CACHE; i++)
	{
		if (cache[set][i].tag == tag && cache[set][i].valid)
		{
			return cache[set][i].data[(address & 63) >> 2]; // Probably need to address each byte - NOT
		}
	}

	// cache read miss: read data from RAM
	__declspec(align(64)) CacheLine line;
	LoadLineFromMem(address, line);

	// TODO: store the data in the cache (where to put it)
	// ...

	uint i = RandomReplacement();
	cache[set][i].tag = tag;
	cache[set][i].valid = true;
	cache[set][i].data = line.data[(address & 63) >> 2];
	cache[set][i].dirty = false;
	return line.data[(address & 63) >> 2];

	// TODO: store the data in the cache
	// ...
}

// writing 32-bit values
void Cache::Write32bit(uint address, uint value)
{
	// TODO: prevent writes to RAM using a cache
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);

	for (uint i = 0; i < N_WAY_SET_ASSOCIATIVE_CACHE; i++)
	{
		if (cache[set][i].valid && cache[set][i].tag == tag)
		{
			cache[set][i].data[offset >> 2] = value;
			cache[set][i].dirty = true;
			return;
		}
	}

	for (uint i = 0; i < N_WAY_SET_ASSOCIATIVE_CACHE; i++)
	{
		if (!cache[set][i].valid)
		{
			cache[set][i].data.tag = tag;
			cache[set][i].data[offset >> 2] = value;
			cache[set][i].dirty = true;
			return;
		}
	}

	uint i = RandomReplacement();
	if (cache[set][i].dirty)
	{
		// cache write miss: write data to RAM
		__declspec(align(64)) CacheLine line;
		line = cache[set][i];
		//LoadLineFromMem(address, line); // Ask if only need to load because we always write/read 4 Bytes we don't need to load from MEM to change each byte
		//line.data[(address & 63) >> 2] = value;
		WriteLineToMem(address, line);
	}

	// Store to cache
	cache[set][i].data.tag = tag;
	cache[set][i].data[offset >> 2] = value;
	cache[set][i].dirty = true;
	cache[set][i].valid = true;

	// TODO: write to cache instead of RAM; evict to RAM if necessary
	// ...
}

// ============================================================================
// CACHE SIMULATOR LOW LEVEL MEMORY ACCESS WITH LATENCY SIMULATION
// ============================================================================

// load a cache line from memory; simulate RAM latency
void Cache::LoadLineFromMem(uint address, CacheLine& line)
{
	uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
	line = *(CacheLine*)lineAddress; // fetch 64 bytes into line
	DELAY;
}

// write a cache line to memory; simulate RAM latency
void Cache::WriteLineToMem(uint address, CacheLine& line)
{
	uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
	*(CacheLine*)lineAddress = line; // write line
	DELAY;
}