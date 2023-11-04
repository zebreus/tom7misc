#ifndef _MODMULT_H
#define _MODMULT_H

#include <memory>

#include "bignbr.h"

// These used to be globals. Now calling GetMontgomeryParams* creates them.
struct MontgomeryParams {
  // PERF: We can dynamically allocate these to the necessary length.


  // R is the power of 2 for Montgomery multiplication and reduction.

  // XXX perhaps rename these. N is Ninv. Don't need to say "MontgomeryMult"
  // This is the inverse of the modulus mod R.
  limb MontgomeryMultN[MAX_LEN];
  // This is the representation of 1 in Montgomery form.
  limb MontgomeryMultR1[MAX_LEN];
  limb MontgomeryMultR2[MAX_LEN];

  int modulus_length = 0;
  // modulus_length + 1 limbs, with a zero at the end.
  std::vector<limb> modulus;
  // XXX this is just used during initialization. Maybe we can
  // get rid of it.
  int NumberLengthR1 = 0;
  // Indicates that the modulus is a power of two.
  int powerOf2Exponent = 0;
};

// modulus must have a 0 limb after its limbs (not sure why).
std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(int modulus_length, const limb *modulus);
// No coverage of this function; it may be broken.
std::unique_ptr<MontgomeryParams>
GetMontgomeryParamsPowerOf2(int powerOf2,
                            // computed from the power of 2
                            int *modulus_length);

// The form of the numbers is determined by the params, so to use this you
// must be consistently using params. If modulus_length == 1,
// we assume regular numbers. If a power of 2, same (but this uses a fast
// method, as mod by power of 2 is easy). Otherwise, everything is in
// montgomery form.
// product <- factor1 * factor2 mod modulus
void ModMult(const MontgomeryParams &params,
             const limb *factor1, const limb *factor2,
             limb *product);

void ModPowBaseInt(const MontgomeryParams &params,
                   int modulus_length, const limb *modulus,
                   int base, const BigInt &Exp,
                   limb *power);
void ModPow(const MontgomeryParams &params,
            int modulus_length, const limb *modulus,
            const limb *base, const limb *exp, int nbrGroupsExp, limb *power);



BigInt GeneralModularDivision(const BigInt &num, const BigInt &den,
                              const BigInt &modulus);

// The params must match the modulus.
// TODO: It's very common for us to derive the params immediately
// before calling this, so we could offer a version that just takes
// num,den,mod.
BigInt BigIntModularDivision(const MontgomeryParams &params,
                             BigInt num, BigInt den,
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
