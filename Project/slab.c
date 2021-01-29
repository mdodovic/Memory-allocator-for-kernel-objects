#include<string.h>
#include "slab.h"
#include "buddy.h"
#include <Windows.h>
#include <stdio.h>

#define EMPTY 0
#define PARTIALLY 1
#define FULL 2

extern buddy* buddy_allocator;

typedef struct slab
{
	struct slab* next_slab;
	struct slab* prev_slab;

	struct kmem_cache_s* my_cache; // cache which this slab belongs to

	void* slots; // need to be void* - it is unknown which type of object is in this slab
	int number_of_free_slots; // current number of free slots 

	int my_offset; // L1 cache allign: Number of free L1 lines after the end of metadata

	char* free_slots_flag; // array that contains info which slot is free: =1 is free and =0 is not free

} slab;


typedef struct kmem_cache_s
{
	struct kmem_cache_s* next_cache;
	struct kmem_cache_s* previous_cache;

	// three types of caches (empty, full and partial)
	slab* full_slabs;
	slab* empty_slabs;
	slab* partially_full_slabs;

	char object_type_name[20]; // object's name
	void(*ctor)(void*); // constructor for cache's type of object
	void(*dtor)(void*); // destructor for cache's type of object

	// sizes:
	size_t size_of_object; // size of object which this cache is
	size_t size_of_one_slab; // size of one slab in number of blocks (estimated so waste_in_bytes is minimal)
	size_t waste_in_bytes; // waste of memory in each slab
	int number_of_objects_per_slab; // size of one slab in number of blocks (estimated so waste_in_bytes is minimal)

	int number_of_alignments_per_cache; // L1 cache allign
	int alignment_for_next_slab; // L1 cache allign

	int has_grown; // flag which tells if cache has grown since last shrinking
	
	int error_code; // 0 everything is ok, 1 unable to create cache, 2 unable to destroy cache

} kmem_cache_t;

typedef struct size_N 
{
	size_t size; // size is poewr of 2 in range [5, 17] bytes
	kmem_cache_t* size_N_cache; // pointer to cache which this belongs to!
} size_N;


kmem_cache_t* cache_of_caches; // pointer to the list of caches
size_N* size_N_array; // pointer to the array of small memory buffer

// declaration of helper functions used by slab allocation
void __estimate_slab_size(int object_size, int* slab_size_blocks, int* waste_bytes, int* number_of_objects_per_slab);
void __initialize_cache_of_caches();
void __add_empty_slab(kmem_cache_t* cache);
void __transfer_from_source_to_destination_slab(kmem_cache_t* cache, slab* source_slab, int id_of_source, int id_of_destination);
int __find_free_slot_index(slab* slab_to_put_in);
void __make_cache_of_size_N();
void __create_size_N_slab(kmem_cache_t* size_N_cache);
void* __allocate_one_slot_from_cache(kmem_cache_t* cache);
int __check_power_of_two(unsigned number);
int __find_and_delete_from_slab(kmem_cache_t* cache_of_object, const void* object_pointer);
void __return_empty_slab_to_buddy(kmem_cache_t* cache);
kmem_cache_t* __make_cache_of_object(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*));
int __number_of_slabs_into_cache(kmem_cache_t* cache);
int __number_of_free_slots_into_cache(kmem_cache_t* cache);
void __print_cache_info(kmem_cache_t* cache_to_be_printed);


void __estimate_slab_size(int object_size, int* slab_size_blocks, int* waste_bytes, int* number_of_objects_per_slab)
{ 
	// Now all the estimations are 2^0 block size! or greater if it is necessary.

	unsigned temporary_pow_of_two_block_size = 0;	
	unsigned temporary_slab_size = BLOCK_SIZE; // first estimation: 1 BLOCK

	unsigned minimal_size = sizeof(slab); // meta data; this is necessary in every slab

	*number_of_objects_per_slab = 0;
	int estimation_of_waste;

	while (*number_of_objects_per_slab == 0)
	{
		*number_of_objects_per_slab = 0;
		while ((*number_of_objects_per_slab) * object_size + *number_of_objects_per_slab + minimal_size <= temporary_slab_size)
		{
			(*number_of_objects_per_slab)++;
		}

		(*number_of_objects_per_slab)--; // final number of object per slab
		if ((*number_of_objects_per_slab) == 0)
		{
			temporary_pow_of_two_block_size++;;
			temporary_slab_size = BLOCK_SIZE << temporary_pow_of_two_block_size; // first estimation: 1 BLOCK
		}
	}
	*slab_size_blocks = 1 << temporary_pow_of_two_block_size;
	*waste_bytes = temporary_slab_size - ((*number_of_objects_per_slab) * object_size + minimal_size + *number_of_objects_per_slab);

}


void __initialize_cache_of_caches()
{
	strcpy_s(cache_of_caches->object_type_name, sizeof("cache-of-caches"), "cache-of-caches");
	
	cache_of_caches->next_cache = NULL;
	cache_of_caches->previous_cache = NULL;

	cache_of_caches->full_slabs = NULL;
	cache_of_caches->empty_slabs = NULL;
	cache_of_caches->partially_full_slabs = NULL;

	cache_of_caches->ctor = NULL;
	cache_of_caches->dtor = NULL;

	cache_of_caches->size_of_object = sizeof(kmem_cache_t);
	__estimate_slab_size(cache_of_caches->size_of_object, &cache_of_caches->size_of_one_slab, &cache_of_caches->waste_in_bytes, &cache_of_caches->number_of_objects_per_slab);

	cache_of_caches->number_of_alignments_per_cache = 1;
	cache_of_caches->alignment_for_next_slab = 0;

	cache_of_caches->has_grown = 0;
	cache_of_caches->error_code = 0;

	// no one empty slot has created yet!
}

void __add_empty_slab(kmem_cache_t* cache)
{
	slab* new_slab = (slab*) buddy_allocate_by_blocks(cache->size_of_one_slab); 

	if (new_slab == NULL)
	{
		cache->error_code = 1;
		return;
	}

	new_slab->my_cache = cache;
	
	new_slab->number_of_free_slots = cache->number_of_objects_per_slab;


	new_slab->prev_slab = NULL;
	new_slab->next_slab = cache->empty_slabs;
	cache->empty_slabs = new_slab;

	new_slab->free_slots_flag = (char*)(new_slab + 1);

	for (int i = 0; i < cache->number_of_objects_per_slab; i++)
		new_slab->free_slots_flag[i] = 1;

	new_slab->slots = (kmem_cache_t*)((char*)new_slab + sizeof(slab) + cache->number_of_objects_per_slab + cache->alignment_for_next_slab * CACHE_L1_LINE_SIZE);

	new_slab->my_offset = cache->alignment_for_next_slab;
	cache->alignment_for_next_slab = (cache->alignment_for_next_slab + 1) % cache->number_of_alignments_per_cache;

	if (cache->ctor != NULL)
	{
		for (int i = 0; i < new_slab->number_of_free_slots; i++)
		{
			void* address_for_constructor = (void*)((unsigned)new_slab->slots + i * cache->size_of_object);
			(cache->ctor)(address_for_constructor);
		}
	}

}

void __transfer_from_source_to_destination_slab(kmem_cache_t* cache, slab* source_slab, int id_of_source, int id_of_destination)
{
	// remove source_slab from list
	if (source_slab->prev_slab == NULL)
	{
		// First
		if (id_of_source == EMPTY)
		{
			cache->empty_slabs = cache->empty_slabs->next_slab;
			if(cache->empty_slabs != NULL)
				cache->empty_slabs->prev_slab = NULL;
		}
		else if (id_of_source == PARTIALLY)
		{
			cache->partially_full_slabs = cache->partially_full_slabs->next_slab;
			if (cache->partially_full_slabs != NULL)
				cache->partially_full_slabs->prev_slab = NULL;
		}
		else
		{
			cache->full_slabs = cache->full_slabs->next_slab;
			if (cache->full_slabs != NULL)
				cache->full_slabs->prev_slab = NULL;
		}
	}
	else if (source_slab->next_slab == NULL)
	{ 
		// Last
		source_slab->prev_slab->next_slab = NULL;
	}
	else
	{
		// Middle
		source_slab->prev_slab->next_slab = source_slab->next_slab;
		source_slab->next_slab->prev_slab = source_slab->prev_slab;
	}


	slab** destination;
	if (id_of_destination == EMPTY)
	{
		destination = &cache->empty_slabs;

	}
	else if (id_of_destination == PARTIALLY)
	{
		destination = &cache->partially_full_slabs;
	}
	else
	{
		destination = &cache->full_slabs;
	}

	// add source to destination list!
	slab* tmp = *destination;
	*destination = source_slab;

	if (tmp == NULL)
	{
		source_slab->prev_slab = NULL;
		source_slab->next_slab = NULL;
	}
	else
	{
		source_slab->prev_slab = NULL;
		source_slab->next_slab = tmp;
		tmp->prev_slab = source_slab;
	}
}

int __find_free_slot_index(slab* slab_to_put_in)
{
	
	for (int i = 0; i < slab_to_put_in->my_cache->number_of_objects_per_slab; i++)
	{
		if (slab_to_put_in->free_slots_flag[i] == 1)
			return i;
	}
	// Exception! 
	return -1;
}


void __make_cache_of_size_N()
{
	char name[20];

	for (int i = 0; i < 13; i++) 
	{
		size_N_array[i].size = 1 << (i + 5);
		// size_N_array[i].size_N_cache is pointer to cache:
		//__print_cache_info(cache_of_caches);
		if (cache_of_caches->partially_full_slabs == NULL)
		{
			if (cache_of_caches->empty_slabs == NULL)
			{
				__add_empty_slab(cache_of_caches);
			}
			__transfer_from_source_to_destination_slab(cache_of_caches, cache_of_caches->empty_slabs, EMPTY, PARTIALLY);
//			__transfer_empty_to_partially_full_slab(cache_of_caches);
		} 
		//__print_cache_info(cache_of_caches);
		//printf("\n FInd index\n");
		int index_to_put_slot = __find_free_slot_index(cache_of_caches->partially_full_slabs);

//		if (cache_of_caches->partially_full_slabs != NULL)

		size_N_array[i].size_N_cache = ((kmem_cache_t*)cache_of_caches->partially_full_slabs->slots + index_to_put_slot);

		cache_of_caches->partially_full_slabs->free_slots_flag[index_to_put_slot] = 0;

		cache_of_caches->partially_full_slabs->number_of_free_slots--;

		if (cache_of_caches->partially_full_slabs->number_of_free_slots == 0)
		{
			__transfer_from_source_to_destination_slab(cache_of_caches, cache_of_caches->partially_full_slabs, PARTIALLY, FULL);
		}
			
		sprintf_s(name, 20, "size-%d", i + 5);
		strcpy_s(size_N_array[i].size_N_cache->object_type_name, sizeof(name), name);

		size_N_array[i].size_N_cache->next_cache = NULL;
		size_N_array[i].size_N_cache->previous_cache = NULL;
		// Add caches to the list of caches
		if (cache_of_caches->next_cache == NULL)
		{
			cache_of_caches->next_cache = size_N_array[i].size_N_cache;
		}
		else
		{
			size_N_array[i].size_N_cache->next_cache = cache_of_caches->next_cache;
			cache_of_caches->next_cache->previous_cache = size_N_array[i].size_N_cache;
			cache_of_caches->next_cache = size_N_array[i].size_N_cache;
		}

		size_N_array[i].size_N_cache->full_slabs = NULL;
		size_N_array[i].size_N_cache->empty_slabs = NULL;
		size_N_array[i].size_N_cache->partially_full_slabs = NULL;

		size_N_array[i].size_N_cache->ctor = NULL;
		size_N_array[i].size_N_cache->dtor = NULL;

		size_N_array[i].size_N_cache->size_of_object = 1 << (i + 5);
		__estimate_slab_size(size_N_array[i].size_N_cache->size_of_object,
			&size_N_array[i].size_N_cache->size_of_one_slab, 
			&size_N_array[i].size_N_cache->waste_in_bytes, 
			&size_N_array[i].size_N_cache->number_of_objects_per_slab);

		size_N_array[i].size_N_cache->number_of_alignments_per_cache = size_N_array[i].size_N_cache->waste_in_bytes / CACHE_L1_LINE_SIZE + 1;
		size_N_array[i].size_N_cache->alignment_for_next_slab = 0;

		size_N_array[i].size_N_cache->has_grown = 0;
		size_N_array[i].size_N_cache->error_code = 0;

	}
}

void __create_size_N_slab(kmem_cache_t* size_N_cache)
{
	if (size_N_cache->partially_full_slabs == NULL)
	{
		if (size_N_cache->empty_slabs == NULL)
		{
			__add_empty_slab(size_N_cache);
			if (size_N_cache->error_code == 1)
				return;
		}
		__transfer_from_source_to_destination_slab(size_N_cache, size_N_cache->empty_slabs, EMPTY, PARTIALLY);
	}
}



void* __allocate_one_slot_from_cache(kmem_cache_t* cache)
{
	__create_size_N_slab(cache); // If partially full slab exists, nothing will happend. If it doesn't exitst, make it and then it is here
	if (cache->error_code == 1)
		return NULL;

	slab* slab_to_put_in = cache->partially_full_slabs; // It is unimportant which partially_full_slabs we choose!
	int index_of_free_slot = __find_free_slot_index(slab_to_put_in);


	void* address = NULL;

	address = (void*)((unsigned)slab_to_put_in->slots + index_of_free_slot * cache->size_of_object);

	slab_to_put_in->free_slots_flag[index_of_free_slot] = 0;
	
	slab_to_put_in->number_of_free_slots--;

	if (slab_to_put_in->number_of_free_slots == 0)
	{
		__transfer_from_source_to_destination_slab(cache, slab_to_put_in, PARTIALLY, FULL);
	}
	return address;
}

void kmem_init(void* space, int block_num)
{
	if (buddy_init(space, block_num) != 0)
	{
		return; // Exception in buddy!
	}
	// Initialize the critical section one time only.
	if (!InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400))
		return;

	cache_of_caches = (kmem_cache_t*)buddy_allocate_by_blocks(1);

	__initialize_cache_of_caches();


	size_N_array = (size_N*)(cache_of_caches + 1);
	for (int i = 0; i < 13; i++)
	{
		size_N_array[i].size = 1 << (i + 5);
		size_N_array[i].size_N_cache = NULL;
	}

	__make_cache_of_size_N();

	//print_buddy_memory(0);
	//__print_cache_info(cache_of_caches);
}

int __check_power_of_two(unsigned number)
{
	return (number != 0) && ((number & (number - 1)) == 0);
}

void* kmalloc(size_t size)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);
	if (size < MIN_SMALL_MEMORY_BUFFER || size > MAX_SMALL_MEMORY_BUFFER)
	{
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return NULL; // Exception!
	}

	int pow_of_2, value, entry;

	__power_of_two_calculation(size, &pow_of_2, &value);

	if (__check_power_of_two(size) == 0)
	{
		pow_of_2 = pow_of_2 ++;
	}

	entry = pow_of_2 - 5;

	void* address = __allocate_one_slot_from_cache(size_N_array[entry].size_N_cache);

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

	return address;
}

int __find_and_delete_from_slab(kmem_cache_t* cache_of_object, const void* object_pointer)
{
	// partially full slabs
	slab* current_slab = cache_of_object->partially_full_slabs;

	while (current_slab != NULL)
	{
		for (int i = 0; i < cache_of_object->number_of_objects_per_slab; i++)
		{
			//printf("%p \t", (void*)((unsigned)current_slab->slots + i * cache_of_object->size_of_object));
			if ((void*)((unsigned)current_slab->slots + i * cache_of_object->size_of_object) == object_pointer)
			{
				current_slab->free_slots_flag[i] = 1;
				current_slab->number_of_free_slots++;
				if (current_slab->number_of_free_slots == cache_of_object->number_of_objects_per_slab)
				{
					__transfer_from_source_to_destination_slab(cache_of_object, current_slab, PARTIALLY, EMPTY);
				}
				return 0;
			}
		}
		current_slab = current_slab->next_slab;
	}

	// full slabs
	current_slab = cache_of_object->full_slabs;
	while (current_slab != NULL)
	{
		for (int i = 0; i < cache_of_object->number_of_objects_per_slab; i++)
		{
			//printf("%p \t", (void*)((unsigned)current_slab->slots + i * cache_of_object->size_of_object));
			if ((void*)((unsigned)current_slab->slots + i * cache_of_object->size_of_object) == object_pointer)
			{
				current_slab->free_slots_flag[i] = 1;
				current_slab->number_of_free_slots++;
				if (current_slab->number_of_free_slots == 1)
				{
					// it must be

					if (current_slab->number_of_free_slots == cache_of_object->number_of_objects_per_slab)
					{
						__transfer_from_source_to_destination_slab(cache_of_object, current_slab, FULL, EMPTY);
					}
					else
					{
						__transfer_from_source_to_destination_slab(cache_of_object, current_slab, FULL, PARTIALLY);
					}
				}
				else
				{
					// Invalid situation
					return -1;
				}
				return 0;
			}
		}
		current_slab = current_slab->next_slab;
	}
	// Invalid situation
	return -1;
}

void __return_empty_slab_to_buddy(kmem_cache_t* cache)
{
	if (cache->empty_slabs != NULL)
	{
		slab* tmp_next = cache->empty_slabs->next_slab;
	
		buddy_return(cache->empty_slabs, cache->size_of_one_slab);

		cache->empty_slabs = tmp_next;
	}
}

void kfree(const void* object_pointer)
{

	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	slab* block_of_object = (slab*)((unsigned)object_pointer & (~(BLOCK_SIZE - 1)));

	kmem_cache_t* cache_of_object = block_of_object->my_cache;

	__find_and_delete_from_slab(cache_of_object, object_pointer);

	__return_empty_slab_to_buddy(cache_of_object);

	// __print_cache_info(cache_of_object);	

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
}

kmem_cache_t* __make_cache_of_object(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*))
{
	kmem_cache_t* new_cache;

	if (cache_of_caches->partially_full_slabs == NULL)
	{
		if (cache_of_caches->empty_slabs == NULL)
		{
			__add_empty_slab(cache_of_caches);
		}
		__transfer_from_source_to_destination_slab(cache_of_caches, cache_of_caches->empty_slabs, EMPTY, PARTIALLY);
	}

	int index_to_put_slot = __find_free_slot_index(cache_of_caches->partially_full_slabs);

	new_cache = ((kmem_cache_t*)cache_of_caches->partially_full_slabs->slots + index_to_put_slot);

	cache_of_caches->partially_full_slabs->free_slots_flag[index_to_put_slot] = 0;

	cache_of_caches->partially_full_slabs->number_of_free_slots--;

	if (cache_of_caches->partially_full_slabs->number_of_free_slots == 0)
	{
		__transfer_from_source_to_destination_slab(cache_of_caches, cache_of_caches->partially_full_slabs, PARTIALLY, FULL);
	}

	strcpy_s(new_cache->object_type_name, 20, name);

	new_cache->ctor = ctor;
	new_cache->dtor = dtor;

	new_cache->full_slabs = NULL;
	new_cache->empty_slabs = NULL;
	new_cache->partially_full_slabs = NULL;

	new_cache->next_cache = NULL;
	new_cache->previous_cache = NULL;
	// Add caches to the list of caches
	if (cache_of_caches->next_cache == NULL)
	{
		cache_of_caches->next_cache = new_cache;
	}
	else
	{
		new_cache->next_cache = cache_of_caches->next_cache;
		cache_of_caches->next_cache->previous_cache = new_cache;
		cache_of_caches->next_cache = new_cache;
	}

	new_cache->size_of_object = size;

	__estimate_slab_size(new_cache->size_of_object, &new_cache->size_of_one_slab, &new_cache->waste_in_bytes, &new_cache->number_of_objects_per_slab);

	new_cache->number_of_alignments_per_cache = new_cache->waste_in_bytes / CACHE_L1_LINE_SIZE + 1;
	new_cache->alignment_for_next_slab = 0;

	new_cache->has_grown = 0;
	new_cache->error_code = 0;

	return new_cache;

}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*))
{

	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	if (name == NULL || strcmp(name, "") == 0 || size <= 0 || (ctor == NULL && dtor != NULL))
	{		
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return NULL;
	}

	//__print_cache_info(cache_of_caches);

	kmem_cache_t* new_cache = __make_cache_of_object(name, size, ctor, dtor);

	//__print_cache_info(cache_of_caches);

	//__print_cache_info(new_cache);

	//print_buddy_memory(1);

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

	return new_cache;
}

void kmem_cache_destroy(kmem_cache_t* cachep)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	if (cachep->partially_full_slabs != NULL || cachep->full_slabs != NULL)
	{
		cachep->error_code = 2; // Unable to destroy cache

		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return;
	}

	//__print_cache_info(cache_of_caches);
	__find_and_delete_from_slab(cache_of_caches, cachep);
	__return_empty_slab_to_buddy(cache_of_caches);
	
	if (cachep->previous_cache == NULL)
	{
		//First
		cache_of_caches->next_cache = cachep->next_cache;
		cache_of_caches->next_cache->previous_cache = NULL;
	}
	else if(cachep->next_cache == NULL)
	{
		cachep->previous_cache->next_cache = NULL;
	}
	else
	{
		cachep->previous_cache->next_cache = cachep->next_cache;
		cachep->next_cache->previous_cache = cachep->previous_cache;
	}

	// __print_cache_info(cache_of_caches);
	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
}

int __number_of_slabs_into_cache(kmem_cache_t* cache)
{
	int number = 0;
	slab* tmp = cache->full_slabs;
	while (tmp != NULL)
	{
		number++;
		tmp = tmp->next_slab;
	}
	tmp = cache->partially_full_slabs;
	while (tmp != NULL)
	{
		number++;
		tmp = tmp->next_slab;
	}
	tmp = cache->empty_slabs;
	while (tmp != NULL)
	{
		number++;
		tmp = tmp->next_slab;
	}
	return number;
}

int __number_of_free_slots_into_cache(kmem_cache_t* cache)
{
	int number = 0;
	slab* tmp = cache->full_slabs;
	
	while (tmp != NULL)
	{
		number += tmp->number_of_free_slots;
		tmp = tmp->next_slab;
	}
	tmp = cache->partially_full_slabs;
	while (tmp != NULL)
	{
		number += tmp->number_of_free_slots;
		tmp = tmp->next_slab;
	}
	tmp = cache->empty_slabs;
	while (tmp != NULL)
	{
		number += tmp->number_of_free_slots;
		tmp = tmp->next_slab;
	}
	return number;
}

void* kmem_cache_alloc(kmem_cache_t* cachep)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	// printf("Allocate ona object into slab %s: \n", cachep->object_type_name);

	int number_of_slabs_before = __number_of_slabs_into_cache(cachep);

	void* address = __allocate_one_slot_from_cache(cachep);
	
	int number_of_slabs_after = __number_of_slabs_into_cache(cachep);

	if (number_of_slabs_after > number_of_slabs_before)
	{
		cachep->has_grown = 1;
	}
	// __print_cache_info(cachep);

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

	return address;

}

void kmem_cache_free(kmem_cache_t* cachep, void* objp)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	__find_and_delete_from_slab(cachep, objp);

	/*
	// Shrink thread will do this:
	__return_empty_slab_to_buddy(cachep);
	*/

	if (cachep->dtor)
	{
		(cachep->dtor)(objp);
	}
	if (cachep->ctor)
	{
		// Remain in initially state
		(cachep->ctor)(objp);
	}

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

}


void __print_cache_info(kmem_cache_t* cache_to_be_printed)
{

	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);


	int number_of_slabs = __number_of_slabs_into_cache(cache_to_be_printed);
	int number_of_free_slots = __number_of_free_slots_into_cache(cache_to_be_printed);

	printf("\nCache name: %s (%p)\n", cache_to_be_printed->object_type_name, cache_to_be_printed);
	printf("Object size [in bytes]: %d\n", cache_to_be_printed->size_of_object);
	//printf("Slab size [in blocks]: %d\n", cache_to_be_printed->size_of_one_slab);
	printf("Cache size [in blocks]: %d\n", cache_to_be_printed->size_of_one_slab * __number_of_slabs_into_cache(cache_to_be_printed));
	printf("Number of slabs: %d\n", __number_of_slabs_into_cache(cache_to_be_printed));	
	printf("Objects per slab: %d\n", cache_to_be_printed->number_of_objects_per_slab);



	if (number_of_slabs * cache_to_be_printed->number_of_objects_per_slab != 0)
	{

		float ratio = 100.0 * (1.0 * number_of_slabs * cache_to_be_printed->number_of_objects_per_slab - number_of_free_slots) / (1.0 * number_of_slabs * cache_to_be_printed->number_of_objects_per_slab);

		printf("Usage: %.2f %%\n", ratio);
	}
	else
	{
		printf("0 slabs exists (free, partialy full or full)\n");
	}
	
	printf("\nStarting address of slabs:\n");
	//printf("Waste of bits per every slab: %d (/%d = %d different alignments)\n", cache_to_be_printed->waste_in_bytes, CACHE_L1_LINE_SIZE, cache_to_be_printed->number_of_alignments_per_cache);

	slab* tmp;
	printf("Full slabs:\n");
	tmp = cache_to_be_printed->full_slabs;
	if (tmp != NULL) printf("*%d free* ", tmp->number_of_free_slots);
	printf("(%p): ", tmp);
	while(tmp != NULL)
	{
		printf("%p -> ", tmp->slots);
		tmp = tmp->next_slab;
	}
	printf("%p\n", tmp);

	printf("Partially full slabs:\n");
	tmp = cache_to_be_printed->partially_full_slabs;

	if (tmp != NULL) printf("*%d free* ", tmp->number_of_free_slots);
	printf("(%p): ", tmp);
	while (tmp != NULL)
	{
		printf("%p -> ", tmp->slots);
		tmp = tmp->next_slab;
	}
	printf("%p\n", tmp);

	printf("Empty slabs:\n");
	tmp = cache_to_be_printed->empty_slabs;
	if (tmp != NULL) printf("*%d free* ", tmp->number_of_free_slots);
	printf("(%p): ", tmp);
	while (tmp != NULL)
	{
		printf("%p -> ", tmp->slots);
		tmp = tmp->next_slab;
	}
	printf("%p\n", tmp);

	if (cache_to_be_printed == cache_of_caches)
	{
		kmem_cache_t* tmp = cache_of_caches->next_cache;
		while (tmp != NULL)
		{
			printf("%s\n", tmp->object_type_name);
			tmp = tmp->next_cache;
		}
	}

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

}

void kmem_cache_info(kmem_cache_t* cachep)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	// Print cache info
	printf("\n\nCACHE INFO:\n");

	__print_cache_info(cachep);

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
}

int kmem_cache_error(kmem_cache_t* cachep)
{

	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	if (cachep->error_code == 1)
	{
		printf("Unable to allocate cache!");
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return -1;
	}
	if (cachep->error_code == 2)
	{
		printf("Unable to deallocate cache!");
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return -2;
	}
	printf("No errors!");
	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
	return 0;
}

int kmem_cache_shrink(kmem_cache_t* cachep)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	if (cachep->has_grown == 1)
	{
		cachep->has_grown = 0;
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return 0;
	}
	slab* tmp = cachep->empty_slabs;
	int number_of_free_slabs = 0;
	while (cachep->empty_slabs != NULL)
	{
		__return_empty_slab_to_buddy(cachep);
		number_of_free_slabs++;
	}

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

	return number_of_free_slabs;
}
