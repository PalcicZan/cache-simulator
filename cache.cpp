#include "precomp.h"

// Instantiate the cache
Cache CPUCache[NUM_OF_LEVELS] = {
	Cache(L1, LEVEL1_SIZE, LEVEL1_N_WAY_SET_ASSOCIATIVE)
#if NUM_OF_LEVELS > 1
	,Cache(L2, LEVEL2_SIZE, LEVEL2_N_WAY_SET_ASSOCIATIVE)
#endif
#if NUM_OF_LEVELS > 2
	,Cache(L3, LEVEL3_SIZE, LEVEL3_N_WAY_SET_ASSOCIATIVE)
#endif
};

// Helper functions; forward all requests to the cache
uint LOADINT(uint* address) { uint value; CPUCache[L1].Read((uintptr_t)address, 4, &value); return value; }
void STOREINT(uint* address, uint value) { CPUCache[L1].Write((uintptr_t)address, 4, &value); }
float LOADFLOAT(float* a) { uint v = LOADINT((uint*)a); return *(float*)&v; }
void STOREFLOAT(float* a, float f) { uint v = *(uint*)&f; STOREINT((uint*)a, v); }
// Support for wider types
#if defined(_WIN64) || defined(SUPPORT_WIDER_TYPES)
void STOREVEC(vec3* a, vec3 t) { CPUCache[L1].Write((uintptr_t)a, sizeof(vec3), &t); }
vec3 LOADVEC(vec3* a) { vec3 r; CPUCache[L1].Read((uintptr_t)a, sizeof(vec3), &r); return r; };
void* LOADPTR(void* a) { uintptr_t v; CPUCache[L1].Read((uintptr_t)a, sizeof(uintptr_t), &v); return *(void**)&v; }
void STOREPTR(void* a, void* p) { uintptr_t v = *(uint*)&p; CPUCache[L1].Write((uintptr_t)a, sizeof(uintptr_t), &v); }
#else
vec3 LOADVEC(vec3* a) { vec3 r; for (int i = 0; i < 4; i++) r.cell[i] = LOADFLOAT((float*)a + i); return r; }
void STOREVEC(vec3* a, vec3 t) { for (int i = 0; i < 4; i++) { float v = t.cell[i]; STOREFLOAT((float*)a + i, v); } }
void* LOADPTR(void* a) { uint v = LOADINT((uint*)a); return *(void**)&v; }
void STOREPTR(void* a, void* p) { uint v = *(uint*)&p; STOREINT((uint*)a, v); }
#endif

// ============================================================================
// CACHE SHOW_PERFORMANCE HELPERS
// ============================================================================
// Performance surface
Surface Cache::realTimeSurface(SCRWIDTH, REAL_TIME_SCRHEIGHT);
#ifdef SAVE_PERFORMANCE
FILE* Cache::results = fopen(FILE_NAME, "w");
#endif
float Cache::performance[SCRWIDTH][NUM_OF_LEVELS + 1];
#ifdef PERFORMANCE_AMAT
float max = 12.0;
#else
float max = 100.0;
#endif
// ============================================================================
// CACHE SIMULATOR IMPLEMENTATION
// ============================================================================
// Cache constructor
Cache::Cache() : level(L1), size(LEVEL1_SIZE), nWaySetAssociative(LEVEL1_N_WAY_SET_ASSOCIATIVE)
{
#if EVICTION_POLICY == EP_RR
	srand((uint)time(NULL));
#endif
	InitCache();
}

Cache::Cache(const uint level, const uint size, const uint n) : level(level), size(size), nWaySetAssociative(n)
{
#if EVICTION_POLICY == EP_RR
	srand((uint)time(NULL));
#endif
	InitCache();
}

void Cache::InitCache()
{
	int sets = size / (CACHE_LINE_SIZE * nWaySetAssociative);
	cache = new CacheLine*[sets];
	for (int i = 0; i < sets; ++i)
		cache[i] = new CacheLine[nWaySetAssociative];
}

// Cache destructor
Cache::~Cache()
{
	int sets = size / (CACHE_LINE_SIZE * nWaySetAssociative);
	for (int i = 0; i < sets; ++i)
	{
		delete[] cache[i];
	}
	delete[] cache;
	printf("%f\n", dummy); // Prevent dead code elimination
}

void Cache::Read(const uintptr_t address, const uint size, void* data)
{
	counters[READ][ALL]++;
	uint offset = (address & 63) >> 2;
	uint set = (address >> 6) & 127;
	uintptr_t tag = (address >> 13);
	LOG(level, "------------------------------------------------\n");
	LOG(level, "Read %d: %d %d %d %d\n", counters[READ][ALL], address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			LOG(level, "Read hit: %d %d %d %d\n", address, tag, set, offset);
			LOG(level, "Read value %u\n", cache[set][slot].data[offset]);
			UpdateEviction(set, slot, tag);
			memcpy(data, &cache[set][slot].data[offset], size);
			return;
		}
	}

	// Cache miss - use eviction policy 
	LOG(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	counters[READ][MISS]++;
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, READ);
	UpdateEviction(set, slot, tag);
	LoadLine(address, cache[set][slot]);
	memcpy(data, &cache[set][slot].data[offset], size);
}

void Cache::Write(const uintptr_t address, const uint size, const void* value)
{
	counters[WRITE][ALL]++;
	uint offset = (address & 63) >> 2;
	uint set = (address >> 6) & 127;
	uintptr_t tag = (address >> 13);
	LOG(level, "------------------------------------------------\n");
	LOG(level, "Write %d: %d %d %d %d\n", counters[WRITE][ALL], address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			UpdateEviction(set, slot, tag);
			memcpy(&cache[set][slot].data[offset], value, size);
			cache[set][slot].dirty = true;
			LOG(level,"	Cache write: %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	LOG(level,"Write miss: %d %d %d %d\n", address, tag, set, offset);

	counters[WRITE][MISS]++;
	// Find invalid set/slot and read all 64B from mem and save value to cache (4B)
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (!cache[set][slot].valid)
		{
			UpdateEviction(set, slot, tag);
			if (level == L1)
				LoadLine(address, cache[set][slot]);

			memcpy(&cache[set][slot].data[offset], value, size);
			cache[set][slot].tag = tag;
			cache[set][slot].dirty = true;
			cache[set][slot].valid = true;
			LOG(level, "	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}
	// Use eviction policy
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, WRITE);
	UpdateEviction(set, slot, tag);
	if (level == L1)
		LoadLine(address, cache[set][slot]);

	memcpy(&cache[set][slot].data[offset], value, size);
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

// ============================================================================
// CACHE HIERARCHY COMMUNICATIONS
// ============================================================================

void Cache::LoadLine(const uintptr_t address, CacheLine& line)
{
	LOG(level, "Level %u cache load: \n", level);
	uintptr_t lineAddress = address & 0xFFFFFFFFFFFFFFC0;
	__declspec(align(64)) CacheLine loadLine;
	switch (level)
	{
	case L1:
#if NUM_OF_LEVELS > 1
		CPUCache[L2].Read(lineAddress, CACHE_LINE_SIZE, &loadLine.data);
		break;
#endif
	case L2:
#if NUM_OF_LEVELS > 2
		CPUCache[L3].Read(lineAddress, CACHE_LINE_SIZE, &loadLine.data);
		break;
#endif
	case L3:
		LoadLineFromMem(lineAddress, loadLine);
		break;
	default:
		printf("FUCK");
		LoadLineFromMem(lineAddress, loadLine);
		break;
	}
	memcpy(line.data, loadLine.data, CACHE_LINE_SIZE); // fetch 64 bytes into line
	line.tag = address >> 13;
	line.valid = true;
	line.dirty = false;
}

void Cache::WriteLine(const uintptr_t address, const CacheLine& line)
{
	// Address is already aligned
	LOG(level, "Level %u cache write: ", level);
	__declspec(align(64)) CacheLine writeLine = line;
	switch (level)
	{
	case L1:
#if NUM_OF_LEVELS > 1
		//CPUCache[L2].SetLine(address, writeLine);
		CPUCache[L2].Write(address, CACHE_LINE_SIZE, &writeLine.data);
		break;
#endif
	case L2:
#if NUM_OF_LEVELS > 2
		//CPUCache[L3].SetLine(address, writeLine);
		CPUCache[L3].Write(address, CACHE_LINE_SIZE, &writeLine.data);
		break;
#endif
	case L3:
		WriteLineToMem(address, writeLine);
		break;
	default:
		WriteLineToMem(address, writeLine);
		break;
	}
}

// ============================================================================
// CACHE HELPER FUNCTIONS
// ============================================================================

void Cache::WriteDirtyLine(CacheLine& line, uint set, uint operation)
{
	if (line.dirty)
	{
		counters[operation][WRITE_LINE]++;
		WriteLine((line.tag << 13 | set << 6), line);
		LOG(level, "Cache read miss, write to mem: %d %d %d\n", line.tag << 13 | set << 6, line.tag, set);
	}
}

// ============================================================================
// CACHE EVICTION POLICIES
// ============================================================================

inline uint Cache::Evict(uint set)
{
	// Always evict invalid first
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (!cache[set][slot].valid)
		{
			return slot;
		}
	}
#if EVICTION_POLICY == EP_RR
	return rand() % nWaySetAssociative;
#else

	// Brute force - avoid using STD
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
#if EVICTION_POLICY == EP_LRU
		if (cache[set][slot].timestamp == 0)
		{
#elif EVICTION_POLICY == EP_FIFO ||  EVICTION_POLICY == EP_MRU
		if (cache[set][slot].timestamp == (nWaySetAssociative - 1))
		{
#endif
			return slot;
		}
	}
	return 0;
#endif
}

inline void Cache::UpdateEviction(uint set, uint accessedSlot, uintptr_t tag)
{
#if EVICTION_POLICY == EP_LRU || EVICTION_POLICY == EP_MRU
	// Correct all, avoid overflow of counters.
	uint previousTimestamp = cache[set][accessedSlot].timestamp;
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].timestamp > previousTimestamp)
			cache[set][slot].timestamp--;
		LOG(level, "%u ", cache[set][slot].timestamp);
	}
	LOG(level, "\n");
	LOG(level, "%u %u\n",accessedSlot, cache[set][accessedSlot].timestamp);
	cache[set][accessedSlot].timestamp = nWaySetAssociative - 1;
#elif EVICTION_POLICY == EP_FIFO
	if (tag == cache[set][accessedSlot].tag) return;
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid)
			cache[set][slot].timestamp++;
	}
	cache[set][accessedSlot].timestamp = 0;
#endif
}

// ============================================================================
// CACHE SHOW_PERFORMANCE ANALYSIS
// ============================================================================

void Cache::DrawGraphPerformance(Surface* gameScreen, const uint nFrame)
{
	uint scrHeight = REAL_TIME_SCRHEIGHT;
#if !defined(BLEND_PERFORMANCE)
	realTimeSurface = *gameScreen;
	scrHeight = SCRHEIGHT;
#endif

	uint offset = nFrame >= SCRWIDTH;
	uint width = (offset) ? SCRWIDTH - 1 : nFrame;
	float sumOfMeasurement = 0, selectedMeasurement = 0;

#ifdef PERFORMANCE_PER_LEVEL
	uint numOfLevels = NUM_OF_LEVELS;
#else
	uint numOfLevels = 1;
#endif

	// Get new values
	for (uint level = 0; level < numOfLevels; level++)
	{
		selectedMeasurement = CPUCache[level].measurement[SELECTED_PERFORMANCE];
		sumOfMeasurement += selectedMeasurement;
		performance[width][level] = (float)selectedMeasurement;
	}

#if INCLUDE_DRAM == 1 && SELECTED_PERFORMANCE == N_ACCESSES
	// Add DRAM access
	selectedMeasurement = CPUCache[NUM_OF_LEVELS - 1].counters[READ][MISS] +
		CPUCache[NUM_OF_LEVELS - 1].counters[WRITE][WRITE_LINE] +
		CPUCache[NUM_OF_LEVELS - 1].counters[READ][WRITE_LINE];
	sumOfMeasurement += selectedMeasurement;
	performance[width][NUM_OF_LEVELS] = (float)selectedMeasurement;
#endif

	// Rescale history if new max
	//bool newMax = max < sumOfMeasurement;
	if (max < sumOfMeasurement)
		max = sumOfMeasurement + max / 10;

	//bool offset = outFrame;
	// Scale history - brute forced every time - I know it doesn't need to be - Optimization opportunity
	for (uint x = 0; x <= width; x++)
	{		
		offset &= (x != width);
		float acc = 0;
		for (uint level = 0; level < numOfLevels + (INCLUDE_DRAM); level++)
		{
			performance[x][level] = performance[x + offset][level];
			float lineHeight = performance[x][level] * REAL_TIME_SCRHEIGHT / max;
			realTimeSurface.Line((float)x, scrHeight - 1 - acc, (float)x, scrHeight - 1 - lineHeight - acc, COLOR[level]);
			acc += lineHeight;
		}
	}

	// Render new graph
#ifdef BLEND_PERFORMANCE
	realTimeSurface.BlendCopyTo(gameScreen, 0, SCRHEIGHT - REAL_TIME_SCRHEIGHT);
#endif
}

void Cache::GetPerformancePerFrame(Surface* gameScreen, const uint nFrame)
{
	// Print to screen 
	char t[128];
	sprintf(t, "Frame: %5d", nFrame);
	gameScreen->Print(t, 2, 2, 0xffffff); 
	float localMissRateL2 = 0.f, localMissRateL3 = 0.f;
#if NUM_OF_LEVELS > 2
	localMissRateL2 = (CPUCache[L2].measurement[LOCAL_MISS_RATE] / 100);
	localMissRateL3 = (CPUCache[L3].measurement[LOCAL_MISS_RATE] / 100);
#elif NUM_OF_LEVELS > 1
	localMissRateL2 = (CPUCache[L2].measurement[LOCAL_MISS_RATE] / 100);
#endif
	// Average memory access time
	float AMAT = L1_PENALTY + (CPUCache[L1].measurement[LOCAL_MISS_RATE] / 100)
		* (L2_PENALTY + localMissRateL2	* (L3_PENALTY + (localMissRateL3 * RAM_PENALTY)));
	CPUCache[L1].measurement[PERFORMANCE_AMAT] = AMAT; // Just to print in cache one performance
	sprintf(t, "AMAT: %4.2f cycles", AMAT);
	gameScreen->Print(t, 2, 12, 0xffffff);
	/*AMAT = ((CPUCache[L1].counters[READ][ALL] + CPUCache[L1].counters[WRITE][ALL])*L1_PENALTY +
		(CPUCache[L2].counters[READ][ALL] + CPUCache[L2].counters[WRITE][ALL])*L2_PENALTY +
			(CPUCache[L3].counters[READ][ALL] + CPUCache[L3].counters[WRITE][ALL])*L3_PENALTY +
			(CPUCache[L3].readMiss + CPUCache[L3].writeMiss)*RAM_PENALTY);*/
	sprintf(t, "Graph presenting %s.", PERFORMANCE_LABELS[SELECTED_PERFORMANCE]);
	gameScreen->Print(t, 2, 22, 0xffffff);
	int acc = 32;
#ifdef PERFORMANCE_PER_LEVEL 
	uint numOfLevels = NUM_OF_LEVELS + (INCLUDE_DRAM);
	uint i = 0;
#else  
	uint numOfLevels = 5;
	uint i = 4;
#endif
	for (i; i < numOfLevels; i++)
	{
		sprintf(t, LEVEL_LABELS[i]);
		gameScreen->Bar(2, acc, 6, acc+4, COLOR[i]);
		gameScreen->Print(t, 12, acc, 0xffffff);
		acc += 10;
	}
	// Print to stout and file
	printf("\n================================================================================================|\n");
	printf("Frame: %-11d | %-40s | %-14s |\n", nFrame, EVICTION_LABELS[EVICTION_POLICY], PERFORMANCE_LABELS[SELECTED_PERFORMANCE]);
	printf("================================================================================================|\n");
	printf("%5s | %10s | %10s | %10s | %14s | %14s | %14s |\n", "", "#ALL ACC", "#TAG MISS", "#WRITE", "GLOBAL MISS", "LOCAL MISS", "#MISS/#L1 ACC");
	printf("------------------------------------------------------------------------------------------------|\n");

	for (uint level = 0; level < NUM_OF_LEVELS; level++)
	{
		CPUCache[level].UpdatePerformance();
		CPUCache[level].GetPerformance();
	}

	// Reset counters
	for (uint level = 0; level < NUM_OF_LEVELS; level++)
		CPUCache[level].ResetPerformanceCounters();
}

void Cache::UpdatePerformance()
{
	measurement[N_ACCESSES] = (float)(counters[WRITE][ALL] + counters[READ][ALL]);
	measurement[READ_MISS] = (float)(counters[READ][MISS] + counters[READ][WRITE_LINE]);
	measurement[WRITE_MISS] = (level > L1) ? (float)(counters[WRITE][WRITE_LINE]) : (float)(counters[WRITE][MISS] + counters[WRITE][WRITE_LINE]);
	measurement[LOCAL_MISS_RATE] = (measurement[READ_MISS] + measurement[WRITE_MISS]) / measurement[N_ACCESSES] * 100;
	measurement[GLOBAL_MISS_RATE] = (measurement[WRITE_MISS] + measurement[READ_MISS]) / (float)(CPUCache[L1].counters[WRITE][ALL] + CPUCache[L1].counters[READ][ALL]) * 100;
}

void Cache::GetPerformance()
{
	float allAccessedToL1Cache = (float)(CPUCache[L1].counters[WRITE][ALL] + CPUCache[L1].counters[READ][ALL]);
	printf("L%1dR%2s | %10d | %10d | %10d | %13.2lf%% | %13.2lf%% | %13.2lf%% |\n", level + 1, "",
		   counters[READ][ALL], counters[READ][MISS], counters[READ][WRITE_LINE],
		   measurement[READ_MISS] / CPUCache[L1].counters[READ][ALL] * 100,
		   measurement[READ_MISS] / counters[READ][ALL] * 100,
		   (float)counters[READ][MISS] / CPUCache[L1].counters[READ][ALL] * 100);
	printf("L%1dW%2s | %10d | %10d | %10d | %13.2lf%% | %13.2lf%% | %13.2lf%% |\n", level + 1, "",
		   counters[WRITE][ALL], counters[WRITE][MISS], counters[WRITE][WRITE_LINE],
		   measurement[WRITE_MISS] / CPUCache[L1].counters[WRITE][ALL] * 100,
		   measurement[WRITE_MISS] / counters[WRITE][ALL] * 100,
		   (float)counters[WRITE][MISS] / CPUCache[L1].counters[WRITE][ALL] * 100);
	printf("L%1dRW%1s | %10d | %10d | %10d | %13.2lf%% | %13.2lf%% | %13.2lf%% |\n", level + 1, "",
		   counters[WRITE][ALL] + counters[READ][ALL], counters[WRITE][MISS] + counters[READ][MISS], counters[WRITE][WRITE_LINE] + counters[READ][WRITE_LINE],
		   measurement[GLOBAL_MISS_RATE], measurement[LOCAL_MISS_RATE], (counters[WRITE][MISS] + counters[READ][MISS]) / allAccessedToL1Cache * 100);
	printf("------------------------------------------------------------------------------------------------|\n");

#ifdef SAVE_PERFORMANCE
	if (results)
	{
		fprintf(results, "%lf;", measurement[SELECTED_PERFORMANCE]);
		if (level == NUM_OF_LEVELS - 1)
		{
			fprintf(results, "\n");
			fflush(results);
		}
	}
#endif
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

uint* RAM = (uint*)MALLOC64(20 * 1024 * 1024); // simulated RAM

// load a cache line from memory; simulate RAM latency
void Cache::LoadLineFromMem(const uintptr_t address, CacheLine& line)
{
	LOG(level, "LOAD FROM MEM!!!\n");
	memcpy(line.data, (void*)address, CACHE_LINE_SIZE); // fetch 64 bytes into line
	DELAY;
}

// write a cache line to memory; simulate RAM latency
void Cache::WriteLineToMem(const uintptr_t address, CacheLine& line)
{
	LOG(level, "WRITE TO MEM!!!\n");
	memcpy((void*)address, line.data, CACHE_LINE_SIZE); // fetch 64 bytes into line
	DELAY;
}