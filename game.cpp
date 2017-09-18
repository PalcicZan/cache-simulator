#include "precomp.h"

#define PARTICLES		8000
#define BALLRADIUS		1.7f

using namespace Tmpl8;

class TreeNode;

vec3 p[PARTICLES], l[PARTICLES], pt[PARTICLES];
float pr[PARTICLES], r = 270, r2 = 270;
uint pidx[PARTICLES], pleft[PARTICLES], pright[PARTICLES];
uint sx[PARTICLES], sy[PARTICLES];
vec3 gmin( -115, -55, -45 ), gmax( 115, 2460, 45 );
Surface back( SCRWIDTH, SCRHEIGHT );
Surface ikea( "assets/back.png" );
TreeNode* node, *root;
Sprite* sprite[4];
static uint ballCount[256], index[256], nodeidx;
uint scale[15 * 15];
uint source[PARTICLES * 2], temp[PARTICLES * 2];

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
		uint s = source[i << 1], idx = LOADINT( &index[(s >> bit) & 255] );
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
		vec3 smin[64], smax[64];
		TreeNode* snode[64];
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
			float splitpos = (LOADFLOAT( &bmax.cell[axis] ) + LOADFLOAT( &bmin.cell[axis] )) * 0.5f;
			for ( uint i = 0; i < LOADINT( &curr->count ); i++ )
			{
				uint idx = LOADINT( &pidx[i + LOADINT( &curr->first )] );
				if (LOADFLOAT( &p[idx].cell[axis] ) < splitpos) STOREINT( &pleft[lcount++], pidx[i + LOADINT( &curr->first )] ); 
				else STOREINT( &pright[rcount++], pidx[i + LOADINT( &curr->first )] );
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
			STOREFLOAT( &lmax.cell[axis], splitpos );
			STOREFLOAT( &rmin.cell[axis], splitpos );
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
		pr[i] = BALLRADIUS - Rand( 0.1f ),
		sx[i] = 0, sy[i] = 0;
	node = (TreeNode*)MALLOC64( sizeof( TreeNode ) * PARTICLES * 2 );
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
	BuildTree();
}

void Game::Tick( float a_DT )
{
	uint start = GetTickCount();
	screen->Clear( 0 );
	Pixel* bptr = back.GetBuffer();
	for( int i = 0; i < 655360; i++ ) back.GetBuffer()[i] = ikea.GetBuffer()[i];
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
							STOREPTR( &stack[stackptr++], (TreeNode*)LOADPTR( &node->left ) + 1 );	
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
				node = (TreeNode*)LOADPTR( &stack[--stackptr] );
			}
			// keep away from  mouse object
			vec3 mpos( gmin.x + ((float)mx / SCRWIDTH) * (gmax.x - gmin.x),
					  -gmin.y + ((float)my / SCRHEIGHT) * (gmin.y + gmin.y), 0 );
			for ( uint a = 0; a < 3; a++ )
			{
				STOREFLOAT( &mpos.cell[a], (LOADFLOAT( &mpos.cell[a] ) < LOADFLOAT( &gmin.cell[a] )) ? LOADFLOAT( &gmin.cell[a] ) : LOADFLOAT( &mpos.cell[a] ) );
				STOREFLOAT( &mpos.cell[a], (LOADFLOAT( &mpos.cell[a] ) > LOADFLOAT( &gmax.cell[a] )) ? LOADFLOAT( &gmax.cell[a] ) : LOADFLOAT( &mpos.cell[a] ) );
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
				if ((LOADFLOAT( &p[i].cell[a] ) - LOADFLOAT( &pr[i] )) < LOADFLOAT( &gmin.cell[a] )) 
					STOREFLOAT( &p[i].cell[a], LOADFLOAT( &gmin.cell[a] ) + LOADFLOAT( &pr[i] ) );
				if ((LOADFLOAT( &p[i].cell[a] ) + LOADFLOAT( &pr[i] )) > LOADFLOAT( &gmax.cell[a] )) 
					STOREFLOAT( &p[i].cell[a], LOADFLOAT( &gmax.cell[a] ) - LOADFLOAT( &pr[i] ) );
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
	for( int i = 0; i < 655360; i++ ) screen->GetBuffer()[i] = back.GetBuffer()[i];
}