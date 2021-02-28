# Slab-Allocator

## Description

Kernel memory allocation using slab and buddy allocator.
This project goals are:
 - allocation of small memory buffers to help eliminate internal fragmentation 
 - caching of commonly used objects so that the system does not waste time allocating, initialising and destroying objects
 - better utilisation of hardware cache by aligning objects to the L1 caches

Allocator is given continuous memory space on the startup and is using buddy allocator for management of free blocks.



