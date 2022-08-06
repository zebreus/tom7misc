// THIS FILE ONLY:
//
// This code was derived from bbattery.c in TestU01. Apache2 license;
// see below.
//
// It's here so that I
// can run BigCrush both (a) threaded and (b) restartable, since it takes
// so long (with these expensive PRNGs0 that I can't actually get it to
// finish between summer power events.

/************************************************************************* \
 *
 * Package:        TestU01
 * File:           bbattery.c
 * Environment:    ANSI C
 *
 * Copyright (c) 2002 Pierre L'Ecuyer, DIRO, Universite de Montreal.
 * e-mail: lecuyer@iro.umontreal.ca
 * All rights reserved.
 *
 * This software is provided under the Apache 2 License.
 *
 * In scientific publications which used this software, a reference to it
 * would be appreciated.
 *
\*************************************************************************/


#include "testu01.h"

// TODO: Trim.

#ifdef __cplusplus
extern "C" {
#endif

  // #include "util.h"
#include "config.h"
#include "bbattery.h"
#include "smultin.h"
#include "sknuth.h"
#include "smarsa.h"
#include "snpair.h"
#include "svaria.h"
#include "sstring.h"
#include "swalk.h"
#include "scomp.h"
#include "sspectral.h"
#include "swrite.h"
#include "sres.h"
#include "unif01.h"
#include "ufile.h"

#include "gofs.h"
#include "gofw.h"
#include "fdist.h"
#include "fbar.h"
#include "num.h"
#include "chrono.h"

#ifdef __cplusplus
}
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>

#include <functional>
#include <string>
#include <mutex>
#include <vector>
#include <optional>

#include "util.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "threadutil.h"
#include "timer.h"

#include "ansi.h"

#define LEN 120
#define NAMELEN 30
#define NDIM 200                  /* Dimension of extern arrays */
#define THOUSAND 1000
#define MILLION (THOUSAND * THOUSAND)
#define BILLION (THOUSAND * MILLION)

/* The number of tests in each battery */
#define SMALLCRUSH_NUM 10
#define CRUSH_NUM 96
#define BIGCRUSH_NUM 106
#define RABBIT_NUM 26
#define ALPHABIT_NUM 9

using namespace std;

static void WritepVal (double p)
/*
 * Write a p-value with a nice format.
 */
{
   if (p < gofw_Suspectp) {
      gofw_Writep0 (p);

   } else if (p > 1.0 - gofw_Suspectp) {
      if (p >= 1.0 - gofw_Epsilonp1) {
         printf (" 1 - eps1");
      } else if (p >= 1.0 - 1.0e-4) {
         printf (" 1 - ");
         num_WriteD (1.0 - p, 7, 2, 2);
         /* printf (" 1 - %.2g ", 1.0 - p); */
      } else if (p >= 1.0 - 1.0e-2)
         printf ("  %.4f ", p);
      else
         printf ("   %.2f", p);
   }
}


// Result summary, suitable for serialization.
namespace {
struct TestResult {
  // Common case of one result.
  TestResult(string name, double pvalue) :
    values{make_pair(std::move(name), pvalue)} {}

  TestResult(vector<pair<string, double>> values) :
    values(std::move(values)) {}

  // name and pvalue
  vector<pair<string, double>> values;
};
}

namespace {
struct Test {
  Test(const string &name, std::function<TestResult(unif01_Gen *)> f) :
    name(name), f(std::move(f)) {}

  std::string name;
  std::function<TestResult(unif01_Gen *)> f;
};
}  // namespace

// TODO: H:M:S, etc.
static std::string AnsiTime(double seconds) {
  if (seconds < 60.0) {
    return StringPrintf(AYELLOW("%.3f") "s", seconds);
  } else {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "m" AYELLOW("%02d") "s",
                        omin, osec);
  }
}

static optional<pair<TestResult, double>> ParseResultFile(
        const string &filename) {
  vector<string> lines = Util::ReadFileToLines(filename);
  if (lines.size() < 2) return nullopt;

  if (lines[0].empty()) return nullopt;
  if (lines[0][0] == '*') {
    // new format. First line gives the number of pvalues.
    const int num = atoi(lines[0].c_str() + 1);
    CHECK(num > 0) << filename << " seems malformed: " << lines[0];
    // next line is the time it took to run.
    std::optional<double> osec = Util::ParseDoubleOpt(lines[1]);
    CHECK(osec.has_value()) << filename << " seems malformed: " << lines[1];

    vector<pair<string, double>> values;
    values.reserve(num);
    for (int i = 0; i < num; i++) {
      const int idx = 2 + i * 2;
      CHECK(idx + 1 < lines.size());
      string name = lines[idx];
      std::optional<double> opv = Util::ParseDoubleOpt(lines[idx + 1]);
      CHECK(opv.has_value()) << filename << " malformed: " << lines[idx + 1];
      values.emplace_back(name, opv.value());
    }

    return make_pair(TestResult(values), osec.value());
  } else {
    std::optional<double> od = Util::ParseDoubleOpt(lines[0]);
    CHECK(od.has_value()) << filename << " seems malformed";
    string desc = lines[1];
    double seconds = lines.size() >= 3 ?
      Util::ParseDouble(lines[2], 0.0) : 0.0;
    TestResult res(desc, od.value());

    // XXX rewrite in new format
    return make_pair(res, seconds);
  }
}


/*=========================================================================*/

#if 0
// This report is the summary of everything at the end. XXX redo it
// to read from a single result.

// TestNumber, pVal, bbattery_Testnames are all parallel arrays
// of length N.

static Report GetReport(
    // Generator or file name
    char *genName,
    // Battery name
    char *batName,
    // Max. number of tests
    int N,
    /* p-values of the tests */
    double pVal[],
    /* Timer */
    chrono_Chrono * Timer,
    /* = TRUE for a file, FALSE for a gen */
    lebool Flag,
    /* = TRUE: write the version number */
    lebool VersionFlag,
    /* Number of bits in the random file */
    double nb) {

  int j, co;

  co = 0;
  /* Some of the tests have not been done: their pVal[j] < 0. */
  for (j = 0; j < N; j++) {
    if (pVal[j] >= 0.0)
      co++;
  }
  printf ("\n Number of statistics:  %1d\n", co);
  printf (" Total CPU time:   ");
  chrono_Write (Timer, chrono_hms);

  co = 0;
  for (j = 0; j < N; j++) {
    if (pVal[j] < 0.0)          /* That test was not done: pVal = -1 */
      continue;
    if ((pVal[j] < gofw_Suspectp) || (pVal[j] > 1.0 - gofw_Suspectp)) {
      co++;
      break;
    }
  }
  if (co == 0) {
    printf ("\n\n All tests were passed\n\n\n\n");
    return;
  }

  if (gofw_Suspectp >= 0.01)
    printf ("\n The following tests gave p-values outside [%.4g, %.2f]",
            gofw_Suspectp, 1.0 - gofw_Suspectp);
  else if (gofw_Suspectp >= 0.0001)
    printf ("\n The following tests gave p-values outside [%.4g, %.4f]",
            gofw_Suspectp, 1.0 - gofw_Suspectp);
  else if (gofw_Suspectp >= 0.000001)
    printf ("\n The following tests gave p-values outside [%.4g, %.6f]",
            gofw_Suspectp, 1.0 - gofw_Suspectp);
  else
    printf ("\n The following tests gave p-values outside [%.4g, %.14f]",
            gofw_Suspectp, 1.0 - gofw_Suspectp);
  printf (":\n (eps  means a value < %6.1e)", gofw_Epsilonp);
  printf (":\n (eps1 means a value < %6.1e)", gofw_Epsilonp1);
  printf (":\n\n       Test                          p-value\n");
  printf (" ----------------------------------------------\n");

  co = 0;
  for (j = 0; j < N; j++) {
    if (pVal[j] < 0.0)          /* That test was not done: pVal = -1 */
      continue;
    if ((pVal[j] >= gofw_Suspectp) && (pVal[j] <= 1.0 - gofw_Suspectp))
      continue;                /* That test was passed */
    printf (" %2d ", TestNumber[j]);
    printf (" %-30s", bbattery_TestNames[j]);
    WritepVal (pVal[j]);
    printf ("\n");
    co++;
  }

  printf (" ----------------------------------------------\n");
  if (co < N - 1) {
    printf (" All other tests were passed\n");
  }
  printf ("\n\n\n");
}
#endif


#if 0
/*=========================================================================*/

static void GetPVal_Walk (swalk_Res * res, int *pj, char *mess, int j2) {
/*
 * Get the p-values in a swalk_RandomWalk1 test
 */

  char CharTemp[LEN + 1] = {};

  static constexpr int N = 1;

  int j = *pj;
  const unsigned int len = 20;

  if (N == 1) {
    bbattery_pVal[++j] = res->H[0]->pVal2[gofw_Mean];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 H");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->M[0]->pVal2[gofw_Mean];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 M");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->J[0]->pVal2[gofw_Mean];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 J");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->R[0]->pVal2[gofw_Mean];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 R");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->C[0]->pVal2[gofw_Mean];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 C");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

  } else {
    bbattery_pVal[++j] = res->H[0]->pVal2[gofw_Sum];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 H");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->M[0]->pVal2[gofw_Sum];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 M");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->J[0]->pVal2[gofw_Sum];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 J");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->R[0]->pVal2[gofw_Sum];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 R");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);

    bbattery_pVal[++j] = res->C[0]->pVal2[gofw_Sum];
    TestNumber[j] = j2;
    strcpy (CharTemp, "RandomWalk1 C");
    strncat (CharTemp, mess, (size_t) len);
    strncpy (bbattery_TestNames[j], CharTemp, (size_t) LEN);
  }

  *pj = j;
}
#endif


static vector<pair<string, double>>
GetPVal_CPairs(snpair_Res * res, int t) {

  /*
   * Get the p-values in a snpair_ClosePairs test
   */

  vector<pair<string, double>> ret;

  ret.emplace_back(
      StringPrintf("ClosePairs NP t=%d", t),
      res->pVal[snpair_NP]);

  ret.emplace_back(
      StringPrintf("ClosePairs mNP t=%d", t),
      res->pVal[snpair_mNP]);

  ret.emplace_back(
      StringPrintf("ClosePairs mNP1 t=%d", t),
      res->pVal[snpair_mNP1]);

  ret.emplace_back(
      StringPrintf("ClosePairs mNP2 t=%d", t),
      res->pVal[snpair_mNP2]);

  ret.emplace_back(
      StringPrintf("ClosePairs NJumps t=%d", t),
      res->pVal[snpair_NJumps]);

  ret.emplace_back(
      StringPrintf("ClosePairs mNP2S t=%d", t),
      res->pVal[snpair_mNP2S]);

  return ret;
}

/*=========================================================================*/

void ParallelBigCrush (
    const std::function<Generator*()> &gengen,
    const std::string &filepart,
    int NUM_THREADS) {
  /*
   * A battery of very stringent statistical tests for Random Number
   * Generators used in simulation.
   */

  // Not sure if this is necessary, but BigCrush sets it
  // unconditionally for all its snpair tests.
  snpair_mNP2S_Flag = TRUE;

  // XXX better names, ugh!
  [[maybe_unused]]
    static constexpr int s = 30;
  [[maybe_unused]]
    static constexpr int r = 0;

#if 0
  int i;
  chrono_Chrono *Timer;
  char genName[LEN + 1] = "";
  int j = -1;
  int j2 = 0;
#endif

  std::vector<Test> tests;

  tests.emplace_back(
      "serialover0",
      [](unif01_Gen *gen) {
        sres_Basic *res;
        res = sres_CreateBasic ();

        smarsa_SerialOver (gen, res, 1, BILLION, 0, 256, 3);
        TestResult result("SerialOver, r = 0",
                          res->pVal2[gofw_Mean]);
        sres_DeleteBasic (res);
        return result;
      });

  tests.emplace_back(
      "serialover22",
      [](unif01_Gen *gen) {
        sres_Basic *res;
        res = sres_CreateBasic ();

        smarsa_SerialOver (gen, res, 1, BILLION, 22, 256, 3);
        TestResult result("SerialOver, r = 22",
                          res->pVal2[gofw_Mean]);
        sres_DeleteBasic (res);
        return result;
      });

  tests.emplace_back(
      "collisionover2_0",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 0, 1024*1024*2, 2);
        TestResult result("CollisionOver, t = 2 (0)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover2_9",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 9, 1024*1024*2, 2);
        TestResult result("CollisionOver, t = 2 (9)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover3_0",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 0, 1024*16, 3);
        TestResult result("CollisionOver, t = 3 (0)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover3_16",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 16, 1024*16, 3);
        TestResult result("CollisionOver, t = 3 (16)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover7_0",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 0, 64, 7);
        TestResult result("CollisionOver, t = 7 (0)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover7_16",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 24, 64, 7);
        TestResult result("CollisionOver, t = 7 (24)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover14_0",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 0, 8, 14);
        TestResult result("CollisionOver, t = 14 (0)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover14_27",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 27, 8, 14);
        TestResult result("CollisionOver, t = 14 (27)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover21_0",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 0, 4, 21);
        TestResult result("CollisionOver, t = 21 (0)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });

  tests.emplace_back(
      "collisionover21_27",
      [](unif01_Gen *gen) {
        smarsa_Res *resm;
        resm = smarsa_CreateRes();

        smarsa_CollisionOver (gen, resm, 30, 20 * MILLION, 28, 4, 21);
        TestResult result("CollisionOver, t = 21 (28)",
                          resm->Pois->pVal2);
        smarsa_DeleteRes(resm);
        return result;
      });


  tests.emplace_back(
      "birthday2",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        const long d = 1073741824L;
        smarsa_BirthdaySpacings (gen, res, 250, 4 * MILLION, 0, d, 2, 1);

        TestResult result("BirthdaySpacings, t = 2",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday2b",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 10 * THOUSAND, MILLION / 10, 0,
                                 67108864, 2, 1);


        TestResult result("BirthdaySpacings, t = 2 (b)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday3",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 20 * MILLION, 0, 2097152, 3,
                                 1);

        TestResult result("BirthdaySpacings, t = 3",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday4",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 30 * MILLION, 14, 65536, 4, 1);

        TestResult result("BirthdaySpacings, t = 4",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday4_14",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 30 * MILLION, 14, 65536, 4, 1);

        TestResult result("BirthdaySpacings, t = 4 (14)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday4_0",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 10 * THOUSAND, MILLION / 10, 0,
                                 1024 * 8, 4, 1);
        TestResult result("BirthdaySpacings, t = 4 (0)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday4_16",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

      smarsa_BirthdaySpacings (gen, res, 10 * THOUSAND, MILLION / 10, 16,
                               1024 * 8, 4, 1);

        TestResult result("BirthdaySpacings, t = 4 (16)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday7_0",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 20 * MILLION, 0, 512, 7, 1);
        TestResult result("BirthdaySpacings, t = 7 (0)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday7_7",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 20 * MILLION, 7, 512, 7, 1);
        TestResult result("BirthdaySpacings, t = 7 (7)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday8_14",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 30 * MILLION, 14, 256, 8, 1);
        TestResult result("BirthdaySpacings, t = 8 (14)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday8_22",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();

        smarsa_BirthdaySpacings (gen, res, 20, 30 * MILLION, 22, 256, 8, 1);
        TestResult result("BirthdaySpacings, t = 8 (22)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday16_0",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();
        smarsa_BirthdaySpacings (gen, res, 20, 30 * MILLION, 0, 16, 16, 1);
        TestResult result("BirthdaySpacings, t = 16 (0)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

  tests.emplace_back(
      "birthday16_26",
      [](unif01_Gen *gen) {
        sres_Poisson *res;
        res = sres_CreatePoisson ();
        smarsa_BirthdaySpacings (gen, res, 20, 30 * MILLION, 26, 16, 16, 1);
        TestResult result("BirthdaySpacings, t = 16 (26)",
                          res->pVal2);
        sres_DeletePoisson(res);
        return result;
      });

   auto AddBirthday13 = [&](int r) {
       tests.emplace_back(
           StringPrintf("birthday13_%d", r),
           [r](unif01_Gen *gen) {
             sres_Poisson *res;
             res = sres_CreatePoisson ();
             smarsa_BirthdaySpacings(
                 gen, res, 10 * THOUSAND, MILLION / 10, r, 16, 13, 1);
             TestResult result(
                 StringPrintf("BirthdaySpacings, t = 13 (%d)", r),
                 res->pVal2);
             sres_DeletePoisson(res);
             return result;
           });
     };

   AddBirthday13(0);
   AddBirthday13(5);
   AddBirthday13(10);
   AddBirthday13(15);
   AddBirthday13(20);
   AddBirthday13(26);

  auto AddCPairs = [&](long N, long n, int r, int t, int p, int m) {
       tests.emplace_back(
           StringPrintf("cpairs%d", t),
           [N, n, r, t, p, m](unif01_Gen *gen) {
             snpair_Res *res;
             res = snpair_CreateRes ();

             snpair_ClosePairs(gen, res, N, n, r, t, p, m);

             vector<pair<string, double>> values =
               GetPVal_CPairs(res, t);

             snpair_DeleteRes(res);
             return TestResult(values);
           });
     };

  AddCPairs(30, 6 * MILLION, 0, 3,  0, 30);
  AddCPairs(20, 4 * MILLION, 0, 5,  0, 30);
  AddCPairs(10, 3 * MILLION, 0, 9,  0, 30);
  AddCPairs(5,  2 * MILLION, 0, 16, 0, 30);


  auto AddPoker = [&](long N, long n, int r, int d, int k) {
      tests.emplace_back(
          StringPrintf("simppoker%d_%d", r, d),
          [N, n, r, d, k](unif01_Gen *gen) {
            sres_Chi2 *res = sres_CreateChi2();
            sknuth_SimpPoker(gen, res, N, n, r, d, k);
            TestResult result(StringPrintf("SimpPoker, r = %d (%d)", r, d),
                              res->pVal2[gofw_Mean]);
            sres_DeleteChi2(res);
            return result;
          });
    };

  AddPoker(1, 400 * MILLION, 0, 8, 8);
  AddPoker(1, 400 * MILLION, 27, 8, 8);
  AddPoker(1, 100 * MILLION, 0, 32, 32);
  AddPoker(1, 100 * MILLION, 25, 32, 32);

  auto AddCoupon = [&](long N, long n, int r, int d) {
      tests.emplace_back(
          StringPrintf("coupon%d", r),
          [N, n, r, d](unif01_Gen *gen) {
            sres_Chi2 *res = sres_CreateChi2();
            sknuth_CouponCollector(gen, res, N, n, r, d);
            TestResult result(StringPrintf("CouponCollector, r = %d", r),
                              res->pVal2[gofw_Mean]);
            sres_DeleteChi2(res);
            return result;
          });
    };

  AddCoupon(1, 200 * MILLION, 0, 8);
  AddCoupon(1, 200 * MILLION, 10, 8);
  AddCoupon(1, 200 * MILLION, 20, 8);
  AddCoupon(1, 200 * MILLION, 27, 8);


#if 0
  {
    sres_Chi2 *res;
    res = sres_CreateChi2 ();

    ++j2;
    {
      sknuth_Gap (gen, res, 1, BILLION/2, 0, 0.0, 1.0/16.0);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Gap, r = 0");
    }

    ++j2;
    {
      sknuth_Gap (gen, res, 1, 300*MILLION, 25, 0.0, 1.0/32.0);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Gap, r = 25");
    }

    ++j2;
    {
      sknuth_Gap (gen, res, 1, BILLION/10, 0, 0.0, 1.0/128.0);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Gap, r = 0");
    }

    ++j2;
    {
      sknuth_Gap (gen, res, 1, 10*MILLION, 20, 0.0, 1.0/1024.0);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Gap, r = 20");
    }

    ++j2;
    {
      sknuth_Run (gen, res, 5, BILLION, 0, FALSE);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Run, r = 0");
    }

    ++j2;
    {
      sknuth_Run (gen, res, 10, BILLION, 15, TRUE);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Run, r = 15");
    }

    ++j2;
    {
      sknuth_Permutation (gen, res, 1, BILLION, 5, 3);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Permutation, t = 3" );
    }

    ++j2;
    {
      sknuth_Permutation (gen, res, 1, BILLION, 5, 5);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Permutation, t = 5");
    }

    ++j2;
    {
      sknuth_Permutation (gen, res, 1, BILLION/2, 5, 7);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Permutation, t = 7");
    }

    ++j2;
    {
      sknuth_Permutation (gen, res, 1, BILLION/2, 10, 10);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Permutation, t = 10");
    }
    sres_DeleteChi2 (res);
  }
  {
    sknuth_Res2 *res;
    res = sknuth_CreateRes2 ();
    ++j2;
    {
      sknuth_CollisionPermut (gen, res, 20, 20 * MILLION, 0, 14);
      bbattery_pVal[++j] = res->Pois->pVal2;
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "CollisionPermut, r = 0");
    }

    ++j2;
    {
      sknuth_CollisionPermut (gen, res, 20, 20 * MILLION, 10, 14);
      bbattery_pVal[++j] = res->Pois->pVal2;
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "CollisionPermut, r = 10");
    }
    sknuth_DeleteRes2 (res);
  }
  {
    sknuth_Res1 *res;
    res = sknuth_CreateRes1 ();
    ++j2;
    {
      sknuth_MaxOft (gen, res, 40, 10 * MILLION, 0, MILLION / 10, 8);
      bbattery_pVal[++j] = res->Chi->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft, t = 8");
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft AD, t = 8");
    }

    ++j2;
    {
      sknuth_MaxOft (gen, res, 30, 10 * MILLION, 0, MILLION / 10, 16);
      bbattery_pVal[++j] = res->Chi->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft, t = 16");
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft AD, t = 16");
    }

    ++j2;
    {
      sknuth_MaxOft (gen, res, 20, 10 * MILLION, 0, MILLION / 10, 24);
      bbattery_pVal[++j] = res->Chi->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft, t = 24");
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft AD, t = 24");
    }

    ++j2;
    {
      sknuth_MaxOft (gen, res, 20, 10 * MILLION, 0, MILLION / 10, 32);
      bbattery_pVal[++j] = res->Chi->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft, t = 32");
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MaxOft AD, t = 32");
    }
    sknuth_DeleteRes1 (res);
  }
  {
    sres_Basic *res;
    res = sres_CreateBasic ();
    ++j2;
    {
      svaria_SampleProd (gen, res, 40, 10 * MILLION, 0, 8);
      bbattery_pVal[++j] = res->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleProd, t = 8");
    }

    ++j2;
    {
      svaria_SampleProd (gen, res, 20, 10*MILLION, 0, 16);
      bbattery_pVal[++j] = res->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleProd, t = 16");
    }

    ++j2;
    {
      svaria_SampleProd (gen, res, 20, 10*MILLION, 0, 24);
      bbattery_pVal[++j] = res->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleProd, t = 24");
    }

    ++j2;
    {
      svaria_SampleMean (gen, res, 20*MILLION, 30, 0);
      bbattery_pVal[++j] = res->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleMean, r = 0");
    }

    ++j2;
    {
      svaria_SampleMean (gen, res, 20*MILLION, 30, 10);
      bbattery_pVal[++j] = res->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleMean, r = 10");
    }

    ++j2;
    {
      svaria_SampleCorr (gen, res, 1, 2*BILLION, 0, 1);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleCorr, k = 1");
    }

    ++j2;
    {
      svaria_SampleCorr (gen, res, 1, 2*BILLION, 0, 2);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SampleCorr, k = 2");
    }

    ++j2;
    {
      svaria_AppearanceSpacings (gen, res, 1, 10 * MILLION, BILLION,
                                 r, 3, 15);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "AppearanceSpacings, r = 0");
    }

    ++j2;
    {
      svaria_AppearanceSpacings (gen, res, 1, 10 * MILLION, BILLION,
                                 27, 3, 15);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "AppearanceSpacings, r = 27");
    }
    sres_DeleteBasic (res);
  }
  {
    smarsa_Res2 *res2;
    sres_Chi2 *res;
    res = sres_CreateChi2 ();
    ++j2;
    {
      svaria_WeightDistrib (gen, res, 1, 20 * MILLION, 0, 256, 0.0, 0.25);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "WeightDistrib, r = 0");
    }

    ++j2;
    {
      svaria_WeightDistrib (gen, res, 1, 20 * MILLION, 20, 256, 0.0, 0.25);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "WeightDistrib, r = 20");
    }

    ++j2;
    {
      svaria_WeightDistrib (gen, res, 1, 20 * MILLION, 28, 256, 0.0, 0.25);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "WeightDistrib, r = 28");
    }

    ++j2;
    {
      svaria_WeightDistrib (gen, res, 1, 20 * MILLION, 0, 256, 0.0, 0.0625);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "WeightDistrib, r = 0");
    }

    ++j2;
    {
      svaria_WeightDistrib (gen, res, 1, 20 * MILLION, 10, 256, 0.0, 0.0625);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "WeightDistrib, r = 10");
    }

    ++j2;
    {
      svaria_WeightDistrib (gen, res, 1, 20 * MILLION, 26, 256, 0.0, 0.0625);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "WeightDistrib, r = 26");
    }

    ++j2;
    {
      svaria_SumCollector (gen, res, 1, 500 * MILLION, 0, 10.0);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "SumCollector");
    }

    ++j2;
    {
      smarsa_MatrixRank (gen, res, 10, MILLION, r, 5, 30, 30);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MatrixRank, L=30, r=0");
    }

    ++j2;
    {
      smarsa_MatrixRank (gen, res, 10, MILLION, 25, 5, 30, 30);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MatrixRank, L=30, r=26");
    }

    ++j2;
    {
      smarsa_MatrixRank (gen, res, 1, 5 * THOUSAND, r, 4, 1000, 1000);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MatrixRank, L=1000, r=0");
    }

    ++j2;
    {
      smarsa_MatrixRank (gen, res, 1, 5 * THOUSAND, 26, 4, 1000, 1000);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MatrixRank, L=1000, r=26");
    }

    ++j2;
    {
      smarsa_MatrixRank (gen, res, 1, 80, 15, 15, 5000, 5000);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MatrixRank, L=5000");
    }

    ++j2;
    {
      smarsa_MatrixRank (gen, res, 1, 80, 0, 30, 5000, 5000);
      bbattery_pVal[++j] = res->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "MatrixRank, L=5000");
    }

    ++j2;
    {
      smarsa_Savir2 (gen, res, 10, 10 * MILLION, 10, 1024*1024, 30);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Savir2");
    }
    sres_DeleteChi2 (res);

    res2 = smarsa_CreateRes2 ();
    ++j2;
    {
      smarsa_GCD (gen, res2, 10, 50 * MILLION, 0, 30);
      bbattery_pVal[++j] = res2->GCD->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "GCD");
    }
    smarsa_DeleteRes2 (res2);
  }
  {
    swalk_Res *res;
    res = swalk_CreateRes ();
    ++j2;
    {
      swalk_RandomWalk1 (gen, res, 1, 100 * MILLION, r, 5, 50, 50);
      GetPVal_Walk(res, &j, " (L=50, r=0)", j2);
    }

    ++j2;
    {
      swalk_RandomWalk1 (gen, res, 1, 100 * MILLION, 25, 5, 50, 50);
      GetPVal_Walk(res, &j, " (L=50, r=25)", j2);
    }

    ++j2;
    {
      swalk_RandomWalk1 (gen, res, 1, 10 * MILLION, r, 10, 1000, 1000);
      GetPVal_Walk(res, &j, " (L=1000, r=0)", j2);
    }

    ++j2;
    {
      swalk_RandomWalk1 (gen, res, 1, 10 * MILLION, 20, 10, 1000, 1000);
      GetPVal_Walk(res, &j, " (L=1000, r=20)", j2);
    }

    ++j2;
    {
      swalk_RandomWalk1 (gen, res, 1, 1 * MILLION, r, 15, 10000, 10000);
      GetPVal_Walk(res, &j, " (L=10000, r=0)", j2);
    }

    ++j2;
    {
      swalk_RandomWalk1 (gen, res, 1, 1 * MILLION, 15, 15, 10000, 10000);
      GetPVal_Walk(res, &j, " (L=10000, r=15)", j2);
    }
    swalk_DeleteRes (res);
  }
  {
    scomp_Res *res;
    res = scomp_CreateRes ();
    ++j2;
    {
      scomp_LinearComp (gen, res, 1, 400 * THOUSAND + 20, r, 1);
      bbattery_pVal[++j] = res->JumpNum->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LinearComp, r = 0");
      bbattery_pVal[++j] = res->JumpSize->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LinearComp, r = 0");
    }

    ++j2;
    {
      scomp_LinearComp (gen, res, 1, 400 * THOUSAND + 20, 29, 1);
      bbattery_pVal[++j] = res->JumpNum->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LinearComp, r = 29");
      bbattery_pVal[++j] = res->JumpSize->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LinearComp, r = 0");
    }
    scomp_DeleteRes (res);
  }
  {
    sres_Basic *res;
    res = sres_CreateBasic ();
    ++j2;
    {
      scomp_LempelZiv (gen, res, 10, 27, r, s);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LempelZiv, r = 0");
    }

    ++j2;
    {
      scomp_LempelZiv (gen, res, 10, 27, 15, 15);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LempelZiv, r = 15");
    }
    sres_DeleteBasic (res);
  }
  {
    sspectral_Res *res;
    res = sspectral_CreateRes ();
    ++j2;
    {
      sspectral_Fourier3 (gen, res, 100 * THOUSAND, 14, r, 3);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Fourier3, r = 0");
    }

    ++j2;
    {
      sspectral_Fourier3 (gen, res, 100 * THOUSAND, 14, 27, 3);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_AD];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Fourier3, r = 27");
    }
    sspectral_DeleteRes (res);
  }
  {
    sstring_Res2 *res;
    res = sstring_CreateRes2 ();
    ++j2;
    {
      sstring_LongestHeadRun (gen, res, 1, 1000, r, 3, 20 + 10 * MILLION);
      bbattery_pVal[++j] = res->Chi->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LongestHeadRun, r = 0");
      bbattery_pVal[++j] = res->Disc->pVal2;
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LongestHeadRun, r = 0");
    }

    ++j2;
    {
      sstring_LongestHeadRun (gen, res, 1, 1000, 27, 3, 20 + 10 * MILLION);
      bbattery_pVal[++j] = res->Chi->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LongestHeadRun, r = 27");
      bbattery_pVal[++j] = res->Disc->pVal2;
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "LongestHeadRun, r = 27");
    }
    sstring_DeleteRes2 (res);
  }
  {
    sres_Chi2 *res;
    res = sres_CreateChi2 ();
    ++j2;
    {
      sstring_PeriodsInStrings (gen, res, 10, BILLION/2, r, 10);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "PeriodsInStrings, r = 0");
    }

    ++j2;
    {
      sstring_PeriodsInStrings (gen, res, 10, BILLION/2, 20, 10);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "PeriodsInStrings, r = 20");
    }
    sres_DeleteChi2 (res);
  }
  {
    sres_Basic *res;
    res = sres_CreateBasic ();
    ++j2;
    {
      sstring_HammingWeight2 (gen, res, 10, BILLION, r, 3, MILLION);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingWeight2, r = 0");
    }

    ++j2;
    {
      sstring_HammingWeight2 (gen, res, 10, BILLION, 27, 3, MILLION);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingWeight2, r = 27");
    }
    sres_DeleteBasic (res);
  }
  {
    sstring_Res *res;
    res = sstring_CreateRes ();
    ++j2;
    {
      sstring_HammingCorr (gen, res, 1, BILLION, 10, 10, s);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingCorr, L = 30");
    }

    ++j2;
    {
      sstring_HammingCorr (gen, res, 1, 100 * MILLION, 10, 10, 10 * s);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingCorr, L = 300");
    }

    ++j2;
    {
      sstring_HammingCorr (gen, res, 1, 100 * MILLION, 10, 10, 40 * s);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingCorr, L = 1200");
    }

    ++j2;
    {
      sstring_HammingIndep (gen, res, 10, 30 * MILLION, r, 3, s, 0);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingIndep, L=30, r=0");
    }

    ++j2;
    {
      sstring_HammingIndep (gen, res, 10, 30 * MILLION, 27, 3, s, 0);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingIndep, L=30, r=27");
    }

    ++j2;
    {
      sstring_HammingIndep (gen, res, 1, 30 * MILLION, r, 4, 10 * s, 0);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingIndep, L=300, r=0");
    }

    ++j2;
    {
      sstring_HammingIndep (gen, res, 1, 30 * MILLION, 26, 4, 10 * s, 0);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingIndep, L=300, r=26");
    }

    ++j2;
    {
      sstring_HammingIndep (gen, res, 1, 10 * MILLION, r, 5, 40 * s, 0);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingIndep, L=1200, r=0");
    }

    ++j2;
    {
      sstring_HammingIndep (gen, res, 1, 10 * MILLION, 25, 5, 40 * s, 0);
      bbattery_pVal[++j] = res->Bas->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "HammingIndep, L=1200, r=25");
    }
    sstring_DeleteRes (res);
  }
  {
    sstring_Res3 *res;
    res = sstring_CreateRes3 ();
    ++j2;
    {
      sstring_Run (gen, res, 1, 2*BILLION, r, 3);
      bbattery_pVal[++j] = res->NRuns->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Run of bits, r = 0");
      bbattery_pVal[++j] = res->NBits->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Run of bits, r = 0");
    }

    ++j2;
    {
      sstring_Run (gen, res, 1, 2*BILLION, 27, 3);
      bbattery_pVal[++j] = res->NRuns->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Run of bits, r = 27");
      bbattery_pVal[++j] = res->NBits->pVal2[gofw_Mean];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "Run of bits, r = 27");
    }
    sstring_DeleteRes3 (res);
  }
  {
    sres_Basic *res;
    res = sres_CreateBasic ();
    ++j2;
    {
      sstring_AutoCor (gen, res, 10, 30 + BILLION, r, 3, 1);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "AutoCor, d=1, r=0");
    }

    ++j2;
    {
      sstring_AutoCor (gen, res, 10, 30 + BILLION, r, 3, 3);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "AutoCor, d=3, r=0");
    }

    ++j2;
    {
      sstring_AutoCor (gen, res, 10, 30 + BILLION, 27, 3, 1);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "AutoCor, d=1, r=27");
    }

    ++j2;
    util_Assert (j2 <= BIGCRUSH_NUM, "BigCrush:   j2 > BIGCRUSH_NUM");
    {
      sstring_AutoCor (gen, res, 10, 30 + BILLION, 27, 3, 3);
      bbattery_pVal[++j] = res->pVal2[gofw_Sum];
      TestNumber[j] = j2;
      strcpy (bbattery_TestNames[j], "AutoCor, d=3, r=27");
    }
    sres_DeleteBasic (res);
  }


  bbattery_NTests = ++j;
  GetName (gen, genName);
  WriteReport (genName, "BigCrush", bbattery_NTests, bbattery_pVal,
               Timer, FALSE, TRUE, 0.0);
  chrono_Delete (Timer);
#endif

  // Process all the tests.

  std::mutex m;

  printf("There are " AYELLOW("%d") " tests in the suite.\n",
         (int)tests.size());

  ParallelAppi(
      tests,
      [&gengen, &filepart, &m](int64_t idx, const Test &test) {
        const string filename =
          StringPrintf("%s.%s.txt", filepart.c_str(),
                       test.name.c_str());

        auto rfo = ParseResultFile(filename);
        if (rfo.has_value()) {
          const auto &[res, seconds] = rfo.value();
          std::unique_lock<std::mutex> ml(m);

          printf("[" AYELLOW("%02d") "] Read previous " AGREEN("%s") "\n",
                 (int)idx, filename.c_str());
          printf("     Took %s\n",
                 AnsiTime(seconds).c_str());
          for (const auto &[name, pv] : res.values) {
            printf("     " ABLUE("%s") ": p=" APURPLE("%.6f") "\n",
                   name.c_str(), pv);
          }
          return;
        }

        {
          std::unique_lock<std::mutex> ml(m);
          printf("[" AYELLOW("%02d") "] Start " ACYAN("%s") "\n",
                 (int)idx, test.name.c_str());
        }
        Timer timer;

        Generator *generator = gengen();
        unif01_Gen gen;
        generator->FillGen(&gen);
        TestResult result = test.f(&gen);
        delete generator;

        const double seconds = timer.Seconds();

        // Prohibited for the result values to be empty.
        CHECK(result.values.size() > 0) << test.name;
        string resultfile;
        StringAppendF(&resultfile, "*%d\n", (int)result.values.size());
        StringAppendF(&resultfile, "%.17g\n", seconds);
        for (const auto &[name, pv] : result.values) {
          StringAppendF(&resultfile,
                        "%.17g\n"
                        "%s\n",
                        pv, name);
        }
        Util::WriteFile(filename, resultfile);
        {
          std::unique_lock<std::mutex> ml(m);
          printf("[" AYELLOW("%02d") "] Finished " ACYAN("%s") "\n"
                 "     Took %s. Wrote to " AGREEN("%s") ".\n",
                 (int)idx, test.name.c_str(),
                 AnsiTime(seconds).c_str(),
                 filename.c_str());
          for (const auto &[name, pv] : result.values) {
            printf("     " ABLUE("%s") ": p=" APURPLE("%.6f") "\n",
                   name.c_str(), pv);
          }
        }
      }, NUM_THREADS);
}
