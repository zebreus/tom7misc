/* The whole game.

   We embed everything as a single movie clip in order to 
   support the "zooming" behavior. 
*/
class Everything extends MovieClip {

  var STARTWIDTH;

  var man;
  var chicken;
  var bigmessage;

  // indexed by textual name. initialized
  // from dynamic text on stage.
  var verts = [];

  // Array of pairs (arrays) of vertices.
  // Symmetry implied.
  var edges = 
    [['e', 'a'],
     ['f', 'b'],

     ['g', 'a'],
     ['a', 'b'],
     ['b', 'c'],
     ['c', 'd'],
     
     ['a', 'i'],
     ['c', 'k'],
     
     ['h', 'i'],
     ['i', 'j'],
     ['j', 'k'],
     ['k', 'l'],
     
     ['h', 'aa'],
     ['i', 't'],
     ['j', 'q'],
     ['k', 'p'],
     ['l', 'm'],
     
     ['t', 's'],
     ['r', 'q'],
     ['r', 's'],

     ['t', 'u'],
     ['s', 'v'],
     ['q', 'ff'],
     ['ff', 'p'],
     ['ff', 'ee'],
     ['ee', 'x'],
     ['v', 'w'],
     ['u', 'uz'],
     ['uz', 'z'],
     ['x', 'y'],
     ['o', 'on'],
     ['on', 'n'],
     
     ['u', 'v'],
     ['w', 'x'],
     ['ee', 'o'],
     ['y', 'n'],
     ['n', 'm'],
     ['z', 'y'],
     ['z', 'cc'],
     ['n', 'gg'],
     ['aa', 'z'],
     ['bb', 'cc'],
     ['cc', 'dd'],
     ['dd', 'gg'],
     ['gg', 'hh'],
     
     ['dd', 'ii'],
     ['cc', 'jj']];

  var boxes = [];
  var foods = [];

  public function onLoad () {
    this.stop();
    STARTWIDTH = this._width;

    for (var o in this) {
      if (this[o].text != undefined) {
	  /* then it's a text area... */
	if (false)
	trace("" +
	      this[o]._x + "," +
	      this[o]._y + ": " +
	      this[o].text);
	verts[this[o].text] =
	  { x : this[o]._x,
	    y : this[o]._y };
	this[o]._visible = false;
      } else if (o.substr(0, 4) === 'zone') {
	boxes.push({x : this[o]._x,
		    y : this[o]._y,
		    w : this[o]._width,
		    h : this[o]._height,
		    active : true,
		    o : this['zone' + o.substr(4, o.length - 4)]});
	this[o]._visible = false;
      } else if (o.substr(0, 9) === 'objective') {
	foods.push({x : this[o]._x,
		    y : this[o]._y,
		    w : this[o]._width,
		    h : this[o]._height,
		    o : this[o]});
      }
    }

    man.edges = edges;
    man.verts = verts;

    chicken.edges = edges;
    chicken.verts = verts;

    // put at start position...
    // this['man'].warpto('a');
    man.warpto('a');
    chicken.warpto('ee');

    bigmessage = _root['bigmessage'];
    bigmessage.say('');
  }

  public function zoomtobox() {
    // Is man in box? Then zoom to box.
    for(var i = 0; i < boxes.length; i++) {
      if (boxes[i].active &&
	  man._x >= boxes[i].x &&
	  man._y >= boxes[i].y &&
	  man._x <= (boxes[i].x + boxes[i].w) &&
	  man._y <= (boxes[i].y + boxes[i].h)) {

	// Chicken, go to box
	//	trace(boxes[i].o.chickento);
	if (!chicken.mad()) {
	  chicken.state.dst = boxes[i].o.chickento;
	}

	// Make zoom closer to box.
	var tx = -boxes[i].x;
	var ty = -boxes[i].y;

	var tz = STARTWIDTH / boxes[i].w;
	// trace('' + STARTWIDTH + '/' + boxes[i].w + ' = ' + tz);

	// XXX pan incrementally...
	this._x = tx * tz;
	this._y = ty * tz;
	// scale uniformly.
	this._yscale = this._xscale = 100.0 * tz;
	return;
      } else {
	// If not in the box and food is eaten, delete box.
	// trace('objective' + i + ' ' + this['objective' + i]);
	if (this['objective' + (i + 1)].eaten) {
	  // trace('make inactive');
	  boxes[i].active = false;
	}
      }
    }

    // Otherwise zoom to normal.
    this._x = 0;
    this._y = 0;
    this._yscale = this._xscale = 100.0;
  }

  public function possiblyeat() {
    // Is man in foods? Then eat foods.
    for(var i = 0; i < foods.length; i++) {
      if (!foods[i].eaten &&
	  man._x >= foods[i].x &&
	  man._y >= foods[i].y &&
	  man._x <= (foods[i].x + foods[i].w) &&
	  man._y <= (foods[i].y + foods[i].h)) {
	foods[i].o._visible = false;
	foods[i].eaten = true;
	// trace('' + foods[i].o.serverok + '... ' + man.ok);
	if (foods[i].o.serverok === true &&
	    man.ok === true ||
	    foods[i].o.serverok === false &&
	    man.ok === false) {
	  // hooray!
	  // XXX zoom to center box to show
	  // status?
	  // XXX score up?
	  // XXX play sound
	  bigmessage.say('Correct: ' + foods[i].o.msg);
	} else {
	  // XXX panic
	  // XXX play sound
	  bigmessage.complain(foods[i].o.msg);
	  chicken.activate();
	}
      }
    }
  }

  public function onEnterFrame() {
    zoomtobox();
    possiblyeat();
  }
}
