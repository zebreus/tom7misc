
// New unattached canvas.
function NewCanvas(w, h) {
  let c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return c;
}

// Not cheap! Has to draw it to a canvas...
function Buf32FromImage(img) {
  let c = NewCanvas(img.width, img.height);
  let cc = c.getContext('2d');
  cc.drawImage(img, 0, 0);

  return new Uint32Array(
    cc.getImageData(0, 0, img.width, img.height).data.buffer);
}

// Chop out a part of an image and return it as a canvas. Used for example
// to chop out the letters in a font, but often useful for spriting too.
function ExtractCanvas(img, x, y, w, h) {
  let px32 = Buf32FromImage(img);
  return ExtractBuf32(px32, x, y, w, h);
}

// Extract from a Uint32Array with known width, in case you can preprocess
// with ExtractBuf32 and call this many times. Otherwise, ExtractCanvas is
// probably what you want.
function ExtractBuf32(px32, src_width, x, y, w, h) {
  let c = NewCanvas(w, h);

  let ctx = c.getContext('2d');
  let id = ctx.createImageData(w, h);
  let buf = new ArrayBuffer(id.data.length);
  let buf8 = new Uint8ClampedArray(buf);
  let buf32 = new Uint32Array(buf);

  // copy pixels from source image
  for (let yy = 0; yy < h; yy++) {
    for (let xx = 0; xx < w; xx++) {
      let p = px32[(yy + y) * src_width + xx + x];
      buf32[yy * w + xx] = p;
    }
  }

  id.data.set(buf8);
  ctx.putImageData(id, 0, 0);
  return c;
}

// Create a copy of the context/image, flipping it horizontally
// (rightward facing arrow becomes leftward).
function FlipHoriz(img) {
  let i32 = Buf32FromImage(img);
  let c = NewCanvas(img.width, img.height);
  let ctx = c.getContext('2d');
  let id = ctx.createImageData(img.width, img.height);
  let buf = new ArrayBuffer(id.data.length);
  let buf8 = new Uint8ClampedArray(buf);
  let buf32 = new Uint32Array(buf);

  for (let y = 0; y < img.height; y++) {
    for (let x = 0; x < img.width; x++) {
      buf32[y * img.width + x] =
	  i32[y * img.width + (img.width - 1 - x)];
    }
  }

  id.data.set(buf8);
  ctx.putImageData(id, 0, 0);
  return c;
}

// TODO: FlipVert, RotateCW

// Create a copy of the context/image, rotating it counter-clockwise
// (rightward facing arrow becomes upward).
function RotateCCW(img) {
  let i32 = Buf32FromImage(img);
  let c = NewCanvas(img.width, img.height);
  let ctx = c.getContext('2d');
  let id = ctx.createImageData(img.width, img.height);
  let buf = new ArrayBuffer(id.data.length);
  let buf8 = new Uint8ClampedArray(buf);
  let buf32 = new Uint32Array(buf);

  for (let y = 0; y < img.height; y++) {
    for (let x = 0; x < img.width; x++) {
      let dx = y;
      let dy = img.width - x - 1;
      buf32[dy * img.width + dx] = i32[y * img.width + x];
    }
  }

  id.data.set(buf8);
  ctx.putImageData(id, 0, 0);
  return c;
}

// img must already be loaded.
function Font(img, w, h, overlap, fontchars) {
  this.width = w;
  this.height = h;
  this.overlap = overlap;

  this.chars = {};
  let px32 = Buf32FromImage(img);
  let srcw = img.width;
  for (let i = 0; i < fontchars.length; i++) {
    let ch = fontchars.charCodeAt(i);
    this.chars[ch] = ExtractBuf32(px32, srcw, i * w, 0, w, h);
  }

  this.Draw = function(ctx, x, y, s) {
    let xx = x;
    for (let i = 0; i < s.length; i++) {
      let ch = s.charCodeAt(i);
      if (ch == 10) {
	    xx = x;
	    y += this.height - 1;
      } else {
	    this.chars[ch] &&
	        ctx.drawImage(this.chars[ch], xx, y);
	    xx += this.width - this.overlap;
      }
    }
  };
}

// n.b. not actually needed by BigCanvas, since it takes arg
/*
let ctx = canvas.getContext('2d');
let id = ctx.createImageData(WIDTH, HEIGHT);
let buf = new ArrayBuffer(id.data.length);
// Make two aliases of the data, the second allowing us
// to write 32-bit pixels.
let buf8 = new Uint8ClampedArray(buf);
let buf32 = new Uint32Array(buf);
*/

// BigCanvas is a 2D array of pixels (backing buffer), with everything
// set up to scale by an integer factor into a browser canvas element
// for display.
let BigCanvas = function(width, height, px) {
  this.width = width;
  this.height = height;
  // initialized below
  this.px = null;

  // this is the source canvas, which you can write to. width x height.
  this.canvas = NewCanvas(this.width, this.height);
  this.ctx = this.canvas.getContext('2d');
/*
  this.id = this.ctx.createImageData(this.width, this.height);
  this.buf = new ArrayBuffer(this.id.data.length);
  // Necessary?
  this.buf8 = new Uint8ClampedArray(this.buf);

  // Usually, access this.
  this.buf32 = new Uint32Array(this.buf);
*/

  this.Clear = (c = 0xFF000000) => {
/*
    for (let i = 0; i * this.width * this.height; i++) {
      this.buf32[i] = c;
    }
*/
  };

  // the 'big' versions should be treated as private.
  this.big_id = null;
  this.big_buf = null;
  this.big_buf8 = null;
  this.big_buf32 = null;

  // this is the output element.
  // XXX should be arg?
  this.big_canvas =
      (() => {
	    let c = document.getElementById('bigcanvas');
	    if (!c) {
	      c = document.createElement('canvas');
	      document.body.appendChild(c);
	      c.id = 'bigcanvas';
	    }
	    c.width = width * px;
	    c.height = height * px;
        // XXX
	    c.style.border = '1px solid black';
	    return c;
      })();

  this.big_ctx = this.big_canvas.getContext('2d');

  // Call this to change the scaling factor. Must be a small integer.
  this.ChangePx = (p) => {
    this.px = p;
    this.big_canvas.width = this.width * this.px;
    this.big_canvas.height = this.height * this.px;
    this.big_id = this.big_ctx.createImageData(this.width * this.px, this.height * this.px);

    // One buf we keep reusing, plus two aliases of it.
    this.big_buf = new ArrayBuffer(this.big_id.data.length);
    // Used just for the final draw.
    this.big_buf8 = new Uint8ClampedArray(this.big_buf);
    // We write RGBA pixels though.
    this.big_buf32 = new Uint32Array(this.big_buf);
  };

  // Initialize at start.
  this.ChangePx(px);

  // Flips the 1:1 canvas to the big canvas.
  this.Flip = () => {
    // d1x : Uint8ClampedArray, has an ArrayBuffer backing it (.buffer)
    let d1x = this.ctx.getImageData(0, 0, this.width, this.height).data;
    // d1x32 : Uint32Array, aliasing d1x
    let d1x32 = new Uint32Array(d1x.buffer);

    let d = this.big_buf32;
    // Blit to px x px sized pixels.
    // PERF: This is slow!
    // Strength reduction. Unroll by doing a case analysis over small px.
    // Shorten names.
    // If browser supports native blit without resampling, use it.
    const width = this.width;
    const height = this.height;
    const px = this.px;
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
	    let p = d1x32[y * width + x];
	    let o = (y * px) * (width * px) + (x * px);
	    for (let u = 0; u < px; u++) {
	      for (let v = 0; v < px; v++) {
	        d[o + u * this.width * px + v] = p;
	      }
	    }
      }
    }

    this.big_id.data.set(this.big_buf8);
    this.big_ctx.putImageData(this.big_id, 0, 0);
  };

  // XXX
  this.Clear(0xFF00FF00);
};
