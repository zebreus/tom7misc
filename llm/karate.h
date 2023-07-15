#include <string>
#include <optional>
#include <utility>

// Karate.
// Two fighters and a judge.
// Players move simultaneously.

// Alternating phases; players simultaneously say something out loud

// Red says: "I will avenge my father!"
// Blue says: "You are weak."
// (or maybe also * Red smiles slyly)

// And then make a structured move:

// Red moves backwards.
// Blue blocks high.

// The structured moves are scored internally according to intuitive rules.
// The judge sees the entire (public) transcript and awards style points.

// Kumite !  Kumite !

struct Karate {
  Karate(const std::string &fighter1, const std::string &fighter2);

  // Move parsing and storage is not going to be the bottleneck here, so we
  // store moves as strings, and then have functions for extracting info
  // from them.

  /*
  enum MoveType {
    BLOCK,
    PUNCH,
    KICK,
    MOVE,
    JUMP,
    HADOUKEN,
  };
  */

  enum Dir {
    LEFT,
    RIGHT,
    NEUTRAL,
  };

  enum Aim {
    HIGH,
    MEDIUM,
    LOW,
  };

  // Maybe a misnomer since it includes blocks...
  enum Attack {
    BLOCK,
    PUNCH,
    KICK,
  };

  std::optional<Aim> GetBlock(const std::string &line);
  std::optional<Aim> GetPunch(const std::string &line);
  std::optional<Aim> GetKick(const std::string &line);
  std::optional<std::pair<Attack, Aim>> GetAttack(const std::string &line);
  std::optional<Dir> GetMove(const std::string &line);
  std::optional<Dir> GetJump(const std::string &line);
  bool GetHadouken(const std::string &line);

  std::string dir_regex = "(LEFT|NEUTRAL|RIGHT)";
  std::string aim_regex = "(HIGH|MEDIUM|LOW)";
  // All legal moves.
  std::string move_regex =
    "("
    "((BLOCK|PUNCH|KICK) " + aim_regex + ")|"
    "((MOVE|JUMP) " + dir_regex + ")|"
    "(HADOUKEN)"
    ")";

  std::string remark_regex =
    "(SAY \"[^\"\n]*\")";

  std::string GetStatus();

  struct FighterPic {
    bool in_air = false;
    int x = 0;
  };

  // This is purely presentational; the canonical state is in the Karate
  // object itself.
  struct ProcessResult {
    FighterPic f1, f2;
    // Plain text explanation of what happened, if interesting.
    std::string message;
    bool clash = false;
    bool reset = false;
  };

  ProcessResult Process(const std::string &say1, const std::string &say2,
                        const std::string &do1, const std::string &do2);

  const std::string &Fighter1() const { return fighter1; }
  const std::string &Fighter2() const { return fighter2; }


  const std::string PlayerPicture() const {
    //  o
    // /|\
    //  |
    // / \
    //
    return "TODO";
  }
  const std::string Visualize() const {
    return "TODO";
  }

private:
  // Reset after clash, ringout, etc.
  void Reset();

  // fighter names
  std::string fighter1, fighter2;
  int score1 = 0, score2 = 0;
  // Starting marks.
  int x1 = 1, x2 = 3;
  // If in the air, the direction of the jump.
  std::optional<Dir> jump1, jump2;
  // True if the last action was a successful parry for that fighter.
  bool parry1 = false, parry2 = false;
};


