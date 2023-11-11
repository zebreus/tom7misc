#ifndef _MODMULT_H
#define _MODMULT_H

#include <memory>

#include "bignbr.h"

// These used to be globals. Now calling GetMontgomeryParams* creates them.
struct MontgomeryParams {
  // R is the power of 2 for Montgomery multiplication and reduction.

  // This is the inverse of the modulus mod R.
  // It's not computed (empty) when we aren't using montgomery form.
  std::vector<limb> Ninv;
  // This is the representation of 1 in Montgomery form.
  std::vector<limb> R1;
  std::vector<limb> R2;
  // modulus_length + 1 limbs, with a zero at the end.
  std::vector<limb> modulus;

  int modulus_length = 0;
  // If nonzero, the modulus is this power of two.
  int powerOf2Exponent = 0;
};

// modulus must have a 0 limb after its limbs (not sure why).
std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(int modulus_length, const limb *modulus);

// Same, but just using a bigint for the modulus. The returned
// parameters have the fixed modulus limbs and length.
std::unique_ptr<MontgomeryParams>
GetMontgomeryParams(const BigInt &Modulus);

// No coverage of this function; it may be broken.
std::unique_ptr<MontgomeryParams>
GetMontgomeryParamsPowerOf2(int powerOf2);

// The form of the numbers is determined by the params, so to use this you
// must be consistently using params. If modulus_length == 1,
// we assume regular numbers. If a power of 2, same (but this uses a fast
// method, as mod by power of 2 is easy). Otherwise, everything is in
// montgomery form.
// product <- factor1 * factor2 mod modulus
void ModMult(const MontgomeryParams &params,
             const limb *factor1, const limb *factor2,
             limb *product);

// Returns base^exp mod n (which comes from MontgomeryParams).
BigInt ModPowBaseInt(const MontgomeryParams &params,
                     int base, const BigInt &Exp);

void ModPow(const MontgomeryParams &params,
            const limb *base, const BigInt &Exp, limb *power);

// Note: This function doesn't work, and I think it's due to bugs in
// the original Alpertron (reading uninitialized data). No coverage
// outside of artificial tests.
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
                          const BigInt &base, const BigInt &exponent);

void AddBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                   int number_length);
void SubtBigNbrModN(const limb *Nbr1, const limb *Nbr2, limb *Sum, const limb *TestNbr,
                    int number_length);

void ComputeInversePower2(const limb *value, /*@out@*/limb *result,
                          int number_length);
BigInt GetInversePower2(const BigInt &Value, int number_length);

#endif
