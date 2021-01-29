#pragma once

#include <stdlib.h>
#include <Windows.h>
typedef struct kmem_cache_s kmem_cache_t;

 
#define BLOCK_SIZE (4096) // block size in bytes = 4KB
#define CACHE_L1_LINE_SIZE (64)
#define BLOCK_BITS (12)

#define MIN_SMALL_MEMORY_BUFFER 1 << 5
#define MAX_SMALL_MEMORY_BUFFER 1 << 17

CRITICAL_SECTION CriticalSection;

void kmem_init(void* space, int block_num);

void* kmalloc(size_t size); // Alloacate one small memory buffer

void kfree(const void* objp); // Deallocate one small memory buffer

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*)); // Allocate cache

void kmem_cache_destroy(kmem_cache_t* cachep); // Deallocate cache

void* kmem_cache_alloc(kmem_cache_t* cachep); // Allocate one object from cache

void kmem_cache_free(kmem_cache_t* cachep, void* objp); // Deallocate one object from cache

void kmem_cache_info(kmem_cache_t* cachep); // Print cache info

int kmem_cache_error(kmem_cache_t* cachep); // Print error message

int kmem_cache_shrink(kmem_cache_t* cachep); // Shrink cache
