/* An enemy character.

   XXX inherit from Man with LFMan...
*/
class Nemesis extends MovieClip {

#include "man.h"

  public var chicken = true;

  public var SPEED = 20.0;
  public var ismad = false;
  public var theman;
  public var headstart;

  public function onLoad () {
    this.gotoAndStop(1);
    theman = _root['everything']['man'];
  }

  public function onEnterFrame() {
    this._rotation ++;
    if (ismad) {
      if (headstart > 0) {
	headstart--;
      } else {

	// try to intercept player.
	var dir = normalizeOrtho(theman._x - this._x,
				 theman._y - this._y);
	if (false)
	trace('' + 
	      this._x + ' ' + theman._x + ' ?? ' +
	      (this._x - theman._x) + ',' +
	      (this._y - theman._y) + ' -> ' +
	      dir.x + ' ' + dir.y);
	move(state.src, dir.x, dir.y);
      }
    }
    updatePosition(SPEED);
  }

  public function mad() {
    return ismad;
  }

  public function activate() {
    ismad = true;
    headstart = 100;
    this.gotoAndStop(2);
  }

}
