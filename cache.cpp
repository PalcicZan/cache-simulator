#include	"precomp.h"
#include <fstream>

// Instantiate the cache
Cache L1Cache(1, LEVEL1_SIZE, LEVEL1_N_WAY_SET_ASSOCIATIVE);
Cache L2Cache(2, LEVEL2_SIZE, LEVEL2_N_WAY_SET_ASSOCIATIVE);
Cache L3Cache(3, LEVEL3_SIZE, LEVEL3_N_WAY_SET_ASSOCIATIVE);

int nFrame;
// Helper functions; forward all requests to the cache
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
Cache::Cache() : level(1), size(LEVEL1_SIZE), nWaySetAssociative(LEVEL1_N_WAY_SET_ASSOCIATIVE)
{
	InitCache();
}

Cache::Cache(const uint level, const uint size, const uint n) : level(level), size(size), nWaySetAssociative(n)
{	
	InitCache();
}

void Cache::InitCache()
{
	int sets = size / (SIZE_OF_CACHE_LINE*nWaySetAssociative);
	cache = new CacheLine*[sets];
	for (int i = 0; i < sets; ++i)
		cache[i] = new CacheLine[nWaySetAssociative];
}

// Cache destructor
Cache::~Cache()
{
	int sets = size / (SIZE_OF_CACHE_LINE * nWaySetAssociative);
	for (int i = 0; i < sets; ++i) {
		delete[] cache[i];
	}
	delete[] cache;

	printf("%f\n", dummy); // Prevent dead code elimination
}

// Reading 32-bit values
uint Cache::Read32bit(uint address)
{
	readCount++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debugL2(level, "------------------------------------------------\n");
	debugL2(level, "Read %d: %d %d %d %d\n", readCount, address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			debug("Read from cache: %d %d %d %d\n", address, tag, set, offset);
			debugL2(level, "Read value%u\n", cache[set][slot].data[offset >> 2]);
			return cache[set][slot].data[offset >> 2];
		}
	}

	// Cache miss - use eviction policy 
	readMiss++;
	uint slot = RandomReplacement();
	debugL2(level, "Cache read miss, read from mem: %d %d %d %d\n", address, tag, set, offset);

	/*if (cache[set][slot].dirty)
	{
		debugL2(level, "Cache read miss, write to mem: %d %d %d %d\n", cache[set][slot].tag << 13 | set << 6, cache[set][slot].tag, set, 0);

		//__declspec(align(64)) CacheLine line;
		//line = cache[set][slot];
		WriteLine((uint)(cache[set][slot].tag << 13 | set << 6), cache[set][slot]);
	}*/
	Cache::WriteDirtyLine(cache[set][slot], set);
	LoadLine(address, cache[set][slot]);
	cache[set][slot].valid = true;
	cache[set][slot].dirty = false;
	debugL2(level, "Read value%u\n", cache[set][slot].data[offset >> 2]);
	return cache[set][slot].data[offset >> 2];
}

// Writing 32-bit values
void Cache::Write32bit(uint address, uint value)
{
	writeCount++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debugL2(level, "------------------------------------------------\n");
	debugL2(level, "Write %d: %d %d %d %d\n",writeCount, address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			cache[set][slot].data[offset >> 2] = value;
			cache[set][slot].dirty = true;
			debug("	Cache write: %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Find invalid set/slot and read all 64B from mem and save value to cache (4B)
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (!cache[set][slot].valid)
		{
			//__declspec(align(64)) CacheLine loadLine;
			LoadLine(address, cache[set][slot]);
			cache[set][slot].data[offset >> 2] = value; // All dirty
			cache[set][slot].dirty = true;
			debug("	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Use eviction policy
	writeMiss++;
	uint slot = RandomReplacement();
	debug("Cache write miss: %d %d %d %d\n", address, tag, set, offset);
	// Sloth dirty store whole slot to RAM
	if (cache[set][slot].dirty)
	{
		// Cache write miss: write data to RAM
		debug("Write old: %d %d %d %d\n", cache[set][slot].tag << 13 | set << 6, tag, set, offset);
		WriteLine((uint)(cache[set][slot].tag << 13 | set << 6), cache[set][slot]);
	}

	LoadLine(address, cache[set][slot]);
	cache[set][slot].data[offset >> 2] = value;
	//cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

// ============================================================================
// CACHE HIERARCHY COMMUNICATIONS
// ============================================================================

void Cache::LoadLine(uint address, CacheLine& line)
{
	uint lineAddress = address & 0xFFFFFFC0;
	uint tag = (address >> 13);
	debugL2(level, "Level %u cache load: \n", level);
	__declspec(align(64)) CacheLine loadLine;
	switch (level) {
	case 1:
		L2Cache.GetLine(lineAddress, loadLine);
		break;
	case 2:
#ifdef LEVEL3
		L3Cache.GetLine(lineAddress, loadLine);
#else
		LoadLineFromMem(lineAddress, loadLine);
#endif
		break;
	case 3:
		LoadLineFromMem(lineAddress, loadLine);
		break;
	default:
		LoadLineFromMem(lineAddress, loadLine);
		break;
	}
	memcpy(line.data, loadLine.data, SIZE_OF_CACHE_LINE); // fetch 64 bytes into line
	line.tag = tag;
	line.valid = true;
}

void Cache::WriteLine(uint address, CacheLine& line)
{
	__declspec(align(64)) CacheLine writeLine;
	writeLine = line;
	debugL2(level, "Level %u cache write: ", level);
	switch (level) {
	case 1:
		L2Cache.SetLine(address, writeLine);
		break;
	case 2:
#ifdef LEVEL3
		L3Cache.SetLine(address, writeLine);
#else
		WriteLineToMem(address, writeLine);
#endif
		break;
	case 3:
		WriteLineToMem(address, writeLine);
		break;
	default:
		WriteLineToMem(address, writeLine);
		break;
	}
}

void Cache::GetLine(uint address, CacheLine& line)
{
	readCount++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debugL2(level, "Read: %d %d %d %d\n", address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			memcpy(line.data, (void*)cache[set][slot].data, SIZE_OF_CACHE_LINE);
			debugL2(level, "Read hit: %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Cache miss - use eviction policy 
	readMiss++;
	debugL2(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	uint slot = RandomReplacement();

	if (cache[set][slot].dirty)
	{
		debugL2(level, "Write old cache line: %d %d %d %d\n", cache[set][slot].tag << 13 | set << 6, line.tag, set, 0);
		WriteLine((uint)(cache[set][slot].tag << 13 | set << 6), cache[set][slot]);
	}

	// Cache read miss: read data from RAM
	LoadLine(address, cache[set][slot]);
	cache[set][slot].valid = true;
	cache[set][slot].dirty = false;
	memcpy(line.data, cache[set][slot].data, SIZE_OF_CACHE_LINE); // fetch 64 bytes into line
	return;
}

void Cache::SetLine(uint address, CacheLine& line)
{

	writeCount++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debugL2(level, "Write: %d %d %d %d\n", address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			memcpy(cache[set][slot].data, (void*)line.data, SIZE_OF_CACHE_LINE);
			cache[set][slot].dirty = true;
			debugL2(level, "Write hit: %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Find invalid set/slot and read all 64B from mem and save value to cache (4B)
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (!cache[set][slot].valid)
		{
			//__declspec(align(64)) CacheLine loadLine;
			//LoadLineFromMem(address, loadLine);

			// Store to cache
			//memcpy(cache[set][slot].data, (void*)loadLine.data, SIZE_OF_CACHE_LINE); // fetch 64 bytes into line
			memcpy(cache[set][slot].data, (void*)line.data, SIZE_OF_CACHE_LINE);
			cache[set][slot].tag = tag;
			//cache[set][slot].data[offset >> 2] = value; // All dirty
			cache[set][slot].dirty = true;
			cache[set][slot].valid = true;
			debug("Write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}


	// Cache miss - use eviction policy
	writeMiss++;
	uint slot = RandomReplacement();
	debugL2(level, "Write miss: %d %d %d %d\n", address, tag, set, offset);

	// Sloth dirty store whole slot to RAM
	if (cache[set][slot].dirty)
	{
		debugL2(level, "Write old: %d %d %d %d\n", cache[set][slot].tag << 13 | set << 6, tag, set, offset);
		WriteLine((uint)(cache[set][slot].tag << 13 | set << 6), cache[set][slot]);
	}

	memcpy(cache[set][slot].data, (void*)line.data, SIZE_OF_CACHE_LINE);
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

// ============================================================================
// CACHE HELPER FUNCTIONS
// ============================================================================

void Cache::WriteDirtyLine(CacheLine& line, uint set)
{
	if (line.dirty)
	{
		debugL2(level, "Cache read miss, write to mem: %d %d %d\n", line.tag << 13 | set << 6, line.tag, set);
		WriteLine((line.tag << 13 | set << 6), line);
	}
}

// ============================================================================
// CACHE EVICTION POLICIES
// ============================================================================

uint Cache::RandomReplacement()
{
	return rand() % nWaySetAssociative;
}


// ============================================================================
// CACHE PERFORMANCE ANALYSIS
// ============================================================================

void CachePerformancePerFrame() {
	nFrame++;
	printf("============================================================================\n");
	printf("Frame: %d\n", nFrame);
	printf("Reads: %d - Reads miss: %d (%.3lf%%)\n", L1Cache.readCount, L1Cache.readMiss,
													((float)L1Cache.readMiss/L1Cache.readCount) * 100);
	printf("Write: %d - Write miss: %d (%.3lf%%)\n", L1Cache.writeCount, L1Cache.writeMiss, 
													((float)L1Cache.writeMiss/L1Cache.writeCount) * 100);
	printf("	Reads: %d - Reads miss: %d (%.3lf%%)\n", L2Cache.readCount, L2Cache.readMiss,
													((float)L2Cache.readMiss / L1Cache.readCount) * 100);
	printf("	Write: %d - Write miss: %d (%.3lf%%)\n", L2Cache.writeCount, L2Cache.writeMiss, 
													((float)L2Cache.writeMiss / L1Cache.writeCount) * 100);
	printf("		Reads: %d - Reads miss: %d (%.3lf%%)\n", L3Cache.readCount, L3Cache.readMiss,
		((float)L3Cache.readMiss / L1Cache.readCount) * 100);
	printf("		Write: %d - Write miss: %d (%.3lf%%)\n", L3Cache.writeCount, L3Cache.writeMiss,
		((float)L3Cache.writeMiss / L1Cache.writeCount) * 100);

	L1Cache.readCount = 0, L1Cache.readMiss = 0, L1Cache.writeCount = 0, L1Cache.writeMiss = 0;
	L2Cache.readCount = 0, L2Cache.readMiss = 0, L2Cache.writeCount = 0, L2Cache.writeMiss = 0;
	L3Cache.readCount = 0, L3Cache.readMiss = 0, L3Cache.writeCount = 0, L3Cache.writeMiss = 0;
}

// ============================================================================
// CACHE SIMULATOR LOW LEVEL MEMORY ACCESS WITH LATENCY SIMULATION
// ============================================================================

uint* RAM = (uint*)MALLOC64( 20 * 1024 * 1024 ); // simulated RAM

// load a cache line from memory; simulate RAM latency
void Cache::LoadLineFromMem( uint address, CacheLine& line )
{
	debugL2(level, "LOAD FROM MEM!!!\n");
    uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
    memcpy(line.data, (void*)lineAddress, SIZE_OF_CACHE_LINE); // fetch 64 bytes into line
    DELAY;
}

// write a cache line to memory; simulate RAM latency
void Cache::WriteLineToMem( uint address, CacheLine& line )
{
	debugL2(level, "WRITE TO MEM!!!\n");
    uint lineAddress = address & 0xFFFFFFC0; // set last six bit to 0
    memcpy((void*)lineAddress, line.data, SIZE_OF_CACHE_LINE); // fetch 64 bytes into line
    DELAY;
}