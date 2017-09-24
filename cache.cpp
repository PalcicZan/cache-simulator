#include	"precomp.h"

// Instantiate the cache
Cache L1Cache(1, LEVEL1_SIZE, LEVEL1_N_WAY_SET_ASSOCIATIVE);
Cache L2Cache(2, LEVEL2_SIZE, LEVEL2_N_WAY_SET_ASSOCIATIVE);
Cache L3Cache(3, LEVEL3_SIZE, LEVEL3_N_WAY_SET_ASSOCIATIVE);

// Helper functions; forward all requests to the cache
uint LOADINT( uint* address ) { return L1Cache.Read32bit( (uint)address ); }
void STOREINT( uint* address, uint value ) { L1Cache.Write32bit( (uint)address, value ); }
float LOADFLOAT( float* a ) { uint v = LOADINT( (uint*)a ); return *(float*)&v; }
void STOREFLOAT( float* a, float f ) { uint v = *(uint*)&f; STOREINT( (uint*)a, v ); }
vec3 LOADVEC( vec3* a ) { vec3 r; for( int i = 0; i < 4; i++ ) r.cell[i] = LOADFLOAT( (float*)a + i ); return r; }
void STOREVEC( vec3* a, vec3 t ) { for( int i = 0; i < 4; i++ ) { float v = t.cell[i]; STOREFLOAT( (float*)a + i, v ); } }
void* LOADPTR( void* a ) { uint v = LOADINT( (uint*)a ); return *(void**)&v; }
void STOREPTR( void* a, void* p ) { uint v = *(uint*)&p; STOREINT( (uint*)a, v ); }
Surface Cache::realTimeSurface(SCRWIDTH, SCRHEIGHT/4);
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
			debug("Read hit: %d %d %d %d\n", address, tag, set, offset);
			debugL2(level, "Read value%u\n", cache[set][slot].data[offset >> 2]);
			return cache[set][slot].data[offset >> 2];
		}
	}

	// Cache miss - use eviction policy 
	debugL2(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	readMiss++;
	uint slot = Evict();

	WriteDirtyLine(cache[set][slot], set);
	LoadLine(address, cache[set][slot]);
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
			LoadLine(address, cache[set][slot]);
			cache[set][slot].data[offset >> 2] = value; // All dirty
			cache[set][slot].dirty = true;
			debug("	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Use eviction policy
	debug("Write miss: %d %d %d %d\n", address, tag, set, offset);
	writeMiss++;
	uint slot = Evict();

	WriteDirtyLine(cache[set][slot], set);
	LoadLine(address, cache[set][slot]);

	cache[set][slot].data[offset >> 2] = value;
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
	line.dirty = false;
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
	uint slot = Evict();
	WriteDirtyLine(cache[set][slot], set);

	LoadLine(address, cache[set][slot]);
	memcpy(line.data, cache[set][slot].data, SIZE_OF_CACHE_LINE);
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
			memcpy(cache[set][slot].data, (void*)line.data, SIZE_OF_CACHE_LINE);
			cache[set][slot].tag = tag;
			cache[set][slot].dirty = true;
			cache[set][slot].valid = true;
			debug("Write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}


	// Cache miss - use eviction policy
	debugL2(level, "Write miss: %d %d %d %d\n", address, tag, set, offset);
	writeMiss++;
	uint slot = Evict();
	WriteDirtyLine(cache[set][slot], set);

	memcpy(cache[set][slot].data, (void*)line.data, SIZE_OF_CACHE_LINE);
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

// ============================================================================
// CACHE HELPER FUNCTIONS
// ============================================================================

inline void Cache::WriteDirtyLine(CacheLine& line, uint set)
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

uint Cache::Evict()
{
	/*switch (EV_POLICY) {
	case EV_LRU:
		
		break;
	case EV_RR:
		break;
	case EV_FIFO:
		break
	}*/
#ifdef EV_LRU
	return 0;
#else
	return rand() % nWaySetAssociative;
#endif
}

// ============================================================================
// CACHE PERFORMANCE ANALYSIS
// ============================================================================
void Cache::GetRealTimePerformance(Surface gameScreen, uint nFrame) {
	uint x = nFrame % SCRWIDTH;
	realTimeSurface.Line(1, (SCRHEIGHT/4)-3, 1, (SCRHEIGHT / 4) - 6, 0xFF0000);	// Satisfy time
	//realTimeSurface.Line(x, SCRHEIGHT, x, 100, 0x00FF00);			// Sort time
	//realTimeSurface.Line(x, SCRHEIGHT, x, 100, 0x0000FF);			// Render time
	//realTimeSurface.BlendCopyTo(&gameScreen, 0, SCRHEIGHT - SCRHEIGHT/4 - 1);
}

void Cache::GetPerformancePerFrame(uint nFrame)
{
	printf("============================================================================\n");
	printf("Frame: %d\n", nFrame);
	L1Cache.GetPerormance();
	L2Cache.GetPerormance();
	L3Cache.GetPerormance();
	L1Cache.ResetPerformanceCounters();
	L2Cache.ResetPerformanceCounters();
	L3Cache.ResetPerformanceCounters();
}

void Cache::GetPerormance()
{
	printf("%sReads: %d - Reads miss: %d (%.3lf%%)\n", LEVEL_OFFSET[level-1], readCount, readMiss,
		((float)readMiss / L1Cache.readCount) * 100);
	printf("%sWrite: %d - Write miss: %d (%.3lf%%)\n", LEVEL_OFFSET[level - 1], writeCount, writeMiss,
		((float)writeMiss / L1Cache.writeCount) * 100);
}

void Cache::ResetPerformanceCounters()
{
	readCount = 0, readMiss = 0, writeCount = 0, writeMiss = 0;
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