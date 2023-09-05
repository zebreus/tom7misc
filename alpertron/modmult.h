#ifndef _MODMULT_H
#define _MODMULT_H

#include "bignbr.h"

void modmult(const limb *factor1, const limb *factor2, limb *product);
void modmultInt(limb *factorBig, int factorInt, limb *result);
void modmultIntExtended(limb* factorBig, int factorInt, limb* result,
                        const limb* pTestNbr, int nbrLen);

void modPowBaseInt(int base, const limb *exp, int nbrGroupsExp, limb *power);
void modPow(const limb *base, const limb *exp, int nbrGroupsExp, limb *power);

void BigIntGeneralModularDivision(const BigInteger *Num, const BigInteger *Den,
  const BigInteger *mod, BigInteger *quotient);
void BigIntModularDivision(const BigInteger* Num, const BigInteger* Den,
  const BigInteger* mod, BigInteger* quotient);
void BigIntModularDivisionPower2(const BigInteger* Num, const BigInteger* Den,
  const BigInteger* mod, BigInteger* quotient);
void BigIntModularDivisionSaveTestNbr(const BigInteger* Num, const BigInteger* Den,
  const BigInteger* mod, BigInteger* quotient);

enum eExprErr BigIntGeneralModularPower(const BigInteger* base, const BigInteger* exponent,
  const BigInteger* mod, BigInteger* power);

void smallmodmult(int factor1, int factor2, limb *product, int mod);

#endif
