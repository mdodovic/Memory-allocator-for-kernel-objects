#include "buddy.h"
#include "slab.h"
#include <stdio.h>

// declaration of helper functions used by buddy allocation
void __power_of_two_calculation(int n, int* first_less, int* value);
int __align_to_next_block_beginning();
void __add_block_initial(int degree, buddy_free_block* address);
buddy_free_block* __are_buddies(int entry, buddy_free_block* address1, buddy_free_block* address2);
void* __remove_first_from_the_list(int entry);
void __add_to_list_front(int entry, buddy_free_block* address);
void __add_to_list_back(int entry, buddy_free_block* address);
void __divide(int first_entry, int needed_entry);
buddy_free_block* __add_to_list(int entry, buddy_free_block* space_address);


void __power_of_two_calculation(int n, int* first_less, int* value)
{
	*first_less = 0;
	*value = 1;
	while (n >= *value) {
		*value <<= 1;
		(*first_less)++;
	}
	(*first_less)--;
	*value >>= 1;
}

int __align_to_next_block_beginning() 
{
	// Allign to beginning of the block: 

	unsigned int mask = (BLOCK_SIZE - 1);
	unsigned int not_mask = ~mask;

	if (((unsigned)buddy_allocator & mask) != 0) 
	{ 
		buddy_allocator = ((unsigned)buddy_allocator & not_mask) + BLOCK_SIZE;
		return 1;
	}
	return 0;
}

void __add_block_initial(int degree, buddy_free_block* address)
{
	if (buddy_allocator->array_of_free_blocks[degree].first != NULL)
	{
		// Fatal error
		exit(-1);
	}
	buddy_allocator->array_of_free_blocks[degree].first = address;
	buddy_allocator->array_of_free_blocks[degree].last = address;
	buddy_allocator->array_of_free_blocks[degree].number_of_free_blocks = 1;
	buddy_allocator->array_of_free_blocks[degree].first->next = NULL;
}

void print_buddy_memory(int general)
{
	// if general is set - print meta data
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);
	printf("\nBuddy memmory specification!\n");

	if (general == 1) 
	{
		printf("Start address: %p\n", buddy_allocator);
		printf("Buddy allocator: \n");
		printf("(%p) %p\n", &buddy_allocator->array_of_free_blocks, buddy_allocator->array_of_free_blocks);
		printf("(%p) %d\n", &buddy_allocator->buddy_max_degree, buddy_allocator->buddy_max_degree);
		printf("(%p) %d\n", &buddy_allocator->buddy_num_of_blocks, buddy_allocator->buddy_num_of_blocks);
		printf("(%p) %d\n", &buddy_allocator->useful_num_of_blocks, buddy_allocator->useful_num_of_blocks);
		printf("(%p) %p\n", &buddy_allocator->first_useful_block, buddy_allocator->first_useful_block);
	}

	printf("First free block: %p\n", buddy_allocator->first_useful_block);
	printf("Number of useful blocks: %d\n", buddy_allocator->useful_num_of_blocks);

	int s = 0;
	for (unsigned int i = 0; i < buddy_allocator->buddy_max_degree; i++)
		s += buddy_allocator->array_of_free_blocks[i].number_of_free_blocks * (1 << i);

	for (unsigned int i = 0; i < buddy_allocator->buddy_max_degree; i++)
	{
		printf("2^%d (%p) | %d : ", i, buddy_allocator->array_of_free_blocks + i, buddy_allocator->array_of_free_blocks[i].number_of_free_blocks);

		buddy_free_block* tmp = buddy_allocator->array_of_free_blocks[i].first;
		if (tmp == NULL)
			printf("NULL\n");
		while (tmp != NULL) 
		{
			printf("%p -> ", tmp);
			tmp = tmp->next;
			if (tmp == NULL)
				printf("NULL\n");

		}
		
	}

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

}

buddy_free_block* __are_buddies(int entry, buddy_free_block* address1, buddy_free_block* address2)
{
	// check if two blocks of the same size are buddies	
	unsigned mask = 1 << entry;
	unsigned difference = mask;

	unsigned position1 = (unsigned)(address1 - buddy_allocator->first_useful_block);
	unsigned position2 = (unsigned)(address2 - buddy_allocator->first_useful_block);

	unsigned flag1 = position1 & mask;
	unsigned flag2 = position2 & mask;

	
	if (abs(position1 - position2) == difference)
	{
		if (flag1 == 0 && flag2 != 0)
		{
//			printf("Address1 is lower than address2\n");
			if (position2 - position1 == difference)
				return address1;
		}
		else
		{
			if (flag1 != 0 && flag2 == 0)
			{
				//printf("Address1 is lower than address2\n");
				if (position1 - position2 == difference)
					return address2;
			}
			else
			{
				// Exception! 
				// Difference is 1 so they must be either 
				// flag1 = 0 and flag2 = 1 or flag2 = 0 and flag1 = 1
				exit(-1);
			}
		}
	}
	return NULL; // Difference between 2 addresses are not equal 1 (* 2^entry * sizeof(BLOCK_SIZE))
}

void* __remove_first_from_the_list(int entry) 
{
	buddy_free_block* ret = buddy_allocator->array_of_free_blocks[entry].first;
	buddy_allocator->array_of_free_blocks[entry].first = buddy_allocator->array_of_free_blocks[entry].first->next;
	if (buddy_allocator->array_of_free_blocks[entry].first == NULL) 
	{
		buddy_allocator->array_of_free_blocks[entry].last = NULL;
	}
	buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks--;
	buddy_allocator->useful_num_of_blocks -= 1 << entry;
	ret->next = NULL;
	return ret;
}

void __add_to_list_front(int entry, buddy_free_block* address) 
{
	address->next = buddy_allocator->array_of_free_blocks[entry].first;
	buddy_allocator->array_of_free_blocks[entry].first = address;	
	if (address->next == NULL)
	{
		buddy_allocator->array_of_free_blocks[entry].last = address;
	}
	buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks++;
	buddy_allocator->useful_num_of_blocks += 1 << entry;

}

void __add_to_list_back(int entry, buddy_free_block* address)
{
	if (buddy_allocator->array_of_free_blocks[entry].first == NULL)
	{
		buddy_allocator->array_of_free_blocks[entry].first = buddy_allocator->array_of_free_blocks[entry].last = address;
	}
	else
	{
		buddy_allocator->array_of_free_blocks[entry].last = buddy_allocator->array_of_free_blocks[entry].last->next = address;
	}
	address->next = NULL;
	buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks++;
	buddy_allocator->useful_num_of_blocks += 1 << entry;

}

void __divide(int first_entry, int needed_entry) 
{

	buddy_free_block* tmp, *add_lower, *add_higher;
	for (int i = first_entry; i > needed_entry; i--)
	{
		tmp = __remove_first_from_the_list(i);
		add_lower = tmp;
		add_higher = tmp + (1 << (i-1));
		__add_to_list_front(i - 1, add_higher);
		__add_to_list_front(i - 1, add_lower);
	}
}

void* buddy_allocate_by_blocks(int num_of_blocks)
{	
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);
	if (num_of_blocks <= 0) { 
		// Exception!
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return NULL;
	}
	//printf("\nTry to allocate %d blocks\n", num_of_blocks);
	int degree, value;
	__power_of_two_calculation(num_of_blocks, &degree, &value);
	//printf("Allocate by blocks: %d = 2^%d\n", num_of_blocks, degree);
	int entry;
	for (entry = degree; entry < buddy_allocator->buddy_max_degree; entry++) {
		if (buddy_allocator->array_of_free_blocks[entry].first != NULL) {
			// printf("Found free block!\n");
			break;
		}
	}
	if (entry == buddy_allocator->buddy_max_degree)
	{
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return NULL;
	}
	void* start_address = NULL;
	if (entry == degree) {
		// There is free block in exact entrance as num_of_blocks = 2^entrance
		// printf("Block in this very entrance \n");
		start_address = __remove_first_from_the_list(entry);
	}
	else {
		// There is free in larger entrance than num_of_blocks = 2^entrance
		// printf("Free block in larger entracne: %d\n", entry);
		__divide(entry, degree);
		start_address = __remove_first_from_the_list(degree);
	}
	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
	return start_address;
}


buddy_free_block* __add_to_list(int entry, buddy_free_block* space_address)
{
	// Add space to the specific entrance. 
	// If this space has free buddy space. Remove this free block from the list 
	// and return both of them in order to merge and put into greater entry.
	if (buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks == 0)
	{
		// No elements in entry entrance
		__add_to_list_front(entry, space_address);
		return NULL; // Added in list whithout merging
	}
	// at least 1 element in entry!
	buddy_free_block* buddy_address = __are_buddies(entry, space_address, buddy_allocator->array_of_free_blocks[entry].first);
	if (buddy_address != NULL)
	{
		// This is buddy with first in list
		__remove_first_from_the_list(entry);
		return buddy_address;
	}

	// He is not buddy with first in list
	buddy_free_block* prev = NULL, * curr = buddy_allocator->array_of_free_blocks[entry].first;
	while (curr != NULL && curr < space_address)
	{
		buddy_address = __are_buddies(entry, space_address, curr);
		if (buddy_address != NULL)
		{
			prev->next = curr->next;
			curr->next = NULL;
			if (prev->next == NULL)
			{
				buddy_allocator->array_of_free_blocks[entry].last = prev;
			}
			buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks--;
			buddy_allocator->useful_num_of_blocks -= 1 << entry;

			return buddy_address;
			// is buddy stop!
		}
		prev = curr;
		curr = curr->next;
	}

	if (prev == NULL)
	{
		// First in list isn't buddy and it is larger than space_address
		// printf("%d\n", curr == buddy_allocator->array_of_free_blocks[entry].first);
		__add_to_list_front(entry, space_address);
		return NULL;
	}
	//	printf("%p < %p < %p\n", prev, space_address, curr);
	if (curr == NULL)
	{
		// Neither in list is buddy of space_address block, and it is larger than any element
		// printf("%d\n", prev == buddy_allocator->array_of_free_blocks[entry].last);
		__add_to_list_back(entry, space_address);
		return NULL;
	}
	// Final check
	buddy_address = __are_buddies(entry, space_address, curr);
	if (buddy_address != NULL)
	{
		// Special situation. I am buddy for the count and I am greater than prev
		prev->next = curr->next;
		curr->next = NULL;
		if (prev->next == NULL)
		{
			buddy_allocator->array_of_free_blocks[entry].last = NULL;
		}
		buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks--;
		buddy_allocator->useful_num_of_blocks -= 1 << entry;
		return buddy_address;
	}
	else
	{
		prev->next = space_address;
		space_address->next = curr;
		buddy_allocator->array_of_free_blocks[entry].number_of_free_blocks++;
		buddy_allocator->useful_num_of_blocks += 1 << entry;
		return NULL;
	}
}

int buddy_return(void* returned_space_address, int num_of_blocks_returned)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	if (returned_space_address == NULL)
	{
		// Release ownership of the critical section.
		LeaveCriticalSection(&CriticalSection);
		return 0;
	}
	buddy_free_block* current_returning_address = (buddy_free_block*)returned_space_address;
	int degree, value;
	__power_of_two_calculation(num_of_blocks_returned, &degree, &value);

	//printf("Returned %d (=2^%d) blocks start at address: %p\n", num_of_blocks_returned, degree, returned_space_address);

	for (int i = degree; i < buddy_allocator->buddy_max_degree; i++)
	{
		buddy_free_block* binding_address  = __add_to_list(i, current_returning_address);
		if (binding_address == NULL) // This entrance now has 1 (passed)
			break;
		current_returning_address = binding_address;
	}	

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);
	return 0;
}


int buddy_init(void* start_address, int num_of_blocks)
{
	printf("Start of buddy allocation!\n");
	buddy_allocator = (buddy*)start_address;
	// printf("Start address: %p\n", buddy_allocator);

	int aligned = __align_to_next_block_beginning();
	// printf("Alligned Start address: %p\n", buddy_allocator);
	
	buddy_allocator->buddy_num_of_blocks = 1 + (aligned); // blocks needed for buddy
	int other_num_of_blocks = buddy_allocator->useful_num_of_blocks = num_of_blocks - 1 - (aligned); 
	// available blocks for slab allocator: at most cases num_of_blocks - 1 (one block for buddy) - 1 (one block lost because of allignment)

	int power, value;
	__power_of_two_calculation(buddy_allocator->useful_num_of_blocks, &power, &value);
	// power will be maximum 2^power size of free blocks
	// and this is also size of array
	buddy_allocator->buddy_max_degree = power + 1; 

	buddy_allocator->array_of_free_blocks = (buddy_header*)(buddy_allocator + 1);

	for (unsigned int i = 0; i < buddy_allocator->buddy_max_degree; i++)
	{
		buddy_allocator->array_of_free_blocks[i].first = NULL;
		buddy_allocator->array_of_free_blocks[i].last = NULL;
		buddy_allocator->array_of_free_blocks[i].number_of_free_blocks = 0;
	}

	buddy_allocator->first_useful_block = (buddy_free_block*)buddy_allocator + 1;

	buddy_free_block* free = buddy_allocator->first_useful_block;
	while (other_num_of_blocks > 0) 
	{
		__power_of_two_calculation(other_num_of_blocks, &power, &value);
//		printf("%p / %d == %d = 2^%d\n", free, other_num_of_blocks, value, power);
		__add_block_initial(power, free);
		free = free + value;
		other_num_of_blocks -= value;
	}

	printf("End of buddy allocation!\n");

	return 0; // No exceptions!
}


void* buddy_allocate_by_size_bytes(int space_size_bytes)
{
	// Request ownership of the critical section.
	EnterCriticalSection(&CriticalSection);

	int number_of_blocks = space_size_bytes / BLOCK_SIZE + (space_size_bytes % BLOCK_SIZE != 0);

	int power, value;
	__power_of_two_calculation(number_of_blocks, &power, &value);

	void* a = buddy_allocate_by_blocks(value * (1 + (value != number_of_blocks)));

	// Release ownership of the critical section.
	LeaveCriticalSection(&CriticalSection);

	return a;
}


