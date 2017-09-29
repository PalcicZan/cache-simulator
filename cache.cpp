#include	"precomp.h"

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

// Performance surface
Surface Cache::realTimeSurface(SCRWIDTH, REAL_TIME_SCRHEIGHT);

//uint LOADINT(uint* address) { return CPUCache[L1].Read32bit((uint)address); }
//void STOREINT(uint* address, uint value) { CPUCache[L1].Write32bit((uint)address, value); }
// Helper functions; forward all requests to the cache
uint LOADINT(uint* address) { uint value; CPUCache[L1].Read((uintptr_t)address, 4, &value); return value; }
void STOREINT(uint* address, uint value) { CPUCache[L1].Write((uintptr_t)address, 4, &value); }
float LOADFLOAT(float* a) { uint v = LOADINT((uint*)a); return *(float*)&v; }
void STOREFLOAT(float* a, float f) { uint v = *(uint*)&f; STOREINT((uint*)a, v);}

// Support for wider types
#ifdef _WIN64 || defined(SUPPORT_WIDER_TYPES)
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
// CACHE SIMULATOR IMPLEMENTATION
// ============================================================================
#ifdef PERFORMANCE_AMAT
uint max = 10;
#else
uint max = 100;
#endif
float Cache::performance[SCRWIDTH][NUM_OF_LEVELS + 1];
float Cache::AMAT;
// Cache constructor
Cache::Cache() : level(L1), size(LEVEL1_SIZE), nWaySetAssociative(LEVEL1_N_WAY_SET_ASSOCIATIVE)
{
	InitCache();
}

Cache::Cache(const uint level, const uint size, const uint n) : level(level), size(size), nWaySetAssociative(n)
{
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

void Cache::Read(const uintptr_t address, const uint size,  void* data)
{
	counters[READ][ALL]++;
	uint offset = (address & 63) >> 2;
	uint set = (address >> 6) & 127;
	uintptr_t tag = (address >> 13);
	debugL2(level, "------------------------------------------------\n");
	debugL2(level, "Read %d: %d %d %d %d\n", counters[READ][ALL], address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			debug("Read hit: %d %d %d %d\n", address, tag, set, offset);
			debug(level, "Read value %u\n", cache[set][slot].data[offset]);
			UpdateEviction(set, slot, tag);
			//cache[set][slot].data[offset >> 2];
			memcpy(data, &cache[set][slot].data[offset], size);
			return;
		}
	}

	// Cache miss - use eviction policy 
	debugL2(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	counters[READ][MISS]++;
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, READ);
	UpdateEviction(set, slot, tag);
	LoadLine(address, cache[set][slot]);
	//cache[set][slot].data[offset >> 2];

	memcpy(data, &cache[set][slot].data[offset], size);
}

void Cache::Write(const uintptr_t address, const uint size, const void* value)
{
	counters[WRITE][ALL]++;
	uint offset = (address & 63) >> 2;
	uint set = (address >> 6) & 127;
	uintptr_t tag = (address >> 13);
	debugL2(level, "------------------------------------------------\n");
	debugL2(level, "Write %d: %d %d %d %d\n", counters[WRITE][ALL], address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			UpdateEviction(set, slot, tag);
			memcpy(&cache[set][slot].data[offset], value, size);
			//cache[set][slot].data[offset >> 2] = value;
			cache[set][slot].dirty = true;
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
			UpdateEviction(set, slot, tag);
			if(level == L1)
				LoadLine(address, cache[set][slot]);
			//cache[set][slot].data[offset >> 2] = value;
			memcpy(&cache[set][slot].data[offset], value, size);
			cache[set][slot].tag = tag;
			cache[set][slot].dirty = true;
			cache[set][slot].valid = true;
			debug("	Cache write (Not valid): %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}
	// Use eviction policy
	uint slot = Evict(set); 
	WriteDirtyLine(cache[set][slot], set, WRITE);
	UpdateEviction(set, slot, tag);
	if (level == L1)
		LoadLine(address, cache[set][slot]);
	//cache[set][slot].data[offset >> 2] = value;
	memcpy(&cache[set][slot].data[offset], value, size);
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}

// ============================================================================
// CACHE HIERARCHY COMMUNICATIONS
// ============================================================================

inline void Cache::LoadLine(const uintptr_t address, CacheLine& line)
{
	uintptr_t lineAddress = address & 0xFFFFFFFFFFFFFFC0;
	debugL2(level, "Level %u cache load: \n", level);
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
	__declspec(align(64)) CacheLine writeLine;
	writeLine = line;
	debugL2(level, "Level %u cache write: ", level);
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
/*
void Cache::GetLine(const uintptr_t address, CacheLine& line)
{
	counters[READ][ALL]++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uintptr_t tag = (address >> 13);
	debugL2(level, "Read: %d %d %d %d\n", address, tag, set, offset);

	// Get if valid and matching tag
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].tag == tag && cache[set][slot].valid)
		{
			UpdateEviction(set, slot, tag);
			memcpy(line.data, (void*)cache[set][slot].data, CACHE_LINE_SIZE);
			debugL2(level, "Read hit: %d %d %d %d\n", address, tag, set, offset);
			return;
		}
	}

	// Cache miss - use eviction policy
	counters[READ][MISS]++;
	debugL2(level, "Read miss: %d %d %d %d\n", address, tag, set, offset);
	uint slot = Evict(set);
	WriteDirtyLine(cache[set][slot], set, READ);
	UpdateEviction(set, slot, tag);
	LoadLine(address, cache[set][slot]);
	memcpy(line.data, cache[set][slot].data, CACHE_LINE_SIZE);
}

void Cache::SetLine(const uintptr_t address, CacheLine& line)
{
	counters[WRITE][ALL]++;
	uint offset = address & 63;
	uint set = (address >> 6) & 127;
	uintptr_t tag = (address >> 13);
	debugL2(level, "Write: %d %d %d %d\n", address, tag, set, offset);

	// Find if set/slot valid and matches tag - save value to cache 4B
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].valid && cache[set][slot].tag == tag)
		{
			memcpy(cache[set][slot].data, (void*)line.data, CACHE_LINE_SIZE);
			UpdateEviction(set, slot, tag);
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
			memcpy(cache[set][slot].data, (void*)line.data, CACHE_LINE_SIZE);
			UpdateEviction(set, slot, tag);
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
	memcpy(cache[set][slot].data, (void*)line.data, CACHE_LINE_SIZE);
	UpdateEviction(set, slot, tag);
	cache[set][slot].tag = tag;
	cache[set][slot].valid = true;
	cache[set][slot].dirty = true;
}
*/
// ============================================================================
// CACHE HELPER FUNCTIONS
// ============================================================================

void Cache::WriteDirtyLine(CacheLine& line, uint set, uint operation)
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
#ifdef EP_RR
	return rand() % nWaySetAssociative;
#else
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
#ifdef EP_LRU
		if (cache[set][slot].timestamp == 0 && cache[set][slot].valid)
#elif defined(EP_FIFO)
		if (cache[set][slot].timestamp == (nWaySetAssociative - 1))
#elif defined(EP_MRU)
		if (cache[set][slot].timestamp == (nWaySetAssociative - 1) && cache[set][slot].valid)
#endif
			return slot;
	}
	return 0;
#endif
}

void Cache::UpdateEviction(uint set, uint accessedSlot, uintptr_t tag)
{
#if defined(EP_LRU) || defined(EP_MRU)
	uint previousTimestamp = cache[set][accessedSlot].timestamp;
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if (cache[set][slot].timestamp > previousTimestamp)
			cache[set][slot].timestamp--;
	}
	cache[set][accessedSlot].timestamp = nWaySetAssociative - 1;
#elif defined(EP_FIFO)
	if (tag == cache[set][accessedSlot].tag) return;
	for (uint slot = 0; slot < nWaySetAssociative; slot++)
	{
		if(cache[set][slot].valid)
			cache[set][slot].timestamp++;
	}
	cache[set][accessedSlot].timestamp = 0;
#endif
}

// ============================================================================
// CACHE PERFORMANCE ANALYSIS
// ============================================================================

void Cache::DrawGraphPerformance(Surface* gameScreen, const uint nFrame)
{
	uint offset = nFrame >= SCRWIDTH;
	uint width = (offset) ? SCRWIDTH - 1 : nFrame;
	float sumOfMeasurement = 0, measurement = 0;

#ifdef PERFORMANCE_PER_LEVEL
	// Get new values
	for (uint level = 0; level < NUM_OF_LEVELS; level++)
	{
#ifdef PERFORMANCE_ACCESS
		measurement = CPUCache[level].counters[READ][ALL] + CPUCache[level].counters[WRITE][ALL];
#else
		measurement = CPUCache[level].counters[READ][ALL] + CPUCache[level].counters[WRITE][ALL];
#endif
		sumOfMeasurement += measurement;
		performance[width][level] = (float)measurement;
	}
#else	
	sumOfMeasurement = AMAT;

	performance[width][0] = sumOfMeasurement;
#endif

#ifdef INCLUDE_DRAM
	// Add DRAM access
	measurement = CPUCache[NUM_OF_LEVELS - 1].counters[READ][MISS] +
		CPUCache[NUM_OF_LEVELS - 1].counters[WRITE][WRITE_LINE] +
		CPUCache[NUM_OF_LEVELS - 1].counters[READ][WRITE_LINE];
	sumOfMeasurement += measurement;
	performance[width][NUM_OF_LEVELS] = (float)measurement;
#endif
	// Rescale history if new max
	bool newMax = max < sumOfMeasurement;
	if (newMax)
		max = sumOfMeasurement + max/10;

	uint x = 0;
	float acc = 0;
	if (offset || newMax)
	{
		realTimeSurface.Clear(0);
		// Scale history
		for (x; x < width; x++)
		{
			
#ifdef PERFORMANCE_PER_LEVEL
			float acc = 0;
			for (uint level = 0; level < NUM_OF_LEVELS + 1; level++)
			{
				performance[x][level] = performance[x + offset][level];
				float lineHeight = performance[x][level] * REAL_TIME_SCRHEIGHT / (float)max;
				realTimeSurface.Line((float)x, REAL_TIME_SCRHEIGHT - 1 - acc, (float)x, REAL_TIME_SCRHEIGHT - 1 - lineHeight - acc, COLOR[level]);
				acc += lineHeight;
			}
#else
			performance[x][0] = performance[x + offset][0];
			float lineHeight = performance[x][0] * REAL_TIME_SCRHEIGHT / (float)max;
			realTimeSurface.Line((float)x, REAL_TIME_SCRHEIGHT - 1 - acc, (float)x, REAL_TIME_SCRHEIGHT - 1 - lineHeight - acc, COLOR[0]);
#endif
		}
	}
	x = width;
#ifdef PERFORMANCE_PER_LEVEL
	// New one
	float acc = 0;
	for (uint level = 0; level < NUM_OF_LEVELS + 1; level++)
	{
		float lineHeight = performance[x][level] * REAL_TIME_SCRHEIGHT / (float)max;
		realTimeSurface.Line((float)x, REAL_TIME_SCRHEIGHT - 1 - acc, (float)x, REAL_TIME_SCRHEIGHT - 1 - lineHeight - acc, COLOR[level]);
		acc += lineHeight;
	}
#else
	performance[x][0] = performance[x + offset][0];
	float lineHeight = performance[x][0] * REAL_TIME_SCRHEIGHT / (float)max;
	realTimeSurface.Line((float)x, REAL_TIME_SCRHEIGHT - 1 - acc, (float)x, REAL_TIME_SCRHEIGHT - 1 - lineHeight - acc, COLOR[0]);

#endif
	// Render new graph
	realTimeSurface.BlendCopyTo(gameScreen, 0, SCRHEIGHT - REAL_TIME_SCRHEIGHT);
}

void Cache::GetPerformancePerFrame(Surface* gameScreen, const uint nFrame, const bool outprint)
{
	char t[128];
	sprintf(t, "Frame: %5d", nFrame);
	gameScreen->Print(t, 2, 2, 0xffffff);
	AMAT = L1_PENALTY + (CPUCache[L1].localMissRate / 100) * (L2_PENALTY + (CPUCache[L2].localMissRate / 100) * (L3_PENALTY + (CPUCache[L3].localMissRate / 100) * RAM_PENALTY));
	AMAT = ((CPUCache[L1].counters[READ][ALL] + CPUCache[L1].counters[WRITE][ALL])*L1_PENALTY +
		(CPUCache[L2].counters[READ][ALL] + CPUCache[L2].counters[WRITE][ALL])*L2_PENALTY +
			(CPUCache[L3].counters[READ][ALL] + CPUCache[L3].counters[WRITE][ALL])*L3_PENALTY +
			(CPUCache[L3].readMiss + CPUCache[L3].writeMiss)*RAM_PENALTY);
	sprintf(t, "AMAT: %4.2f cycles", AMAT);
	gameScreen->Print(t, 2, 12, 0xffffff);
	
	if (outprint)
	{
		printf("\n=================================================================================================\n");
		printf("Frame: %5d\n", nFrame);
		printf("%5s | %10s | %10s | %10s | %14s | %14s | %14s |\n", "", "#ALL ACC", "#TAG MISS", "#WRITE", "GLOBAL MISS", "LOCAL MISS", "#MISS/#L1 ACC");
		printf("------------------------------------------------------------------------------------------------|\n");

		for (uint level = 0; level < NUM_OF_LEVELS; level++)
		{
			CPUCache[level].UpdatePerformance();
			CPUCache[level].GetPerformance();
		}
	}

	for (uint level = 0; level < NUM_OF_LEVELS; level++)
		CPUCache[level].ResetPerformanceCounters();
}

void Cache::UpdatePerformance()
{
	readMiss = float(counters[READ][MISS] + counters[READ][WRITE_LINE]);
	writeMiss = (level > L1) ? float(counters[WRITE][WRITE_LINE]) : (float)(counters[WRITE][MISS] + counters[WRITE][WRITE_LINE]);
	localMissRate = (readMiss + writeMiss) / (counters[WRITE][ALL] + counters[READ][ALL]) * 100;
	globalMissRate = (writeMiss + readMiss) / (float)(CPUCache[L1].counters[WRITE][ALL] + CPUCache[L1].counters[READ][ALL]) * 100;
}

void Cache::GetPerformance()
{
	float allAccessedToL1Cache = (float)(CPUCache[L1].counters[WRITE][ALL] + CPUCache[L1].counters[READ][ALL]);
	printf("L%1dR%2s | %10d | %10d | %10d | %13.2lf%% | %13.2lf%% | %13.2lf%% |\n", level + 1, "",
		   counters[READ][ALL], counters[READ][MISS], counters[READ][WRITE_LINE],
		   readMiss / CPUCache[L1].counters[READ][ALL] * 100,
		   readMiss / counters[READ][ALL] * 100,
		   (float)counters[READ][MISS] / CPUCache[L1].counters[READ][ALL] * 100);
	printf("L%1dW%2s | %10d | %10d | %10d | %13.2lf%% | %13.2lf%% | %13.2lf%% |\n", level + 1, "",
		   counters[WRITE][ALL], counters[WRITE][MISS], counters[WRITE][WRITE_LINE],
		   writeMiss / CPUCache[L1].counters[WRITE][ALL] * 100,
		   writeMiss / counters[WRITE][ALL] * 100,
		   (float)counters[WRITE][MISS] / CPUCache[L1].counters[WRITE][ALL] * 100);
	printf("L%1dRW%1s | %10d | %10d | %10d | %13.2lf%% | %13.2lf%% | %13.2lf%% |\n", level + 1, "",
		   counters[WRITE][ALL] + counters[READ][ALL], counters[WRITE][MISS] + counters[READ][MISS], counters[WRITE][WRITE_LINE] + counters[READ][WRITE_LINE],
		   globalMissRate, localMissRate, (counters[WRITE][MISS] + counters[READ][MISS]) / allAccessedToL1Cache * 100);
	printf("------------------------------------------------------------------------------------------------|\n");
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
	debugL2(level, "LOAD FROM MEM!!!\n");
	//uintptr_t lineAddress = address & 0xFFFFFFFFFFFFFFC0; // set last six bit to 0
	memcpy(line.data, (void*)address, CACHE_LINE_SIZE); // fetch 64 bytes into line
	DELAY;
}

// write a cache line to memory; simulate RAM latency
void Cache::WriteLineToMem(const uintptr_t address, CacheLine& line)
{
	debugL2(level, "WRITE TO MEM!!!\n");
	//uintptr_t lineAddress = address & 0xFFFFFFFFFFFFFFC0; // set last six bit to 0
	memcpy((void*)address, line.data, CACHE_LINE_SIZE); // fetch 64 bytes into line
	DELAY;
}