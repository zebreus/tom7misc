#include "globals.h"

#include <cstdlib>

#include "bignbr.h"

// char output[3000000];
char *output = new char[2'000'000'000];
bool lang = false;

limb *Mult1 = (limb*)malloc(sizeof (limb) * MAX_LEN);
limb *Mult2 = (limb*)malloc(sizeof (limb) * MAX_LEN);
limb *Mult3 = (limb*)malloc(sizeof (limb) * MAX_LEN);
limb *Mult4 = (limb*)malloc(sizeof (limb) * MAX_LEN);
int *valueQ = (int*)malloc(sizeof (int) * MAX_LEN);

bool hexadecimal;
