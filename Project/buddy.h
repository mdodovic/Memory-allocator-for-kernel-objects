#pragma once

#include "slab.h"

#define SIZE_OF_POINTER 4

typedef struct buddy_free_block{
	struct buddy_free_block* next;
	char fill_data[BLOCK_SIZE - SIZE_OF_POINTER];
} buddy_free_block;

typedef struct buddy_free_block_header {
	buddy_free_block* first, * last;
	int number_of_free_blocks;
}buddy_header;

typedef struct buddy {
	buddy_header* array_of_free_blocks;
	int buddy_max_degree;
	int buddy_num_of_blocks;
	int useful_num_of_blocks; 
	buddy_free_block* first_useful_block;
} buddy;

buddy* buddy_allocator;

// Info
void print_buddy_memory(int);

// Initialisations
int buddy_init(void* , int );

// Allocations
void* buddy_allocate_by_blocks(int);
void* buddy_allocate_by_size_bytes(int);


// Dealocations
int buddy_return(void* , int);

// Utility functions called from slab allocator
void __power_of_two_calculation(int n, int* first_less, int* value);