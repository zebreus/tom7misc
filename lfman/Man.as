/* Any character that lives on the grid,
   like the player and the chicken.
*/
class Man extends MovieClip {

  public var edges;
  public var verts;

  // always has .src being the vertex we're
  // coming from, or the vertex we're stopped
  // at. May also have dst if we're moving
  // towards a vertex.
  public var state = {};
  public function warpto(pt) {
    state.src = pt;
    state.dst = undefined;
    this._x = verts[pt].x;
    this._y = verts[pt].y;
  }

  public function normalizeOrtho(dx, dy) {
    if (Math.abs(dx) > Math.abs(dy)) {
      // dx is the major axis,
      return { x : Math.abs(dx) / dx, y : 0 };
    } else {
      return { x : 0, y : Math.abs(dy) / dy };
    }
  }

  public function normalize(dx, dy) {
    var len = Math.sqrt(dx * dx + dy * dy);
    return { x : dx / len, y : dy / len };
  }

  // If we're at s, and want to go in dx/dy,
  // is d in the right direction?
  public function compatible(s, d, dx, dy) {
    // get vector
    var v = normalizeOrtho(verts[d].x - verts[s].x,
			   verts[d].y - verts[s].y);
    // trace('norm: ' + v.x + ',' + v.y);
    return (v.x == dx && v.y == dy);
  }

  // If stationary, find a destination and go
  // there, if we can.
  public function move(s, dx, dy) {
    for(var i = 0; i < edges.length; i++) {
      var d = undefined;
      if (edges[i][0] == s) d = edges[i][1];
      else if (edges[i][1] == s) d = edges[i][0];
      
      if (d != undefined) {
	// This is a destination for the node
	// we're on. If it's in the correct
	// direction, go to it.
	// trace(s + ' -> ' + d);
	if (compatible(s, d, dx, dy)) {
	  // trace('compat!');
	  state.dst = d;
	  // warpto(d);
	}
      }
    }
  }

  public function updatePosition(speed) {
    // Move towards destination, if we have one.
    if (state.dst !== undefined) {
      var v = normalize(verts[state.dst].x - 
			verts[state.src].x,
			verts[state.dst].y - 
			verts[state.src].y);
      // If moving more towards the desination
      // would put it on the other side of the
      // destination, then warp directly and
      // stop moving.
      var tx = this._x + (v.x * speed);
      var ty = this._y + (v.y * speed);
      var tv = normalize(verts[state.dst].x - tx,
			 verts[state.dst].y - ty);
      if (false)
      trace('at ' + this._x + ',' + this._y +
	    ' going ' + v.x + ','  + v.y +
	    ' consider ' + tx + ',' + ty);

      // If the proposed intermediate destination does
      // not have the same direction, warp.
      if (samedir(v, tv)) {
	// trace('go ' + tx + ',' + ty);
	this._x = tx;
	this._y = ty;
      } else {
	// trace('warp ' + state.dst);
	warpto(state.dst);
      }
    }
  }

  public function samedir(v, vv) {
    if(false)
    trace('sd: ' + v.x + ',' + v.y +
	  '  ' + vv.x + ',' + vv.y);
    if (v.x > 0 && vv.x < 0) return false;
    if (v.x < 0 && vv.x > 0) return false;
   
    if (v.y > 0 && vv.y < 0) return false;
    if (v.y < 0 && vv.y > 0) return false;

    return true;
  }

}
