/* The player's character.

   Since this is the user's interface into the
   game (keys), most of the logic takes place
   here.
*/
class LFMan extends MovieClip {

#include "man.h"

  public var chicken = false;

  public var SPEED = 18.0;
  public var signtext;

  public var ok = true;
  public function setok(b) {
    // trace(b);
    ok = b;
    // XXX grow/shrink to fit
    signtext.text = b?"SERVER OK":"ABORT";
  }

  /* startup stuff */
  public function onLoad () {
    Key.addListener(this);
    this.stop();
  }

  public function onKeyDown() {
    var k = Key.getCode();
    if (state.dst == undefined) // XXX
    switch(k) {
    case 38: /* up */
      this._rotation = 270;
      move(state.src, 0, -1);
      break;
    case 37: /* left */
      this._rotation = 0;
      move(state.src, -1, 0);
      break;
    case 39: /* right */
      this._rotation = 0;
      move(state.src, 1, 0);
      break;
    case 40: /* down */
      this._rotation = 90;
      move(state.src, 0, 1);
      break;
    case 49: /* 1 */
      setok(true);
      break;
    case 50: /* 2 */
      setok(false);
      break;
    }
  }

  public function onEnterFrame() {
    updatePosition(SPEED);
  }

}
