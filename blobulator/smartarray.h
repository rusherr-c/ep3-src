//==== Blobulator. Copyright (c) 2005-2006, Rosware LLC, All rights reserved. ====//
#pragma once

/////////////////////////////////////////////
// SmartArray version 2.0
//
// 11/13/2006 Added quick sort function
//
/////////////////////////////////////////////

// The following define controls whether ASSERTS will be enabled
// in a debug build. It should have no effect on Release builds
// since asserts are not compiled in release builds.
#define ASSERT_SMART_ARRAY

#define KEEP_MAX_SIZE 0

#ifdef SPU
#include "ps3/spu_job_shared.h"

// The SPU version is very lightweight. Just the bare minimum to make it compile
// There is no allocation either.
// But it needs the same memory footprint as the PPU version.

template <class T, bool sse_align = false, int initialCapacity = 16>
class SmartArray
{
public:
	FORCEINLINE void pushAutoSize(const T& e)
	{
		// We are not allocating on the SPU, so the capacity has to be big enough...
		Assert(size < capacity);
		a[size] = e;
		size ++;
	}

	FORCEINLINE T& pop(void)
	{
#ifdef ASSERT_SMART_ARRAY
		Assert(size >= 1);
#endif
		return a[--size];
	}

	FORCEINLINE T& operator[] (int i)
	{
#ifdef ASSERT_SMART_ARRAY
		Assert(i >= 0 && i < size);
#endif
		return a[i];
	}

	T* a;
	int size;
	int capacity;
#if (KEEP_MAX_SIZE == 1)
	int max_size;
#endif
};

#else

#ifndef _PS3
#include <malloc.h>
#include <memory.h>
#endif
#include "implicit/impdefines.h"
#include "tier0/memalloc.h"

#if (defined(RENDER_API) && RENDER_API==VALVE_API)
	#include "tier0/dbg.h"
#else
	#include <assert.h>
	#define Assert assert
#endif

// BIG WARNING: Never keep a pointer to a smart array element.
// When the array resizes the pointer will point to junk!!!

namespace Helper
{
	// Static version, can be used for SmartArray or standard C++ arrays

	// The following function quick-sorts an array of size 3 or more.
	// For identifying the pivot, it takes the median of the first, last
	// and central item. Due to this, it performs well on arrays that are
	// sorted, backwards sorted, and on degenerate cases such as an array
	// where many values are the same.

	// This code has been thoroughly tested.... do not change without
	// careful testing afterwards

	// TODO: Evaluate whether making variables static in here actually
	// saves stack space, and whether it makes the code run faster.
	// TODO: Use macros for swapping items, and for insuring an ordering

	template <class T, class Sorter>
	static void sort(Sorter sorter, T * a, int lo0, int hi0)
	{
		//depth ++;
		//printf("x %d %d %d\n", lo0, hi0, depth);
		//fflush(stdout);

		int c0;
		c0 = (lo0 + hi0) / 2;

		T t;

		if(sorter.gt(a[lo0], a[hi0])) { t = a[lo0]; a[lo0] = a[hi0]; a[hi0] = t; }
		if(sorter.gt(a[lo0], a[c0]))  { t = a[lo0]; a[lo0] = a[c0];  a[c0] = t;  }
		if(sorter.gt(a[c0],  a[hi0])) { t = a[c0];  a[c0]  = a[hi0]; a[hi0] = t; }

		// if the size is 3, everything is now sorted... return.
		// the size should not be less than 3
		if(hi0 - lo0 <= 2) return;

		//printf("%d %d %d\n", a[lo0], a[c0], a[hi0]);

		// Find the value of the pivot and swap out the pivot to the end of the array
		// We don't explicitly need to store the pivot in the end of the array because
		// we store it inside of mid.
		// NOTE: A slight optimization might be to do the sort above for size==3 and to
		// do a slightly different sort here which doesn't modify a[hi0] (since it gets
		// overwritten with a[lo] after the partition step anyway.
		T mid;
		mid = a[c0]; a[c0] = a[hi0]; /*a[hi0] = mid;*/

		// lo is set to lo0-1 instead of lo0 and hi is set to hi0 instead
		// of hi0-1 because in the while loop, we immediately add +1 to lo
		// and subtract 1 from hi
		int lo = lo0-1; // lo can be made static
		int hi = hi0;

		// This is the partition step
		Assert(lo < hi);
		while (1)
		{
			// Note that because of the 3 way sort at the beginning, we are guaranteed
			// that there is either one value greater than or equal to the pivot
			// and that there is another value less than or equal to the pivot between lo0 and hi0-1.
			// Thus, there is no way that lo can go below lo0 and that hi can go above hi0-1.

			// We use a do-while loop here instead of a regular while loop, because
			// we always want lo to be incremented and hi to be decremented at the start
			// of each iteration. Otherwise, we can go into an infinite loop if there are
			// multiple items equal to the pivot.

			do {lo++;} while (sorter.gt(mid, a[lo]));
			do {hi--;} while (sorter.gt(a[hi], mid));

			if (lo >= hi) break;

			t = a[lo]; a[lo] = a[hi]; a[hi] = t;
		}

		Assert(hi >= lo0);
		Assert(lo <= hi0-1);

		// Move the mid back to the middle. We swap mid with lo because
		// we are guaranteed that lo ends up at a value that is greater or
		// equal to mid, thus we want that value to go to the end of the array
		// rather than sending a[hi] to the end (which could be less than mid).
		a[hi0] = a[lo];
		a[lo] = mid;

		// Update lo to be the element before mid, and hi to be the element
		// after mid
		hi = lo + 1;
		lo = lo - 1;

		// The two subarrays to be sorted now are lo0...lo, hi...hi0

		int lSizeM1; lSizeM1 = lo-lo0; // the size of the left subarray minus one
		// If the left subarray is size 2, sort it here.
		// If it is size 3 or more, recurse.
		// If it is size 1, do nothing.
		if(lSizeM1 == 1)
		{
			if(sorter.gt(a[lo0], a[lo]))
			{
				t = a[lo0]; a[lo0] = a[lo]; a[lo] = t;
			}
		}
		else if(lSizeM1 > 1)
		{
			sort<T, Sorter>(sorter, a, lo0, lo);
		}

		int rSizeM1; rSizeM1 = hi0-hi; // the size of the right subarray minus one
		// If the right subarray is size 2, sort it here.
		// If it is size 3 or more, recurse.
		// If it is size 1 do nothing.
		if(rSizeM1 == 1)
		{
			if (sorter.gt(a[hi], a[hi0]))
			{
				t = a[hi]; a[hi] = a[hi0]; a[hi0] = t;
			}
		}
		else if(rSizeM1 > 1)
		{
			sort<T, Sorter>(sorter, a, hi, hi0);
		}

		//depth --;
	}

	template <class T, class Sorter>
	FORCEINLINE
	static void sort(Sorter sorter, T * a, int size)
	{
		if(size == 2)
		{
			if(sorter.gt(a[0], a[1]))
			{
				T t = a[0]; a[0] = a[1]; a[1] = t;
			}
		}
		else if(size > 2)
		{
			// size is 3 or more
			Helper::sort<T, Sorter>(sorter, a, 0, size - 1);
		}
	}

	template <class T, class Sorter>
	FORCEINLINE
	static bool testSort(Sorter sorter, T * a, int size)
	{
		for(int i = 0; i < size-1; i++)
		{
			if(sorter.gt(a[i], a[i+1])) return false;
		}
		return true;
	}
}

template <class T, bool sse_align = false, int initialCapacity = 16> class SmartArray
{
	public:

	T* a;
	int size;
	int capacity;
#if (KEEP_MAX_SIZE == 1)
	int max_size;
#endif

	SmartArray()
	{
		size = 0;
		#if (KEEP_MAX_SIZE == 1)
			max_size = 0;
		#endif
		capacity = initialCapacity;
		if(initialCapacity > 0)
		{
			if(sse_align)
				a = ( T* ) MemAlloc_AllocAligned( capacity * sizeof(T), 16);
			else
				a = ( T*) malloc( capacity * sizeof( T ) );
			#ifdef ASSERT_SMART_ARRAY
			Assert(a);
			#endif
		}
		else
		{
			a = NULL;
		}
	}

	~SmartArray()
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(initialCapacity == 0 || a);
		#endif
		if(sse_align) MemAlloc_FreeAligned(a);
		else free(a);
	}

	/*SmartArray(int initialCapacity)
	{
		size = 0;
		capacity = initialCapacity;
		a = (T*)malloc(capacity * sizeof(T));
	}*/

	FORCEINLINE void ensureCapacity(int c)
	{
		if(capacity < c)
		{
			capacity = c * 2;
			if(sse_align){
				// TODO: For some reason, _aligned_realloc crashes...
				//       I think this is a problem with a Valve memory routeine.
				//       this must be fixed!!!!
				//a = (T*)_aligned_realloc(a, capacity * sizeof(T), 16);
				T* temp = (T*)MemAlloc_AllocAligned(capacity * sizeof(T), 16);
				memcpy(temp, a, size * sizeof(T));
				MemAlloc_FreeAligned(a);
				a = temp;
			}
			else a = (T*)realloc(a, capacity * sizeof(T));
			#ifdef ASSERT_SMART_ARRAY
			Assert(a);
			#endif
		}
	}

	// Make sure we can add c more elements to the array
	FORCEINLINE void ensureAdditionalCapacity(int c)
	{
		c += size;
		if(capacity < c)
		{
			capacity = c * 2;
			if(sse_align) {
				//a = (T*)_aligned_realloc(a, capacity * sizeof(T), 16);
				T* temp = (T*)MemAlloc_AllocAligned(capacity * sizeof(T), 16);
				memcpy(temp, a, size * sizeof(T));
				MemAlloc_FreeAligned(a);
				a = temp;
			}
			else a = (T*)realloc(a, capacity * sizeof(T));
			#ifdef ASSERT_SMART_ARRAY
			Assert(a);
			#endif
		}
	}

	FORCEINLINE void resize(int c)
	{
		capacity = c;
		if(sse_align) {
			//a = (T*)_aligned_realloc(a, capacity * sizeof(T), 16);
			T* temp = (T*)MemAlloc_AllocAligned(capacity * sizeof(T), 16);
			memcpy(temp, a, size * sizeof(T));
			MemAlloc_FreeAligned(a);
			a = temp;
		}
		else a = (T*)realloc(a, capacity * sizeof(T));
		#ifdef ASSERT_SMART_ARRAY
		Assert(a);
		#endif
	}

	FORCEINLINE T& operator[] (int i)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(i >= 0 && i < size);
		#endif
		return a[i];
	}

	FORCEINLINE void put(int i, const T& e)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(i >= 0 && i < size);
		#endif
		a[i] = e;
	}

	/*
	FORCEINLINE void putAutoSize(int i, const T& e)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(i >= 0);
		#endif
		if(i >= size)
		{
			size = i + 1;
			resize(size * 2);
		}
		a[i] = e;
	}
	*/

	FORCEINLINE void push(const T& e)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size < capacity);
		#endif
		a[size] = e;
		size ++;
	}

	// Many times, you want to push a new item, but rather
	// than passing in data, you want to just get a pointer
	// or a reference to it so that you can then write
	// directly into that item. That's what this function is for.
	FORCEINLINE T& push()
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size < capacity);
		#endif
		return a[size++];
	}
	
#if 0

	// Unfortunately interlockedPush is not really thread safe.
	// I.e. the size is incremented, but the item is not pushed yet.
	// Thus if the result is popped before the value has been set, we will retrieve an incorrect data.
	// We actually have to use a futex for push and pop.

	// NOTE: The int32 type used to be type LONG. Must verify int32 is safe.
	FORCEINLINE T& interlockedPush(void)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size < capacity);
		#endif

		return a[ThreadInterlockedIncrement((int32 volatile*)&size) - 1];
	}

	FORCEINLINE void interlockedPush(const T & e)
	{
#ifdef ASSERT_SMART_ARRAY
		Assert(size < capacity);
#endif
		int index = ThreadInterlockedIncrement((int32 volatile*)&size) - 1;
		a[index] = e;
	}

	FORCEINLINE bool interlockedIsEmpty()
	{
		// We don't want to increment / decrement as it would not be thread safe.
		// This atomically set the size to zero if it was zero (i.e. no-op), and return the previous value in any case.
		int oldValue = ThreadInterlockedCompareExchange((int32 volatile*)&size, 0, 0);
		return (oldValue == 0);
	}

	// NOTE: The int32 type used to be type LONG. Must verify int32 is safe.
	FORCEINLINE int interlockedIncrementSize(void)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size >= 1);
		#endif

		return InterlockedIncrementAcquire((int32 volatile*)&size);
	}
#endif
	
	FORCEINLINE void push2(const T& e1, const T& e2)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert( ( size + 1 ) < capacity);
		#endif
		a[size] = e1;
		a[size+1] = e2;
		size += 2;
	}

	FORCEINLINE void push2AutoSize(const T& e1, const T& e2)
	{
		if(size+1 >= capacity)
		{
			resize((size+2) * 2);
		}
		a[size] = e1;
		a[size+1] = e2;
		size += 2;
	}

	FORCEINLINE void pushAutoSize(const T& e)
	{
		if(size >= capacity)
		{
			resize((size + 1) * 2);
		}
		a[size] = e;
		size ++;
	}

	// This is analogous to the push() function
	FORCEINLINE T& pushAutoSize()
	{
		if(size >= capacity)
		{
			resize((size + 1) * 2);
		}
		return a[size++];
	}

	FORCEINLINE T& pop(void)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size >= 1);
		#endif
		return a[--size];
	}

#if 0
	FORCEINLINE T& interlockedPop(void)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size >= 1);
		#endif

		return a[ThreadInterlockedDecrement( (int32 volatile*)&size )];
	}

	FORCEINLINE int interlockedDecrementSize(void)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size >= 1);
		#endif

		return InterlockedDecrementAcquire((int32 volatile*)&size);
	}
#endif

	FORCEINLINE void pop2(T& e1, T& e2)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(size >= 2);
		#endif
		e1 = a[size-2];
		e2 = a[size-1];
		size -= 2;
	}

	FORCEINLINE void remove(int i)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(i >= 0 && i < size);
		#endif
		for( ;i < size-1; i++)
		{
			a[i] = a[i+1];
		}
		size--;
	}

	FORCEINLINE T removeAndReturn(int i)
	{
		#ifdef ASSERT_SMART_ARRAY
		Assert(i >= 0 && i < size);
		#endif
		T removed = a[i];
		for( ;i < size-1; i++)
		{
			a[i] = a[i+1];
		}
		size--;
		return removed;
	}

	FORCEINLINE int find(const T& e)
	{
		for(int i = 0; i < size; i++)
		{
			if(e == a[i]) return i;
		}
		return -1;
	}

	FORCEINLINE bool findAndRemove(const T& e)
	{
		int i = find(e);
		if(i == -1)
		{
			return false;
		}
		else
		{
			remove(i);
			return true;
		}
	}

	FORCEINLINE void findAndRemoveAll(const T& e)
	{
		while(findAndRemove(e) == true) {};
	}

	// Get the last item
	FORCEINLINE T& last()
	{
		return a[size-1];
	}

	template <class Sorter>
	FORCEINLINE bool testSort(Sorter sorter)
	{
		return Helper::testSort<T, Sorter>(sorter, a, size);
	}

	// TODO: Should try using swap in sort
	FORCEINLINE void swap(int i, int j)
	{
		T t = a[i]; a[i] = a[j]; a[j] = t;
	}

	// The following function quicksorts the smart array, and will work on normal array as well (static version only).
	// It uses a median of the first, last, and middle value of a
	// sub-array to find pivots, thus it does well on both sorted,
	// and reverse sorted arrays. It also does well on arrays that
	// contain many duplicate values.

	// Non-static version, only for SmartArray

	template <class Sorter>
	FORCEINLINE
	void sort(Sorter sorter)
	{
		Helper::sort<T, Sorter>(sorter, a, size);
	}

	template <class Sorter>
	FORCEINLINE
	void sort(Sorter sorter, int lo0, int hi0)
	{
		Helper::sort<T, Sorter>(sorter, a, lo0, hi0);
	}

	FORCEINLINE void swapData(SmartArray<T, sse_align, initialCapacity>& other)
	{
		T* tempa = a;
		a = other.a;
		other.a = tempa;

		int temp_size = size;
		size = other.size;
		other.size = temp_size;
		
		#if (KEEP_MAX_SIZE == 1)
		int temp_max_size = max_size;
		max_size = other.max_size;
		other.max_size = temp_max_size;
		#endif
			
		int temp_capacity = capacity;
		capacity = other.capacity;
		other.capacity = temp_capacity;
	}

private:
	// Start do not implement
	SmartArray(const SmartArray<T> & other);
	SmartArray & operator=(const SmartArray<T> & other);
	// End do not implement
};
#endif

template <typename T, int SIZE>
class FixedArray
{
public:
	FixedArray()
		:
		m_nSize(0)
	{
		// Do nothing...
	}

	// Kept the name for easy switch between FixedArray and SmartArray
	FORCEINLINE void pushAutoSize(const T& e)
	{
		// We are not allocating on the SPU, so the capacity has to be big enough...
		if (m_nSize < SIZE)
		{
			m_Items[m_nSize] = e;
			++m_nSize;
		}
	}

	FORCEINLINE T& pop(void)
	{
		Assert(m_nSize >= 1);
		return m_Items[--m_nSize];
	}

	FORCEINLINE T& operator[] (int i)
	{
		Assert((i >= 0) && (i < m_nSize));
		return m_Items[i];
	}

	int GetSize() const
	{
		return m_nSize;
	}

	void SetSize(int nSize)
	{
		Assert(nSize >= 0);
		m_nSize = nSize;
	}

	void Clear()
	{
		m_nSize = 0;
	}

private:
	T m_Items[SIZE];
	int m_nSize;
};

