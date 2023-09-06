#ifndef _MODMULT_H
#define _MODMULT_H

#include "bignbr.h"

extern limb MontgomeryMultN[MAX_LEN];
extern enum eNbrCached MontgomeryMultNCached;
extern enum eNbrCached TestNbrCached;

// NumberLength
extern limb TestNbr[MAX_LEN];

void GetMontgomeryParms(int len);
void GetMontgomeryParmsPowerOf2(int powerOf2);

void modmult(const limb *factor1, const limb *factor2, limb *product);

void modPowBaseInt(int base, const limb *exp, int nbrGroupsExp, limb *power);
void modPow(const limb *base, const limb *exp, int nbrGroupsExp, limb *power);

void BigIntGeneralModularDivision(const BigInteger *Num, const BigInteger *Den,
                                  const BigInteger *mod, BigInteger *quotient);
void BigIntModularDivision(const BigInteger* Num, const BigInteger* Den,
                           const BigInteger* mod, BigInteger* quotient);
void BigIntModularPower(const BigInteger* base, const BigInteger* exponent,
                        BigInteger* power);

void AddBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                   int number_length);
void SubtBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                    int number_length);

#endif
