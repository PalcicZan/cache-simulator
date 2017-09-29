#include "precomp.h"

#define PARTICLES		8000
#define BALLRADIUS		1.7f

using namespace Tmpl8;

class TreeNode;

extern uint* RAM;

vec3* p = (vec3*)&RAM[0], *l = (vec3*)&RAM[PARTICLES * 4], *pt = (vec3*)&RAM[PARTICLES * 8];
float* pr = (float*)&RAM[PARTICLES * 12], r = 270, r2 = 270;
uint* pidx = (uint*)&RAM[PARTICLES * 13], *pleft = (uint*)&RAM[PARTICLES * 14], *pright = (uint*)&RAM[PARTICLES * 15];
vec3 gmin( -115, -55, -45 ), gmax( 115, 2460, 45 );
Sprite* sprite[4];
uint* ballCount = (uint*)&RAM[PARTICLES * 18], *index = (uint*)&RAM[PARTICLES * 19], nodeidx;
uint* source = (uint*)&RAM[PARTICLES * 21], *temp = (uint*)&RAM[PARTICLES * 23];
vec3* smin = (vec3*)&RAM[PARTICLES * 25], *smax = (vec3*)&RAM[PARTICLES * 26];
TreeNode** snode = (TreeNode**)&RAM[PARTICLES * 28];
TreeNode* root, *node = (TreeNode*)&RAM[PARTICLES * 29];
Surface back( SCRWIDTH, SCRHEIGHT, (Pixel*)&RAM[PARTICLES * 40], SCRWIDTH );
Surface ikea( SCRWIDTH, SCRHEIGHT, (Pixel*)&RAM[2048 * 1024], SCRWIDTH );
Surface ik_( "assets/back.png" );
uint nFrame;

inline void radix8 ( uint bit, uint N, uint *source, uint *dest )
{
	for( uint i = 0; i < 256; i++ ) STOREINT( &ballCount[i], 0 );
	for( uint i = 0; i < N; i++ ) 
	{
		uint s = (LOADINT( &source[i << 1] ) >> bit) & 255;
		STOREINT( &ballCount[s], LOADINT( &ballCount[s] ) + 1 ); 
	}
	STOREINT( &index[0], 0 );
	for( uint i = 1; i < 256; i++ ) STOREINT( &index[i], LOADINT( &index[i - 1] ) + LOADINT( &ballCount[i - 1] ) );
	for( uint i = 0; i < N; i++ ) 
	{
		//uint s = source[i << 1], idx = LOADINT( &index[(s >> bit) & 255] );
		uint s = LOADINT( &source[i << 1]), idx = LOADINT(&index[(s >> bit) & 255]);
		STOREINT( &index[(s >> bit) & 255], idx + 1 );
		idx <<= 1;
		STOREINT( &dest[idx], LOADINT( &source[i << 1] ) );
		STOREINT( &dest[idx + 1], LOADINT( &source[(i << 1) + 1] ) );
	}
}

class TreeNode
{
public:
	void SubDiv( vec3 bmin, vec3 bmax )
	{
		STOREVEC( &smin[0], bmin );
		STOREVEC( &smax[0], bmax );
		STOREPTR( &snode[0], this );
		uint stackptr = 1;
		while (stackptr)
		{
			vec3 bmin = LOADVEC( &smin[--stackptr] ), rmin = bmin;
			vec3 bmax = LOADVEC( &smax[stackptr] ), lmax = bmax;
			TreeNode* curr = (TreeNode*)LOADPTR( &snode[stackptr] );
			uint axis = 0, lcount = 0, rcount = 0;
			float ex[3] = { bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z };
			if (ex[1] > ex[0]) axis = 1; else axis = 0;
			if (ex[2] > ex[axis]) axis = 2;
			STOREPTR( &curr->left, &node[nodeidx] );
			nodeidx += 2;
			float splitpos = (bmax.cell[axis] + bmin.cell[axis]) * 0.5f;
			for ( uint i = 0; i < LOADINT( &curr->count ); i++ )
			{
				uint idx = LOADINT( &pidx[i + LOADINT( &curr->first )] );
				if (LOADFLOAT( &p[idx].cell[axis] ) < splitpos) 
				{
					STOREINT( &pleft[lcount++], LOADINT( &pidx[i + LOADINT( &curr->first )] ) );
				} 
				else STOREINT( &pright[rcount++], LOADINT( &pidx[i + LOADINT( &curr->first )] ) );
			}
			for( uint i = 0; i < lcount; i++ ) STOREINT( &pidx[LOADINT( &curr->first ) + i], LOADINT( &pleft[i] ) );
			for( uint i = 0; i < rcount; i++ ) STOREINT( &pidx[LOADINT( &curr->first ) + lcount + i], LOADINT( &pright[i] ) );
			TreeNode* currLeft = (TreeNode*)LOADPTR( &curr->left );
			STOREINT( &currLeft->first, LOADINT( &curr->first ) );
			STOREINT( &currLeft->count, lcount );
			STOREPTR( &currLeft->left, 0 );
			STOREINT( &(currLeft + 1)->first, LOADINT( &curr->first ) + lcount );
			STOREINT( &(currLeft + 1)->count, rcount );
			STOREPTR( &(currLeft + 1)->left, 0 );
			lmax.cell[axis] = rmin.cell[axis] = splitpos;
			if (lcount > 8) 
			{
				STOREPTR( &snode[stackptr], (TreeNode*)LOADPTR( &curr->left ) );
				STOREVEC( &smin[stackptr], bmin );
				STOREVEC( &smax[stackptr++], lmax );
			}
			if (rcount > 8) 
			{
				STOREPTR( &snode[stackptr], (TreeNode*)LOADPTR( &curr->left ) + 1 );
				STOREVEC( &smin[stackptr], rmin );
				STOREVEC( &smax[stackptr++], bmax );
			}
			STOREINT( &curr->axis, axis );
			STOREFLOAT( &curr->splitpos, splitpos );
		}
	}
	// data members
	TreeNode* left;
	union { float splitpos; uint first; };
	uint count, axis;
};

void BuildTree()
{
	root = &node[0];
	nodeidx = 1;
	for ( uint i = 0; i < PARTICLES; i++ ) STOREINT( &pidx[i], i );
	STOREINT( &root->first, 0 );
	STOREINT( &root->count, PARTICLES );
	root->SubDiv( gmin, gmax );
}

void Game::Init()
{
	// note: no attempts to use the cache simulator here; initialization code
	for ( uint i = 0; i < PARTICLES; i++ )
		p[i].x = l[i].x = Rand( 15 ) - 100,
		p[i].z = l[i].z = Rand( 30 ) - 15,
		p[i].y = l[i].y = 2400 - Rand( 2014 ),
		pr[i] = BALLRADIUS - Rand( 0.1f );
	for( uint i = 0; i < 4; i++ ) sprite[i] = new Sprite( new Surface( 15, 15 ), 1 );
	for ( uint y = 0; y < 15; y++ ) for ( uint x = 0; x < 15; x++ )
	{
		float dx = (float)x - (15 / 2), dy = (float)y - (15 / 2);
		float l = sqrtf( dx * dx + dy * dy ), d = (15 / 2) - l;
		if (d <= 0) continue; else d /= 15 / 2;
		uint scale = 12 + (int)(20 * tanf( d * d * 0.8f ));
		sprite[0]->GetSurface()->GetBuffer()[x + y * 15] = ScaleColor( 0xff0000, scale );
		sprite[1]->GetSurface()->GetBuffer()[x + y * 15] = ScaleColor( 0x00ff00, scale );
		sprite[2]->GetSurface()->GetBuffer()[x + y * 15] = ScaleColor( 0x0000ff, scale );
		sprite[3]->GetSurface()->GetBuffer()[x + y * 15] = ScaleColor( 0xffffff, scale );
	}
	for( uint i = 0; i < 4; i++ ) sprite[i]->InitializeStartData();
	nFrame = 0;
	BuildTree();
	ik_.CopyTo( &ikea, 0, 0 );
}

void Game::Tick( float a_DT )
{
	uint start = GetTickCount();
	screen->Clear( 0 );
	for( int i = 0; i < 655360; i++ ) 
		STOREINT( &back.GetBuffer()[i], LOADINT( &ikea.GetBuffer()[i] ) );
	if (GetAsyncKeyState( VK_LEFT )) { r -= 1.8f; if (r < 0) r += 360; }
	if (GetAsyncKeyState( VK_RIGHT )) { r += 1.8f; if (r > 360) r -= 360; }
	if (GetAsyncKeyState( VK_UP )) { r2 -= 1.8f; if (r2 < 0) r2 += 360; }
	if (GetAsyncKeyState( VK_DOWN )) { r2 += 1.8f; if (r2 > 360) r2 -= 360; }
	float sinr = sin( r * PI / 180 ), cosr = cos( r * PI / 180 );
	float sinr2 = sin( r2 * PI / 180 ), cosr2 = cos( r2 * PI / 180 );
	// verlet: update positions, apply forces
	for ( uint j = 0; j < 2; j++ )
	{
		for ( uint i = 0; i < PARTICLES; i++ )
		{
			vec3 _p = LOADVEC( &p[i] );
			STOREVEC( &p[i], _p + (_p - LOADVEC( &l[i] )) - vec3( -0.015f * cosr2, -0.015f * sinr2, 0 ) );
			STOREVEC( &l[i], _p );
		}
		// verlet: satisfy constraints
		BuildTree();
		for ( uint i = 0; i < PARTICLES; i++ )
		{
			TreeNode* node = root;
			TreeNode* stack[64];
			uint stackptr = 0;
			while (1)
			{
				if (LOADPTR( &node->left ))
				{
					uint axis = LOADINT( &node->axis );
					if ((LOADFLOAT( &p[i].cell[axis] ) - 2 * BALLRADIUS) < LOADFLOAT( &node->splitpos ))
					{
						if ((LOADFLOAT( &p[i].cell[axis] ) + 2 * BALLRADIUS) > LOADFLOAT( &node->splitpos )) 
							stack[stackptr++] = (TreeNode*)LOADPTR( &node->left ) + 1;	
						node = (TreeNode*)LOADPTR( &node->left );
					}
					else if ((LOADFLOAT( &p[i].cell[axis] ) + 2 * BALLRADIUS) > LOADFLOAT( &node->splitpos )) 
						node = (TreeNode*)LOADPTR( &node->left ) + 1;
					continue;
				}
				// move away from other balls
				for ( uint k = 0; k < LOADINT( &node->count ); k++ )
				{
					uint idx = LOADINT( &pidx[LOADINT( &node->first ) + k] );
					if (idx <= i) continue;
					vec3 d = LOADVEC( &p[idx] ) - LOADVEC( &p[i] );
					float dist = sqrtf( d.x * d.x + d.y * d.y + d.z * d.z );
					float mindist = LOADFLOAT( &pr[i] ) + LOADFLOAT( &pr[idx] );
					if (dist >= mindist) continue;
					float intrusion = mindist - dist;
					float il = 0.5f / dist;
					vec3 m = d * intrusion * il;
					STOREVEC( &p[idx], LOADVEC( &p[idx] ) + m );
					STOREVEC( &p[i], LOADVEC( &p[i] ) - m );
				}
				if (!stackptr) break;
				node = stack[--stackptr];
			}
			// keep away from  mouse object
			vec3 mpos( gmin.x + ((float)mx / SCRWIDTH) * (gmax.x - gmin.x),
					  -gmin.y + ((float)my / SCRHEIGHT) * (gmin.y + gmin.y), 0 );
			for ( uint a = 0; a < 3; a++ )
			{
				mpos.cell[a] = mpos.cell[a] < gmin.cell[a] ? gmin.cell[a] : mpos.cell[a];
				mpos.cell[a] = mpos.cell[a] > gmax.cell[a] ? gmax.cell[a] : mpos.cell[a];
			}
			vec3 d = mpos - LOADVEC( &p[i] );
			float dist = d.x * d.x + d.y * d.y;
			float mindist = LOADFLOAT( &pr[i] ) + 15;
			if (dist < (mindist * mindist))
			{
				float l = sqrtf( dist );
				float intrusion = mindist - l;
				float il = 1.0f / l;
				vec3 m = d * intrusion * il;
				STOREVEC( &p[i], LOADVEC( &p[i] ) - m );
			}
			// keep away from floor and sides
			for ( uint a = 0; a < 3; a++ )
			{
				if ((LOADFLOAT( &p[i].cell[a] ) - LOADFLOAT( &pr[i] )) < gmin.cell[a]) 
					STOREFLOAT( &p[i].cell[a], gmin.cell[a] + LOADFLOAT( &pr[i] ) );
				if ((LOADFLOAT( &p[i].cell[a] ) + LOADFLOAT( &pr[i] )) > gmax.cell[a]) 
					STOREFLOAT( &p[i].cell[a], gmax.cell[a] - LOADFLOAT( &pr[i] ) );
			}
		}
	}
	// add to sort array
	for ( uint i = 0; i < PARTICLES; i++ )
	{
		float rx = LOADFLOAT( &p[i].x ) * sinr + LOADFLOAT( &p[i].z ) * cosr;
		float rz = LOADFLOAT( &p[i].x ) * cosr - LOADFLOAT( &p[i].z ) * sinr;
		float frx = rx * sinr2 + LOADFLOAT( &p[i].y ) * cosr2;
		float fry = rx * cosr2 - LOADFLOAT( &p[i].y ) * sinr2;
		STOREVEC( &pt[i], vec3( frx, fry, rz ) );
		STOREINT( &source[i << 1], (uint)(100 * (500 - rz)) );
		STOREINT( &source[(i << 1) + 1], i );
	}
	radix8( 0, PARTICLES, (uint*)source, (uint*)temp );
	radix8( 8, PARTICLES, (uint*)temp, (uint*)source );
	// render
	for ( uint i = 0; i < PARTICLES; i++ )
	{
		uint idx = LOADINT( &source[(i << 1) + 1] );
		float reciz = 1.0f / (LOADFLOAT( &pt[idx].z ) + 500);
		uint sx = (int)((LOADFLOAT( &pt[idx].x ) * 2000) * reciz + SCRWIDTH / 2);
		uint sy = (uint)(SCRHEIGHT / 2 - (LOADFLOAT( &pt[idx].y ) * 2000) * reciz);
		sprite[idx & 3]->Draw( &back, sx - 8, sy - 8 );
	}	
	// finalize
	back.CopyTo(screen, 0, 0);
	// Print data from cache
#ifdef PERFORMANCE
	Cache::DrawGraphPerformance(screen, nFrame);
	Cache::GetPerformancePerFrame(screen, nFrame, true);
#endif
	nFrame++;
}