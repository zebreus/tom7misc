
#include <cstdlib>
#include <memory>

#include "aevent.h"
#include "disamb.h"
#include "level-base.h"
#include "level.h"
#include "ptrlist.h"

using namespace std;

using AList = PtrList<AEvent>;

/* disambiguation context implementation */
Disamb *Disamb::Create(const Level *l) {
  std::unique_ptr<Disamb> d{new Disamb};
  d->w = l->w;
  d->h = l->h;
  d->serials = (unsigned int *)malloc(d->w * d->h * sizeof(unsigned int));
  if (!d->serials) return nullptr;
  d->Clear();
  return d.release();
}

Disamb::~Disamb() {
  free(serials);
}

void Disamb::Clear() {
  serial = 1;
  player = 0;
  for (int i = 0; i < LEVEL_MAX_ROBOTS; i++) {
    bots[i] = 0;
  }

  for (int i = 0; i < (w * h); i++) {
    serials[i] = 0;
  }
}

bool Disamb::Affect(int x, int y, Level *l, PtrList<AEvent> **& etail) {
  return Affecti(y * w + x, l, etail);
}

bool Disamb::Affecti(int i, Level *l, PtrList<AEvent> **& etail) {
  /* was this last updated in this same
     serial? if so, we need to move to
     the next serial. */

  if (serials[i] == serial) {
    SerialUp(l, etail);
    serials[i] = serial;
    return true;
  } else {
    serials[i] = serial;
    return false;
  }
}

#define DIS_PUSHEVENT(type, var)                \
  AEvent *a ## var = new AEvent;                \
  *etail = new AList(a ## var, nullptr);        \
  etail = &((*etail)->next);                    \
  a ## var->serial = serial;                    \
  a ## var->t = tag_ ## type;                   \
  type ## _t *var = & (a ## var->u. type);

/* maintain the invariant that in every serial within the list
   etail, there is at least one animation for every active
   entity */
void Disamb::SerialUp(Level *l, PtrList<AEvent> **& etail) {
  // Print("serialup(old serial = {})!\n", serial);

  if (serial != player) {
    // Print("  {} (stand player @ {}/{})\n", player, l->guyx, l->guyy);
    {
      DIS_PUSHEVENT(stand, e);
      e->x = l->guyx;
      e->y = l->guyy;
      e->d = l->guyd;
      e->data = 0;
      e->entt = B_PLAYER;
    }
    player = serial;
  }

  for (int i = 0; i < l->nbots; i++) {
    if (l->bott[i] != B_DELETED &&
        l->bott[i] != B_BOMB_X &&
        bots[i] != serial) {
      {
        DIS_PUSHEVENT(stand, e);
        l->Where(l->boti[i], e->x, e->y);
        // Print("  {} stand bot {} (@ {}/{})\n", bots[i], i, e->x, e->y);
        e->d = l->botd[i];
        e->entt = l->bott[i];
        e->data = l->bota[i];
      }

      bots[i] = serial;
    }
  }

  /* now we can increment the serial */
  serial++;
}

/* XXX should these also do the same check that affecti does? */
void Disamb::PreAffectPlayer(Level *l, PtrList<AEvent> **& etail) {
  if (player == serial) SerialUp(l, etail);
}

void Disamb::PostAffectPlayer() {
  player = serial;
}

void Disamb::PreAffectBot(int i, Level *l, PtrList<AEvent> **& etail) {
  if (bots[i] == serial) SerialUp(l, etail);
}

void Disamb::PostAffectBot(int i) {
  bots[i] = serial;
}
