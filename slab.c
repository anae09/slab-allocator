#include "slab.h"
#include "buddy.h"
#include "utilities.h"
#include <windows.h>

#define FRAGM_BORDER 512
#define PTR_SIZE 8
#define UINT_SIZE 4
#define FREE_END 4096
#define LARGE_OBJ 4030
#define SLABS_L sizeof(slab) + 4
#define SIZE_N_OFFSET 5

#define CHECK_ALLOC(x) if(!x) \
{ printf("Memory allocation error!"); exit(1);}

#define slabListStart(ss) (unsigned int*)((unsigned long)ss + sizeof(slab))
#define cacheListStart(start_addr) (unsigned int*)((unsigned long)start_addr + sizeof(cacheBlock))

CRITICAL_SECTION CriticalSection;

typedef struct cache_size_s {
    size_t cs_size;
    kmem_cache_t* cs_cachep;
} cache_size_t;

cache_size_t cache_sizes[13];

typedef struct Slab {
    struct Slab* next;
    void* firstObj;
    unsigned long colouroff;
    unsigned numAllocated;
    unsigned int free; // INDEX OF HEAD OF THE FREE LIST
} slab;

void print_slab_info(slab* s) {
    printf("--- Slab %p info ---\n", s);
    printf("First object address: %p\n", s->firstObj);
    printf("Number of allocated objects: %d\n", s->numAllocated);
    printf("Free slot head: %d\n", s->free);
    printf("Next slab: %p\n", s->next);
    printf("------------------------\n");
}

struct kmem_cache_s {
    char name[20];
    size_t object_size;
    slab* empty;
    slab* full;
    slab* partial;
    void (*constructor)(void *);
    void (*destructor)(void *);
    struct kmem_cache_s* next;
    unsigned slab_offset;
    unsigned wastage;
    unsigned slab_size; 
    unsigned slab_num;
    unsigned object_num;
    int error;
    char flag;
};


typedef struct cache_block {
    kmem_cache_t* firstCache;
    struct cache_block* next;
    unsigned int free;
    unsigned int inuse;
} cacheBlock;

typedef struct slab_allocator {
    cacheBlock* firstCacheBlock;
    kmem_cache_t* off_slab_cache;
    int cache_block_num;
} slabAllocator;

slabAllocator s;

void print_cb_info() { // for testing purposes
    cacheBlock* cb = s.firstCacheBlock;
    printf("--- Cache block info ----\n");
    while (cb) {
        printf("First cache address: %p\n", cb->firstCache);
        printf("In use: %d\n", cb->inuse);
        printf("Free slot head: %d\n", cb->free);
        printf("Next cache address: %p\n", (void*)((unsigned long)cb->firstCache + cb->free * sizeof(kmem_cache_t)));
        cb = cb->next;
    }
    printf("---------------------\n");
    
}


double calcUsage(kmem_cache_t* cachep) {
    int totalNumObject = cachep->slab_num*cachep->object_num;
    int totalAllocatedObjects = 0;
    slab* currSlab = cachep->full;
    while(currSlab) {
        totalAllocatedObjects += cachep->object_num;
        currSlab = currSlab->next;
    }
    currSlab = cachep->partial;
    while(currSlab) {
        totalAllocatedObjects += currSlab->numAllocated;
        currSlab = currSlab->next;
    }
    return (totalAllocatedObjects*1.0/totalNumObject)*100;
}

void printList(unsigned* lst) {
    while (*lst != FREE_END) {
        printf("%d ", *lst);
        lst++;
    } 
    printf("%d ", *lst);
    printf("\n");
}

kmem_cache_t* findCache(char* name) {
    for(cacheBlock* cb = s.firstCacheBlock; cb; cb = cb->next) {
        for(kmem_cache_t* currCache = cb->firstCache; currCache; currCache++) {
            if (strcmp(name, currCache->name) == 0) return currCache;
        }
    }
    printf("Cache %s not found\n", name);
    return 0;
}

int calcNumPages(size_t size) {
    size_t page = BLOCK_SIZE;
    size_t fragm = page % size;
    while (fragm > FRAGM_BORDER) {
        page <<= 1;
        fragm = page % size;
    }
    return page / BLOCK_SIZE;
}

int calcNumObject(size_t size, size_t slab_size) {
    size_t total = slab_size*BLOCK_SIZE - sizeof(slab);
    int numObject = 0;
    while (total >= UINT_SIZE + size) {
        total -= (UINT_SIZE + size);
        numObject++;
    }
    return numObject; 
}

int calcNumCaches() {
    size_t cache_size = sizeof(kmem_cache_t);
    size_t total = BLOCK_SIZE - sizeof(cacheBlock); // minus pointer to next block of caches
    int numCaches = 0;
    while (total >= UINT_SIZE + cache_size) {
        total -= (UINT_SIZE + cache_size);
        numCaches++;
    }
    return numCaches;
}

void init_cache_block(cacheBlock* cb) {
    cb->next = 0;
    int cache_num = calcNumCaches();
    cb->firstCache = (kmem_cache_t*)((unsigned long)cb + cache_num*UINT_SIZE + sizeof(cacheBlock));
    cb->free = 0;
    cb->inuse = 0;
    unsigned int* lst = cacheListStart(cb);
    for (int i = 0; i < cache_num - 1; i++) {
        lst[i] = i + 1;
    }
    lst[cache_num - 1] = FREE_END;
}

void init_cache_sizes() {
    size_t size = 32;
    for (int i = 0; i < 13; i++) {
        cache_sizes[i].cs_size = size;
        cache_sizes[i].cs_cachep = 0;
        size <<= 1;
    }
}

void kmem_init(void* space, int block_num) {
    init_bud(space, block_num);
    s.firstCacheBlock = (cacheBlock*)alloc(1);
    s.cache_block_num = 1;
    s.off_slab_cache = 0;
    init_cache_block(s.firstCacheBlock);
    init_cache_sizes();
    if (!InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400)) {
        printf("Error: initializing critical section.\n"); exit(-1);
    }
        
}


void cache_init(kmem_cache_t* cache, const char* name, size_t size, void (*ctor)(void *), void (*dtor)(void *)) {
    if (snprintf(cache->name, 20, "%s", name) < 0) cache->error = 1;
    else cache->error = 0;
    cache->object_size = size;
    cache->slab_size = calcNumPages(size);
    cache->slab_num = 1;
    cache->partial = 0; cache->full = 0;
    if (size <= LARGE_OBJ) {
        cache->flag = 0;
        cache->empty = alloc(1);
        CHECK_ALLOC(cache->empty);
        cache->object_num = calcNumObject(size, cache->slab_size); // per slab
        cache->wastage = cache->slab_size * BLOCK_SIZE - sizeof(slab) - cache->object_num * (4 + size);
        cache->slab_offset = 0;
    }
    else {
        if (!s.off_slab_cache) s.off_slab_cache = kmem_cache_create("off-slabs", SLABS_L, 0, 0);
        cache->empty = kmem_cache_alloc(s.off_slab_cache);
        cache->flag = 1;
        cache->object_num = cache->slab_size*BLOCK_SIZE/size;
        cache->wastage = 0;
        cache->slab_offset = 0;
    }
    cache->constructor = ctor;
    cache->destructor = dtor;
    
}

void slab_init(kmem_cache_t * cachep, slab* ss) {
    ss->free = 0;
    ss->colouroff = cachep->slab_offset;
    // update offset for colouroff
    if (cachep->wastage > CACHE_L1_LINE_SIZE) {
        if (cachep->slab_offset + CACHE_L1_LINE_SIZE > cachep->wastage) cachep->slab_offset = 0;
        else cachep->slab_offset += CACHE_L1_LINE_SIZE;
    }
    // set parameters
    if(!(cachep->flag & 1)) ss->firstObj = (void*)((unsigned long)ss + sizeof(slab) + cachep->object_num*UINT_SIZE + ss->colouroff);
    else { ss->firstObj = alloc(cachep->slab_size); CHECK_ALLOC(ss->firstObj); }
    ss->numAllocated = 0;
    ss->next = 0;
    // initialize free list
    unsigned int* lst = (unsigned int*)((unsigned long int)ss + sizeof(slab));
    for (unsigned i = 0; i < cachep->object_num - 1; i++) {
        lst[i] = i + 1;
    }
    lst[cachep->object_num - 1] = FREE_END;
    // initialize objects 
    if (cachep->constructor) {
        void* currSlot = ss->firstObj;
        for (unsigned i = 0; i < cachep->object_num; i++) {
            (*cachep->constructor)(currSlot);
            currSlot = (void*)((unsigned long)currSlot + cachep->object_size);
        }
    }
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void (*ctor)(void *), void (*dtor)(void *)) {
    EnterCriticalSection(&CriticalSection);
    // allocate new cache
    cacheBlock* cb = s.firstCacheBlock;
    while (cb->free == FREE_END) cb = cb->next; // find cache block with empty slots
    // 
    if (cb == 0) { // no cache block with empty caches
        cb = (cacheBlock*)alloc(1); // print_arr();
        CHECK_ALLOC(cb);
        cb->next = s.firstCacheBlock;
        s.firstCacheBlock = cb;
        s.cache_block_num++;
        init_cache_block(cb);
    }
    //
    kmem_cache_t* new_cache = (kmem_cache_t*)((unsigned long)cb->firstCache + cb->free * sizeof(kmem_cache_t));
    // update cache block
    unsigned int* lst = cacheListStart(cb);
    cb->free = lst[cb->free];
    cb->inuse++;
    // initialize cache
    cache_init(new_cache, name, size, ctor, dtor);
    // initialize slab
    slab_init(new_cache, (slab*)new_cache->empty);
    
    /* check if cache block full*/
    LeaveCriticalSection(&CriticalSection);
    return new_cache;
} // Allocate cache

int kmem_cache_shrink(kmem_cache_t* cachep) {
    if (cachep == 0) return -1;
    EnterCriticalSection(&CriticalSection);
    int numBlocks = 0;
    slab* curr = cachep->empty;
    while (curr) {
        slab* next = curr->next;
        if (cachep->flag & 1) {
            dealloc(curr->firstObj, cachep->slab_size); 
            kmem_cache_free(s.off_slab_cache, curr);
        } else {
            dealloc(curr, cachep->slab_size); 
        }
        numBlocks += cachep->slab_size;
        cachep->slab_num--;
        curr = next;
    }
    cachep->empty = 0;
    LeaveCriticalSection(&CriticalSection);
    return numBlocks;
} // Shrink cache

void* kmem_cache_alloc(kmem_cache_t* cachep) {
    if (cachep == 0) return 0;
    EnterCriticalSection(&CriticalSection);
    slab* ss = 0;
    if (cachep->partial) ss = cachep->partial; 
    else if (cachep->empty) { // use empty slab and link it to partial slabs list
        ss = cachep->empty;
        ss->next = cachep->partial;
        cachep->partial = ss;
        cachep->empty = cachep->empty->next;
        
    } else { // no partial nor empty slab --> allocate new partial slab
        if (cachep->flag & 1) {
            cachep->partial = kmem_cache_alloc(s.off_slab_cache);
        } else {
            cachep->partial = alloc(1);
            CHECK_ALLOC(cachep->partial);
        }
        cachep->slab_num += cachep->slab_size;
        slab_init(cachep, (slab*)cachep->partial);
        if ((cachep->flag >> 1) & 2) {
            printf("%s called alloc after shrink\n", cachep->name);
            cachep->flag |= 4;
        }
        ss = cachep->partial;
    }

    void * obj = (void*)((unsigned long)ss->firstObj + ss->free*cachep->object_size);
    unsigned int* lst = slabListStart(ss);
    ss->free = lst[ss->free];
    ss->numAllocated++;
    if (ss->free == FREE_END) { // reallocate slab to full list
        cachep->partial = cachep->partial->next;
        ss->next = cachep->full;
        cachep->full = ss;
    }

    LeaveCriticalSection(&CriticalSection);
    return obj;
} // Allocate one object from cache

void free_object(int index, slab* currSlab) {
    currSlab->numAllocated--;
    unsigned int* lst = slabListStart(currSlab);
    lst[index] = currSlab->free;
    currSlab->free = index;
}

void* find_obj(kmem_cache_t* cachep, slab* currSlab, const void* objp) { // search slabs for obj
    while (currSlab) { 
        if (objp >= currSlab->firstObj && objp < (void*)((unsigned long)currSlab + cachep->slab_size*BLOCK_SIZE)) {
            return currSlab;
        }
        currSlab = currSlab->next;
    }
    return 0;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
    if (cachep == 0 || objp == 0) return;
    EnterCriticalSection(&CriticalSection);
    /* find slab where objp is */
    slab* currSlab = 0;
    char listFlag = 0;
    if (cachep->full) {
        currSlab = find_obj(cachep, cachep->full, objp);
        listFlag = 'f';
    }
    if (currSlab == 0 && cachep->partial) {
        currSlab = find_obj(cachep, cachep->partial, objp);
        listFlag = 'p';
    }
    if (!currSlab) { printf("Object not found in cache %s.\n", cachep->name); LeaveCriticalSection(&CriticalSection); return; }
    /* free object */
    int index = ((unsigned long)objp - (unsigned long)currSlab->firstObj)/cachep->object_size;
    free_object(index, currSlab);
    if (cachep->destructor) (*(cachep->destructor))(objp); /* pozvati destruktor*/
    if (listFlag == 'f') { /* full slab -> partial slab*/
        if (cachep->full == currSlab) cachep->full = cachep->full->next;
        else {
            slab* prevSlab = cachep->full;
            for (; prevSlab->next != currSlab; prevSlab = prevSlab->next);
            prevSlab->next = currSlab->next;
        }
        
        currSlab->next = cachep->partial;
        cachep->partial = currSlab;
    }/* partial slab -> empty slab*/
    else if (listFlag == 'p' && currSlab->numAllocated == 0) { // reallocate to empty list
        if (cachep->partial == currSlab) cachep->partial = cachep->partial->next;
        else {
            slab* prevSlab = cachep->partial;
            for (; prevSlab->next != currSlab; prevSlab = prevSlab->next);
            prevSlab->next = currSlab->next;
        }
        currSlab->next = cachep->empty;
        cachep->empty = currSlab;
        if (!((cachep->flag >> 1) & 1)) cachep->flag |= 2;
        if ((cachep->flag >> 1) != 3) {
            kmem_cache_shrink(cachep);
        }
    }
    LeaveCriticalSection(&CriticalSection);
} // Deallocate one object from cache


void* kmalloc(size_t size) {
    if (size == 0) return 0;
    size = nearestPowerOfTwo(size);
    int index = power_of_two(size); 
    EnterCriticalSection(&CriticalSection);
    if (!cache_sizes[index - SIZE_N_OFFSET].cs_cachep) {
        char name[20];
        sprintf_s(name, 20, "%lu", size);
        cache_sizes[index - SIZE_N_OFFSET].cs_cachep = kmem_cache_create(name, size, 0, 0);
    }
    LeaveCriticalSection(&CriticalSection);
    return kmem_cache_alloc(cache_sizes[index - SIZE_N_OFFSET].cs_cachep);
} // Allocate one small memmory buffer 

void* find_buffer(kmem_cache_t* cachep, slab* currSlab, const void* objp) { // search slabs for obj
    while (currSlab) {
        if (objp >= currSlab->firstObj && objp < (void*)((unsigned long)currSlab->firstObj + cachep->slab_size * BLOCK_SIZE)) {
            return currSlab;
        }
        currSlab = currSlab->next;
    }
    return 0;
}

void kfree(const void* objp) {
    if (objp == 0) return;
    EnterCriticalSection(&CriticalSection);
    int i = 0;
    slab* currSlab = 0;
    char listFlag = 0;
    for (; i < 13; i++) { // search slabs for every sizeN_cache
        if (cache_sizes[i].cs_cachep){
            if (cache_sizes[i].cs_cachep->full) {
                currSlab = find_buffer(cache_sizes[i].cs_cachep, cache_sizes[i].cs_cachep->full, objp);
                if (currSlab) { listFlag = 'f'; break; }
            }
            if (cache_sizes[i].cs_cachep->partial) {
                currSlab = find_buffer(cache_sizes[i].cs_cachep, cache_sizes[i].cs_cachep->partial, objp);
                if (currSlab) { listFlag = 'p'; break; }
            }
        } 
    }
    if (!currSlab) { printf("Object not found.\n"); LeaveCriticalSection(&CriticalSection); return; }
    int index = ((unsigned long)objp - (unsigned long)currSlab->firstObj)/cache_sizes[i].cs_cachep->object_size;
    free_object(index, currSlab);
    kmem_cache_t* cachep = cache_sizes[i].cs_cachep;
    if (listFlag == 'f' && currSlab->numAllocated > 0) { /* full slab -> partial slab*/
        if (cachep->full == currSlab) cachep->full = cachep->full->next;
        else {
            slab* prevSlab = cachep->full;
            for (; prevSlab->next != currSlab; prevSlab = prevSlab->next);
            prevSlab->next = currSlab->next;
        }
        currSlab->next = cachep->partial;
        cachep->partial = currSlab;
    } else if(listFlag == 'f' && currSlab->numAllocated == 0){ /* full slab -> empty slab*/
        if (cachep->full == currSlab) cachep->full = cachep->full->next;
       else {
            slab* prevSlab = cachep->full;
            for (; prevSlab->next != currSlab; prevSlab = prevSlab->next);
            prevSlab->next = currSlab->next;
        }
        currSlab->next = cachep->empty;
        cachep->empty = currSlab;
        if (!((cachep->flag >> 1) & 1)) cachep->flag |= 2;
        else if ((cachep->flag >> 1) != 3) {
            kmem_cache_shrink(cachep);
        }
    } else if (listFlag == 'p' && currSlab->numAllocated == 0) { /* partial slab -> empty slab -> deallocate*/
        if (cachep->partial == currSlab) cachep->partial = cachep->partial->next;
        else {
            slab* prevSlab = cachep->partial;
            for (; prevSlab->next != currSlab; prevSlab = prevSlab->next);
            prevSlab->next = currSlab->next;
        }
        currSlab->next = cachep->empty;
        cachep->empty = currSlab;
        if (!((cachep->flag >> 1) & 1)) cachep->flag |= 2;
        if ((cachep->flag >> 1) != 3) {
            kmem_cache_shrink(cachep);
        }
    }
    LeaveCriticalSection(&CriticalSection);
} // Deallocate one small memory buffer

void dealloc_slab(kmem_cache_t* cachep, slab* currSlab) {
    while (currSlab) {
        slab* next = currSlab->next;
        if (cachep->flag & 1) {
            dealloc(currSlab->firstObj, cachep->slab_size); 
            kmem_cache_free(s.off_slab_cache, currSlab);
        } else {
            dealloc(currSlab, cachep->slab_size);
        }
        currSlab = next;
    }
}

void kmem_cache_destroy(kmem_cache_t* cachep) {
    if (cachep == 0) return;
    EnterCriticalSection(&CriticalSection);
    cacheBlock* cb = s.firstCacheBlock;
    cacheBlock* prevCb = 0;
    while (cb) {
        if (cachep >= cb->firstCache && cachep < (kmem_cache_t*)((unsigned long)cb + BLOCK_SIZE)) {
            break;
        }
        prevCb = cb;
        cb = cb->next;
    }
    if (cb == 0) { printf("ERROR: Cache not found\n"); LeaveCriticalSection(&CriticalSection); return; }

    // deallocate slabs
    if (cachep->empty) dealloc_slab(cachep, cachep->empty); 
    if (cachep->partial) dealloc_slab(cachep, cachep->partial);
    if (cachep->full) dealloc_slab(cachep, cachep->full);

    // deallocate cache
    unsigned int* lst = cacheListStart(cb);
    int index = ((unsigned long)cachep - (unsigned long)cb->firstCache)/sizeof(kmem_cache_t);
    lst[index] = cb->free;
    cb->free = index;
    cb->inuse--;
    if (cb->inuse == 0 && s.cache_block_num > 1) { // dealloc cache block if empty and there are others
        if (!prevCb) {
            s.firstCacheBlock = cb->next;
        } else {
            prevCb->next = cb->next;
        }
        s.cache_block_num--;
        dealloc(cb, 1); 
    }
    LeaveCriticalSection(&CriticalSection);
} 



void kmem_cache_info(kmem_cache_t* cachep) {
    EnterCriticalSection(&CriticalSection);
    printf("--- cache info ---\n");
    printf("name: %s\n", cachep->name);
    //
    printf("cache address: %p\n", cachep);
    //
    printf("object size: %luB\n", cachep->object_size);
    printf("cache size: %luB\n", cachep->slab_num*cachep->slab_size*BLOCK_SIZE);
    printf("slab num: %d\n", cachep->slab_num);
    printf("num objects/slab: %d\n", cachep->object_num);
    double usage = calcUsage(cachep);
    printf("cache usage: %.3lf%% \n", usage);
    printf("-----------------\n");
    LeaveCriticalSection(&CriticalSection);
} // Print cache info

int kmem_cache_error(kmem_cache_t* cachep) {
    // 1 : cache name overflow
    return cachep->error;
} // Print error message

