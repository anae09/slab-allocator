#include "utilities.h"

void binprintf(int v) {
    unsigned int mask = 1U << 31;
    while(mask) {
        printf("%d", (v&mask ? 1 : 0));
        mask >>= 1;
    }
    printf("\n");
}
// calculates position of a highest '1'
unsigned pos(unsigned num) {
    unsigned mask = 1U;
    unsigned pos = 0;
    for (int i = 0; i < 32; i++) {
        if(mask & num) {
            pos = i; 
        } 
        mask <<= 1; 
    } 
    return pos;
}

int power_of_two(unsigned num) {
    unsigned mask = 1U << 31;
    for (int i = 31; i >= 0; i--) {
        if (num & mask) {
            return i;
        }
        mask >>= 1;
    }
    return -1;
}

unsigned nearestPowerOfTwo(unsigned int v) { // return nearest power of two of v
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

