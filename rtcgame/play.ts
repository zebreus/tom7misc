
// Leftover stuff from the original net demo. Clean this up!

// From url, etc?
const ROOM_NAME = 'test';

// XXX debugging
let stop_running = false;

let net = new Net;


// Debugging crap.

function Stop() {
  // XXX debugging thing
  stop_running = true;
}

function anyPeer() {
  for (let k in net.peers) return net.peers[k];
  return null;
}



function updateUI() {
  let uelt = document.getElementById('uid');
  if (!uelt) return;
  uelt.innerHTML = net.myUid == '' ? '(not yet assigned)' : net.myUid;

  if (net.chats_dirty) {
    drawChats();
  }

  updateListenUI();
  updatePeersUI();
  updateMatrixUI();
}

function updateListenUI() {
  let elt = document.getElementById('listen');
  if (!elt) return;
  elt.innerHTML = '';
  if (net.makingOffer) {
    TEXT('(making offer)', DIV('', elt));
  }
  TEXT('Offer uid: ' +
       (net.waitingForOfferUid ? '(waiting)' : '') + ' ' +
       (net.offerUid || ''), DIV('', elt));
  TEXT((net.offerToSend ? '(offer to send)' : '(no offer to send)'),
       DIV('', elt));
  if (net.listenConnection) {
    TEXT('signalingState = ' + net.listenConnection.signalingState,
         DIV('', elt));
    TEXT('connectionState = ' + net.listenConnection.connectionState,
         DIV('', elt));
    TEXT('iceConnectionState = ' + net.listenConnection.iceConnectionState,
         DIV('', elt));
    TEXT('iceGatheringState = ' + net.listenConnection.iceGatheringState,
         DIV('', elt));
  }
}

function updatePeersUI() {
  let elt = document.getElementById('peers');
  if (!elt) return;
  elt.innerHTML = '';

  let table = TABLE('peers', elt);
  let hdr = TR('', table);
  let cols = ['uid', 'type',
              'conn state', 'ice state', 'ice g state',
              'channel', 'rtt',
              'nick'];
  for (let c of cols)
    TEXT(c, TD('', hdr));

  for (let k in net.players) {
    let player = net.players[k];
    let tr = TR('', table);

    let peer = net.peers[k];
    let peerclass =
        peer ? 'peeruid' :
        (player.playerType === PlayerType.BLACKLISTED) ?
        'blacklistuid' :
        'nopeeruid';
    TEXT(player.puid, TD(peerclass, tr));

    if (peer) {
      let s =((peer.peerType === PeerType.I_CALL) ? 'I call' : 'They call');
      TEXT(s, TD('', tr));
      TEXT((peer.connection ? peer.connection.connectionState : 'null'),
           TD('', tr));
      TEXT((peer.connection ? peer.connection.iceConnectionState : 'null'),
           TD('', tr));
      TEXT((peer.connection ? peer.connection.iceGatheringState : 'null'),
           TD('', tr));
      TEXT((peer.channel ? peer.channel.readyState : 'null'),
           TD('', tr));
      TEXT('' + peer.lastPing.toFixed(1) + ' ms', TD('', tr));
      TEXT('"' + player.nick + '"', TD('', tr));
    } else {
      TD('', tr).colSpan = cols.length - 1;
    }
  }
}

function updateMatrixUI() {
  let elt = document.getElementById('matrix');
  if (!elt) return;
  elt.innerHTML = '';
  let mtx = TABLE('matrix', elt);

  let hdr = TR('', mtx);
  // corner
  TEXT('src \\ dest', TD('', hdr));
  for (let k in net.players) {
    TEXT(k.substr(0, 3), TD('', hdr));
  }

  let awoltr = TR('', mtx);
  TEXT('net awol', TD('', awoltr));
  for (let dst in net.players) {
    let sec = net.getNetworkAwolTime(dst) / 1000.0;
    TEXT(sec.toFixed(1) + 's', TD('', awoltr));
  }

  let now = window.performance.now();
  for (let src in net.players) {
    let p = net.players[src];
    // No rows for blacklisted players -- we don't store their data.
    if (p.playerType === PlayerType.BLACKLISTED)
      continue;
    let tr = TR('', mtx);
    TEXT(src, TD('', tr));

    for (let dst in net.players) {
      let ct = p.connectivityTo[dst];
      let cell = TD('cell', tr);
      if (ct) {
        // NARROW NO-BREAK SPACE
        // let txt = '\u202f';
        let txt =
            (isFinite(ct.a) && (now - ct.a) > 0.1) ?
            ((now - ct.a) / 1000.0).toFixed(1) :
            '\u202f';
        switch (ct.c) {
        case Connectivity.UNKNOWN:
          cell.style.backgroundColor = '#CCC';
          break;
        case Connectivity.SELF:
          cell.style.backgroundColor = '#FFF';
          break;
        case Connectivity.NEVER_CONNECTED:
          cell.style.backgroundColor = '#AA5';
          break;
        case Connectivity.CONNECTED:
          cell.style.backgroundColor = '#5A5';
          // This is awol 0 by definition, so instead
          // show the most recent ping time.
          // U+221E INFINITY
          txt = isFinite(ct.p) ? ''+(ct.p | 0) : '\u221e';
          break;
        case Connectivity.DISCONNECTED:
          cell.style.backgroundColor = '#A55';
          break;
        }
        TEXT(txt, cell);
        // cell.classList.add();
      } else if (src == dst) {
        // U+202F NARROW NO-BREAK SPACE
        TEXT('\u202F', cell);
      } else {
        // U+2014 EM DASH
        TEXT('\u2014', cell);
      }
    }
  }
}


function drawChats() {
  let elt = document.getElementById('chats');
  if (!elt) return;
  elt.innerHTML = '';
  for (let chat of net.chats) {
    let cr = DIV('', elt);
    let nick = net.getNick(chat.uid);
    TEXT(chat.uid, SPAN('chat-uid', elt));
    TEXT('<' + nick + '>', SPAN('chat-nick', elt));
    TEXT(chat.msg, SPAN('chat-msg', elt));
  }
}

// Chat crap
function chatKey(e : KeyboardEvent) {
  if (e.keyCode == 13) {
    let elt = (<HTMLInputElement>document.getElementById('chatbox'));
    let msg = elt.value;
    elt.value = '';

    // Could also support /nick etc. here, which is maybe better
    // than having separate boxes?
    net.broadcastChat(msg);
  }
}

// Nickname
function nicknameKey(e : KeyboardEvent) {
  let elt = (<HTMLInputElement>document.getElementById('nickname'));
  let nick = elt.value;
  net.broadcastNick(nick);
}


// TODO: Perhaps this function should be constructing net with its
// initial values, especially if we are doing it from the hash...
function init() {
  let chatbox = document.getElementById('chatbox');
  if (chatbox) chatbox.onkeyup = chatKey;
  let nickbox = document.getElementById('nickname');
  if (nickbox) nickbox.onkeyup = nicknameKey;

  let a = window.location.hash.split('|');
  if (a.length == 3) {
    // We have saved state info.
    let elt = document.getElementById('intro');
    if (elt)
      elt.style.display = 'none';
    net.roomUid = a[0];
    net.myUid = a[1];
    net.mySeq = a[2];
    window.location.hash = net.roomUid + '|' + net.myUid + '|' + net.mySeq;
    if (VERBOSE)
      console.log('joined!')
    net.addSelfPlayer();

    // I probably need to replace the offer on the server? They
    // probably don't remain valid across a page reload like this?
    net.makeOffer();

    // Start loop.
    periodic();

  } else {
    // wait for user to click to join.

    // XXX or maybe here we should insert the button to click
  }
}

function joinButton() {
  {
    let elt = document.getElementById('intro');
    if (elt) elt.style.display = 'none';
  }

  net.doJoin(ROOM_NAME);
  periodic();
}

let periodicallyUpdateUi : Periodically = new Periodically(100);
function periodic() {
  // Start loop.
  net.uPeriodic();

  if (periodicallyUpdateUi.shouldRun()) {
    // Perhaps only if dirty? 10x a second is ridiculous
    updateUI();
  }

  // Keep looping.
  if (!stop_running) {
    window.setTimeout(periodic, 15);
  }
}
