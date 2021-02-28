#ifndef _BUDDY_H_
#define _BUDDY_H_

// initializes array of pointers to available blocks and other elements of a buddyAllocator structure
void print_arr();

void init_bud(void* space, unsigned block_num);

void* alloc(unsigned block_num);

// deallocate and merge if there is a pair 
void dealloc(void* addr, unsigned block_size);

#endif