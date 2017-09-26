#include	"precomp.h"

Cache CPUCache[NUM_OF_LEVELS] = {
	Cache(1, LEVEL1_SIZE, LEVEL1_N_WAY_SET_ASSOCIATIVE)
	// Instantiate the cache
#if NUM_OF_LEVELS > 1
	,Cache(2, LEVEL2_SIZE, LEVEL2_N_WAY_SET_ASSOCIATIVE)
#endif
#if NUM_OF_LEVELS > 2
	,Cache(3, LEVEL3_SIZE, LEVEL3_N_WAY_SET_ASSOCIATIVE)
#endif
};

Surface Cache::realTimeSurface(SCRWIDTH, REAL_TIME_SCRHEIGHT);

// Helper functions; forward all requests to the cache
uint LOADINT( uint* address ) { return CPUCache[L1].Read32bit( (uint)address ); }
void STOREINT( uint* address, uint value ) { CPUCache[L1].Write32bit( (uint)address, value ); }
float LOADFLOAT( float* a ) { uint v = LOADINT( (uint*)a ); return *(float*)&v; }
void STOREFLOAT( float* a, float f ) { uint v = *(uint*)&f; STOREINT( (uint*)a, v ); }
vec3 LOADVEC( vec3* a ) { vec3 r; for( int i = 0; i < 4; i++ ) r.cell[i] = LOADFLOAT( (float*)a + i ); return r; }
void STOREVEC( vec3* a, vec3 t ) { for( int i = 0; i < 4; i++ ) { float v = t.cell[i]; STOREFLOAT( (float*)a + i, v ); } }
void* LOADPTR( void* a ) { uint v = LOADINT( (uint*)a ); return *(void**)&v; }
void STOREPTR( void* a, void* p ) { uint v = *(uint*)&p; STOREINT( (uint*)a, v ); }

// Wider operators
void* LOAD64PTR(void* a) { uint v = LOADINT((uint*)a); return *(void**)&v; }
void STORE64PTR(void* a, void* p) { uint v = *(uint*)&p; STOREINT((uint*)a, v); }
vec3 LOAD128VEC(vec3* a) { vec3 r; for (int i = 0; i < 4; i++) r.cell[i] = LOADFLOAT((float*)a + i); return r; }
void STORE128VEC(vec3* a, vec3 t) { for (int i = 0; i < 4; i++) { float v = t.cell[i]; STOREFLOAT((float*)a + i, v); } }
// ============================================================================
// CACHE SIMULATOR IMPLEMENTATION
// ============================================================================

uint max = 100;
uint Cache::performance[SCRWIDTH][4];
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
	int sets = size / (SIZE_OF_CACHE_LINE * nWaySetAssociative);
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

/*void* Cache::Read(uint address, uint size, void* value)
{

}*/


// Reading 32-bit values
uint Cache::Read32bit(uint address)
{
	counters[READ][ALL]++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debugL2(level, "------------------------------------------------\n");
	debugL2(level, "Read %d: %d %d %d %d\n", counters[READ][ALL], address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			debug("Read hit: %d %d %d %d\n", address, tag, set, offset);
			debugL2(level, "Read value%u\n", cache[set][slot].data[offset >> 2]);
			UpdateEviction(set, slot);
			return cache[set][slot].data[offset >> 2];
		}
	}

	// Cache miss - use eviction policy 
	debugL2(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	counters[READ][MISS]++;
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, READ);
	LoadLine(address, cache[set][slot]);
	UpdateEviction(set, slot);
	return cache[set][slot].data[offset >> 2];
}

// Writing 32-bit values
void Cache::Write32bit(uint address, uint value)
{

	counters[WRITE][ALL]++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uint tag = (address >> 13);
	debugL2(level, "------------------------------------------------\n");
	debugL2(level, "Write %d: %d %d %d %d\n", counters[WRITE][ALL], address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			cache[set][slot].data[offset >> 2] = value;
			cache[set][slot].dirty = true;
			UpdateEviction(set, slot);
			debug("	Cache write: %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	debug("Write miss: %d %d %d %d\n", address, tag, set, offset);

	counters[WRITE][MISS]++;
	// Find invalid set/slot and read all 64B from mem and save value to cache (4B)
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (!cache[set][slot].valid)
		{
			LoadLine(address, cache[set][slot]);
			UpdateEviction(set, slot);
			cache[set][slot].data[offset >> 2] = value; // All dirty
			cache[set][slot].dirty = true; 
			debug("	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Use eviction policy
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, WRITE);
	LoadLine(address, cache[set][slot]);
	UpdateEviction(set, slot);
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
#if NUM_OF_LEVELS > 1
		CPUCache[L2].GetLine(lineAddress, loadLine);
		break;
#endif
	case 2:
#if NUM_OF_LEVELS > 2
		CPUCache[L3].GetLine(lineAddress, loadLine);
		break;
#endif
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
#if NUM_OF_LEVELS > 1
		CPUCache[L2].SetLine(address, writeLine);
		break;
#endif
	case 2:
#if NUM_OF_LEVELS > 2
		CPUCache[L3].SetLine(address, writeLine);
		break;
#endif
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
	counters[READ][ALL]++;
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
	counters[READ][MISS]++;
	debugL2(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, READ);
	LoadLine(address, cache[set][slot]);
	memcpy(line.data, cache[set][slot].data, SIZE_OF_CACHE_LINE);
}

void Cache::SetLine(uint address, CacheLine& line)
{
	counters[WRITE][ALL]++;
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

	counters[WRITE][MISS]++;
	// Find invalid set/slot
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
	uint slot = Evict(set);
	// Miss only if accessed next level in cache hierarchy
	WriteDirtyLine(cache[set][slot], set, WRITE);
	memcpy(cache[set][slot].data, (void*)line.data, SIZE_OF_CACHE_LINE);
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

// ============================================================================
// CACHE HELPER FUNCTIONS
// ============================================================================

inline void Cache::WriteDirtyLine(CacheLine& line, uint set, uint operation)
{
	if (line.dirty)
	{
		counters[operation][WRITE_LINE]++;
		debugL2(level, "Cache read miss, write to mem: %d %d %d\n", line.tag << 13 | set << 6, line.tag, set);
		WriteLine((line.tag << 13 | set << 6), line);
	}
}

// ============================================================================
// CACHE EVICTION POLICIES
// ============================================================================

uint Cache::Evict(uint set)
{
#ifdef EP_LRU
	for (uint s = 0; s < nWaySetAssociative; s++)
	{
		if (cache[set][s].timestamp == 0 && cache[set][s].valid)
			return s;
	}
	return 0;
#elif defined(EP_FIFO)
	return 0;
#elif defined(EP_MRU)
	return 0;
#else
	return rand() % nWaySetAssociative;
#endif
}

void Cache::UpdateEviction(uint set, uint accessedSlot)
{
#ifdef EP_LRU
	// LRU 
	debugL2(level, "Accessed %u: \n", accessedSlot);
	uint previousTimestamp = cache[set][accessedSlot].timestamp;
	for (uint s = 0; s < nWaySetAssociative; s++)
	{
		debugL2(level, "%u: (%u ", s, cache[set][s].timestamp);
		if (cache[set][s].timestamp > previousTimestamp)
			cache[set][s].timestamp--;
		debugL2(level, "%u) ",cache[set][s].timestamp);
	}
	debugL2(level, "\n");
	cache[set][accessedSlot].timestamp = 3;
#endif
}

// ============================================================================
// CACHE PERFORMANCE ANALYSIS
// ============================================================================
void Cache::GetRealTimePerformance(Surface* gameScreen, uint nFrame) {
	// Get relative of READ

	// Get relative of WRITE

	// Get relative of ALL ACCESS 
	uint offset = nFrame >= SCRWIDTH;
	uint width = (offset) ?  SCRWIDTH-1 : nFrame;
	uint sumOfMeasurement = 0, measurement = 0;
	
	// Get new values
	for (uint level = 0; level < NUM_OF_LEVELS; level++) {
#ifdef PERFORMANCE_ACCESS
		uint measurement = CPUCache[level].counters[READ][ALL] + CPUCache[level].counters[WRITE][ALL];
#else
		measurement = CPUCache[level].counters[READ][ALL] + CPUCache[level].counters[WRITE][ALL];
#endif
		sumOfMeasurement += measurement;
		performance[width][level] = measurement;
	}

	// Add DRAM access
	measurement = CPUCache[NUM_OF_LEVELS - 1].counters[READ][MISS] + 
					CPUCache[NUM_OF_LEVELS - 1].counters[WRITE][WRITE_LINE] + 
					CPUCache[NUM_OF_LEVELS - 1].counters[READ][WRITE_LINE];
	sumOfMeasurement += measurement;
	performance[width][NUM_OF_LEVELS] = measurement;

	// Rescale history if new max
	bool newMax = max < sumOfMeasurement;
	if (newMax) 
		max = sumOfMeasurement + 100000;
	

	float scale = (float)REAL_TIME_SCRHEIGHT / max;
	if (offset || newMax) 
	{
		realTimeSurface.Clear(0);
		// Scale history
		for (uint x = 0; x < width; x++)
		{
			float acc = 0;
			for (uint level = 0; level < NUM_OF_LEVELS + 1; level++)
			{
				performance[x][level] = performance[x + offset][level];
				float lineHeight = performance[x][level] * scale;
				realTimeSurface.Line(x, REAL_TIME_SCRHEIGHT - 1 - acc, x, REAL_TIME_SCRHEIGHT - 1 - lineHeight - acc, COLOR[level]);
				acc += lineHeight;
			}
		}
	}

	// New one
	float acc = 0;
	for (uint level = 0; level < NUM_OF_LEVELS + 1; level++) {
		float lineHeight = performance[width][level] * scale;
		realTimeSurface.Line(width, REAL_TIME_SCRHEIGHT - 1 - acc, width, REAL_TIME_SCRHEIGHT - 1 - lineHeight - acc, COLOR[level]);
		acc += lineHeight;
	}
	
	// Render new graph
	realTimeSurface.BlendCopyTo(gameScreen, 0, SCRHEIGHT - REAL_TIME_SCRHEIGHT - 1);
}

void Cache::GetPerformancePerFrame(uint nFrame)
{
	printf("\n==========================================================================\n");
	printf("Frame: %5d |\n", nFrame);
	printf("%12s | %12s | %12s | %12s | %12s |\n", "Level (R\\W)", "All accesses", "Misses", "Write", "Miss ratio");
	printf("--------------------------------------------------------------------------\n");
	for (uint level = 0; level < NUM_OF_LEVELS; level++)
	{
		CPUCache[level].GetPerformance(false, true);
	}

	for (uint level = 0; level < NUM_OF_LEVELS; level++)
	{
		CPUCache[level].ResetPerformanceCounters();
	}
}

void Cache::GetPerformance(const bool withTitles, const bool withOffset)
{
	char* offset = "";
	if (withOffset)
		offset = (char *)LEVEL_OFFSET[level - 1];

	if (withTitles) {
		printf("%12s | %12s | %12s | %12s | %12s |\n", "", "All", "Miss", "Write", "Actual miss", "Actual miss (all)");
	}

	float readMiss = counters[READ][MISS] + counters[READ][WRITE_LINE];
	float writeMiss = counters[WRITE][MISS] + counters[WRITE][WRITE_LINE];

	printf("L%1dR%9s | %12d | %12d | %12d | %12.3f | %12.3f |\n", level, "", 
		counters[READ][ALL], counters[READ][MISS], counters[READ][WRITE_LINE], 
		((float)readMiss / CPUCache[L1].counters[READ][ALL]) * 100,
		((float)counters[READ][MISS] / CPUCache[L1].counters[READ][ALL]) * 100);
	printf("L%1dW%9s | %12d | %12d | %12d | %12.3f | %12.3f |\n", level, "", 
		counters[WRITE][ALL], counters[WRITE][MISS], counters[WRITE][WRITE_LINE], 
		((float)writeMiss / CPUCache[L1].counters[WRITE][ALL]) * 100,
		((float)counters[WRITE][MISS] / CPUCache[L1].counters[WRITE][ALL]) * 100);
	printf("L%1dRW%8s | %12d | %12d | %12d | %12.3f | %12.3f |\n", level, "",
		counters[WRITE][ALL] + counters[READ][ALL], counters[WRITE][MISS] + counters[READ][MISS], counters[WRITE][WRITE_LINE] + counters[READ][WRITE_LINE],
		((float)(writeMiss + readMiss) / CPUCache[L1].counters[WRITE][ALL]) * 100,
		((float)(counters[WRITE][MISS] + counters[READ][MISS]) / (CPUCache[L1].counters[WRITE][ALL]+ CPUCache[L1].counters[READ][ALL]) * 100));
	printf("--------------------------------------------------------------------------\n");
}

void Cache::ResetPerformanceCounters()
{
	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 3; j++)
			counters[i][j] = 0;
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