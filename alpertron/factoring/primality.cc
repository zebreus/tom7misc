// from bignbr.cc

static BigInteger expon;

static void Halve(int number_length, limb *pValue) {
  if ((pValue[0].x & 1) == 0) {
    // Number to halve is even. Divide by 2.
    DivBigNbrByInt(pValue, 2, pValue, number_length);
  } else {
    // Number to halve is odd. Add modulus and then divide by 2.
    AddBigNbr(pValue, TestNbr, pValue, number_length + 1);
    DivBigNbrByInt(pValue, 2, pValue, number_length + 1);
  }
}

static int Perform2SPRPtest(int nbrLimbs, const limb* limbs) {
  int Mult3Len;
  int ctr;
  int lenBytes;

  limb Mult1[MAX_LEN];
  limb Mult3[MAX_LEN];
  limb Mult4[MAX_LEN];

  // Perform 2-SPRP test
  lenBytes = nbrLimbs * (int)sizeof(limb);
  int valueQ[MAX_LEN];
  (void)memcpy(valueQ, limbs, lenBytes);
  valueQ[nbrLimbs] = 0;
  valueQ[0]--;                     // q = p - 1 (p is odd, so there is no carry).
  lenBytes = (nbrLimbs + 1) * (int)sizeof(valueQ[0]);
  (void)memcpy(Mult3, valueQ, lenBytes);
  Mult3Len = nbrLimbs;
  DivideBigNbrByMaxPowerOf2(&ctr, Mult3, &Mult3Len);
  lenBytes = nbrLimbs * (int)sizeof(limb);
  (void)memcpy(TestNbr, limbs, lenBytes);
  TestNbr[nbrLimbs].x = 0;
  GetMontgomeryParms(nbrLimbs);
  // Find Mult1 = 2^Mult3.
  lenBytes = (NumberLength + 1) * (int)sizeof(limb);
  (void)memcpy(Mult1, MontgomeryMultR1, lenBytes);  // power <- 1
  for (int index = Mult3Len - 1; index >= 0; index--)
  {
    int groupExp = Mult3[index].x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1)
    {
      modmult(Mult1, Mult1, Mult1);
      if (((unsigned int)groupExp & mask) != 0U)
      {
        modmultInt(Mult1, 2, Mult1);
      }
    }
  }
  // If Mult1 != 1 and Mult1 = TestNbr-1, perform full test.
  if (!checkOne(Mult1, nbrLimbs) && !checkMinusOne(Mult1, nbrLimbs))
  {
    int i;
    for (i = 0; i < ctr; i++)
    {               // Loop that squares number.
      modmult(Mult1, Mult1, Mult4);
      if (checkOne(Mult4, nbrLimbs) != 0)
      {  // Current value is 1 but previous value is not 1 or -1: composite
        return 2;       // Composite. Not 2-strong probable prime.
      }
      if (checkMinusOne(Mult4, nbrLimbs) != 0)
      {
        return 0;         // Number is strong pseudoprime.
      }
      lenBytes = nbrLimbs * (int)sizeof(limb);
      (void)memcpy(Mult1, Mult4, lenBytes);
    }
    if (i == ctr)
    {
      return 1;         // Not 2-Fermat probable prime.
    }
    return 2;         // Composite. Not 2-strong probable prime.
  }
  return 0;
}


static bool BigNbrIsZero(int number_length, const limb *value) {
  const limb* ptrValue = value;
  for (int ctr = 0; ctr < number_length; ctr++) {
    if (ptrValue->x != 0) {
      return false;  // Number is not zero.
    }
    ptrValue++;
  }
  return true;       // Number is zero
}

// Perform strong Lucas primality test on n with parameters D, P=1, Q just found.
// Let d*2^s = n+1 where d is odd.
// Then U_d = 0 or v_{d*2^r} = 0 for some r < s.
// Use the following recurrences:
// U_0 = 0, V_0 = 2.
// U_{2k} = U_k * V_k
// V_{2k} = (V_k)^2 - 2*Q^K
// U_{2k+1} = (U_{2k} + V_{2k})/2
// V_{2k+1} = (D*U_{2k} + V_{2k})/2
// Use the following temporary variables:
// Mult1 for Q^n, Mult3 for U, Mult4 for V, Mult2 for temporary.

static int PerformStrongLucasTest(
    int number_length,
    const BigInteger* pValue, int D, int absQ, int signD) {
  int nbrLimbsBytes;
  int index;
  int signPowQ;
  bool insidePowering = false;
  int ctr;
  int nbrLimbs = pValue->nbrLimbs;

  limb Mult1[MAX_LEN];
  limb Mult3[MAX_LEN];
  limb Mult4[MAX_LEN];

  nbrLimbsBytes = (nbrLimbs + 1) * (int)sizeof(limb);
  (void)memcpy(Mult1, MontgomeryMultR1, nbrLimbsBytes); // Q^0 <- 1.
  signPowQ = 1;
  (void)memset(Mult3, 0, nbrLimbsBytes);                // U_0 <- 0.
  (void)memcpy(Mult4, MontgomeryMultR1, nbrLimbsBytes);
  AddBigNbrMod(Mult4, Mult4, Mult4);                    // V_0 <- 2.
  CopyBigInt(&expon, pValue);
  addbigint(&expon, 1);                                 // expon <- n + 1.
  Temp.limbs[nbrLimbs].x = 0;
  Temp2.limbs[nbrLimbs].x = 0;
  expon.limbs[expon.nbrLimbs].x = 0;
  DivideBigNbrByMaxPowerOf2(&ctr, expon.limbs, &expon.nbrLimbs);
  for (index = expon.nbrLimbs - 1; index >= 0; index--)
  {
    int groupExp = expon.limbs[index].x;
    for (unsigned int mask = HALF_INT_RANGE_U; mask > 0U; mask >>= 1)
    {
      if (insidePowering)
      {
        // Use the following formulas for duplicating Lucas numbers:
        // For sequence U: U_{2k} = U_k * V_k
        // For sequence V: V_{2k} = (V_k)^2 - 2*Q^K
        modmult(Mult3, Mult4, Mult3);          // U <- U * V
        modmult(Mult4, Mult4, Mult4);          // V <- V * V
        if (signPowQ > 0)
        {
          SubtBigNbrMod(Mult4, Mult1, Mult4);  // V <- V - Q^k
          SubtBigNbrMod(Mult4, Mult1, Mult4);  // V <- V - Q^k
        }
        else
        {
          AddBigNbrMod(Mult4, Mult1, Mult4);   // V <- V - Q^k
          AddBigNbrMod(Mult4, Mult1, Mult4);   // V <- V - Q^k
        }
        signPowQ = 1;                          // Indicate it is positive.
        modmult(Mult1, Mult1, Mult1);          // Square power of Q.
      }
      if (((unsigned int)groupExp & mask) != 0U)
      {        // Bit of exponent is equal to 1.
        int lenBytes;
        // U_{2k+1} = (U_{2k} + V_{2k})/2
        // V_{2k+1} = (D*U_{2k} + V_{2k})/2
        Mult3[number_length].x = 0;
        Mult4[number_length].x = 0;
        AddBigNbrMod(Mult3, Mult4, Temp.limbs);
        // Temp <- (U + V)/2
        Halve(number_length, Temp.limbs);
        MultBigNbrByIntModN(Mult3, D, Temp2.limbs, TestNbr, nbrLimbs);
        if (signD > 0) {
          // D is positive
          AddBigNbrMod(Mult4, Temp2.limbs, Mult4);
        } else {
          // D is negative.
          SubtBigNbrMod(Mult4, Temp2.limbs, Mult4);
        }
        // V <- (V +/- U*D)/2
        Halve(number_length, Mult4);
        lenBytes = number_length * (int)sizeof(limb);
        (void)memcpy(Mult3, Temp.limbs, lenBytes);
        modmultInt(Mult1, absQ, Mult1); // Multiply power of Q by Q.
        signPowQ = -signD;                   // Attach correct sign to power.
        insidePowering = true;
      }
    }
  }
  // If U is zero, the number passes the BPSW primality test.
  if (BigNbrIsZero(number_length, Mult3)) {
    return 0;         // Indicate number is probable prime.
  }
  for (index = 0; index < ctr; index++) {
    // If V is zero, the number passes the BPSW primality test.
    if (BigNbrIsZero(number_length, Mult4)) {
      return 0;       // Indicate number is probable prime.
    }
    modmult(Mult4, Mult4, Mult4);          // V <- V * V
    if (signPowQ > 0)
    {
      SubtBigNbrMod(Mult4, Mult1, Mult4);  // V <- V - Q^k
      SubtBigNbrMod(Mult4, Mult1, Mult4);  // V <- V - Q^k
    }
    else
    {
      AddBigNbrMod(Mult4, Mult1, Mult4);   // V <- V - Q^k
      AddBigNbrMod(Mult4, Mult1, Mult4);   // V <- V - Q^k
    }
    modmult(Mult1, Mult1, Mult1);          // Square power of Q.
    signPowQ = 1;                          // Indicate it is positive.
  }
  return 3;        // Number does not pass strong Lucas test.
}

// BPSW primality test:
// 1) If the input number is 2-SPRP composite, indicate composite and go out.
// 2) If number is perfect square, indicate it is composite and go out.
// 3) Find the first D in the sequence 5, -7, 9, -11, 13, -15, ...
//    for which the Jacobi symbol (D/n) is −1. Set P = 1 and Q = (1 - D) / 4.
// 4) Perform a strong Lucas probable prime test on n using parameters D, P,
//    and Q. If n is not a strong Lucas probable prime, then n is composite.
//    Otherwise, n is almost certainly prime.
// Output: 0 = probable prime.
//         1 = composite: not 2-Fermat pseudoprime.
//         2 = composite: does not pass 2-SPRP test.
//         3 = composite: does not pass strong Lucas test.
int BpswPrimalityTest(const BigInteger* pValue) {
  int D;
  int absQ;
  int signD;
  int retcode;
  int nbrLimbs = pValue->nbrLimbs;
  const limb* limbs = pValue->limbs;
  static BigInteger tmp;
  if (pValue->sign == SIGN_NEGATIVE)
  {
    return 1;      // Indicate not prime.
  }
  if (nbrLimbs < 1)
  {      // It should never come here.
    return 1;      // Indicate prime.
  }
  if (nbrLimbs == 1)
  {
    int smallPrimesLen = (int)(sizeof(smallPrimes) / sizeof(smallPrimes[0]));
    if (limbs->x <= 1)
    {
      return 1;    // Indicate not prime if 0, -1, or 1.
    }
    initializeSmallPrimes(smallPrimes);
    for (int index = 0; index < smallPrimesLen; index++)
    {
      int prime = smallPrimes[index];
      if ((unsigned int)(prime * prime) > (unsigned int)limbs->x)
      {
        return 0;  // Number is prime.
      }
      if ((limbs->x % prime) == 0)
      {
        return 1;  // Number is not prime.
      }
    }
  }
  if ((limbs->x & 1) == 0)
  {
    return 1;    // Number is even and different from 2. Indicate composite.
  }
  if (nbrLimbs > 1)
  {              // Check whether it is divisible by small number.
    initializeSmallPrimes(smallPrimes);
    for (int primeIndex = 0; primeIndex < 180; primeIndex += 3)
    {
      int primeProd = smallPrimes[primeIndex] * smallPrimes[primeIndex+1] * smallPrimes[primeIndex+2];
      int remainder = getRemainder(pValue, primeProd);
      if (((remainder % smallPrimes[primeIndex]) == 0) ||
        ((remainder % smallPrimes[primeIndex + 1]) == 0) ||
        ((remainder % smallPrimes[primeIndex + 2]) == 0))
      {
        return 1;   // Number is divisible by small number. Indicate composite.
      }
    }
  }
  retcode = Perform2SPRPtest(nbrLimbs, limbs);
  if (retcode != 0)
  {
    return retcode;
  }
  // At this point, the number is 2-SPRP, so check whether the number is perfect square.
  squareRoot(pValue->limbs, tmp.limbs, pValue->nbrLimbs, &tmp.nbrLimbs);
  tmp.sign = SIGN_POSITIVE;
  (void)BigIntMultiply(&tmp, &tmp, &tmp);
  if (BigIntEqual(pValue, &tmp))
  {                  // Number is perfect square.
    return 3;        // Indicate number does not pass strong Lucas test.
  }
  // At this point, the number is not perfect square, so find value of D.
  signD = -1;
  D = 7;
  for (;;)
  {
    int rem = getRemainder(pValue, D);
    if (JacobiSymbol(rem, D*signD) == -1)
    {
      break;
    }
    signD = -signD;
    D += 2;
  }
  absQ = (1 - (D*signD)) / 4;   // Compute Q <- (1 - D)/4
  if (absQ < 0)
  {
    absQ = -absQ;
  }
  return PerformStrongLucasTest(NumberLength, pValue, D, absQ, signD);
}
