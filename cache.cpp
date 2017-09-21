#include	"precomp.h"

// instantiate the cache
Cache cache;

// helper functions; forward all requests to the cache
uint LOADINT( uint* address ) { return cache.Read32bit( (uint)address ); }
void STOREINT( uint* address, uint value ) { cache.Write32bit( (uint)address, value ); }
float LOADFLOAT( float* a ) { uint v = LOADINT( (uint*)a ); return *(float*)&v; }
void STOREFLOAT( float* a, float f ) { uint v = *(uint*)&f; STOREINT( (uint*)a, v ); }
vec3 LOADVEC( vec3* a ) { vec3 r; for( int i = 0; i < 4; i++ ) r.cell[i] = LOADFLOAT( (float*)a + i ); return r; }
void STOREVEC( vec3* a, vec3 t ) { for( int i = 0; i < 4; i++ ) { float v = t.cell[i]; STOREFLOAT( (float*)a + i, v ); } }
void* LOADPTR( void* a ) { uint v = LOADINT( (uint*)a ); return *(void**)&v; }
void STOREPTR( void* a, void* p ) { uint v = *(uint*)&p; STOREINT( (uint*)a, v ); }

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
	printf("Read: %d %d %d %d\n", address, tag, set, offset);


	// Get if valid and matching tag
	for (uint slot = 0; slot < N_WAY_SET_ASSOCIATIVE_CACHE; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			printf("	Read from cache: %d %d %d %d\n", address, tag, set, offset);
			// Check if values are in sync
			if (!cache[set][slot].dirty) {
				printf("	%d %d\n", cache[set][slot].data[(address & 63) >> 2], (void*)address);
			}

			return cache[set][slot].data[(address & 63) >> 2]; // Probably need to address each byte - NOT
		}
	}

	// Cache read miss: read data from RAM
	__declspec(align(64)) CacheLine loadLine;
	printf("	Cache read miss, read from mem: %d %d %d %d\n", address, tag, set, offset);
	LoadLineFromMem(address, loadLine);

	int slot = RandomReplacement();

	if (cache[set][slot].dirty)
	{
		__declspec(align(64)) CacheLine line;
		line = cache[set][slot];
		printf("	Cache read miss, write to mem: %d %d %d %d\n", line.tag << 13 | set << 6, line.tag, set, 0);
		WriteLineToMem(line.tag << 13 | set << 6, line);
	}

	// TODO: store the data in the cache (where to put it)
	cache[set][slot].tag = tag;
	memcpy(cache[set][slot].data, (void*)loadLine.data, 64); // fetch 64 bytes into line
	cache[set][slot].valid = true;
	cache[set][slot].dirty = false;
	return cache[set][slot].data[(address & 63) >> 2];
}

// writing 32-bit values
void Cache::Write32bit(uint address, uint value)
{
	// TODO: prevent writes to RAM using a cache
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	printf("Write: %d %d %d %d\n", address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < N_WAY_SET_ASSOCIATIVE_CACHE; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			cache[set][slot].data[offset >> 2] = value;
			cache[set][slot].dirty = true;
			printf("	Cache write: %d %d %d %d\n", address, tag, set, offset);
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
			printf("	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Use eviction policy and get "best" slot in the set
	int slot = RandomReplacement();
	printf("%d\n", slot);
	// Sloth dirty store whole slot to RAM
	if (cache[set][slot].dirty)
	{
		// cache write miss: write data to RAM
		__declspec(align(64)) CacheLine line;
		line = cache[set][slot];
		//LoadLineFromMem(address, line);
		// Ask if only need to load because we always write/read 4 Bytes we don't need to load from MEM to change each byte
		//line.data[(address & 63) >> 2] = value;
		// Get address of this slot and write it back to mem

		printf("	Cache write miss, write to mem: %d %d %d %d\n", line.tag << 13 | set << 6, tag, set, offset);
		WriteLineToMem(line.tag << 13 | set << 6, line);
	}

	__declspec(align(64)) CacheLine loadLine;
	printf("	Cache write miss, read from mem: %d %d %d %d\n", address, tag, set, offset);
	LoadLineFromMem(address, loadLine);
	// Store to cache
	cache[set][slot].tag = tag;
	memcpy(cache[set][slot].data, (void*)loadLine.data, 64); // fetch 64 bytes into line
	cache[set][slot].data[offset >> 2] = value;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;

	// TODO: write to cache instead of RAM; evict to RAM if necessary
	// ...
}

int Cache::RandomReplacement() {
	return rand() % N_WAY_SET_ASSOCIATIVE_CACHE;
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