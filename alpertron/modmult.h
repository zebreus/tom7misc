#ifndef _MODMULT_H
#define _MODMULT_H

#include "bignbr.h"

// These used to be globals. Now calling GetMontgomeryParams* creates them.
struct MontgomeryParams {
  limb MontgomeryMultN[MAX_LEN];
  limb MontgomeryMultR1[MAX_LEN];
  limb MontgomeryMultR2[MAX_LEN];
  int NumberLengthR1;
  // Indicates that the modulus is a power of two.
  int powerOf2Exponent;
};

MontgomeryParams GetMontgomeryParams(int modulus_length, const limb *modulus);
MontgomeryParams GetMontgomeryParamsPowerOf2(int powerOf2,
                                             // computed from the power of 2
                                             int *modulus_length);

// If modulus_length > 1, then everything is in montgomery form; otherwise
// they are just regular numbers. (IMO it would be better if params determined
// the form.)
// XXX this is probably wrong for power of 2 modulus
// product <- factor1 * factor2 mod modulus
void ModMult(const MontgomeryParams &params,
             const limb *factor1, const limb *factor2,
             int modulus_length, const limb *modulus,
             limb *product);

void ModPowBaseInt(const MontgomeryParams &params,
                   int modulus_length, const limb *modulus,
                   int base, const limb *exp, int nbrGroupsExp, limb *power);
void ModPow(const MontgomeryParams &params,
            int modulus_length, const limb *modulus,
            const limb *base, const limb *exp, int nbrGroupsExp, limb *power);

void BigIntegerGeneralModularDivision(
    const BigInteger *Num, const BigInteger *Den,
    const BigInteger *mod, BigInteger *quotient);

BigInt GeneralModularDivision(const BigInt &num, const BigInt &den,
                              const BigInt &modulus);

// The params must match the modulus.
// TODO: It's very common for us to derive the params immediately
// before calling this, so we could offer a version that just takes
// num,den,mod.
BigInt BigIntModularDivision(const MontgomeryParams &params,
                             const BigInt &num, const BigInt &den,
                             const BigInt &mod);

BigInt BigIntModularPower(const MontgomeryParams &params,
                          int modulus_length, const limb *modulus,
                          const BigInt &base, const BigInt &exponent);

void AddBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                   int number_length);
void SubtBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                    int number_length);

void ComputeInversePower2(const limb *value, /*@out@*/limb *result,
                          int number_length);

#endif
