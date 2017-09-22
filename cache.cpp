#include	"precomp.h"

// instantiate the cache
Cache L1Cache;
Cache L2Cache = Cache();
Cache L3Cache;
int frame;

// helper functions; forward all requests to the cache
uint LOADINT( uint* address ) { return L1Cache.Read32bit( (uint)address ); }
void STOREINT( uint* address, uint value ) { L1Cache.Write32bit( (uint)address, value ); }
float LOADFLOAT( float* a ) { uint v = LOADINT( (uint*)a ); return *(float*)&v; }
void STOREFLOAT( float* a, float f ) { uint v = *(uint*)&f; STOREINT( (uint*)a, v ); }
vec3 LOADVEC( vec3* a ) { vec3 r; for( int i = 0; i < 4; i++ ) r.cell[i] = LOADFLOAT( (float*)a + i ); return r; }
void STOREVEC( vec3* a, vec3 t ) { for( int i = 0; i < 4; i++ ) { float v = t.cell[i]; STOREFLOAT( (float*)a + i, v ); } }
void* LOADPTR( void* a ) { uint v = LOADINT( (uint*)a ); return *(void**)&v; }
void STOREPTR( void* a, void* p ) { uint v = *(uint*)&p; STOREINT( (uint*)a, v ); }

// ============================================================================
// CACHE SIMULATOR IMPLEMENTATION
// ============================================================================

// Cache constructor
Cache::Cache()
{
}

// Cache destructor
Cache::~Cache()
{
	printf("%f\n", dummy); // Prevent dead code elimination
}

// Reading 32-bit values
uint Cache::Read32bit(uint address)
{
	readCount++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debug("Read: %d %d %d %d\n", address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < N_WAY_SET_ASSOCIATIVE_CACHE; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			debug("	Read from cache: %d %d %d %d\n", address, tag, set, offset);
			return cache[set][slot].data[offset >> 2];
		}
	}
	readMiss++;
	uint slot = RandomReplacement();
	if (cache[set][slot].dirty)
	{
		__declspec(align(64)) CacheLine line;
		line = cache[set][slot];
		debug("	Cache read miss, write to mem: %d %d %d %d\n", line.tag << 13 | set << 6, line.tag, set, 0);
		WriteLineToMem((uint)(line.tag << 13 | set << 6), line);
	}
	// Cache read miss: read data from RAM
	__declspec(align(64)) CacheLine loadLine;
	debug("	Cache read miss, read from mem: %d %d %d %d\n", address, tag, set, offset);
	LoadLineFromMem(address, loadLine);

	memcpy(cache[set][slot].data, (void*)loadLine.data, 64); // fetch 64 bytes into line
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = false;

	return cache[set][slot].data[offset >> 2];
}

// Writing 32-bit values
void Cache::Write32bit(uint address, uint value)
{
	writeCount++;
	// TODO: prevent writes to RAM using a cache
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debug("Write: %d %d %d %d\n", address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < N_WAY_SET_ASSOCIATIVE_CACHE; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			cache[set][slot].data[offset >> 2] = value;
			cache[set][slot].dirty = true;
			debug("	Cache write: %d %d %d %d\n", address, tag, set, offset);
			/*__declspec(align(64)) CacheLine line;
			line = cache[set][slot];
			debug("	Cache write miss, write to mem: %d %d %d %d\n", line.tag << 13 | set << 6, tag, set, offset);
			WriteLineToMem((uint)(line.tag << 13 | set << 6), line);*/
			return;
		}
	}

	// Find invalid set/slot and read all 64B from mem and save value to cache (4B)
	for (uint slot = 0; slot < N_WAY_SET_ASSOCIATIVE_CACHE; slot++)
	{
		if (!cache[set][slot].valid)
		{
			__declspec(align(64)) CacheLine loadLine;
			LoadLineFromMem(address, loadLine);
			cache[set][slot].tag = tag;
			memcpy(cache[set][slot].data, (void*)loadLine.data, 64); // fetch 64 bytes into line
			cache[set][slot].data[offset >> 2] = value; // All dirty
			cache[set][slot].dirty = true;
			cache[set][slot].valid = true;
			debug("	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			/*__declspec(align(64)) CacheLine line;
			line = cache[set][slot];
			debug("	Cache write miss, write to mem: %d %d %d %d\n", line.tag << 13 | set << 6, tag, set, offset);
			WriteLineToMem((uint)(line.tag << 13 | set << 6), line);*/
			return;
		}
	}

	writeMiss++;
	// Use eviction policy and get "best" slot in the set
	uint slot = RandomReplacement();
	debug("%d\n", slot);
	// Sloth dirty store whole slot to RAM
	if (cache[set][slot].dirty && cache[set][slot].valid)
	{
		// cache write miss: write data to RAM
		__declspec(align(64)) CacheLine line;
		line = cache[set][slot];
		debug("	Cache write miss, write to mem: %d %d %d %d\n", line.tag << 13 | set << 6, tag, set, offset);
		WriteLineToMem((uint)(line.tag << 13 | set << 6), line);
	}

	debug("	Cache write miss, read from mem: %d %d %d %d\n", address, tag, set, offset);

	__declspec(align(64)) CacheLine loadLine;
	LoadLineFromMem(address, loadLine);

	// Store to cache
	memcpy(cache[set][slot].data, (void*)loadLine.data, 64); // fetch 64 bytes into line
	cache[set][slot].tag = tag;
	cache[set][slot].data[offset >> 2] = value;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

uint Cache::RandomReplacement() {
	return rand() % N_WAY_SET_ASSOCIATIVE_CACHE;
}

// Performance analysis

void CachePerformancePerFrame() {
	frame++;
	printf("Frame: %d\n", frame);
	printf("Reads: %d\n Reads miss: %d (%lf)\n", L1Cache.readCount, L1Cache.readMiss, (float)L1Cache.readMiss/L1Cache.readCount);
	printf("Write: %d\n Write miss: %d (%lf)\n", L1Cache.writeCount, L1Cache.writeMiss, (float)L1Cache.writeMiss/L1Cache.writeCount);
	L1Cache.readCount = 0;
	L1Cache.readMiss = 0;
	L1Cache.writeCount = 0;
	L1Cache.writeMiss = 0;
}

// ============================================================================
// CACHE SIMULATOR LOW LEVEL MEMORY ACCESS WITH LATENCY SIMULATION
// ============================================================================

uint* RAM = (uint*)MALLOC64( 20 * 1024 * 1024 ); // simulated RAM

// load a cache line from memory; simulate RAM latency
void Cache::LoadLineFromMem( uint address, CacheLine& line )
{
    uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
    memcpy( line.data, (void*)lineAddress, 64 ); // fetch 64 bytes into line
    DELAY;
}

// write a cache line to memory; simulate RAM latency
void Cache::WriteLineToMem( uint address, CacheLine& line )
{
    uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
    memcpy( (void*)lineAddress, line.data, 64 ); // fetch 64 bytes into line
    DELAY;
}