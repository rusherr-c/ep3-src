//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_POOL_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_POOL_H

#include "blobulator/light_bucket_implementation/light_csection.h"

#include "tier0/tslist.h"
#include "tier0/fasttimer.h"
#include "tier1/convar.h"

#ifdef SPU
#error	This source should not be included on SPU
#endif

extern ConVar blob_csection_pool_enable;
extern ConVar blob_csection_pool_size;
extern ConVar blob_csection_pool_free_per_frame;
extern ConVar blob_csection_pool_block_size;
extern ConVar blob_csection_pool_spew;

#ifdef _DEBUG
#define CSectionPool_Debug( ... ) __VA_ARGS__
#else
#define CSectionPool_Debug( ... )
#endif

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		// Sharing of CSections via this pool *drastically* (10x) reduces the number of CSections which need
		// to be allocated concurrently, thus reducing the blobulator's effect on our Small Block Heap.
		// CSections are acquired/released in large blocks, to keep the CPU impact of this pool as low as possible.
		class CSectionPool
		{
		public:
			CSectionPool()
			{
				m_nExtantSections = 0;
			}

			~CSectionPool()
			{
				// NOTE: All CSectionBlobRenderers must be destroyed before this happens, otherwise
				//       they will crash if they call ReleaseSections in their destructor!
				Purge();
			}

			// Free all CSections currently in the pool (should happen on level transitions)
			void Purge()
			{
				AUTO_LOCK( m_Mutex );
				Init();
				m_nExtantSections -= m_SectionCache.Count();
				for ( int i = 0; i < m_SectionCache.Count(); i++ ) delete m_SectionCache[i];
				m_SectionCache.RemoveAll();
			}

			// Acquire a set of CSections from the pool - if the pool is empty, 'new' them
			void AcquireSections( SmartArray<CSection *> &sections )
			{
				AUTO_LOCK( m_Mutex );
				Init();
				CSectionPool_Debug( m_AcquireTimer.Start(); )
				CSectionPool_Debug( m_nAcquire++; )

				int nSections            = m_nSectionsToAcquire;
				sections.ensureAdditionalCapacity( nSections );
				CSection **ppNewSections = sections.a + sections.size;
				sections.size           += nSections;

				int nNumToReuse = MIN( nSections, m_SectionCache.Count() );
				CSectionPool_Debug( m_nReuse += nNumToReuse; )
				if ( nNumToReuse )
				{
					int nFirstReusedItem = m_SectionCache.Count() - nNumToReuse;
					for ( int i = 0; i < nNumToReuse; i++ )
					{
						ppNewSections[i] = m_SectionCache[ nFirstReusedItem + i ];
					}
					m_SectionCache.RemoveMultipleFromTail( nNumToReuse );
					ppNewSections += nNumToReuse;
					nSections -= nNumToReuse;
				}
				CSectionPool_Debug( m_nNew += nSections; )
				m_nExtantSections += nSections;
				for ( int i = 0; i < nSections; i++ )
				{
					ppNewSections[i] = new CSection;
				}
				CSectionPool_Debug( m_AcquireTimer.End(); )
				CSectionPool_Debug( m_nAcquireCycles += m_AcquireTimer.GetDuration().GetCycles(); )
			}

			// Release unused CSections back into the pool - if the pool is full, 'free' them
			// !!NOTE!!: the number passed in is the number of CSections to retain, not to release! (makes the calling code simpler)
			void ReleaseSections( SmartArray<CSection *> &sections, int nSectionsToRetain )
			{
				AUTO_LOCK( m_Mutex );
				Init();
				CSectionPool_Debug( m_ReleaseTimer.Start(); )
				CSectionPool_Debug( m_nRelease++; )

				int        nSections  = sections.size - nSectionsToRetain;
				CSection **ppSections = sections.a + nSectionsToRetain;
				sections.size         = nSectionsToRetain;

				int nSpaceInCache = MIN( MAX_CACHED_SECTIONS, m_nMaxCachedSections ) - m_SectionCache.Count();
				int nNumToCache   = MIN( nSections, nSpaceInCache );
				if ( nNumToCache > 0 )
				{
					CSectionPool_Debug( m_nCache += nNumToCache; )
					m_SectionCache.AddMultipleToTail( nNumToCache, ppSections );
					ppSections += nNumToCache;
					nSections  -= nNumToCache;
				}
				CSectionPool_Debug( m_nFree += nSections; )
				m_nExtantSections -= nSections;
				for ( int i = 0; i < nSections; i++ )
				{
					delete ppSections[i];
				}
				CSectionPool_Debug( m_ReleaseTimer.End(); )
				CSectionPool_Debug( m_nReleaseCycles += m_ReleaseTimer.GetDuration().GetCycles(); )
			}

			// Perform per-frame tasks (spew stats, gradually free unused memory)
			void FrameUpdate( int renderFrame )
			{
				AUTO_LOCK( m_Mutex );
				Init();
				CSectionPool_Debug( m_ReleaseTimer.Start(); )

				// Don't want to do this stuff more than once per frame
				static int s_LastFrame = -1;
				if ( s_LastFrame == renderFrame ) return;
				s_LastFrame = renderFrame;


				// To reclaim CSection memory (when blob rendering stops or reduces), without introducing big
				// CPU spikes (due to CSection allocation), we free a few CSections from the pool each frame.
				// The size of the pool can also be reduced via a convar, for tuning (perf Vs memory).
				//
				// --NOTE-- it is important to gradually free *all* CSections while the blobulator is not in use, since caching
				//          a few CSections permanently will waste many pages in the Small Block Heap due to fragmentation.
				// --TODO-- perform block allocation of CSections, to take pressure off the SBH, avoid fragmentation and reduce alloc overhead
				//
				int nSectionsToDelete = m_SectionCache.Count() - m_nMaxCachedSections;
				nSectionsToDelete = MAX( nSectionsToDelete, blob_csection_pool_free_per_frame.GetInt() );
				nSectionsToDelete = MIN( nSectionsToDelete, m_SectionCache.Count() );
				int nFirstDeletedSection = m_SectionCache.Count() - nSectionsToDelete;
				for ( int i = 0; i < nSectionsToDelete; i++ ) delete m_SectionCache[ nFirstDeletedSection + i ];
				m_SectionCache.RemoveMultipleFromTail( nSectionsToDelete );
				m_nExtantSections -= nSectionsToDelete;
				CSectionPool_Debug( m_nFree += nSectionsToDelete; )


				// Spew current usage stats
				if ( blob_csection_pool_spew.GetBool() )
				{
					unsigned int allocSizeKB = ( m_nExtantSections*sizeof(CSection) ) / 1024;
					if ( !(renderFrame&15) )
					{
						CSectionPool_Debug( if ( true ) )
						{
							CSectionPool_Debug( CCycleCount acquireTime( m_nAcquireCycles ), releaseTime( m_nReleaseCycles ); )
							CSectionPool_Debug( Msg( "--BLOB CSECTION POOL-- acquire: %4d (reuse: %4d, new: %4d, %4.2fms), release: %4d (cache: %4d, free: %4d, %4.2fms), items in pool: %5d/%5d (%5dKB)\n", \
													m_nAcquire, m_nReuse, m_nNew, acquireTime.GetMillisecondsF(), m_nRelease, m_nCache, m_nFree, releaseTime.GetMillisecondsF(), m_SectionCache.Count(), (int)m_nExtantSections, allocSizeKB ); )
						}
						CSectionPool_Debug( if ( false ) )
						{
							Msg( "--BLOB CSECTION POOL-- items in pool: %5d/%5d (%5dKB)\n", m_SectionCache.Count(), (int)m_nExtantSections, allocSizeKB );
						}
					}
				}


				// Update config from convars each frame, and clear counters.
				ResetConfig();

				// If the pool is disabled, CSectionBlobRenderers will allocate CSections but not return them to the pool
				if ( !m_bEnabled )
					Purge();

				CSectionPool_Debug( m_ReleaseTimer.End(); )
				CSectionPool_Debug( m_nReleaseCycles += m_ReleaseTimer.GetDuration().GetCycles(); )
			}

		private:

			void Init( void )
			{
				// Static initialization order isn't correctly determined on some platforms, so we can't use convars in our constructor :o/
				if ( !m_bInit )
				{
					ResetConfig();
					m_bInit = true;
				}
			}

			void ResetConfig( void )
			{
				m_bEnabled				= blob_csection_pool_enable.GetBool();
				m_nMaxCachedSections	= m_bEnabled ? blob_csection_pool_size.GetInt() : 0;
				m_nSectionsToAcquire	= m_bEnabled ? clamp( blob_csection_pool_block_size.GetInt(), 1, 1000 ) : 1;
				CSectionPool_Debug( m_nAcquire = m_nReuse = m_nNew = m_nRelease = m_nFree = m_nCache = m_nAcquireCycles = m_nReleaseCycles = 0; )
			}

			// MAX_CACHED_SECTIONS limits the number of CSections which can be held in this global pool at any one time.
			// If the renderer's CSection usage becomes high enough that the pool fills up (this also depends on job thread
			// timing), perf will suffer since many 'new'/'free' calls will be made in Acquire/ReleaseSections each frame.
			// However, sharing of CSections via this pool *drastically* (10x) reduces the number of CSections which need
			// to be allocated concurrently, making this problem unlikely (and less risky than allowing huge memory spikes).
			//   [ ---- I have never seen a case where the pool fills up with a limit of 4000 - IESTYN (23/1/2011) ---- ]
			static const unsigned int MAX_CACHED_SECTIONS = 4000;

			CThreadMutex								m_Mutex;				// Mutex for access to the CSection pool
			bool										m_bEnabled;				// Whether m_SectionCache should be used to recycle CSections (if not, Deallocate just frees them)
			unsigned int								m_nMaxCachedSections;	// The max number of CSections to store in m_SectionCache
			unsigned int								m_nSectionsToAcquire;	// The number of sections to acquire at a time (higher numbers help perf but may hurt memory)
			unsigned int								m_nExtantSections;		// The number of currently allocated CSections (some of which may be held in m_SectionCache)
			CUtlVectorFixed<CSection*,MAX_CACHED_SECTIONS> m_SectionCache;		// A cache of CSection pointers (implemented as a simple stack for memory locality)
			bool										m_bInit;				// Works around static initialization issues on some platforms

			// Additional debug counters:
			CSectionPool_Debug( unsigned int m_nAcquire, m_nReuse, m_nNew, m_nRelease, m_nCache, m_nFree, m_nAcquireCycles, m_nReleaseCycles; )
			CSectionPool_Debug( CFastTimer m_AcquireTimer, m_ReleaseTimer; )
		};
	} // namespace LightBucketImplementation

} // namespace Blobulator


#endif // BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_POOL_H
