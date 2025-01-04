
function makeElement(what : string, cssclass : string | null,
                     parent : HTMLElement) : HTMLElement {
  let e = document.createElement(what);
  if (cssclass) e.setAttribute('class', cssclass);
  if (parent) parent.appendChild(e);
  return e;
}

// TODO: Many of these have narrower types than HTMLElement
function IMG(cssclass : string | null, parent : HTMLElement) {
  return makeElement('IMG', cssclass, parent);
}
function DIV(cssclass : string | null, parent : HTMLElement) {
  return makeElement('DIV', cssclass, parent);
}
function SPAN(cssclass : string | null, parent : HTMLElement) {
  return makeElement('SPAN', cssclass, parent);
}
function BR(cssclass : string | null, parent : HTMLElement) {
  return makeElement('BR', cssclass, parent);
}
function TABLE(cssclass : string | null, parent : HTMLElement) {
  return makeElement('TABLE', cssclass, parent);
}
function TR(cssclass : string | null, parent : HTMLElement) {
  return makeElement('TR', cssclass, parent);
}
function TD(cssclass : string | null,
            parent : HTMLElement) : HTMLTableCellElement {
  return makeElement('TD', cssclass, parent) as HTMLTableCellElement;
}
function TEXT(contents : string, parent : HTMLElement) {
  let e = document.createTextNode(contents);
  if (parent) parent.appendChild(e);
  return e;
}
