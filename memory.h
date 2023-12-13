#ifndef memory_h
#define memory_h

#include <stdlib.h>
#include "object.h"

#define ALLOCATE(type, count) \
			(type*)reallocate(NULL, 0, sizeof(type) * count)

#define GROW_CAPACITY(capacity) \
		((capacity) < 8 ? 8 : 2 * (capacity))

#define GROW_ARRAY(type, pointer, oldSize, newSize) \
	(type*)reallocate(pointer, sizeof(type) * (oldSize), sizeof(type) * (newSize))

#define FREE_ARRAY(type, pointer, oldSize) \
	reallocate(pointer, sizeof(type) * (oldSize), 0)

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

void freeObjects();

#endif memory_h
