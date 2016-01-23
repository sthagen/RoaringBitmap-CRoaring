#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <x86intrin.h>
#include <stdint.h>

#include "util.h"



#ifdef BRANCHLESSBINSEARCH


/**
* the branchless approach is inspired by 
*  Array layouts for comparison-based searching
*  http://arxiv.org/pdf/1509.05053.pdf
*/
// could potentially use SIMD-based bin. search
int32_t binarySearch(uint16_t* source, int32_t n, uint16_t target) {
	uint16_t * base = source;
    if(n == 0) return -1;
    if(target > source[n-1]) return - n - 1;// without this we have a buffer overrun
    while(n>1) {
    	int32_t half = n >> 1;
#ifdef BRANCHLESSBINSEARCHPREFETCH
        __builtin_prefetch(base+(half>>1),0,0);
        __builtin_prefetch(base+half+(half>>1),0,0);
#endif
        base = (base[half] < target) ? &base[half] : base;
        n -= half;
    }
    // todo: over last cache line, you can just scan or use SIMD instructions
    base += *base < target;
    return *base == target ? base - source : source - base -1;
}

#elif defined(HYBRIDBINSEACH)

// good old bin. search ending with a sequential search
// could potentially use SIMD-based bin. search
int32_t binarySearch(uint16_t * array, int32_t lenarray, uint16_t ikey )  {
	int32_t low = 0;
	int32_t high = lenarray - 1;
	while( low+16 <= high) {
		int32_t middleIndex = (low+high) >> 1;
		uint16_t middleValue = array[middleIndex];
		if (middleValue < ikey) {
			low = middleIndex + 1;
		} else if (middleValue > ikey) {
			high = middleIndex - 1;
		} else {
			return middleIndex;
		}
	}
	for (; low <= high; low++) {
		uint16_t val = array[low];
		if (val >= ikey) {
			if (val == ikey) {
				return low;
			}
			break;
		}
	}
	return -(low + 1);
}


#else

// good old bin. search 
int32_t binarySearch(uint16_t * array, int32_t lenarray, uint16_t ikey )  {
	int32_t low = 0;
	int32_t high = lenarray - 1;
	while( low <= high) {
		int32_t middleIndex = (low+high) >> 1;
		uint16_t middleValue = array[middleIndex];
		if (middleValue < ikey) {
			low = middleIndex + 1;
		} else if (middleValue > ikey) {
			high = middleIndex - 1;
		} else {
			return middleIndex;
		}
	}
	return -(low + 1);
}

#endif

int32_t advanceUntil(
		uint16_t * array,
		int32_t pos,
		int32_t length,
		uint16_t min)  {
	int32_t lower = pos + 1;

	if ((lower >= length) || (array[lower] >= min)) {
		return lower;
	}

	int32_t spansize = 1;

	while ((lower+spansize < length) && (array[lower+spansize] < min)) {
		spansize <<= 1;
	}
	int32_t upper = (lower+spansize < length )?lower + spansize:length - 1;

	if (array[upper] == min) {
		return upper;
	}

	if (array[upper] < min) {
		// means
		// array
		// has no
		// item
		// >= min
		// pos = array.length;
		return length;
	}

	// we know that the next-smallest span was too small
	lower += (spansize >> 1);

	int32_t mid = 0;
	while( lower+1 != upper) {
		mid = (lower + upper) >> 1;
		if (array[mid] == min) {
			return mid;
		} else if (array[mid] < min) {
			lower = mid;
		} else {
			upper = mid;
		}
	}
	return upper;

}