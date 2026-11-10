//========== Copyright (c) Valve Corporation, All rights reserved. ========

#ifndef BLOBULATOR_JOB_IMPLEMENTATION_SOFTWARE_CACHE_H
#define BLOBULATOR_JOB_IMPLEMENTATION_SOFTWARE_CACHE_H

#include "blobulator/implicit/impdefines.h"
#ifdef SPU
#include "ps3/spu_job_shared.h"
#include "blobulator/light_bucket_implementation/spu/light_threadtools.h"
#else
#include "platform.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "tier1/strtools.h"
#endif

namespace Blobulator
{
	namespace LightBucketImplementation
	{

#define CONCAT2(a, b)	a ## b
#define CONCAT(a, b)	CONCAT2(a, b)
#define BLOB_COMPILE_TIME_ASSERT(pred)                 CONCAT(typedef struct { char Check[pred ? 1 : -1 ]; } __Check__, __LINE__ );

#if _DEBUG
#define BLOB_ENABLE_COUNTER	1
#endif

		// Class to allocate / deallocate a static list of free items.
		//
		// The number of items is known ahead of time (the SIZE template parameters).
		// sizeof(T) has to be greater than 4 bytes. It does not have to be aligned on 16 bytes.
		// Although the size will be aligned on 16 bytes (to be DMAed safely).
		// There is a potential speed up improvement if the aligned size is a power of 2.
		//
		// This implementation maintains a linked-list (in place with the allocated objects)
		// of all the free items. If user code writes the memory after calling Release(), undefined behavior will occur. 
		template <typename T, int SIZE>
		class ALIGN16 CFreeList
		{
		public:
			// To be DMA friendly, the size has to be multiple of 16.
			// Has the size can't be zero (at a minimum, it is 1. Thus there is at least 4 bytes to store the free list).
			BLOB_COMPILE_TIME_ASSERT(sizeof(T) > 4);

			static const int ITEM_SIZE_FOR_DMA = ALIGN_VALUE(sizeof(T), 16);

			CFreeList()
				:
				m_nFirstAvailableItem(0)
#if BLOB_ENABLE_COUNTER
				,m_nCounter(0)
#endif
			{
				for (int i = 0 ; i < SIZE - 1 ; ++i)
				{
					SetAvailableItemIndex(i, i + 1);
				}
				// Finish the list
				SetAvailableItemIndex(SIZE - 1, LAST_ITEM);
			}

			~CFreeList()
			{
				// Make sure that everything has been freed correctly
				Assert(IsEmpty());
			}

			// Allocates an item from the free list. The object is not constructed, merely allocated.
			// The allocated memory is in RANDOM state.
			// The returned value is undefined if the free list is running our of memory. There is an assert though.
			T * Allocate()
			{
#if BLOB_ENABLE_COUNTER
				++m_nCounter;
				Assert(m_nCounter <= SIZE);
#endif
				Assert(m_nFirstAvailableItem != LAST_ITEM);
				int nAvailableItem = m_nFirstAvailableItem;
				// Remove it from the list
				void * pItemPointer;
				m_nFirstAvailableItem = GetNextAvailableItemIndex(nAvailableItem, pItemPointer);
				Assert(IsEmpty() == false);	// Check that the linked-list is set properly
				return static_cast<T *>(pItemPointer);
			}

			// Allocates an item from the free list. The object is not constructed, merely allocated.
			// The allocated memory is in RANDOM state.
			// The returned value is undefined if the free list is running our of memory. There is an assert though.
			// The user data is also stored alongside the pointer information (only 16 bits stored for the moment).
			T * Allocate(int nUserData)
			{
#if BLOB_ENABLE_COUNTER
				++m_nCounter;
				Assert(m_nCounter <= SIZE);
#endif
				Assert(m_nFirstAvailableItem != LAST_ITEM);
				int nAvailableItem = m_nFirstAvailableItem;
				// Remove it from the list
				void * pItemPointer;
				m_nFirstAvailableItem = GetNextAvailableItemIndex(nAvailableItem, pItemPointer);
				m_nUserData[nAvailableItem] = nUserData;
				Assert(IsEmpty() == false);	// Check that the linked-list is set properly
				return static_cast<T *>(pItemPointer);
			}

			// Releases an item to the free list. The content of the freed memory will be discarded.
			// The state of the free list is undefined if the pointer has not been allocated by this instance. There is an assert though.
			void Release(T * pItem)
			{
				Assert(IsEmpty() == false);	// Check that the linked-list is set properly
#if BLOB_ENABLE_COUNTER
				Assert(m_nCounter > 0);
				--m_nCounter;
#endif
				// The index has to be calculated from ITEM_SIZE and not sizeof(T)
				int nIndex = (reinterpret_cast<int8 *>(pItem) - &m_Items[0]) / ITEM_SIZE_FOR_DMA;
				Assert((nIndex >= 0) && (nIndex < SIZE));
				Assert(IsItemFree(nIndex) == false);		// Already released!
				// Add it back to the list
				SetAvailableItemIndex(nIndex, m_nFirstAvailableItem);
				m_nFirstAvailableItem = nIndex;
			}

			// Gets the user data related to the pointer.
			// It will return the user data even if the item is actually free (there is a debug check though).
			int GetUserData(T * pItem) const
			{
				// The index has to be calculated from ITEM_SIZE and not sizeof(T)
				int nIndex = (reinterpret_cast<int8 *>(pItem) - &m_Items[0]) / ITEM_SIZE_FOR_DMA;
				Assert((nIndex >= 0) && (nIndex < SIZE));
				Assert(IsItemFree(nIndex) == false);		// Already released!
				return m_nUserData[nIndex];
			}

			// Sets the user data related to the pointer.
			// It will set the user data even if the item s actually free (there is a debug check though).
			void SetUserData(T * pItem, int nUserData)
			{
				// The index has to be calculated from ITEM_SIZE and not sizeof(T)
				int nIndex = (reinterpret_cast<int8 *>(pItem) - &m_Items[0]) / ITEM_SIZE_FOR_DMA;
				Assert((nIndex >= 0) && (nIndex < SIZE));
				Assert(IsItemFree(nIndex) == false);		// Already released!
				m_nUserData[nIndex] = nUserData;
			}

			// Indicates if the free list has some items available.
			bool HasAvailableItem() const
			{
				bool bResult = (m_nFirstAvailableItem != LAST_ITEM);
#if BLOB_ENABLE_COUNTER
				Assert((m_nCounter != SIZE) == bResult);
#endif
				return bResult;
			}

			bool IsEmpty() const
			{
				// Count how many freed items we got
				int count = 0;
				int nAvailableItem = m_nFirstAvailableItem;
				while (nAvailableItem != LAST_ITEM)
				{
					++count;
					nAvailableItem = GetNextAvailableItemIndex(nAvailableItem);
				}
#if BLOB_ENABLE_COUNTER
				Assert(m_nCounter == (SIZE - count));
#endif
				return (count == SIZE);
			}

			int GetCounter() const
			{
#if BLOB_ENABLE_COUNTER
				return m_nCounter;
#else
				return -1;	// -1 to indicate the value is not valid
#endif
			}

		private:
			// Start do not implement
			CFreeList(const CFreeList & other);
			CFreeList & operator=(const CFreeList & other);
			// End do not implement

			// Gets the item address from its local index in the free list.
			void * GetItemPointer(int nIndex) const
			{
				return &m_Items[ITEM_SIZE_FOR_DMA * nIndex];
			}

			// Gets the index of the next available item.
			int GetNextAvailableItemIndex(int nIndex) const
			{
				return *(int *)GetItemPointer(nIndex);
			}

			// Gets the index of the next available item (and extract the nIndex pointer).
			int GetNextAvailableItemIndex(int nIndex, void * & itemPointerForCurrentIndex) const
			{
				itemPointerForCurrentIndex = GetItemPointer(nIndex);
				return *(int *)itemPointerForCurrentIndex;
			}

			// Sets the index for the next available item.
			void SetAvailableItemIndex(int nIndex, int nNextAvailableIndex)
			{
				void * pItem = GetItemPointer(nIndex);

#if _DEBUG
				// To discover issues sooner let's fill the memory with some data
				V_memset(pItem, 0xBA, ITEM_SIZE_FOR_DMA);
#endif

				*(int *)pItem = nNextAvailableIndex;
			}

			// Indicates if the item is free.
			// Beware that this function scans all the free linked list. As such it is slow.
			bool IsItemFree(int nIndex) const
			{
				int nAvailableItem = m_nFirstAvailableItem;
				while (nAvailableItem != LAST_ITEM)
				{
					if (nAvailableItem == nIndex)
					{
						return true;
					}
					nAvailableItem = GetNextAvailableItemIndex(nAvailableItem);
				}
				return false;
			}

			// Constants for last item of the free items linked-list (indexes are all positive).
			static const int LAST_ITEM = -1;

			mutable int8 m_Items[ITEM_SIZE_FOR_DMA * SIZE];		// mutable to avoid create const / and non-const version of private functions.
			int16 m_nUserData[SIZE];
			int m_nFirstAvailableItem;
#if BLOB_ENABLE_COUNTER
			int m_nCounter;
#endif

		} ALIGN16_POST;

		// Simple implementation of an hash-table. This implementation does not grow as needed (revisit if necessary). 
		// HASH_SIZE represents the modulo applied to the hash-key. Better if the modulo is a power of 2, but it does not have to be.
		// WAYS is used to handle the number of collisions on a given hash key % HASH_SIZE.
		// A good rule of thumb is to have HASH_SIZE * WAYS >= 4 * Number of items.
		// On a given entry, is all WAYS slots are filled AND in use, this cache implementation will stop working
		// (with undefined behavior in a cert build, DebuggerBreak(0 otherwise).
		// Various values for HASH_SIZE and WAYS can be used, HASH_SIZE * WAYS indicates the probability of collisions given a number of items added.
		// For a fixed HASH_SIZE * WAYS value:
		//	- Having a bigger HASH_SIZE will improve the performance of the cache-hit.
		//  - Having a bigger WAYS is more forgiving though with collisions, and lookup could still be potentially optimized on SPU
		//		(one SPU comparison for eight 16 bits keys).
		//		Currently WAYS = 4 is a good trade-off, once the SPU optimization is implemented, WAYS = 8 will be a better trade-off.
		//
		// Every time a key is added / removed from the hash, its usage counter changes accordingly.
		// The same key can be added / removed several times, to make sure we handle properly the lifetime of the items.
		//
		// When a key is removed from the hash-table (and usage reaches 0), it is actually not really removed. The key and the pointer stays,
		// and the slot is added to an internal double linked-list. (using the counter to handle the double linked-list. At that point the counter has a value of "0" anyway).
		// The head of the double linked-list represents the MRU items. Only updated when the usage reaches 0, so it has a relatively low overhead. 
		// The tail of the double linked-list represents the LRU items.
		//
		// There is a mechanism in place that enable the CFreeList to recycle LRU items that are not in use.
		// In short, when setup properly, we can keep in the cache many items (currently used or previously used).
		// If a previously used item is requested again, we are going to restore its usage from the cache.
		// If we are running out of free item, we are going to recycle the LRU item in the cache.
		//
		// Most of the operations are using a Functor as parameter. It is used as bridge between CHashTable and CFreeList.
		// It is passed as a template parameter for each methods instead of the class. There are PROs and CONs both ways.
		// This can be revisited at a later point.
		// The main reason is to allow the bridge to contain both CHashTable and CFreeList together.
		// By having it as template parameter, and thus passed at construction time, there would be a chicken and egg issue.
		//
		// Here are the methods necessary for the functor:
		//	class CFunctor
		//	{
		//	public:
		//		// Allocate and DMA in the item from main memory. 
		//		// This implementation can call FreeLeastRecentlyUsed() as needed if the free list is empty.
		//		CBucket * AcquireInstance(int nKey);
		//		// DMA the item back to main memory (if necessary) and deallocate.
		//		void ReleaseInstance(int nIndex, CBucket * pLsBucket);
		//		// Gets the user data (using the CFreeList feature for example).
		//		int GetUserData(CBucket * bucket);
		//		// Sets the user data (using the CFreeList feature for example).
		//		void SetUserData(CBucket * bucket, int nUserData);
		//	};
		template <typename T, int HASH_SIZE, int WAYS>
		class ALIGN16 CHashTable
		{
		private:
			static const int16 KEY_NOT_USED = 0xffff;
			static const int16 END_LINKED_LIST = 0x7fff;	// Due to the masking, we can't use the full 16 bits for indices
			static const int16 MAX_INDEX = 0x7ffe;
			static const uint32 LINKED_LIST_MASK = 0x80000000;
			static const int FULL_SIZE = HASH_SIZE * WAYS;

			FORCEINLINE
			bool IsInRange(int nIndex)
			{
				return ( ( nIndex >= 0 ) && ( nIndex < FULL_SIZE ) );
			}

		public:
			// To fit in SPU memory, the size will be small enough so the indexes can fit in 15 bits or less.
			BLOB_COMPILE_TIME_ASSERT(HASH_SIZE * WAYS <= MAX_INDEX);

			CHashTable()
				:
				m_nIndexHead(END_LINKED_LIST),
				m_nIndexTail(END_LINKED_LIST)
#if BLOB_ENABLE_COUNTER
				,m_nCounter(0)
#endif
			{
				// Verify that InRange() is working as expected
				Assert( IsInRange( -1 ) == false );
				Assert( IsInRange( FULL_SIZE ) == false );

				for (int i = 0 ; i < FULL_SIZE ; ++i)
				{
					Assert( IsInRange( i ) );
					m_nKeys[i] = KEY_NOT_USED;
					m_nUsages[i] = 0;
					m_pValues[i] = NULL;
				}
			}

			// Call RemoveAllUnused() before destroying CHashTable, to free pointers back to the free list.
			~CHashTable()
			{
				// Make sure everything has been removed from the hash (even the cached object).
				Assert(IsEmpty());
			}

			// Adds a key to the Hash-Table.
			// The value is retrieved by calling the functor f.AcquireInstance(int nKey).
			// If the free list behind the functor f is actually exhausted, it can call FreeLeastRecentlyUsed()
			// to try to free an item.
			template <typename Functor>
			T * AddKeyValue(int nKey, Functor & f)
			{
				int nHash = nKey % HASH_SIZE;
				int nIndex = nHash * WAYS;
				Assert( IsInRange( nIndex) );
				int16 * pKeys = &m_nKeys[nIndex];

				int nFoundValue = -1;
#define HASH_CHECK_VALUE(i)									\
				if (pKeys[i] == nKey)						\
				{											\
					nFoundValue = nIndex + i;				\
					break;									\
				}

				// Hopefully the compiler will inline this switch case (i.e. no test as it is a constant).

				// To improve the performance of the cache-hit, we are looking first the match
				// Available slots will be only detected if we have to insert a new item.
				switch (WAYS)
				{
				case 1:
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					break;

				case 2:
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					HASH_CHECK_VALUE(1)					// Will short-cut if found it
					break;

				case 4:
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					HASH_CHECK_VALUE(1)					// Will short-cut if found it
					HASH_CHECK_VALUE(2)					// Will short-cut if found it
					HASH_CHECK_VALUE(3)					// Will short-cut if found it
					break;

				case 8:
					// This is the code that should be optimized with SIMD comparison
					// Do this once everything else is working and we are short in SPU perf.
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					HASH_CHECK_VALUE(1)					// Will short-cut if found it
					HASH_CHECK_VALUE(2)					// Will short-cut if found it
					HASH_CHECK_VALUE(3)					// Will short-cut if found it
					HASH_CHECK_VALUE(4)					// Will short-cut if found it
					HASH_CHECK_VALUE(5)					// Will short-cut if found it
					HASH_CHECK_VALUE(6)					// Will short-cut if found it
					HASH_CHECK_VALUE(7)					// Will short-cut if found it
					break;

				default:
					for (int i = 0 ; i < WAYS ; ++i)
					{
						HASH_CHECK_VALUE(i)
					}
					break;
				}

#undef HASH_CHECK_VALUE

				T * pPointer;
				if (nFoundValue != -1)
				{
					// We found the corresponding key, which may or may not be already used.
					// Increase its usage.
					IncrementUsageCounter(nFoundValue);
					Assert( IsInRange( nFoundValue ) );
					pPointer = m_pValues[nFoundValue];
					// The user data has already been set
					Assert(f.GetUserData(pPointer) == nFoundValue);
					return pPointer;
				}

				// There is a trick here...
				// Let's try to retrieve the instance first before looking for the empty slot.
				// The reason is that if the free list is actually empty, the functor could
				// potentially remove an unused item in this hash-table (reentrant using the LRU side of the linked-list).
				// This would give an available item back to the free list,
				// and will potentially free a used slot in the hash-table too.
				// (so we could go from "no available slot" to "one available slot").

				pPointer = f.AcquireInstance(nKey);

				// The cache should contain more element than the free list (to handle collisions, etc...),
				// thus there is a big chance that there is an available slot (outside of the MRU / LRU linked-list).
				// Implement SPU optimization here too.
				for (int i = 0 ; i < WAYS ; ++i)
				{
					Assert( IsInRange( nIndex + i ) );
					if (m_nUsages[nIndex + i] == 0)
					{
						// Found a free slot, let's use it
						m_nKeys[nIndex + i] = nKey;
						m_nUsages[nIndex + i] = 1;
						m_pValues[nIndex + i] = pPointer;
						f.SetUserData(pPointer, nIndex + i);
#if BLOB_ENABLE_COUNTER
						++m_nCounter;
						CheckSize();
#endif
						return pPointer;
					}
				}

				// There is no slot readily available
				// Let's see if we can find one available by looking at one not in use right now,
				// because we may be pointing in the middle of the LRU/MRU linked-list,
				// there is no point trying to find the more LRU of the available slots. The first one is a good one.

				// Implement SPU optimization here too.
				int nCounter = 0;
				int nLocalIndex;
				for (nLocalIndex = 0 ; nLocalIndex < WAYS ; ++nLocalIndex)
				{
					Assert( IsInRange( nIndex + nLocalIndex ) );
					nCounter = m_nUsages[nIndex + nLocalIndex];
					if (IsPartOfLinkedList(nCounter))
					{
						break;
					}
				}

				if (nLocalIndex == WAYS)
				{
					// All the slots are in use! The hash is too small for our usage.
					// Increase HASH_SIZE or WAYS.
					// What is a good backup solution here?
					BLOB_LOG(BLOB_LOG_HASHFULL, VjobSpuLog("WAYS are full!\n"));

#if defined(_CERT)
					Assert(false);
					nLocalIndex = 0;			// On shipped build, we use the first item
												// This will create nasty side effects though :(
												// But it may prevent a crash...
#else
					DebuggerBreak();
#endif
				}

				nIndex += nLocalIndex;			// I used repetitively nIndex + nLocalIndex in the code below
												// And that generated an incorrect SPU code :(

				RemoveFromLinkedList(nIndex, nCounter);
				// Because this item has been removed, let's release it directly
				Assert( IsInRange( nIndex ) );
				f.ReleaseInstance(m_nKeys[nIndex], m_pValues[nIndex]);
				// We can now replace the slot by the new value

				m_nKeys[nIndex] = nKey;
				m_nUsages[nIndex] = 1;
				m_pValues[nIndex] = pPointer;
				f.SetUserData(pPointer, nIndex);
				// We removed one item and replaced it by another, m_nCounter does not need to change...
				CheckSize();
				return pPointer;
			}

			// Acquires the key from the hash-table (i.e. increase its usage by 1).
			// Assumes that the pointer is already acquired previously anyway, so just a very simple look-up.
			// We don't have to test if that's part of the linked-list or not.
			template <typename Functor>
			void AcquireKeyValue(T * pPointer, Functor & f)
			{
				int nUserData = f.GetUserData(pPointer);
				Assert( IsInRange( nUserData ) );
				Assert(m_pValues[nUserData] == pPointer);
				int nCounter = m_nUsages[nUserData];
				// Check that it was acquired previously
				Assert(IsPartOfLinkedList(nCounter) == false);
				Assert(nCounter >= 1);
				m_nUsages[nUserData] = nCounter + 1;
				CheckSize();
			}

			// Removes the key from the hash-table (i.e. decrease its usage by 1).
			// As a cache optimization, this will not exactly remove the key, it will just reduce the usage of the pointer.
			// By knowing what pointer is not in use, we can have a better cache usage with MRU / LRU.
			// When the usage reaches zero, the slot will be added to the linked list.
			//
			// nKey is used to make sure the values are in sync.
			template <typename Functor>
			void ReleaseKeyValue(int nKey, T * pPointer, Functor & f)
			{
				int nUserData = f.GetUserData(pPointer);
				Assert( IsInRange( nUserData ) );
				Assert(m_nKeys[nUserData] == nKey);
				Assert(m_pValues[nUserData] == pPointer);
				Assert(IsPartOfLinkedList(m_nUsages[nUserData]) == false);
				DecrementUsageCounter(nUserData);
				CheckSize();
			}


			// Removes the key from the hash-table (i.e. decrease its usage by 1).
			// As a cache optimization, this will not exactly remove the key, it will just reduce the usage of the pointer.
			// By knowing what pointer is not in use, we can have a better cache usage with MRU / LRU.
			// When the usage reaches zero, the slot will be added to the linked list.
			template <typename Functor>
			void ReleaseKeyValue(T * pPointer, Functor & f)
			{
				int nUserData = f.GetUserData(pPointer);
				Assert( IsInRange(nUserData) );
				Assert(m_pValues[nUserData] == pPointer);
				Assert(IsPartOfLinkedList(m_nUsages[nUserData]) == false);
				DecrementUsageCounter(nUserData);
				CheckSize();
			}

			/*
				Now that we have the user data in the free list, is this method really needed?
				After all the user data version is faster (no tests),
				and we had to support user data for some use cases anyway.

			void RemoveKeyValue(int nKey, T * pPointer)
			{
				int nHash = nKey % HASH_SIZE;
				int nIndex = nHash * WAYS;
				int16 * pKeys = &m_nKeys[nIndex];

				int nFoundValue = -1;
#define HASH_CHECK_VALUE(i)										\
				if (pKeys[i] == nKey)							\
				{												\
					nFoundValue = i;							\
					break;										\
				}

				// Hopefully the compiler will inline this switch case (i.e. no test as it is a constant).

				// To improve the performance of the cache-hit, we are looking first the match
				// Available slots will be only detected if we have to insert a new item.
				switch (WAYS)
				{
				case 1:
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					break;

				case 2:
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					HASH_CHECK_VALUE(1)					// Will short-cut if found it
					break;

				case 4:
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					HASH_CHECK_VALUE(1)					// Will short-cut if found it
					HASH_CHECK_VALUE(2)					// Will short-cut if found it
					HASH_CHECK_VALUE(3)					// Will short-cut if found it
					break;

				case 8:
					// This is the code that should be optimized with SIMD comparison
					// Do this once everything else is working and we are short in SPU perf.
					HASH_CHECK_VALUE(0)					// Will short-cut if found it
					HASH_CHECK_VALUE(1)					// Will short-cut if found it
					HASH_CHECK_VALUE(2)					// Will short-cut if found it
					HASH_CHECK_VALUE(3)					// Will short-cut if found it
					HASH_CHECK_VALUE(4)					// Will short-cut if found it
					HASH_CHECK_VALUE(5)					// Will short-cut if found it
					HASH_CHECK_VALUE(6)					// Will short-cut if found it
					HASH_CHECK_VALUE(7)					// Will short-cut if found it
					break;

				default:
					for (int i = 0 ; i < WAYS ; ++i)
					{
						HASH_CHECK_VALUE(i)
					}
					break;
				}

				if (nFoundValue != -1)
				{
					// We found the corresponding key
					// Decrease its usage.
					Assert(m_pValues[nFoundValue] == pPointer);
					DecrementUsageCounter(nFoundValue);
					return;
				}

				// If we reached here, it means that we did not find the corresponding pointer to release.
				// This should not happen.
				Assert(false);
			}
			*/

			// Frees the least recently used slot.
			//
			// If a slot can be freed, the functor f.ReleaseInstance(T * pPointer) will be called.
			template <typename Functor>
			bool FreeLeastRecentlyUsed(Functor & f)
			{
				CheckSize();
				int nOutTailKey;
				T * pPointer = PopFromTail(nOutTailKey);
				if (pPointer != NULL)
				{
					f.ReleaseInstance(nOutTailKey, pPointer);
					CheckSize();
					return true;
				}
				else
				{
					return false;
				}
			}

			// Removes all unused slots from the cache.
			// This should be called before calling the destructor.
			//
			// Note that after calling this function, there could be still some slot used
			// (if some pointers have been manually acquired but not released yet).
			template <typename Functor>
			void RemoveAllUnused(Functor & f)
			{
				CheckSize();
				for ( ; ; )
				{
					int nOutTailKey;
					T * pPointer = PopFromTail(nOutTailKey);
					if (pPointer == NULL)
					{
						break;
					}
					f.ReleaseInstance(nOutTailKey, pPointer);
				}
				CheckSize();
			}

			bool IsEmpty() const
			{
				CheckSize();
				for (int i = 0 ; i < HASH_SIZE * WAYS ; ++i)
				{
					if (m_nKeys[i] != KEY_NOT_USED)
					{
						return false;
					}
					if (m_nUsages[i] != 0)
					{
						return false;
					}
					if (m_pValues[i] != NULL)
					{
						return false;
					}
				}
				return true;
			}

			int GetCounter() const
			{
#if BLOB_ENABLE_COUNTER
				return m_nCounter;
#else
				return -1;	//	-1 to indicate that the value is not valid
#endif
			}

		private:

			void CheckSize() const
			{
#if BLOB_ENABLE_COUNTER
				int nCount = 0;
				for (int i = 0 ; i < HASH_SIZE * WAYS ; ++i)
				{
					if (m_nKeys[i] != KEY_NOT_USED)
					{
						++nCount;
						continue;
					}
					if (m_nUsages[i] != 0)
					{
						++nCount;
						continue;
					}
					if (m_pValues[i] != NULL)
					{
						++nCount;
						continue;
					}
				}
				Assert(nCount == m_nCounter);
#endif
			}

			// Indicates if the counter represents the linked list chaining instead of the usage counter.
			static bool IsPartOfLinkedList(int nCounter)
			{
				return (nCounter & LINKED_LIST_MASK);
			}

			// Increments the usage counter for a given index.
			//
			// We have two cases here.
			//	1) Either the pointer is already in use, thus we simply need to increment its usage directly.
			//	2) Or the pointer is not in use (was ready to be recycled), thus it was 
			//		on the linked list, and we need to remove it from the linked-list before setting its usage to 1. 
			void IncrementUsageCounter(int nIndex)
			{
				Assert( IsInRange( nIndex ) );
				int nCounter = m_nUsages[nIndex];
				if (IsPartOfLinkedList(nCounter))
				{
					RemoveFromLinkedList(nIndex, nCounter);
					m_nUsages[nIndex] = 1;
				}
				else
				{
					// Simple case, it's already in use
					m_nUsages[nIndex] = nCounter + 1;
				}
			}

			// Decrements the usage counter for a given index.
			//
			// If the counter reaches 0, the index will be added to the MRU side of the linked-list.
			void DecrementUsageCounter(int nIndex)
			{
				Assert( IsInRange( nIndex ) );
				int nCounter = m_nUsages[nIndex];
				// Usage should be positive, thus it should not be part of the linked-list
				Assert(IsPartOfLinkedList(nCounter) == false);
				Assert(nCounter > 0);
				--nCounter;
				if (nCounter == 0)
				{
					PushToHead(nIndex);
				}
				else
				{
					m_nUsages[nIndex] = nCounter;
				}
			}

			// Removes the index from the MRU / LRU linked list.
			//
			// This could happen when:
			//	- A previously used pointer is in use again (it is not free anymore).
			//	- We override a previously used pointer with a new value.
			void RemoveFromLinkedList(int nIndex, int nCounter)
			{
				Assert( IsInRange( nIndex ) );
				Assert(m_nUsages[nIndex] == nCounter);
				Assert(IsPartOfLinkedList(nCounter));

				nCounter &= ~LINKED_LIST_MASK;
				int nPrev = GetPrev(nCounter);
				int nNext = GetNext(nCounter);

				if (nPrev == END_LINKED_LIST)
				{
					Assert(m_nIndexHead == nIndex);
					m_nIndexHead = nNext;
				}
				else
				{
					Assert( IsInRange( nPrev ) );
					int nPrevCounter = m_nUsages[nPrev];
					Assert(IsPartOfLinkedList(nPrevCounter));
					m_nUsages[nPrev] = SetNext(nPrevCounter, nNext);
				}

				if (nNext == END_LINKED_LIST)
				{
					Assert(m_nIndexTail == nIndex);
					m_nIndexTail = nPrev;
				}
				else
				{
					Assert( IsInRange( nNext ) );
					int nNextCounter = m_nUsages[nNext];
					Assert(IsPartOfLinkedList(nNextCounter));
					m_nUsages[nNext] = SetPrev(nNextCounter, nPrev);
				}
			}

			// Pushes the index to the MRU side of the linked-list, invalidating the usage value (used by the linked list).
			// The key and pointer are not changed as the object will stay in the cache.
			void PushToHead(int nIndex)
			{
				Assert( IsInRange( nIndex ) );
				if (m_nIndexHead == END_LINKED_LIST)
				{
					int nCounter = SetNext(LINKED_LIST_MASK, END_LINKED_LIST);
					nCounter = SetPrev(nCounter, END_LINKED_LIST);
					Assert(IsPartOfLinkedList(nCounter));
					m_nUsages[nIndex] = nCounter;

					m_nIndexHead = m_nIndexTail = nIndex;
				}
				else
				{
					int nCounter = SetNext(LINKED_LIST_MASK, m_nIndexHead);
					nCounter = SetPrev(nCounter, END_LINKED_LIST);
					Assert(IsPartOfLinkedList(nCounter));
					m_nUsages[nIndex] = nCounter;

					Assert( IsInRange( m_nIndexHead ) );
					int nNextCounter = m_nUsages[m_nIndexHead];
					nNextCounter = SetPrev(nNextCounter, nIndex);
					m_nUsages[m_nIndexHead] = nNextCounter;

					m_nIndexHead = nIndex;
				}
			}

			// Pops a slot from the LRU side of the linked-list. The slot becomes free after that.
			// Returns NULL if there is no free slot (i.e. all slots are in use explicitly).
			//
			// We use a reference instead of a pointer, it makes the convention a bit awkward,
			// but should be more SPU friendly.
			T * PopFromTail(int & nOutTailKey)
			{
				int nTailIndex = m_nIndexTail;
				if (nTailIndex == END_LINKED_LIST)
				{
					// Nothing in the linked list...
					nOutTailKey = KEY_NOT_USED;
					return NULL;
				}

				// Code similar as above (except that we know that nNext == END_LINKED_LIST)
				Assert( IsInRange( nTailIndex ) );
				int nCounter = m_nUsages[nTailIndex];
				nCounter &= ~LINKED_LIST_MASK;
				int nPrev = GetPrev(nCounter);
				Assert(GetNext(nCounter) == END_LINKED_LIST);

				m_nIndexTail = nPrev;

				if (nPrev == END_LINKED_LIST)
				{
					Assert(m_nIndexHead == nTailIndex);
					m_nIndexHead = END_LINKED_LIST;
				}
				else
				{
					Assert( IsInRange( nPrev ) );
					int nPrevCounter = m_nUsages[nPrev];
					Assert(IsPartOfLinkedList(nPrevCounter));
					m_nUsages[nPrev] = SetNext(nPrevCounter, END_LINKED_LIST);
				}

				// Save previous state
				Assert( IsInRange( nTailIndex ) );
				nOutTailKey = m_nKeys[nTailIndex];
				T * pPointer = m_pValues[nTailIndex];

				// Reset the slot
				m_nKeys[nTailIndex] = KEY_NOT_USED;
				m_nUsages[nTailIndex] = 0;
				m_pValues[nTailIndex] = NULL;

#if BLOB_ENABLE_COUNTER
				--m_nCounter;
				CheckSize();
#endif
				return pPointer;
			}

			// Gets the next index from the linked-list counter.
			static int GetNext(int nCounter)
			{
				return nCounter & 0xffff;
			}

			// Gets the prev index from the linked-list counter.
			static int GetPrev(int nCounter)
			{
				return (uint32)(nCounter) >> 16;
			}

			// Sets the next index on the linked-list counter.
			static int SetNext(int nCounter, int nNewNext)
			{
				Assert(nNewNext <= END_LINKED_LIST);
				nCounter &= 0xffff0000;
				nCounter |= nNewNext;
				return nCounter;
			}

			// Sets the prev index on the linked-list counter.
			static int SetPrev(int nCounter, int nNewPrev)
			{
				Assert(nNewPrev <= END_LINKED_LIST);
				nCounter &= 0x8000ffff;
				nCounter |= nNewPrev << 16;
				return nCounter;
			}

			// Use 3 different arrays to optimize the look-up on m_Keys
			// Because m_Keys is aligned on 16 bytes, if the ways are multiple of 8,
			// 8 keys would be stored in a SPU register.
			// With one comparison statement we could detect where the match is.

			// Note that m_nKeys and m_nUsages are also used to maintain MRU and LRU lists.
			// If the top bit is set, it means that they are part of the internal linked list.
			// As such using the slot will have to fix the linked-list accordingly.
			int16	m_nKeys[HASH_SIZE * WAYS];
			int		m_nUsages[HASH_SIZE * WAYS];			// m_nUsages is used as usage counter when the pointer is in use.
															// Otherwise the high 16 bits represent NEXT, low 16 bits represent PREV on the linked list.
															// Because of the linked list, it has to use 32 bits. instead of 8 or 16 bits.
															// If HASH_SIZE * WAYS was less or equal to 126, we could have used int16 instead.
															// (126 would work for around 30 items).
			T *		m_pValues[HASH_SIZE * WAYS];
			int16	m_nIndexHead;							// This is considered the MRU
			int16	m_nIndexTail;							// This is considered the LRU
#if BLOB_ENABLE_COUNTER
			int		m_nCounter;
#endif
		};

		// This code is similar to CTSListBase.
		// It is SPU friendly as it does not manipulate memory (LS / EA usage is annoying on SPU),
		// and does not need allocations either.
		// Instead it uses indices. Note that the size has to be known at compile time.
		// CLockLessStack should not be used directly, use CLockLessStackHandle instead.
		// CLockLessStack is used as a container, the handle is the main usage point (making the SPU version always fetching the main memory,
		// avoiding using stale SPU data, using more memory).
		//
		// The counter of operations avoid situations where a thread is popping and pushing items in a way that it looks like the head did not change.
		// A more modular design would separate the array of indices and the management of the head of the list,
		// but we keep this design as it is right now, as it is simpler to use.
		//
		// Consider adding PushTail() and PopTail() as the header would still fit in 64 bits.
		template <int MIN_SIZE>
		class ALIGN16 CLockLessStackContainer
		{
		public:
			union HeadInfo
			{
				struct S
				{
					int16 m_nFirstItem;				// The next on the head
					int16 m_nCounterOfOperations;	// To make sure that nobody touched the stack (pushing and popping same indices, etc...) while we are interacting with it.
				} m_S;
				int32 m_nValue32;
			};
			HeadInfo m_nHeadInfo;

			static const int LAST_ITEM = -1;
			static const int HEADER_SIZE = sizeof(HeadInfo);
			static const int ALIGNED_SIZE_IN_BYTES = ALIGN_VALUE((MIN_SIZE * sizeof(int16)) + HEADER_SIZE, 16);
			static const int EXACT_SIZE = (ALIGNED_SIZE_IN_BYTES - HEADER_SIZE) / sizeof(int16);

			int16 m_nItems[EXACT_SIZE];				// The next on each slot
			// Bigger size than what was asked but guarantee alignment on 16 bytes.

			// Constructs CLockLessStack on the main memory (not on SPU, otherwise m_pEaAddress will be incorrect).
			CLockLessStackContainer()
			{
				m_nHeadInfo.m_S.m_nCounterOfOperations = 0;
				m_nHeadInfo.m_S.m_nFirstItem = LAST_ITEM;

				V_memset(m_nItems, 0, sizeof(int16));
			}
		} ALIGN16_POST;

		template <int MIN_SIZE>
		class CLockLessStack
		{
			static const int TAG = 6;

		public:
			static const int SIZE = MIN_SIZE;
			typedef CLockLessStackContainer<MIN_SIZE> Container;

			CLockLessStack()
				:
				m_pEaInstance(NULL)
			{
				// Do nothing...
			}

			CLockLessStack(Container * pEaInstance)
				:
				m_pEaInstance(pEaInstance)
			{
				// Do nothing...
			}

			CLockLessStack(const CLockLessStack<MIN_SIZE> & other)
				:
				m_pEaInstance(other.m_pEaInstance)
			{
				// Do nothing...
			}

			~CLockLessStack()
			{
				// Do nothing...
			}

			CLockLessStack<MIN_SIZE> & operator=(const CLockLessStack<MIN_SIZE> & other)
			{
				// No need to self test
				m_pEaInstance = other.m_pEaInstance;
				return *this;
			}

			void SetContainer(Container * pEaInstance)
			{
				// No need to assert for NULL etc... No issue allowing a user to re-use the same stack but swapping the container over time.
				m_pEaInstance = pEaInstance;
			}

			Container * GetContainer() const
			{
				return m_pEaInstance;
			}

			void PushToHead(int nIndex)
			{
				Assert((nIndex >= 0) && (nIndex < MIN_SIZE));		// We use MIN_SIZE to detect incorrect usage earlier
																	// The user should not know about EXACT_SIZE for DMA alignment reasons.
				Assert( (unsigned int)(m_pEaInstance) >= 0x40000 );	// Check if the address looks like an EA address
				ThreadMemoryBarrier();

				for (;;)
				{
					typename Container::HeadInfo oldHeadInfo;

#if IsPlatformPS3_SPU()
					// We need to get the content from main memory
					oldHeadInfo.m_nValue32 = cellDmaGetUint32((uint64)&m_pEaInstance->m_nHeadInfo, TAG, 0, 0);

					// Make sure we are detecting infinite loop before we write it
					Assert(oldHeadInfo.m_S.m_nFirstItem != nIndex);

					// We need to write the content to the main memory (only the one index we are interested though,
					// otherwise we'll erase other data that could be written by other threads).
					cellDmaPutUint16(oldHeadInfo.m_S.m_nFirstItem, (uint64)&m_pEaInstance->m_nItems[nIndex], TAG, 0, 0);
#else
					oldHeadInfo = m_pEaInstance->m_nHeadInfo;

					// Make sure we are detecting infinite loop before we write it
					Assert(oldHeadInfo.m_S.m_nFirstItem != nIndex);

					m_pEaInstance->m_nItems[nIndex] = oldHeadInfo.m_S.m_nFirstItem;
#endif

					typename Container::HeadInfo newHeadInfo;
					newHeadInfo.m_S.m_nCounterOfOperations = oldHeadInfo.m_S.m_nCounterOfOperations + 1;
					newHeadInfo.m_S.m_nFirstItem = nIndex;

					if (ThreadInterlockedAssignIf(&m_pEaInstance->m_nHeadInfo.m_nValue32, newHeadInfo.m_nValue32, oldHeadInfo.m_nValue32))
					{
						ThreadMemoryBarrier();
						break;
					}
					ThreadPause();
				}
			}

			int PopFromHead()
			{
				Assert( (unsigned int)(m_pEaInstance) >= 0x40000 );	// Check if the address looks like an EA address

				ThreadMemoryBarrier();

				for (;;)
				{
					typename Container::HeadInfo oldHeadInfo;
#if IsPlatformPS3_SPU()
					// We need to get the content from main memory
					oldHeadInfo.m_nValue32 = cellDmaGetUint32((uint64)&m_pEaInstance->m_nHeadInfo, TAG, 0, 0);
#else
					oldHeadInfo = m_pEaInstance->m_nHeadInfo;
#endif
					if (oldHeadInfo.m_S.m_nFirstItem == Container::LAST_ITEM)
					{
						return Container::LAST_ITEM;
					}

					int nNewFirstItem;
#if IsPlatformPS3_SPU()
					nNewFirstItem = cellDmaGetUint16((uint64)&m_pEaInstance->m_nItems[oldHeadInfo.m_S.m_nFirstItem], TAG, 0, 0);
#else
					nNewFirstItem = m_pEaInstance->m_nItems[oldHeadInfo.m_S.m_nFirstItem];
#endif
					// Make sure we are detecting infinite loop
					Assert(oldHeadInfo.m_S.m_nFirstItem != nNewFirstItem);

					typename Container::HeadInfo newHeadInfo;
					newHeadInfo.m_S.m_nCounterOfOperations = oldHeadInfo.m_S.m_nCounterOfOperations + 1;
					newHeadInfo.m_S.m_nFirstItem = nNewFirstItem;

					if (ThreadInterlockedAssignIf(&m_pEaInstance->m_nHeadInfo.m_nValue32, newHeadInfo.m_nValue32, oldHeadInfo.m_nValue32))
					{
						ThreadMemoryBarrier();
						return oldHeadInfo.m_S.m_nFirstItem;
					}
					ThreadPause();
				}
			}

			bool IsEmpty() const
			{
				Assert( (unsigned int)(m_pEaInstance) >= 0x40000 );	// Check if the address looks like an EA address

				ThreadMemoryBarrier();

				typename Container::HeadInfo oldHeadInfo;
#if IsPlatformPS3_SPU()
				// We need to get the content from main memory
				oldHeadInfo.m_nValue32 = cellDmaGetUint32((uint64)&m_pEaInstance->m_nHeadInfo, TAG, 0, 0);
#else
				oldHeadInfo = m_pEaInstance->m_nHeadInfo;
#endif
				return (oldHeadInfo.m_S.m_nFirstItem == Container::LAST_ITEM);
			}

			void Clear()
			{
				Assert( (unsigned int)(m_pEaInstance) >= 0x40000 );	// Check if the address looks like an EA address

				ThreadMemoryBarrier();

				for (;;)
				{
					typename Container::HeadInfo oldHeadInfo;
#if IsPlatformPS3_SPU()
					// We need to get the content from main memory
					oldHeadInfo.m_nValue32 = cellDmaGetUint32((uint64)&m_pEaInstance->m_nHeadInfo, TAG, 0, 0);
#else
					oldHeadInfo = m_pEaInstance->m_nHeadInfo;
#endif
					typename Container::HeadInfo newHeadInfo;
					newHeadInfo.m_S.m_nCounterOfOperations = oldHeadInfo.m_S.m_nCounterOfOperations + 1;
					newHeadInfo.m_S.m_nFirstItem = Container::LAST_ITEM;
					if (ThreadInterlockedAssignIf(&m_pEaInstance->m_nHeadInfo.m_nValue32, newHeadInfo.m_nValue32, oldHeadInfo.m_nValue32))
					{
						ThreadMemoryBarrier();
						return;
					}
					ThreadPause();
				}
			}

#if !IsPlatformPS3_SPU()
			// This function is NOT thread-safe, and not supported on SPU.
			void ResetToFull()
			{
				ThreadMemoryBarrier();

				m_pEaInstance->m_nHeadInfo.m_S.m_nFirstItem = 0;
				m_pEaInstance->m_nHeadInfo.m_S.m_nCounterOfOperations++;

				int i;
				for (i = 0 ; i < MIN_SIZE - 1 ; ++i)
				{
					m_pEaInstance->m_nItems[i] = i + 1;
				}
				m_pEaInstance->m_nItems[i] = Container::LAST_ITEM;

				ThreadMemoryBarrier();
			}

			// This function is NOT thread-safe, and not supported on SPU.
			int GetSize() const
			{
				ThreadMemoryBarrier();

				int nCount = 0;
				int nItem = m_pEaInstance->m_nHeadInfo.m_S.m_nFirstItem;
				while (nItem != Container::LAST_ITEM)
				{
					++nCount;
					nItem = m_pEaInstance->m_nItems[nItem];
				}
				return nCount;
			}
#endif

		private:
			Container * m_pEaInstance;
		};

		class ALIGN16 CSoftwareCacheUnitTest
		{
		public:
			enum UnitTest
			{
				// Test with no parameters
				TEST_EMPTY,
				TEST_NOT_EMPTY,
				TEST_CLEAR,
				TEST_RESET_TO_FULL,

				// Test with one parameter
				TEST_WITH_ONE_PARAMETER,
				
				TEST_PUSH_ONE = TEST_WITH_ONE_PARAMETER,
				TEST_POP_ONE,
				TEST_PUSH_ALL,
				TEST_POP_ALL,
				TEST_POP_ALL_INVERTED,
				TEST_PUSH_AND_POP_ALL,

				// More advanced tests (PPU and SPU are continuously accessing the same stacks).
				TEST_PULL_FROM_PUSH_TO,
				TEST_PULL_FROM_PUSH_TO_INFINITE,

				// If you add a value here, make sure to check if there is zero or one parameter.
			};

			CSoftwareCacheUnitTest()
				:
				m_Kind(TEST_EMPTY),
				m_nData(0),
				m_Stack1(),
				m_nStopInDebugger(0)
			{
				// Do nothing...
			}
				
			UnitTest m_Kind;		// SPU doesn't like PPU function pointers, so use an enum. :)
			int m_nData;

			typedef CLockLessStack<12> Stack;
			Stack	m_Stack1;
			Stack	m_Stack2;
			int		m_nStopInDebugger;
			// This should be 20 bytes long
			int m_nPad1, m_nPad2, m_nPad3;			// To make it 32 bytes long

			void SetTestToRun(Stack & stack, UnitTest kind)
			{
				Assert(kind < TEST_WITH_ONE_PARAMETER);
				m_Stack1 = stack;
				m_Kind = kind;
			}

			void SetTestToRun(Stack & stack, UnitTest kind, int nData)
			{
				Assert(kind >= TEST_WITH_ONE_PARAMETER);
				m_Stack1 = stack;
				m_Kind = kind;
				m_nData = nData;
			}

			void SetTestToRun(Stack & stack1, Stack & stack2, UnitTest kind, int nData)
			{
				Assert(kind >= TEST_WITH_ONE_PARAMETER);
				m_Stack1 = stack1;
				m_Stack2 = stack2;
				m_Kind = kind;
				m_nData = nData;
			}

			void RunTest()
			{
				switch (m_Kind)
				{
				case TEST_EMPTY:
					TestEmpty();
					break;
				case TEST_NOT_EMPTY:
					TestNotEmpty();
					break;
				case TEST_CLEAR:
					TestClear();
					break;
				case TEST_RESET_TO_FULL:
					TestResetToFull();
					break;

				case TEST_PUSH_ONE:
					TestPushOne(m_nData);
					break;
				case TEST_POP_ONE:
					TestPopOne(m_nData);
					break;
				case TEST_PUSH_ALL:
					TestPushAll(m_nData);
					break;
				case TEST_POP_ALL:
					TestPopAll(m_nData);
					break;
				case TEST_POP_ALL_INVERTED:
					TestPopAllInverted(m_nData);
					break;
				case TEST_PUSH_AND_POP_ALL:
					TestPushAndPopAll(m_nData);
					break;

				case TEST_PULL_FROM_PUSH_TO:
					TestPullFromPushTo(m_nData, false);
					break;
				case TEST_PULL_FROM_PUSH_TO_INFINITE:
					TestPullFromPushTo(m_nData, true);
					break;
				}
			}

			// Expects the stack to be empty.
			void TestEmpty()
			{
				Assert(m_Stack1.IsEmpty());
#if !IsPlatformPS3_SPU()
				Assert(m_Stack1.GetSize() == 0);
#endif
			}

			// Expects the stack to be not empty.
			void TestNotEmpty()
			{
				Assert(m_Stack1.IsEmpty() == false);
#if !IsPlatformPS3_SPU()
				Assert(m_Stack1.GetSize() != 0);
#endif
			}

			// Pushes one item on the stack.
			void TestPushOne(int nValue)
			{
				m_Stack1.PushToHead(nValue);
#if !IsPlatformPS3_SPU()
				Assert(m_Stack1.GetSize() == 1);
#endif
			}

			// Pops one item from the stack (and expects a passed value).
			void TestPopOne(int nValue)
			{
				int nStoredValue;
				nStoredValue = m_Stack1.PopFromHead();
				Assert(nStoredValue == nValue);
			}

			// Pushes 0...N
			void TestPushAll(int nNumberToPush)
			{
				Assert(m_Stack1.IsEmpty());
				for (int i = 0 ; i < nNumberToPush ; ++i)
				{
					m_Stack1.PushToHead(i);
				}
#if !IsPlatformPS3_SPU()
				Assert(m_Stack1.GetSize() == nNumberToPush);
#endif
			}

			// Pops all the stack, expects N...0
			void TestPopAll(int nExpectedNumberToPop)
			{
				while (m_Stack1.IsEmpty() == false)
				{
					--nExpectedNumberToPop;
					int nValue;
					nValue = m_Stack1.PopFromHead();
					Assert(nExpectedNumberToPop == nValue);
				}
				Assert(nExpectedNumberToPop == 0);
			}

			// Pops all the stack, expects 0...N
			// ResetToFull() returns that format.
			void TestPopAllInverted(int nExpectedNumberToPop)
			{
				int nExpectedValue = 0;
				while (m_Stack1.IsEmpty() == false)
				{
					--nExpectedNumberToPop;
					int nValue;
					nValue = m_Stack1.PopFromHead();
					Assert(nExpectedValue == nValue);
					++nExpectedValue;
				}
				Assert(nExpectedNumberToPop == 0);
			}

			// Pushes 0...N to the stack, then pop them all.
			void TestPushAndPopAll(int nExpectedNumberToPush)
			{
				Assert(m_Stack1.IsEmpty());
				for (int i = 0 ; i < nExpectedNumberToPush ; ++i)
				{
					m_Stack1.PushToHead(i);
				}

				for (int i = 0 ; i < nExpectedNumberToPush ; ++i)
				{
					int nValue;
					nValue = m_Stack1.PopFromHead();
					Assert(nValue == (nExpectedNumberToPush - 1) - i);
				}
				Assert(m_Stack1.IsEmpty());
			}

			// Tests that we can clear the stack.
			void TestClear()
			{
				m_Stack1.Clear();
				Assert(m_Stack1.IsEmpty());
			}

			void TestResetToFull()
			{
				Assert(m_Stack1.IsEmpty());
#if !IsPlatformPS3_SPU()
				m_Stack1.ResetToFull();
#else
				Assert(false);		// Not supported on SPU
#endif
			}

			void TestPullFromPushTo(int nNumberOfIterations, bool bInfinite)
			{
				for (int i = 0 ; i < nNumberOfIterations ; ++i)
				{
					int nIndex;
					if (bInfinite)
					{
						do
						{
							nIndex = m_Stack1.PopFromHead();
						}
						while (nIndex < 0);					// If < 0, the head is empty, wait for the other thread to push a new one
					}
					else
					{
						nIndex = m_Stack1.PopFromHead();
						if (nIndex < 0)
						{
							return;
						}
					}

					m_Stack2.PushToHead(nIndex);
				}
			}

		} ALIGN16_POST;

		BLOB_COMPILE_TIME_ASSERT((sizeof(CSoftwareCacheUnitTest) % 32) == 0);
	}
}

#endif
