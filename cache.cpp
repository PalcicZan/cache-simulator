#include "precomp.h"

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
	printf( "%f\n", dummy ); // prevent dead code elimination
}

// reading 32-bit values
uint Cache::Read32bit( uint address )
{
	// TODO: prevent reads from RAM using a cache
	// ...

	// cache read miss: read data from RAM
	__declspec(align(64)) CacheLine line;
	LoadLineFromMem( address, line );
	return line.data[(address & 63) >> 2];

	// TODO: store the data in the cache
	// ...
}

// writing 32-bit values
void Cache::Write32bit( uint address, uint value )
{
	// TODO: prevent writes to RAM using a cache
	// ...

	// cache write miss: write data to RAM
	__declspec(align(64)) CacheLine line;
	LoadLineFromMem( address, line );
	line.data[(address & 63) >> 2] = value;
	WriteLineToMem( address, line );

	// TODO: write to cache instead of RAM; evict to RAM if necessary
	// ...
}

// ============================================================================
// CACHE SIMULATOR LOW LEVEL MEMORY ACCESS WITH LATENCY SIMULATION
// ============================================================================

// load a cache line from memory; simulate RAM latency
void Cache::LoadLineFromMem( uint address, CacheLine& line )
{
	uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
	line = *(CacheLine*)lineAddress; // fetch 64 bytes into line
	DELAY;
}

// write a cache line to memory; simulate RAM latency
void Cache::WriteLineToMem( uint address, CacheLine& line )
{
	uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
	*(CacheLine*)lineAddress = line; // write line
	DELAY;
}