#ifndef _UTILITIES_H
#define _UTILITIES_H
#include <stdio.h>

void binprintf(int v);

// calculates position of a highest '1'
unsigned pos(unsigned num);

int power_of_two(unsigned num);

unsigned nearestPowerOfTwo(unsigned int v); // return nearest power of two of v


#endif