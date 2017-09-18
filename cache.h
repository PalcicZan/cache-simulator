#pragma once

uint LOADINT( uint* address );
void STOREINT( uint* address, uint value );
float LOADFLOAT( float* a );
void STOREFLOAT( float* a, float f );
vec3 LOADVEC( vec3* a );
void STOREVEC( vec3* a, vec3 t );
void* LOADPTR( void* a );
void STOREPTR( void* a, void* p );

#define DELAY dummy = min( 10, dummy + sinf( (float)(address / 1789) ) ); // artificial delay

struct CacheLine
{
	uint data[16]; // 64 bytes
	bool valid, dirty;
};

class Cache
{
public:
	Cache();
	~Cache();
	void ArtificialDelay();
	uint Read32bit( uint address );
	void Write32bit( uint address, uint value );
	float GetDummyValue() const { return dummy; }
private:
	void LoadLineFromMem( uint address, CacheLine& line );
	void WriteLineToMem( uint address, CacheLine& line );
	float dummy;
};