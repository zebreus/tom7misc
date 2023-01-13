
function makeElement(what, cssclass, elt) {
  var e = document.createElement(what);
  if (cssclass) e.setAttribute('class', cssclass);
  if (elt) elt.appendChild(e);
  return e;
}
function DIV(cssclass, elt) { return makeElement('DIV', cssclass, elt); }
function CANVAS(cssclass, elt) { return makeElement('CANVAS', cssclass, elt); }
function SPAN(cssclass, elt) { return makeElement('SPAN', cssclass, elt); }
function BR(cssclass, elt) { return makeElement('BR', cssclass, elt); }
function TEXT(contents, elt) {
  var e = document.createTextNode(contents);
  if (elt) elt.appendChild(e);
  return e;
}

function FetchLines(url, k) {
  let xhr = new XMLHttpRequest;
  xhr.open('GET', url);
  xhr.onreadystatechange = e => {
    if (xhr.readyState === XMLHttpRequest.DONE) {
      k(xhr.responseText);
    }
  };
  xhr.send();
}

function IsWord(w) {
  return WORD_RE.test(w);
}

// PERF Could cache the list...
function SetSimilar(idx, value) {
  let elt = document.getElementById('under' + idx);
  FetchLines('/similar/' + WORD[idx] + '/' + value,
             text => {
               elt.innerHTML = '';
               let lines = text.split(/\r?\n/);
               for (let line of lines) {
                 TEXT(line, elt);
                 BR('', elt);
               }
             });
}

function InputChange(idx, elt, value) {
  // XXX check start char
  if (IsWord(value)) {
    elt.style.background = '#DFD';
    elt.style.border = '2px solid #090';
    SetSimilar(idx, value);
  } else {
    elt.style.background = '#FDD';
    elt.style.border = '2px solid #900';
  }
}

function Fill() {
  for (let idx = 0; idx < WORD.length; idx++) {
    let elt = document.getElementById('word' + idx);
    elt.value = WORD;
    InputChange(idx, elt, WORD);
  }
}

function Start() {
  // Register handlers
  for (let idx = 0; idx < WORD.length; idx++) {
    let elt = document.getElementById('word' + idx);
    if (!elt) console.error('missing elt?');

    elt.addEventListener('input', () => InputChange(idx, elt, elt.value));
    InputChange(idx, elt, '');
  }

  Fill();
}

