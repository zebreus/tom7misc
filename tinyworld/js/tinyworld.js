
// XXX clean up
var DEBUG = false; // true;

var MINFRAMEMS = 15.0;

const FONTW = 18;
const FONTH = 32;

const SFONTW = 9;
const SFONTH = 16;

const TILESW = 40;
const TILESH = 25;

const WIDTH = TILESW * SFONTW;
const HEIGHT = TILESH * SFONTH;


let frames = 0;

function Init() {
  bc = new BigCanvas(WIDTH, HEIGHT, 2);
}

function Draw() {
  let x = (frames * 31337) % WIDTH;
  let y = (frames ^ 0xBEEF) % HEIGHT;
  // let c = ((frames * 67 + 0x1234567) & 0xFFFFFF) | 0xFF000000;
  let c = (frames * 67) & 0xFFFFFF;

  bc.ctx.fillStyle = "#" + (c % 16).toString(16) +
      ((c >> 4) % 16).toString(16) + ((c >> 9) % 16).toString(16);
  bc.ctx.fillRect(0, 0, WIDTH, HEIGHT);

  bc.Flip();
}

let last = 0;
let skipped = 0;
function Step(time) {
  // Throttle to 30 fps or something we
  // should be able to hit on most platforms.
  // Word has it that 'time' may not be supported on Safari, so
  // compute our own. (TODO: use performance.now probably)
  let now = (new Date()).getTime();
  let diff = now - last;
  // debug.innerHTML = diff;
  // Don't do more than 30fps.
  if (diff < MINFRAMEMS) {
    skipped++;
    window.requestAnimationFrame(Step);
    return;
  }
  last = now;

  frames++;
  if (frames > 1000000) frames = 0;

  Draw();

  if (false && DEBUG) {
    counter++;
    let sec = ((new Date()).getTime() - start_time) / 1000;
    document.getElementById('counter').innerHTML =
        'skipped ' + skipped + ' drew ' +
        counter + ' (' + (counter / sec).toFixed(2) + ' fps)';
  }

  // And continue the loop...
  window.requestAnimationFrame(Step);
}

function Start() {
  Init();

  window.requestAnimationFrame(Step);
}

let holdingShift = false,
  holdingLeft = false, holdingRight = false,
  holdingUp = false, holdingDown = false,
  holdingSpace = false, holdingEnter = false,
  holdingX = false, holdingZ = false,
  holdingPlus = false, holdingMinus = false;

document.onkeydown = function(e) {
  e = e || window.event;
  if (e.ctrlKey) return true;

  switch (e.keyCode) {
    case 9:  // tab?
    // CHEATS
    // if (true || DEBUG)
    // textpages = [];
    // XXX CHEAT
    SpawnBlock();

    break;
    case 27: // ESC
    if (true || DEBUG) {
      document.body.innerHTML =
	  '<b style="color:#fff;font-size:40px">(SILENCED. ' +
          'RELOAD TO PLAY)</b>';
      Step = function() { };
      // n.b. javascript keeps running...
    }
    case 8: // BACKSPACE
    if (DEBUG) {
      gang = null;
    }
    break;
    case 37: // LEFT
    holdingLeft = true;
    break;
    case 38: // UP
    holdingUp = true;
    break;
    case 39: // RIGHT
    holdingRight = true;
    break;
    case 40: // DOWN
    holdingDown = true;
    break;
    case 32: // SPACE
    holdingSpace = true;
    break;
    case 13: // ENTER
    holdingEnter = true;
    break;
    case 90: // Z
    holdingZ = true;
    break;
    case 88: // X
    holdingX = true;
    break;
    case 187: // +/=
    holdingPlus = true;
    break;
    case 189: // -/_
    holdingMinus = true;
    break;
    case 16: // shift
    holdingShift = true;
    break;
    default:
    // console.log(e.keyCode);
  }
  // let elt = document.getElementById('key');
  // elt && (elt.innerHTML = 'key: ' + e.keyCode);
  return false;
}

document.onkeyup = function(e) {
  e = e || window.event;
  if (e.ctrlKey) return true;

  switch (e.keyCode) {
    case 37: // LEFT
    holdingLeft = false;
    break;
    case 38: // UP
    holdingUp = false;
    break;
    case 39: // RIGHT
    holdingRight = false;
    break;
    case 40: // DOWN
    holdingDown = false;
    break;
    case 32: // SPACE
    holdingSpace = false;
    break;
    case 13: // ENTER
    holdingEnter = false;
    break;
    case 90: // Z
    holdingZ = false;
    break;
    case 88: // X
    holdingX = false;
    break;
    case 187: // +/=
    holdingPlus = false;
    break;
    case 189: // -/_
    holdingMinus = false;
    break;
    case 16:
    holdingShift = false;
    break;
  }
  return false;
}


