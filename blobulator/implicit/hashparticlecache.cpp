//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/util/constants.h"
#include "blobulator/implicit/hashparticlecache.h"

#if (DRAW_MODE==DRAW_VALVE_STRIPS) || (DRAW_MODE==DRAW_VALVE_INDEX)
#include "tier0/vprof.h"
#else
#include <stdio.h>
#endif

#include "memdbgon.h"

HashParticleCache::HashParticleCache(float _renderCubeWidth, float _renderInteractionRadius)
	: insertCount(0)
	, getCount(0)
	, searchCount(0)
	, clearCount(0)
	, check_list_size(0)
	, cubeWidth(_renderCubeWidth)
	, oneOverCubeWidth(1.0f / _renderCubeWidth)
	, array_item_cache_used(0)
	, n_tiles(0)
	, memory_total(0)
{
	// NOTE: This is only for rendering... symCubeWidth is completely disregarded
	cacheDimensions.set(mcpc_xSize*_renderCubeWidth, mcpc_ySize*_renderCubeWidth, mcpc_zSize*_renderCubeWidth);
	setOffset(Point3D(0.0f, 0.0f, 0.0f));

	for(int i = 0; i < 1000; i++)
	{
		slots.pushAutoSize(NULL);
	}

	particles_size = 0;
	particles_closeby_size = 0;

	generate_check_list(_renderInteractionRadius, cubeWidth);
}	

void HashParticleCache::generate_check_list(float cutoff_r, float cube_width)
{
	check_list_size = 0;

	float radius = cutoff_r / cube_width;
	int i_radius = round_up(radius);

	float radiusSq = radius * radius;

	#if (USE_BIT_MASK == 1)
		float radius2 = radius * 3;
		float radius2Sq = radius2 * radius2;

		int mark_count = 0;
	#endif

	for(int i = -i_radius; i <= i_radius; i++)
	for(int j = -i_radius; j <= i_radius; j++)
	for(int k = -i_radius; k <= i_radius; k++)
	{
		int mindx = (i>0 ? i-1 : (i<0 ? i+1 : 0));
		int mindy = (j>0 ? j-1 : (j<0 ? j+1 : 0));
		int mindz = (k>0 ? k-1 : (k<0 ? k+1 : 0));
		
		int minDistSq = (mindx*mindx + mindy*mindy + mindz*mindz);

		if(float(minDistSq) <= radiusSq)
		{
			CheckListItem* item = &(check_list[check_list_size++]);

			item->offset_x = i;
			item->offset_y = j;
			item->offset_z = k;

			#if (USE_BIT_MASK == 1)
				item->mask = 0;

				for(int i2 = (i*3)-1; i2 <= (i*3)+1; i2++)
				for(int j2 = (j*3)-1; j2 <= (j*3)+1; j2++)
				for(int k2 = (k*3)-1; k2 <= (k*3)+1; k2++)
				{
					int mindx2 = (i2>1 ? i2-2 : (i2<-1 ? i2+2 : 0));
					int mindy2 = (j2>1 ? j2-2 : (j2<-1 ? j2+2 : 0));
					int mindz2 = (k2>1 ? k2-2 : (k2<-1 ? k2+2 : 0));

					int minDistSq2 = (mindx2*mindx2 + mindy2*mindy2 + mindz2*mindz2);

					if(float(minDistSq2) <= radius2Sq)
					{
						int pos = ((i2-((i*3)-1))*9) + ((j2-((j*3)-1))*3) + (k2-((k*3)-1));
						item->mask |= (1 << pos);
						mark_count++;
					}
				}
			#endif
		}
	}

	//printf("mark_count = %d\n", mark_count);
	IMP_PRINT("i_radius = %d, check_list_size = %d\n", i_radius, check_list_size);
}


// TODO: I should divide space into 4 blocks instead of three... then I 
// can use bit shifting instead of division here to make this stuff much faster.
void HashParticleCache::addParticle(ImpParticle* b)
{
	float fx = (b->center.p[0]-BBMins[0]) * oneOverCubeWidth;
	float fy = (b->center.p[1]-BBMins[1]) * oneOverCubeWidth;
	float fz = (b->center.p[2]-BBMins[2]) * oneOverCubeWidth;

	int x = trunc(fx);
	int y = trunc(fy);
	int z = trunc(fz);

	Assert(x >= 0 && x < mcpc_xSize && y >= 0 && y < mcpc_ySize && z >= 0 && z < mcpc_zSize);

	#if (USE_BIT_MASK == 1)
		int pos = trunc((fx - float(x)) * 3.0f);
		pos *= 3;
		pos += trunc((fy - float(y)) * 3.0f);
		pos *= 3;
		pos += trunc((fz - float(z)) * 3.0f);

		if(pos >= 27) printf("Error... pos too big\n");

		int mask = 1 << pos;
	#endif

	// Get the entry from the array
	MCArrayItem_t* arrayItem = findArrayItem(x, y, z);

	// Get or allocate new node
	MCCParticleListNode_t* newNode = &particles[particles_size++];

	newNode->particle = b;
	#if (USE_BIT_MASK == 1)
		newNode->mask = mask;
	#endif
	newNode->next = arrayItem->particlesHere;
	arrayItem->particlesHere = newNode;

	insertCount ++;
}