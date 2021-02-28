#include "buddy.h"
#include "utilities.h"
#include <stdlib.h>

#define BLOCK_SIZE 4096
#define BLOCK_NUM 256
#define SIZE 12

typedef struct buddy_elem {
    struct buddy_elem*  next;
} buddyElem;

typedef struct buddy_allocator {
    struct buddy_elem* buddy_array[SIZE];
    unsigned size;
    unsigned block_num;
    unsigned available_blocks;
    void* start_addr;
} buddyAllocator;

buddyAllocator b;

// initializes array of pointers to available blocks and other elements of a buddyAllocator structure
void init_bud(void* space, unsigned block_num) {
    b.start_addr = space;
    b.block_num = block_num;
    b.size = pos(block_num) + 1;
    b.available_blocks = block_num;
    // initialize buddy_array
    unsigned mask = 1U;
    void* next_block_addr = space;
    for (unsigned i = 0; i < b.size; i++) {
        if(mask & block_num) {
            b.buddy_array[i] = next_block_addr;
            *(buddyElem**)(next_block_addr) = 0;
            next_block_addr =(void*)((unsigned long)next_block_addr + (1 << (i + 12)));
        } else {
            b.buddy_array[i] = 0;
        } 
        mask <<= 1; 
    } 
    // printf("Buddy System successfully allocated.\n");
}

void print_arr() {
    for (unsigned i = 0; i < b.size; i++) {
        if (!b.buddy_array[i]) printf("%d. 0\n", i);
        else {
            for (buddyElem* curr = b.buddy_array[i]; curr; curr = curr->next){
                printf("%d. %p ", i, curr);
            }
            printf("\n");
        }
    }
}

// first available chunk that we find is realocated to arr[i-1] with shift of block_num
void* split(unsigned start_index, unsigned curr_index) {
    buddyElem* allocated = 0;
    if (start_index == b.size || curr_index == b.size) return 0;
    else if (b.buddy_array[curr_index] == 0) {
        allocated = split(start_index, curr_index + 1);
    } else {
        allocated = b.buddy_array[curr_index];
    }
    if (allocated == 0) return 0;
    // split into halves and return lower half
    unsigned lvl_block_size = 1 << (curr_index - 1);
    // allocate new half
    buddyElem* newElem = (buddyElem*)((unsigned long)allocated + (lvl_block_size*BLOCK_SIZE));
    if (b.buddy_array[curr_index - 1]) *(buddyElem**)(newElem) = b.buddy_array[curr_index - 1]->next;
    else *(buddyElem**)(newElem) = 0;
    // link
    b.buddy_array[curr_index - 1] = newElem;
    if (b.buddy_array[curr_index] == allocated) b.buddy_array[curr_index] = allocated->next;
    return allocated;
}

void* alloc(unsigned block_num) {
    if (b.available_blocks < block_num) return 0;
    int index = power_of_two(block_num);
    if (index == -1 || b.available_blocks < block_num) return 0;
    void* addr = 0;
    if (b.buddy_array[index]) {
        addr = b.buddy_array[index];
        b.buddy_array[index] = *(buddyElem**)(b.buddy_array[index]);
    } else {
        addr = split(index, index + 1);
    }
    b.available_blocks -= block_num;
    return addr;
}

// returns number of a pair for a given block_size
unsigned long get_pair(unsigned long addr, unsigned block_size) {
    unsigned long startAddr = (unsigned long)b.start_addr;
    unsigned long currAddr = startAddr;
    unsigned capacity =  1 << (b.size - 1);
    unsigned offset = 2*block_size;
    while (currAddr < startAddr + capacity*BLOCK_SIZE){
        if ((unsigned long)addr == currAddr) return currAddr + block_size*BLOCK_SIZE;
        else if ((unsigned long)addr == currAddr + block_size*BLOCK_SIZE) return currAddr;
        currAddr += offset*BLOCK_SIZE;
    }
    return 0;
}

// checks if given address is in the list of b.buddy_array[index]; return pointer to prev element
// return -1 if there is no
int long pairInList(unsigned long pair, int index) {
    buddyElem* prev = 0;
    buddyElem* curr = b.buddy_array[index];
    while (curr) {
        if (curr == (buddyElem*)pair) return (int long)prev;
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

void merge(void* addr, int index) {
    unsigned long pair = get_pair((unsigned long)addr, 1 << index);
    if (pair == 0) { printf("Error: no pair.\n"); exit(-1); }
    long int ret = pairInList(pair, index);
    if (ret == -1 || index == b.size - 1) { // no pair in list or last level
        *(buddyElem**)addr = b.buddy_array[index];
        b.buddy_array[index] = (buddyElem*)addr;
    } else { // has pair in list
        buddyElem* prev = (buddyElem*)ret; // points to previos element to pair
        if (!prev) {
            b.buddy_array[index] = ((buddyElem*)pair)->next;
        } else {
            prev->next = ((buddyElem*)pair)->next;
        }
        buddyElem* node = ((unsigned long)addr < (unsigned long)pair) ? (buddyElem*)addr : (buddyElem*)pair;
        merge(node, index + 1);
    }
}

// deallocate and merge if there is a pair 
void dealloc(void* addr, unsigned block_size) {
    if (addr == 0 || addr < b.start_addr || addr > (void*)((unsigned long)b.start_addr + (b.block_num)*BLOCK_SIZE)) return;
    int index = power_of_two(block_size);
    if (b.buddy_array[index] == 0) { // link
        *((buddyElem**)addr) = b.buddy_array[index];
        b.buddy_array[index] = (buddyElem*)addr;
    } else { // merge
        merge(addr, index);
    }
    b.available_blocks += block_size;
}


