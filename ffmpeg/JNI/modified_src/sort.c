#include <string.h>
#include <alloca.h>

/**
 * Implements insertion sort as a stable alternative of qsort() for small data.
 * @see http://en.wikipedia.org/wiki/Insertion_sort
 */
void isort( void* base, size_t num, size_t size, int (*compar)(const void*,const void*) )
{
	char *temp = 0;
	int i, j;

	// for i ← 1 to length(A)
	for( i = 1; i < (int)num; ++i )
	{
		// j ← i

	    // while j > 0 and A[j-1] > A[j]
	    for( j = i; j > 0; --j )
	    {
	    	char* j_ptr = (char*)base + j * size;
	    	char* j_1_ptr = j_ptr - size;

	    	if( (*compar)(j_1_ptr, j_ptr) <= 0 )
	    		break;

	    	// swap A[j] and A[j-1]
	    	if( temp == 0 )
	    		temp = (char*)alloca(size);

	    	// Memcpy is fast enough.
	    	memcpy( temp, j_1_ptr, size );
	    	memcpy( j_1_ptr, j_ptr, size );
	    	memcpy( j_ptr, temp, size );

	    	// j ← j - 1
	    }
	}
}
