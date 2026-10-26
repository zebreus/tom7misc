
#include "level.h"

#include <cstdlib>
#include <memory>
#include <string>

#include "base/logging.h"
#include "base/print.h"
#include "level-base.h"

static void CheckLevelsEqual(const Level &a, const Level &b) {
  CHECK_EQ(a.title, b.title);
  CHECK_EQ(a.author, b.author);
  CHECK_EQ(a.w, b.w);
  CHECK_EQ(a.h, b.h);
  CHECK_EQ(a.guyx, b.guyx);
  CHECK_EQ(a.guyy, b.guyy);
  CHECK_EQ(a.IsCorrupted(), b.IsCorrupted());

  for (int i = 0; i < a.w * a.h; i++) {
    CHECK_EQ(a.tiles[i], b.tiles[i]);
    CHECK_EQ(a.otiles[i], b.otiles[i]);
    CHECK_EQ(a.dests[i], b.dests[i]);
    CHECK_EQ(a.flags[i], b.flags[i]);
  }

  CHECK_EQ(a.nbots, b.nbots);
  for (int i = 0; i < a.nbots; i++) {
    CHECK_EQ(a.boti[i], b.boti[i]);
    CHECK_EQ(a.bott[i], b.bott[i]);
  }
}

static void TestRoundTrip(const Level &level) {
  // Serialize, deserialize; check equality and other basic properties (e.g. not broken).
  // Just use CHECK to assert.
  CHECK(!level.IsCorrupted());
  string s = level.ToString();
  CHECK(!s.empty());

  std::unique_ptr<Level> decoded = Level::FromString(s, false);
  CHECK(decoded != nullptr);
  CHECK(!decoded->IsCorrupted());
  CheckLevelsEqual(level, *decoded);

  // Also verify Clone() produces an identical level.
  std::unique_ptr<Level> cloned = level.Clone();
  CHECK(cloned != nullptr);
  CheckLevelsEqual(level, *cloned);
  CHECK_EQ(s, decoded->ToString());
  CHECK_EQ(s, cloned->ToString());
}

static void TestSerialization() {
  // Construct various Levels, and use TestRoundTrip on them.
  {
    std::unique_ptr<Level> blank = Level::Blank(10, 8);
    blank->title = "Blank Level";
    blank->author = "Test Author";
    TestRoundTrip(*blank);
  }

  {
    std::unique_ptr<Level> def = Level::DefBoard(18, 10);
    def->title = "Default Board";
    def->author = "Someone";
    def->guyx = 3;
    def->guyy = 4;
    def->SetTile(5, 5, T_EXIT);
    def->SetTile(6, 5, T_LASER);
    def->SetTile(2, 2, T_PANEL);
    def->SetFlag(2, 2, TF_HASPANEL);
    def->SetDestk(2, 2, 7, 7);
    def->SetOTile(7, 7, T_GOLD);

    def->nbots = 2;
    def->boti = (int *)malloc(2 * sizeof(int));
    def->bott = (bot *)malloc(2 * sizeof(bot));
    def->botd = (dir *)malloc(2 * sizeof(dir));
    def->bota = (int *)malloc(2 * sizeof(int));
    def->boti[0] = def->Index(4, 4);
    def->bott[0] = B_DALEK;
    def->botd[0] = DIR_DOWN;
    def->bota[0] = -1;
    def->boti[1] = def->Index(8, 2);
    def->bott[1] = (bot)(B_BOMB_0 + 5);
    def->botd[1] = DIR_DOWN;
    def->bota[1] = -1;

    CHECK(def->Sanitize());
    TestRoundTrip(*def);
  }

  // Test invalid strings.
  CHECK(Level::FromString("not a level") == nullptr);
  CHECK(Level::FromString("ESXL") == nullptr);
}

static void TestBasic() {
  // Test basic access to tiles, flags, entities, etc.
  CHECK_EQ(TurnLeft(DIR_UP), DIR_LEFT);
  CHECK_EQ(TurnRight(DIR_UP), DIR_RIGHT);
  CHECK_EQ(DirReverse(DIR_LEFT), DIR_RIGHT);
  CHECK_EQ(TurnLeft(TurnRight(DIR_DOWN)), DIR_DOWN);

  std::unique_ptr<Level> lev = Level::DefBoard(6, 5);
  CHECK_EQ(lev->w, 6);
  CHECK_EQ(lev->h, 5);
  CHECK_EQ(lev->TileAt(0, 0), T_BLUE);
  CHECK_EQ(lev->TileAt(1, 1), T_FLOOR);
  CHECK(lev->PlayerAt(1, 1));
  CHECK(!lev->PlayerAt(2, 1));
  CHECK(!lev->IsWon());

  // Coordinates and indices.
  int x = 0, y = 0;
  int idx = lev->Index(4, 3);
  lev->Where(idx, x, y);
  CHECK_EQ(x, 4);
  CHECK_EQ(y, 3);

  // Tiles, otiles, flags, and destinations.
  lev->SetTile(2, 2, T_RPANEL);
  lev->SetOTile(2, 2, T_BPANEL);
  lev->SetFlag(2, 2,
               TF_HASPANEL | TF_RPANELH | TF_RPANELL |
               TF_OPANEL | TF_ROPANELL);
  lev->SetDestk(2, 2, 4, 3);
  int dx = 0, dy = 0;
  lev->GetDest(2, 2, dx, dy);
  CHECK_EQ(dx, 4);
  CHECK_EQ(dy, 3);
  CHECK_EQ(lev->DestAt(2, 2), lev->Index(4, 3));

  // SwapO swaps tiles and panel flag refinements.
  lev->SwapO(lev->Index(2, 2));
  CHECK_EQ(lev->TileAt(2, 2), T_BPANEL);
  CHECK_EQ(lev->OTileAt(2, 2), T_RPANEL);
  CHECK(lev->Sanitize());

  // Travel and simple movement to exit.
  int nx = 0, ny = 0;
  CHECK(!lev->Travel(0, 0, DIR_UP, nx, ny));
  CHECK(!lev->Travel(0, 0, DIR_LEFT, nx, ny));
  CHECK(lev->Travel(1, 1, DIR_RIGHT, nx, ny));
  CHECK_EQ(nx, 2);
  CHECK_EQ(ny, 1);

  lev->SetTile(2, 1, T_EXIT);
  CHECK(lev->Move(DIR_RIGHT));
  CHECK(lev->PlayerAt(2, 1));
  CHECK(lev->IsWon());

  // Resize.
  lev->Resize(8, 7);
  CHECK_EQ(lev->w, 8);
  CHECK_EQ(lev->h, 7);
  CHECK_EQ(lev->TileAt(2, 1), T_EXIT);
}

static void TestSanitize() {
  std::unique_ptr<Level> lev = Level::DefBoard(6, 5);
  CHECK(lev->Sanitize());

  // Out-of-range tiles (too large and negative).
  lev->SetTile(1, 1, NUM_TILES + 5);
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->TileAt(1, 1), T_BLACK);
  CHECK(lev->Sanitize());

  lev->SetTile(1, 1, -1);
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->TileAt(1, 1), T_BLACK);
  CHECK(lev->Sanitize());

  // Out-of-range otiles (too large and negative).
  lev->SetOTile(2, 2, NUM_TILES);
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->OTileAt(2, 2), T_BLACK);
  CHECK(lev->Sanitize());

  lev->SetOTile(2, 2, -1);
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->OTileAt(2, 2), T_BLACK);
  CHECK(lev->Sanitize());

  // Out-of-range destinations.
  lev->dests[0] = -1;
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->dests[0], 0);

  lev->dests[0] = lev->w * lev->h;
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->dests[0], 0);

  // Mismatched panel flags.
  lev->SetTile(3, 3, T_GPANEL);
  lev->SetFlag(3, 3, TF_NONE);
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->FlagAt(3, 3), TF_HASPANEL | TF_RPANELH);
  CHECK(lev->Sanitize());

  // Player out of bounds.
  lev->guyx = -5;
  lev->guyy = 100;
  CHECK(!lev->Sanitize());
  CHECK_EQ(lev->guyx, 0);
  CHECK_EQ(lev->guyy, lev->h - 1);
  CHECK(lev->Sanitize());
}

int main(int argc, char **argv) {

  TestBasic();
  TestSerialization();
  TestSanitize();

  Print("OK\n");
  return 0;
}
