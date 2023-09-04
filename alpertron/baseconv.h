
#ifndef _BASECONV_H
#define _BASECONV_H

#include "bignbr.h"

void Dec2Bin(const char *decimal, limb *binary, int digits, int *bitGroups);
void Bin2Dec(char **ppDecimal, const limb *binary, int nbrLimbs, int groupLength);
void int2dec(char **pOutput, int nbr);
void BigInteger2Dec(char **ppDecimal, const BigInteger *pBigInt, int groupLength);

#endif
