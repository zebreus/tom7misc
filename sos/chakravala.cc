
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_set>

#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "sos-util.h"

using namespace std;

static constexpr bool CHECK_INVARIANTS = true;

using Sol = std::pair<BigInt, BigInt>;


// For sol = (x, y), compute k such that n * x^2 + k = y^2.
static BigInt Error(const BigInt &n, const Sol &sol) {
  const auto &[x, y] = sol;
  return y * y - n * x * x;
}

static Sol CombineSelf(const BigInt &n, const Sol &sol) {
  const auto &[x, y] = sol;
  return make_pair(
      2_b * x * y,
      y * y + n * x * x);
  // and also the degenerate (0, y^2 - nx^2)
}

static std::pair<Sol, Sol> Combine(
    const BigInt &n, const Sol &sol1, const Sol &sol2) {
  const auto &[a, b] = sol1;
  const auto &[c, d] = sol2;

  return
    make_pair(
        make_pair(b * c + a * d, b * d + n * a * c),
        make_pair(b * c - a * d, b * d - n * a * c));
}

struct HashSol {
  size_t operator ()(const Sol &sol) const {
    return (size_t)BigInt::LowWord(sol.first - sol.second);
  }
};

struct EqSol {
  bool operator ()(const Sol &a, const Sol &b) const {
    return a.first == b.first && a.second == b.second;
  }
};

using Triple = std::pair<Sol, BigInt>;

struct HashTriple {
  size_t operator()(const Triple &tri) const {
    const auto [sol, k] = tri;
    size_t s = HashSol()(sol);
    return (size_t)(BigInt::LowWord(k) * 0x314159 + s);
  }
};

struct EqTriple {
  bool operator()(const Triple &a, const Triple &b) const {
    const auto [sol1, k1] = a;
    const auto [sol2, k2] = b;
    return k1 == k2 &&
      sol1.first == sol2.first &&
      sol1.second == sol2.second;
  }
};

#define TERM_A AFGCOLOR(39, 179, 214, "%s")
#define TERM_M AFGCOLOR(39, 214, 179, "%s")
#define TERM_B AFGCOLOR(232, 237, 173, "%s")
#define TERM_K AFGCOLOR(220, 173, 237, "%s")
#define TERM_N AFGCOLOR(227, 198, 143, "%s")
#define TERM_SQRTN AFGCOLOR(210, 200, 180, "%s")
#define TERM_E AFGCOLOR(200, 120, 140, "%s")


static std::string SolString(const Sol &a) {
  return StringPrintf("(" TERM_A "," TERM_B ")",
                      a.first.ToString().c_str(),
                      a.second.ToString().c_str());
}


using TripleSet = std::unordered_set<Triple, HashTriple, EqTriple>;

// Given sol=(x,y) such that nx^2 + k = y^2, find a solution
// to the Pell equation nx^2 + 1 = y^2.
Sol Bhaskara(BigInt n, BigInt k, Sol sol) {
  Periodically bar_per(1.0);
  bool first_progress = true;
  const int start_k_size = k.ToString().size();
  Timer timer;
  static constexpr bool VERBOSE = false;

  BigInt sqrtn = BigInt::Sqrt(n);
  if (VERBOSE) {
    printf("Sqrt " TERM_N " = " TERM_SQRTN "\n",
           n.ToString().c_str(),
           sqrtn.ToString().c_str());
  }

  TripleSet seen;

  for (int iters = 0; true; iters ++) {
    const Triple triple = make_pair(sol, k);
    if (CHECK_INVARIANTS) {
      CHECK(!seen.contains(triple));
    }
    seen.insert(triple);

    if (!VERBOSE)
      bar_per.RunIf([&first_progress, start_k_size, &timer,
                     &iters, &k, &sol]() {
        if (first_progress) {
          printf("\n");
          first_progress = false;
        }
        int k_size = k.ToString().size();
        printf(ANSI_PREVLINE
               ANSI_CLEARLINE
               "%s\n",
               ANSI::ProgressBar(
                   start_k_size - k_size, start_k_size,
                   StringPrintf("%d iters, k: %d dig a: %d dig b: %d dig",
                                iters, k_size,
                                (int)sol.first.ToString().size(),
                                (int)sol.second.ToString().size()),
                   timer.Seconds()).c_str());
      });

    if (VERBOSE) {
      printf(AWHITE(ABGCOLOR(80, 0, 0, "== %d iters =="))
             " k %d dig, a %d dig, b %d dig\n",
             iters,
             (int)k.ToString().size(),
             (int)sol.first.ToString().size(),
             (int)sol.second.ToString().size());
    }
    // Then we have a solution to the Pell equation.
    if (k == BigInt{1}) {
      if (VERBOSE) {
        printf("Done in " AWHITE("%d") " iterations.\n", iters);
      }
      return sol;
    }

    BigInt a, b;
    std::tie(a, b) = sol;
    BigInt gcd = BigInt::GCD(a, b);
    if (gcd != BigInt{1}) {
      if (CHECK_INVARIANTS) {
        CHECK(a % gcd == BigInt{0});
        CHECK(b % gcd == BigInt{0});
      }
      a = a / gcd;
      b = b / gcd;
    }
    if (CHECK_INVARIANTS) {
      CHECK(Error(n, sol) == k);
    }

    // Now we need m such that am+b is divisible by k, and we
    // want the one that minimizes m^2-n.
    // For the latter condition, we'll work with sqrt(n),
    // which we computed outside the loop.

    // So now we're looking for m that's close to sqrtn,
    // am + b must be divisible by k. That's the same as
    // saying that am mod k  =  -b mod k.
    BigInt negbmodk = -b % k;
    // Now we can find the multiplicative inverse of a mod k,
    // using the extended euclidean algorithm.
    const auto [g, s, t] = BigInt::ExtendedGCD(a, k);
    // now we have a*s + k*t = g.
    CHECK(g == BigInt{1}) << "?? Don't know why this must be true, "
      "but it's seemingly assumed by descriptions of this?";
    // so if a*s + k*t = 1, then a*s mod k is 1 (because k*t is 0 mod k).
    // In other words, s is the multiplicative inverse of a (mod k).
    // so if am = -b (mod k), then a * (a^-1) * m = -b * (a^-1)  (mod k)
    // and thus m = -b * (a^-1)  (mod k), which is -b * s.
    BigInt m = (negbmodk * s) % k;
    if (CHECK_INVARIANTS) {
      CHECK((a * m + b) % k == BigInt{0}) <<
        StringPrintf("Expect k | (am + b). But got remainder " ARED("%s") ".\n"
                     TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
                     ((a * m + b) % k).ToString().c_str(),
                     k.ToString().c_str(),
                     a.ToString().c_str(),
                     m.ToString().c_str(),
                     b.ToString().c_str());
    }

    if (VERBOSE) {
      printf("We have k | (am + b):\n"
             TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
             k.ToString().c_str(),
             a.ToString().c_str(),
             m.ToString().c_str(),
             b.ToString().c_str());
    }

    // Now it remains to find m that is closest to sqrtn.

    if (VERBOSE) printf("Want " TERM_M " close to " AGREEN("%s") "\n",
                        m.ToString().c_str(),
                        sqrtn.ToString().c_str());

    // Compare three cases. m + d, m + d - k, m + d + k.
    // d is some multiple of k that is closest to sqrtn - m.
    // (Only two of these are actually possible depending on signs,
    // but addition is cheap.)

    BigInt best_m;
    std::optional<BigInt> best_err;
    auto Consider = [&seen, &a, &b, &n, &k,
                     &best_m, &best_err](const BigInt &m) {
        BigInt new_a = (a * m + b) / BigInt::Abs(k);
        BigInt new_b = (b * m + n * a) / BigInt::Abs(k);
        BigInt new_k = (m * m - n) / k;
        Triple new_triple = make_pair(make_pair(new_a, new_b), new_k);

        if (seen.contains(new_triple)) {
          if (VERBOSE) {
            printf("Ignored " TERM_M " which would repeat the triple "
                   "(" TERM_A ", " TERM_B ", " TERM_K ")\n",
                   m.ToString().c_str(),
                   new_a.ToString().c_str(),
                   new_b.ToString().c_str(),
                   new_k.ToString().c_str());
          }
          return;
        }

        // Don't allow trivial tuples.
        if (new_a == BigInt{0}) {
          if (VERBOSE) {
            printf("Ignored " TERM_M " which would yield a=0\n",
                   m.ToString().c_str());
          }
          return;
        }

        #if 0
        // (This is now tested for *all* loops above.)
        //
        // Or self-loops.
        // ("New Light on Bhaskara's Chakravala or Cyclic Method
        //  of solving Indeterminate Equations of the Second Degree
        //  in two Variables." Ayyangar, AA Krishnaswami 1929, p.235)
        // https://www.ms.uky.edu/~sohum/aak/pdf%20files/chakravala.pdf
        if (new_a == a) {
          if (VERBOSE) {
            printf("Ignored " TERM_M " which would yield a = " TERM_A
                   " again; a cycle of length 1\n",
                   m.ToString().c_str(),
                   a.ToString().c_str());
          }
          return;
        }
        #endif

        BigInt err = BigInt::Abs(m * m - n);
        if (VERBOSE) {
          printf("consider " TERM_M " (error " TERM_E "); yields triple"
                 "(" TERM_A ", " TERM_B ", " TERM_K ")\n",
                 m.ToString().c_str(), err.ToString().c_str(),
                 new_a.ToString().c_str(),
                 new_b.ToString().c_str(),
                 new_k.ToString().c_str());
        }

        if (!best_err.has_value() ||
            err < best_err.value()) {
          best_m = m;
          best_err.emplace(std::move(err));
        }
      };

    auto TryPoint = [&m, &k, &Consider](const BigInt &sqrtn) {
        BigInt diff = sqrtn - m;
        BigInt d = (diff / k) * k;
        BigInt m1 = m + d;
        if (VERBOSE) {
          printf("For target " TERM_SQRTN " have diff = " AYELLOW("%s")
                 " and d = " APURPLE("%s") " and m1 = " TERM_M " +/- k\n",
                 sqrtn.ToString().c_str(),
                 diff.ToString().c_str(),
                 d.ToString().c_str(),
                 m1.ToString().c_str());
        }
        Consider(m1);
        Consider(m1 + k);
        Consider(m1 - k);
      };

    TryPoint(sqrtn);
    // PERF don't need to keep copying this
    // Wikipedia ignores these solutions. The example of 67 doesn't
    // terminate if we include them. They are valid; though?
    // TryPoint(-sqrtn);

    CHECK(best_err.has_value());
    m = std::move(best_m);

    if (VERBOSE) {
      printf("So we take m = " TERM_M "\n",
             m.ToString().c_str());
    }

    if (CHECK_INVARIANTS) {
      CHECK((a * m + b) % k == BigInt{0}) <<
        StringPrintf("Expect k | (am + b). But got remainder " ARED("%s") ".\n"
                     TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
                     ((a * m + b) % k).ToString().c_str(),
                     k.ToString().c_str(),
                     a.ToString().c_str(),
                     m.ToString().c_str(),
                     b.ToString().c_str());
    }

    if (VERBOSE) {
      printf("And NOW We have k | (am + b):\n"
             TERM_K " | (" TERM_A " * " TERM_M " + " TERM_B ")\n",
             k.ToString().c_str(),
             a.ToString().c_str(),
             m.ToString().c_str(),
             b.ToString().c_str());
    }

    if (CHECK_INVARIANTS) {
      CHECK((m * m - n) % k == BigInt{0});
      CHECK((a * m + b) % k == BigInt{0});
      CHECK((b * m + n * a) % k == BigInt{0});
    }

    if (VERBOSE) {
      printf("And have k | (m^2 - n):\n"
             TERM_K " | (" TERM_M "^2 - " TERM_N ")\n",
             k.ToString().c_str(),
             m.ToString().c_str(),
             n.ToString().c_str());
    }

    // PERF GMP has a faster version when we know the remainder is zero
    BigInt new_a = (a * m + b) / BigInt::Abs(k);
    BigInt new_b = (b * m + n * a) / BigInt::Abs(k);
    BigInt new_k = (m * m - n) / k;


    if (VERBOSE) {
      printf("New triple (" TERM_A ", " TERM_B ", " TERM_K ")\n",
             new_a.ToString().c_str(),
             new_b.ToString().c_str(),
             new_k.ToString().c_str());
    }

    Sol new_sol = make_pair(std::move(new_a), std::move(new_b));
    if (CHECK_INVARIANTS) {
      CHECK(Error(n, new_sol) == new_k);
    }

    sol = std::move(new_sol);
    k = std::move(new_k);

    // CHECK(iters < 3);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Start.\n");

  // two equations
  // 222121 x^2 + 1 = y_a^2
  // 360721 x^2 + 1 = y_h^2
  // (y_a is called a in the square; y_h is called h. but we are going
  // to use a,b,c for other coefficients here.)

  const BigInt n_a = 222121_b;
  const BigInt n_h = 360721_b;

  printf("n_a: %s\n", n_a.ToString().c_str());
  printf("n_h: %s\n", n_h.ToString().c_str());

  /*
  const Sol sol_a0 =
    make_pair(8842189948832648717976105224334566231789600792803642348336846827782225806006855280976217289118615305107682054895297594374602589755796972496322910297363322405874424178328332781536562552307657505453833599478503983455352282819585114679974738910948956926775461863362701876119522363590230122779441071446571252735232045259462920927627602773872009296099583607374282385807997919609229375157099719827845573764419215460756307117338718705124111599350778355070690163390358271136546610163464791543734856676842230966901796548189022153685361283212100818504956402828918077049598784514157937329970721134766921855778777097018434984093865266237449423446137852415940785748186382900083801424749832395073802747373107275339458916177922982623150814648914210247907914320688212459625265454072461678943182520613760660711242184871092934051875746555178887920026779546814493124227951855861559722332256073075000637308442102258111126057878132592612075567003501660768306324641643851953872905233068817472148281418992759748342510080_b,
              4167298888890582095707942644915909558693314338697460802361027516877389482010683040734334685893379069518445325738888043591720130533090476155218905963353853138415389805842948191472397753805538136043353419743732439776651789419466216397061517616908164550469414914222796669598174932940417079930866612420109453717999380832147081416783219050603157443451274858295790645852469148942430369467476733682592406931237359920948068211055237872219528168235894378054900825814402581104144526649820275635441124896092749338898851010534461783770286817451522331727606529272875580972170116823257246119045270884996675133123042535167048097824065361852817095182387885561517191950666918569584524337401505215087336893548454054178142397694042797084464051758314470302325664351167823711913554602094558720922988601091194640067523138656436344755648112597091057443163587216452729560253513930017234722189432729945326245909311513386404346667972770519088392806077797566373789678779978979570023446023484159626315711113877814558678479667201_b);
  */
  const Sol sol_a0 =
    make_pair(96853990143729182446466254903787102920420186066745135266595909679523238492820905098594426380259780074224586852201498992449275127756378129855820172958584294747666448702649536007880590904630402994485054414777494200716316411350928848627022864018323664548200672490871343394924009459667481509707703598930232145417695483619768820532418380893489621163234820873999155059853985917579838562873820424199495555730452754055968329456327571681829182817764194271946670769735346243288252413916542521030793275016960_b,
              45647009151151305718708252078329478619159688396560344940484846739545404737863346364814575382380658468268696696162077346578703916050375366743990587977745934350267627720790556553334680620537062793215771183142869525305229082597324857232212169256203590974623367552076861467765584622698250583658967988921977292909237331892140561912612147974391374706186555174904116425103638903793323393015436354152507544138006929555916470598773073491241252241089212693998884441309720519067489536318638130578697131025203199_b);

  CHECK(Error(n_a, sol_a0) == 1_b);

  const Sol sol_h0 =
    make_pair(5254301467178253668063501573805488115701470062129148344048204657019748885380657942526435850589822056436907162208858662833748764291164063392849591565232182340896344102417561460498588341287763598974028219631368919894791141180699352614842162329575092178804714936187264001803984435149307802237910897195756701561689733829416441654000294332579338938600777476665373690729282455769460482322102437685615931720435571771362615173743048328285921760676802224631583102137529433447859249096920912083501391265263092551585295016322744236018859511648734714069848447775860243716078071973487185423849887186837594810420676451477817334917442425724234656945231269179119186700637627642532511974556138751986944397915640033290288664241890733655443663188699611507335767790934980909769231545567661414370796993132434555657909168192875390617801233376652983464964576564440125236085759750607806749224929539111304657072598148941302658707149037639442679980053501547520718161071834317467291327489266555022779213264638058690142715120_b,
              3155736260680638626215586515224779475000982538318510234796596948969542279640527333764830931669758997548264003306978761631985610163542280769957617791837115815855505466192424155483747195334026528686100467671456903838445918904622247229490490143150205748045220079365084818251874000588893203069462517365771614948335859034465574562253629688393366532513342848079469578491642318761744360133449340864599791431253295837903633977372300833275627599528361343594279999123827422243803042839623732782710017503150424671269804820044584384922764477211533947638524985602275604505195848101327992205477251957539278637282982381170957580513664760124760363535719750512488336024599849383281905125902008900085015792063893406749523426160808130369628761209934170648229913920676290078798350463180533834821461892201220681643823782682294011883387286116223752801467866509139817280697848323355423553433987427070952556267278422608907045802820393525018738409778215962756148887404031077236716404130655643413294151356469733756144336771201_b);

  BigInt err1 = Error(n_h, sol_h0);
  CHECK(err1 == 1_b) << err1.ToString();

  /*
  printf("sol_a0: %s\n", SolString(sol_a0).c_str());
  printf("sol_h0: %s\n", SolString(sol_h0).c_str());
  */

  // this method is basically trivial when k=1.
  // Sol bb = Bhaskara(n_a, BigInt{1}, sol_a0);
  // Sol bsol = Bhaskara(n_h, Error(n_h, sol_a0), sol_a0);
  // Sol bsol = Bhaskara(n_a, Error(n_a, sol_h0), sol_h0);

#if 0
  {
    BigInt n{2};
    Sol start_sol = make_pair(1_b, 8_b);
    Sol bsol = Bhaskara(n, Error(n, start_sol), start_sol);

    printf("Derived solution for n = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n, bsol) == BigInt{1});
  }
  return 0;


  {
    BigInt n{67};
    Sol start_sol = make_pair(1_b, 8_b);
    Sol bsol = Bhaskara(n, Error(n, start_sol), start_sol);

    printf("Derived solution for n = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n, bsol) == BigInt{1});
  }
  return 0;
#endif

  for (int ni = 2; ni < 150; ni++) {
    uint64_t s = Sqrt64(ni);
    // This algorithm doesn't work for perfect squares.
    if (s * s == ni)
      continue;

    BigInt n{ni};
    Sol start_sol = make_pair(1_b, 8_b);
    Sol bsol = Bhaskara(n, Error(n, start_sol), start_sol);

    printf("Derived solution for n = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n, bsol) == BigInt{1});
    printf(AGREEN("OK") "\n");
  }

  {
    Sol bsol = Bhaskara(n_a, Error(n_a, sol_h0), sol_h0);
    printf("Derived solution for n_a = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n_a.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n_a, bsol) == BigInt{1});
    printf(AGREEN("OK") "\n");
  }

  {
    Sol bsol = Bhaskara(n_h, Error(n_h, sol_a0), sol_a0);
    printf("Derived solution for n_h = " TERM_N "\n"
           "a: " TERM_A "\n"
           "b: " TERM_B "\n",
           n_h.ToString().c_str(),
           bsol.first.ToString().c_str(),
           bsol.second.ToString().c_str());
    CHECK(bsol.first != BigInt{0});
    CHECK(Error(n_h, bsol) == BigInt{1});
    printf(AGREEN("OK") "\n");
  }


  return 0;

  // First let's try using Brahmagupta's identity
  // Sol sol_a = sol_a0;
  // Sol sol_h = sol_h0;


  // Solutions we've found so far. We can get infinitely many,
  // so the goal is not to generate a large set here. We want
  // to find some (ax, ay) and (hx, hy) such that ax = hx.
  using SolSet = std::unordered_set<Sol, HashSol, EqSol>;
  SolSet asols, hsols;
  asols.insert(sol_a0);
  hsols.insert(sol_h0);

  printf("Loop...\n");

  // As an invariant, anything in asols is a solution for a;
  // and bsols for h.
  // n_a * x_a^2 + 1 = y_a^2
  // n_h * x_h^2 + 1 = y_h^2
  for (;;) {
    if (CHECK_INVARIANTS) {
      for (const Sol &sol_h : hsols) {
        BigInt err1 = Error(n_h, sol_h);
        CHECK(err1 == 1_b) << err1.ToString();
      }
      for (const Sol &sol_a : asols) {
        BigInt err2 = Error(n_a, sol_a);
        CHECK(err2 == 1_b) << err2.ToString();
      }
    }

    // Note: CombineSelf always generates a larger x.
    // We take one step for each set (symmetric).

    auto Step = [](const BigInt &n, SolSet *sols, SolSet *other) {
        // We'll combine two solutions. The heuristic here
        // is to take the step that brings us closest to
        // any x in the other solution set.

        // PERF: Can do inner loop in log time.
        auto Distance = [&other](const Sol &sol) {
            // metric is the smallest absolute difference between
            // the x coordinates of this solution anything in other.
            std::optional<BigInt> best_diff = nullopt;
            for (const Sol &osol : *other) {
              BigInt diff = BigInt::Abs(sol.first - osol.first);
              if (!best_diff.has_value() ||
                  diff < best_diff.value()) {
                if (diff == 0_b) {
                  printf("%s and %s\n",
                         SolString(sol).c_str(),
                         SolString(osol).c_str());
                }
                best_diff = {diff};
              }
            }
            CHECK(best_diff.has_value()) << "Because other is not empty";
            return best_diff.value();
          };

        std::optional<std::pair<BigInt, Sol>> best;
        auto Consider = [&sols, &other, &best, &Distance](const Sol &sol) {
            // Don't allow degenerate solutions.
            if (sol.first == 0_b)
              return;

            // And ignore solutions already present.
            if (sols->contains(sol))
              return;

            BigInt score = Distance(sol);
            if (!best.has_value() ||
                score < best.value().first) {
              best = {make_pair(score, sol)};
              int digits = (int)score.ToString().size();
              if (digits < 10) {
                printf("  New best diff: %s\n", score.ToString().c_str());
              } else {
                printf("  New best with %d-digit diff\n", digits);
              }
            }
          };

        printf("Step %s (%d sols):\n", n.ToString().c_str(),
               (int)sols->size());
        for (const Sol &sol1 : *sols) {
          for (const Sol &sol2 : *sols) {
            const auto &[s0, s1] = Combine(n, sol1, sol2);
            Consider(s0);
            Consider(s1);
          }
        }

        CHECK(best.has_value()) << "Because matrix is non-empty";
        CHECK(Error(n, best.value().second) == 1_b);
        sols->insert(best.value().second);
      };

    Step(n_a, &asols, &hsols);
    Step(n_h, &hsols, &asols);
  }

  printf(AGREEN("OK") " :)\n");
  return 0;
}
