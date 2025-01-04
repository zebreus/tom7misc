
// TODO: SERVER_URL argument to net constructor
const SERVER_URL = 'http://spacebar.org/f/a/rtcgame';
const POLL_MS = 5000;

const VERBOSE = false;

// ?
let RTCPEER_ARGS = {
  iceServers: [{urls: ['stun:stun.l.google.com:19302']}]
};


// Unfortunately there are two ways we may become connected: We
// initiated the connection, or someone initiated a connection to us.
enum MsgType {
  PING = "P",
  PING_RESPONSE = "R",
  CHAT = "C",
  SET_NICK = "N",
  CONNECTIVITY = "Y",
}


// Unfortunately there are two ways we may become connected: We
// initiated the connection, or someone initiated a connection to us.
enum PeerType {
  I_CALL = "I",
  THEY_CALL = "T",
}


// TODO: Can reduce bandwidth/space on server (each is about 1kb)
// by having a custom encoder for SDPs built into the JS code. If we
// do this we probably want some version info in the encoded SDP.
function encodeSdp(sdp : string) {
  // console.log(sdp);
  const b64 = btoa(sdp);
  return b64.replace(/[+]/g, '_').replace(/[/]/g, '.');
}

function decodeSdp(enc : string) {
  let b64 = enc.replace(/[.]/g, '/').replace(/_/g, '+');
  return atob(b64);
}

// A Peer is a connection (possibly in progress, or failed) with
// a player.
class Peer {
  puid : string;
  peerType : PeerType;
  connection : RTCPeerConnection | null;
  channel : RTCDataChannel | null;
  lastPing : number;
  private periodicallyPing : Periodically;

  // Always have the player's uid when creating a peer, either
  // with the answer or the poll response (which contains all
  // outstanding players).
  constructor(private readonly net : Net, puid : string) {
    this.puid = puid;
    this.peerType = net.getPeerType(puid);
    // Initialized by factory function.
    this.connection = null;
    this.channel = null;

    // Set when receiving a ping response.
    // TODO: This can be a time series...
    this.lastPing = Infinity;

    this.periodicallyPing = new Periodically(1000);
  }

  // Returns true if the connection is failed; this means we couldn't
  // connect or we got disconnected. Pre-connection states are not failed.
  isFailed() : boolean {
    // I-call peers have their connection/channel initialized asynchronously
    // by promises. Might want to make this more explicit though?
    // TODO: Failure during an i-call connection probably does not
    // ever get cleaned up?
    if (!this.connection) return false;

    // "disconnected" should eventually become "failed" or "connected"
    // again (if another ice candidate works).
    if (this.connection.connectionState == 'failed')
      return true;

    // TODO: We should consider the peer failed if we are unable to
    // create a channel. But null is also used in pre-connection states.
    if (!this.channel) return false;
    if (this.channel.readyState == 'closed') return true;

    return false;
  }

  deliverAnswer(answer : string) : void {
    if (this.peerType == PeerType.THEY_CALL) {
      if (this.connection == null)
        throw 'in wrong state?';
      this.connection.setRemoteDescription({'type': 'answer',
                                            'sdp': answer});
    } else {
      console.log('unimplemented: deliverAnswer when I_CALL');
    }
  }

  // After we've set the local description, this gets called
  // whenever the ice gathering state changes. We're waiting
  // for it to be complete so that we can send a single answer
  // with all ICE candidates.
  //
  // Arguments are the peer and offer uids.
  sendAnswerRemotely(puid : string, ouid : string) : void {
    if (!this.connection) {
      if (VERBOSE)
        console.log('no connection in sendAnswerRemotely?');
      return;
    }
    // Only send a complete answer with all ice candidates.
    if (this.connection.iceGatheringState == 'complete') {
      let desc = this.connection.localDescription;
      if (!desc || desc.type != 'answer')
        throw 'Expected an answer-type description?';

      let enc = encodeSdp(desc.sdp);
      let params = {'to': puid, 'o': ouid, 'a': enc};
      /* result is ignored... */
      requestJSON(SERVER_URL + '/answer/' + net.myUid + '/' + net.mySeq,
                  params);
      if (VERBOSE)
        console.log('Sending answer to ' + puid + ' ' + desc.sdp);
    } else {
      if (VERBOSE)
        console.log('Not sending answer yet because ICE state is ' +
                    this.connection.iceGatheringState);
    }
  }

  // Send a message already in json form, if we have a data
  // channel.
  sendJson(json : string) : void {
    if (this.channel &&
        this.channel.readyState === 'open') {
      this.channel.send(json);
    }
  }

  sendMessage(msg : object) : void {
    this.sendJson(JSON.stringify(msg));
  }

  // Process a message sent BY this peer.
  // (TODO: Perhaps most of this code should be done in Net, since it
  // often involves state beyond this peer.)
  processMessage(data : string) : void {
    let json = JSON.parse(data);
    let now = window.performance.now();
    switch (json['t']) {
      // Handle network timing stuff first.
    case MsgType.PING:
      // All we do for a PING is echo it back to the same peer
      // as a PING_RESPONSE.
      //
      // Treat the payload as number, though. There's no specific
      // risk to echoing back a huge arbitrary payload, but it's not
      // supposed to happen and could be a venue for abuse.
      let p = 0 + json['p'];
      this.sendMessage({'t': MsgType.PING_RESPONSE, 'p': p});
      break;

    case MsgType.PING_RESPONSE:
      // When we get a ping response, we assume it's the last ping
      // we sent, and so the difference between now and the timestamp
      // therein is the round trip latency.
      let ms = now - (0 + json['p']);
      this.lastPing = ms;
      if (VERBOSE)
        console.log('ping rtt: ' + ms);
      break;

    case MsgType.CHAT:
      net.pushChat(this.puid, json['msg']);
      net.chats_dirty = true;
      break;

    case MsgType.SET_NICK: {
      if (VERBOSE)
        console.log('SET_NICK ' + json['nick']);
      let player = net.players[this.puid];
      if (!player) throw 'players should be superset of peers';
      player.nick = json['nick'];
      net.chats_dirty = true;
      break;
    }

    case MsgType.CONNECTIVITY: {
      let row = json['row']
      let player = net.players[this.puid];
      if (!player) throw 'players should be superset of peers';
      player.connectivityTo = {};
      for (let ouid in row) {
        let ct = row[ouid];
        // Player learned about this peer before me; add it...
        net.maybeAddPlayer(ouid);
        // Rebase to my own timeOrigin.
        let a = now - ct.a;
        // console.log('atime: ' + a);
        player.connectivityTo[ouid] = {c: ct.c, p: ct.p, a: a};
      }
      break;
    }
    }

  }

  // Called periodically.
  periodic() : void {
    if (this.channel) {
      if (this.periodicallyPing.shouldRun()) {
        this.sendMessage({'t': MsgType.PING, 'p': window.performance.now()});
      }

      // TODO: Send connectivity info.
    }
  }

};

// ?
enum PlayerType {
  ME = "M",
  OTHER = "O",
  BLACKLISTED = "B",
}

enum Connectivity {
  // Haven't heard anything yet.
  UNKNOWN = "U",
  SELF = "S",
  NEVER_CONNECTED = "V",
  CONNECTED = "C",
  DISCONNECTED = "D",
  // Decided that this player is gone and we won't try to contact it
  // again (unless perhaps there's some positive evidence of
  // reinstatement?)
  AWOL = "A",
}

// A player is a possibly-active UID we know about on the network,
// including ourself. This includes blacklisted (awol) UIDs, but we
// don't store full information about them and most operations pretend
// they don't exist.
// We can learn about these by polling the server, or from other peers.
//
// No explicit connection to the peer, but they use the same uid key.
//
// Values of the connectivityTo map.
// Not stored for blacklisted uids.
// Map from other uid to object.
//  { c : Connectivity,
//    p : number, RTT in msec,
//    a : number, time that awol began (in msec since time origin) }
// AWOL begins when we are waiting to hear from a peer (i.e., not
// actively connecting or connected).
// It is effectively now() for connected peers.
type ConnectivityData = { c : Connectivity, p : number, a : number };
class Player {
  playerType : PlayerType;
  puid : string;
  connectivityTo : Record<string, ConnectivityData>;
  nick : string;

  constructor(private readonly net : Net, puid : string) {
    this.playerType = (puid === net.myUid) ? PlayerType.ME : PlayerType.OTHER;
    this.puid = puid;

    this.connectivityTo = {};
    this.nick = '???';
  }
};

function todoInfo(e : any) {
  console.log('TODO Info');
  console.log(e);
}

function todoError(e : any) {
  console.log('TODO error');
  console.log(e);
}

// Represents the network from the point of view of one of the
// participants.
class Net {

  // Approximately 0.
  // (Note this is not actually used! Should it be?)
  readonly timeOrigin : number = window.performance.now();

  // Initialized upon joining, and then stays the same for the length
  // of the session.
  roomUid : string = '';
  myUid : string = '';
  mySeq : string = '';

  // TODO: Add visualization of offer-creation state to UI.
  // If non-null, an offer to deliver to the server during the poll
  // call.
  offerToSend : string | null = null;
  // If non-null, this is the uid for the offer that the server
  // currently knows about (and which we generated in this session).
  offerUid : string | null = null;
  // Connection corresponding to the outstanding offer.
  listenConnection : RTCPeerConnection | null = null;
  sendChannel : RTCDataChannel | null = null;
  // XXX we progress from makingOffer to waitingForOfferUid, so this
  // is better as a state enum. Should encapsulate this. It would also
  // allow us to have more than one outstanding offer, like connections
  // made via peers.
  makingOffer = false;
  waitingForOfferUid = false;

  // keyed by uid.
  peers : Record<string, Peer> = {};
  players : Record<string, Player> = {};

  // One thing we have to do is decide who is going to call whom.
  // We can get into a mess if both sides try to initiate a connection
  // at the same time, and then e.g. both abort when it seems the other
  // is making the connection! We establish a global ordering using
  // uids: The player with the lexicographically earlier uid makes
  // the call (reads the other's offer and sends an answer to it).
  //
  // PERF: Rather than have 0xFFFFF receive all calls, we could use
  // some function like "is the peer closer going up (modulo radix)
  // or down?" which would keep the call/receive load balanced for
  // any given participant.
  getPeerType(puid : string) : PeerType {
    if (!this.myUid) throw 'precondition: must join first!';
    return this.myUid < puid ? PeerType.I_CALL : PeerType.THEY_CALL;
  }

  // A they-call peer is created from my outstanding listen-connection.
  // (Maybe the listen-connection should be wrapped in an object so that
  // we can just pass it all here and it can retain any handlers?)
  createTheyCallPeer(puid : string,
                     conn : RTCPeerConnection,
                     channel : RTCDataChannel) : Peer {
    if (puid in this.peers) throw 'precondition';
    if (this.getPeerType(puid) != PeerType.THEY_CALL) throw 'precondition';
    let peer = new Peer(this, puid);
    if (conn == null) throw 'precondition';
    if (channel == null) throw 'precondition';
    peer.connection = conn;
    peer.channel = channel;
    peer.channel.onmessage = e => {
      if (VERBOSE) {
        console.log('[tc] message on channel');
        console.log(e);
      }
      peer.processMessage(e.data);
    };
    this.peers[puid] = peer;
    if (VERBOSE)
      console.log('Created they-call peer ' + puid);
    return peer;
  }

  // An I-call peer is created from an offer sdp and its uid. We create
  // the connection and send an answer to the server.
  createICallPeer(puid : string,
                  offer : string,
                  ouid : string) : Peer {
    if (puid in this.peers) throw 'precondition';
    if (this.getPeerType(puid) != PeerType.I_CALL) throw 'precondition';
    if (offer === '') throw 'precondition';
    if (ouid === '') throw 'precondition';
    let peer = new Peer(this, puid);
    peer.connection = new RTCPeerConnection(RTCPEER_ARGS);
    let conn = peer.connection;
    // Should be member function...
    conn.ondatachannel = e => {
      if (VERBOSE) {
        console.log('icall.datachannel');
        console.log(e);
      }
      if (e.type === 'datachannel') {
        if (VERBOSE)
          console.log('got data channel!');
        peer.channel = e.channel;
        peer.channel.onmessage = e => {
          if (VERBOSE) {
            console.log('[ic] message on channel');
            console.log(e);
          }
          peer.processMessage(e.data);
        };
      }
    };

    if (VERBOSE)
      console.log('Try setting remote description to ' + offer);
    conn.setRemoteDescription({'type': 'offer', 'sdp': offer}).
        then(() => conn.createAnswer()).
        then(answer => conn.setLocalDescription(answer)).
        then(() => {
          // Is it possible for this to be complete already? If so,
          // act on it now.
          if (conn.iceGatheringState == 'complete') {
            peer.sendAnswerRemotely(puid, ouid);
          } else {
            conn.onicegatheringstatechange =
                e => peer.sendAnswerRemotely(puid, ouid);
          }
        });
    // TODO: catch errors, explicitly fail!

    if (VERBOSE)
      console.log('Created I-call peer ' + puid);
    this.peers[puid] = peer;
    return peer;
  }

  getPeerByUid(puid : string) : Peer | null {
    if (puid in this.peers) {
      return this.peers[puid];
    }
    return null;
  }


  // Chat stuff maybe does not belong in Net.
  chats_dirty : boolean = false;
  readonly MAX_CHATS : number = 32;
  chats : Array<{uid: string, msg: string}> = [];
  pushChat(uid : string, msg : string) : void {
    if (this.chats.length == this.MAX_CHATS) {
      this.chats.shift();
    }
    this.chats.push({uid: uid, msg: msg});
    this.chats_dirty = true;
  }

  broadcastChat(msg : string) {
    // Send to self.
    this.pushChat(this.myUid, msg);
    let json = JSON.stringify({'t': MsgType.CHAT, 'msg': msg});
    for (let k in this.peers) {
      let peer = this.peers[k];
      peer.sendJson(json);
    }
    this.chats_dirty = true;
  }

  broadcastNick(nick : string) {
    // Can lose keystrokes here, but...
    let me = this.players[this.myUid];
    if (!me) return;
    me.nick = nick;

    let json = JSON.stringify({'t': MsgType.SET_NICK, 'nick': nick});
    for (let k in this.peers) {
      let peer = this.peers[k];
      peer.sendJson(json);
    }
    this.chats_dirty = true;
  }

  // Share connectivity with all connected peers. Note this is an n^2
  // operation, since we send information about all peers to all peers
  // (this is even worse if we have old players that we haven't cleaned
  // up). But we use this to keep the set of active players from growing
  // without bound, and the per-cell payload is small.
  broadcastConnectivity() : void {
    if (this.myUid == '') throw 'precondition';
    let me = this.players[this.myUid];
    if (!me) throw 'precondition'

    let now = window.performance.now();
    let msg : Record<string, ConnectivityData> = {};
    for (let puid in me.connectivityTo) {
      let ct = me.connectivityTo[puid];
      // Round ping to integer to make these message smaller... peers
      // don't care about sub-millisecond timing.
      let roundp = isFinite(ct.p) ? Math.round(ct.p) : ct.p;
      // Note that awol time here is stored as absolute (time since
      // timeOrigin) but sent as relative (how long ago). Different
      // peers of course disagree on timeOrigin, and we avoid using unix
      // epoch so that we don't have to worry about clock skew / NTP /
      // etc.
      let awolSec = Math.round(now - ct.a);
      msg[puid] = { 'c': ct.c,
                    'p': roundp,
                    'a': awolSec };
    }

    let json = JSON.stringify({'t': MsgType.CONNECTIVITY, 'row': msg});
    for (let k in this.peers) {
      let peer = this.peers[k];
      peer.sendJson(json);
    }
  }

  // Mark (in my connectivity map) that the player was seen at 'when'
  // (given as ms since "time origin", i.e., window.performance.now() is
  // now). Since we may be learning that the server saw this player 8
  // seconds ago, but we saw them 1 second ago, this only updates the
  // awol time if it is more recent.
  markPlayerSeen(puid : string, when : number) : void {
    let me = this.players[this.myUid];
    if (!me) throw 'recondition';
    let ct = me.connectivityTo[puid];
    if (!ct) {
      me.connectivityTo[puid] =
          { c: Connectivity.UNKNOWN, p: Infinity, a: when };
    } else {
      let preva = me.connectivityTo[puid].a;
      if (when > preva) {
        me.connectivityTo[puid].a = when;
      }
    }
  }

  // Compute network awol time for p, which is the number of
  // milliseconds since we believe any player saw p. Idea is that if
  // this is long ago, we have consensus to retire the player.
  getNetworkAwolTime(puid : string) : number {
    // Before we're established, return a conservative answer.
    if (this.myUid == '') return 0;
    if (!this.players[this.myUid]) return 0;

    let maxAwolTime = -Infinity;
    for (let k in this.players) {
      let p = this.players[k];
      if (p.playerType == PlayerType.BLACKLISTED)
        continue;

      if (p.connectivityTo && p.connectivityTo[puid]) {
        // Note that this is the last info we received from the player.
        // It could be the case that they appeared CONNECTED but we
        // last heard from them 30 minutes ago. So the relevant thing
        // is always the awol time.
        let ct = p.connectivityTo[puid];
        if (ct.a > maxAwolTime) maxAwolTime = ct.a;
      }
    }

    let t = window.performance.now() - maxAwolTime;
    return t < 0 ? 0 : t;
  }


  // Moves players to the blacklist. A player goes on the blacklist
  // when the minimum awol time that we know about (including from other
  // connected peers) exceeds a threshold.
  // When on the blacklist, we mostly ignore the player. A player can
  // become unblacklisted with some positive evidence of their aliveness
  // (e.g. they connect to us, update the offer on the server, etc.)
  readonly BLACKLIST_MS : number = 5 * 60 * 1000;
  updateBlacklist() : void {
    if (this.myUid == '') throw 'precondition';
    let me = this.players[this.myUid];
    if (!me) throw 'precondition'

    for (let puid in this.players) {
      let p = this.players[puid];
      // Never blacklist myself!
      if (p.playerType == PlayerType.ME)
        continue;
      // Already blacklisted...
      if (p.playerType == PlayerType.BLACKLISTED)
        continue;

      let awolt = this.getNetworkAwolTime(puid);
      if (awolt > this.BLACKLIST_MS) {
        p.playerType = PlayerType.BLACKLISTED;
        // Perhaps I should announce this?
      }
    }
  }

  // Updates my own connectivity/ping maps (for the player that corresponds
  // to me.)
  updateMyConnectivity() : void {
    if (this.myUid == '') throw 'precondition';
    let me = this.players[this.myUid];
    if (!me) throw 'precondition'

    // We assume that peers is a subset of players..
    for (let puid in this.peers) {
      if (!this.players[puid]) throw 'peers should be a subset of players';
    }

    let now = window.performance.now();
    for (let puid in this.players) {
      let player = this.players[puid];
      // Don't do work for blacklisted players.
      if (player.playerType == PlayerType.BLACKLISTED)
        continue;

      let peer = this.peers[puid];

      if (puid == this.myUid) {
        // Self treated specially (no peer).
        me.connectivityTo[puid] = { c: Connectivity.SELF, p: 0, a: now };
      } else {
        let peer = this.peers[puid];

        let good = peer &&
            peer.connection &&
            (peer.connection.connectionState == 'connected') &&
            peer.channel &&
            (peer.channel.readyState == 'open');

        if (good) {
          me.connectivityTo[puid] = { c: Connectivity.CONNECTED,
                                      p: peer.lastPing || Infinity,
                                      a: now };

        } else {
          // No peer, or the connection is pending/broken.
          // Might be useful to add more fine-grained states here
          // (like trying to connect, waiting for offer, sent answer,
          // etc.)?

          let ct = me.connectivityTo[puid];
          if (ct && ct.c == Connectivity.CONNECTED) {
            // If the peer was in connected state, then we update to
            // DISCONNECTED and set the awol time.
            //
            // (Note this requires updateconnectivity to run at least once
            // while connected. We could set this explicitly when a connection
            // is made. Or consider very short-lived connections to not be
            // connections at all, which is probably fine too)
            me.connectivityTo[puid] = { c: Connectivity.DISCONNECTED,
                                        p: Infinity,
                                        a: now };
          } else if (ct && (ct.c == Connectivity.DISCONNECTED ||
                            ct.c == Connectivity.NEVER_CONNECTED)) {

            // Leave in DISCONNECTED or NEVER_CONNECTED states, and
            // don't update awol time--player is still awol.
          } else if (ct) {
            // e.g. if UNKNOWN
            me.connectivityTo[puid].c = Connectivity.NEVER_CONNECTED;
            me.connectivityTo[puid].p = Infinity;
            // Leave awol time as-is.
          } else {
            // Weird to not have ct at all; when we insert players from
            // the server for example we have awol times. 'now' is conservative
            // but may keep very stale peers alive longer than we want.
            me.connectivityTo[puid] = { c: Connectivity.NEVER_CONNECTED,
                                        p: Infinity,
                                        a: now };
          }
        }
      }
    }
  }


  // Asynchronously create an offer. Waits for all the ice candidates
  // to be gathered, then initializes offerToSend and listenConnection
  // upon success.
  makeOffer() {
    if (this.makingOffer) return;
    if (this.waitingForOfferUid) return;

    this.makingOffer = true;
    this.listenConnection = null;
    let lc = new RTCPeerConnection(RTCPEER_ARGS);
    this.sendChannel = lc.createDataChannel("sendChannel");
    lc.onicecandidate = e => {
      if (VERBOSE) {
        console.log('icecandidate');
        console.log(e);
      }
    };
    // XXX figure this out -- can we set it up after promoting this
    // connection to a Peer?
    this.sendChannel.onopen = e => {
      if (VERBOSE) {
        console.log('channel.onopen');
        console.log(e);
      }
    };

    this.sendChannel.onclose = e => {
      if (VERBOSE) {
        console.log('channel.onclose');
        console.log(e);
      }
    };

    return lc.createOffer().
        then(offer => lc.setLocalDescription(offer)).
        then(() => {
          // Is it possible for this to be complete already? If so,
          // act on it now.
          if (lc.iceGatheringState == 'complete') {
            this.markOfferReady(lc);
          } else {
            // Otherwise, wait for the ICE candidates.
            lc.onicegatheringstatechange = e => this.markOfferReady(lc);
          }
        }).
        catch(e => {
          this.listenConnection = null;
          this.sendChannel = null;
          this.makingOffer = false;
          todoError(e);
        });
  }

  markOfferReady(conn : RTCPeerConnection) {
    if (conn.iceGatheringState == 'complete') {
      let desc = conn.localDescription;
      if (!desc)
        throw 'expected desc if gathering state is complete?';
      if (desc.type != 'offer')
        throw 'Expected an offer-type description?';
      if (VERBOSE)
        console.log('Got offer description: ' + desc.sdp);
      let enc = encodeSdp(desc.sdp);
      if (VERBOSE)
        console.log('Encoded: ' + enc);
      this.offerToSend = enc;
      this.listenConnection = conn;
      this.makingOffer = false;
      // Consider polling immediately?
    }
  }


  getNick(puid : string) {
    if (!this.players[puid]) return '???';
    return this.players[puid].nick || '???';
  }

  addSelfPlayer() {
    if (this.myUid == '') throw 'precondition';
    this.players[this.myUid] = new Player(this, this.myUid);
    this.updateMyConnectivity();
  }

  maybeAddPlayer(puid : string) {
    if (this.players[puid]) return;
    this.players[puid] = new Player(this, puid);
  }


  periodicallyPoll : Periodically = new Periodically(POLL_MS);
  periodicallyCleanupPeers : Periodically = new Periodically(125);
  periodicallyShareConnectivity : Periodically = new Periodically(1000);

  // Call this function regularly (at least every frame) to do
  // the periodic network maintenance tasks. It manages its own
  // timers so it's fine to call it very often.
  uPeriodic() {
    // PERF: We can perhaps avoid long frames by only doing one
    // of these periodic actions per frame (but have to be a little
    // fancy to avoid starvation).
    if (this.periodicallyPoll.shouldRun()) {
      this.doPoll();
    }

    if (this.periodicallyCleanupPeers.shouldRun()) {
      // Clean up disconnected peers.
      for (let k in this.peers) {
        if (this.peers[k].isFailed()) {
          // TODO: Explicitly note this in connectivity map?
          // Otherwise updateMyConnectivity should do it?

          // TODO: Any way to actively discard these (to prevent them
          // from hanging around in callbacks, etc.?)
          this.peers[k].connection = null;
          this.peers[k].channel = null;
          delete this.peers[k];
        }
      }
    }

    // On some delay? Or rename this to uPeriodic?
    for (let k in this.peers)
      this.peers[k].periodic();

    if (this.periodicallyShareConnectivity.shouldRun()) {
      if (this.myUid !== '') {
        this.updateBlacklist();
        this.updateMyConnectivity();
        this.broadcastConnectivity();
      }
    }
  }

  doPoll() {
    // Must have already joined.
    if (this.myUid === '' ||
        this.mySeq === '' ||
        this.roomUid === '')
      return;

    let params : Record<string, string> = {};
    if (this.offerToSend !== null) {
      params['offer'] = this.offerToSend;
      // Consume it.
      this.offerToSend = null;
      this.waitingForOfferUid = true;
    }

    // Don't spam the server: Only retry polling once the promise completes.
    this.periodicallyPoll.pause();

    requestJSON(SERVER_URL + '/poll/' + this.myUid + '/' + this.mySeq,
                params).
        then(json => {
          // Process response...
          if (VERBOSE) {
            console.log('parsed poll response');
            console.log(json);
          }
          this.processPollResponse(json);
          // Allow polling again.
          this.periodicallyPoll.reset();
        }).
        catch(e => {
          if (VERBOSE) {
            console.log('XXX poll error.');
            console.log(e);
          }
          this.waitingForOfferUid = false;
          // XXX restart polling? regen offer?
          // Perhaps increase timeout..?
          this.periodicallyPoll.reset();
        });
  }

  // TODO: flesh out interface
  processPollResponse(json : {answers: any, others: any, ouid: string}) {
    // Process answers first (before creating a new offer).
    // The first one to answer (with the right offer uid) gets to take
    // on the listeningConnection as its connection.
    let answers = json['answers'];
    for (let answer of answers) {
      let puid = answer['uid'];
      let sdp = decodeSdp(answer['s']);

      // First, an answer from anyone resets their awol time.
      this.markPlayerSeen(puid, window.performance.now());

      // If the offer uid is wrong (stale or race condition), don't
      // accept the answer.
      if (!this.offerUid || answer['ouid'] != this.offerUid) {
        // Reset the peer.
        if (VERBOSE) {
          console.log(puid + ' sent wrong offeruid: got ' + answer['ouid'] +
            ' have ' + this.offerUid);
        }
        delete this.peers[puid];
        continue;
      }

      let peer = this.getPeerByUid(puid);
      if (peer == null) {
        // Answer from unknown peer. This is normal when a peer
        // connects to us using our offer before we find out
        // about it.

        // (can be forced by a misbehaving peer, but should
        // not normally happen...)
        if (this.getPeerType(puid) != PeerType.THEY_CALL) {
          if (VERBOSE)
            console.log('peer ' + puid + ' should not call me');
          continue;
        }

        if (this.listenConnection == null ||
            this.sendChannel == null) {
          // Already used up our listening connection, like if
          // two peers try to connect to the same offer.
          if (VERBOSE) {
            console.log('peer ' + puid + ' tried to connect but ' +
                        'listening channel is null');
          }
          continue;
        }

        peer = this.createTheyCallPeer(puid,
                                       this.listenConnection, this.sendChannel);
        this.listenConnection = null;
        this.sendChannel = null;
        this.offerToSend = null;
      }
      peer.deliverAnswer(sdp);
    }

    let others = json['others'];
    for (let other of others) {
      let puid = other['puid'];
      this.maybeAddPlayer(puid);
      // Update awol time if the server has seen this player
      // recently.
      let relAwol = window.performance.now() - (1000 * other['a']);
      this.markPlayerSeen(puid, relAwol);

      let peer = this.getPeerByUid(puid);
      if (VERBOSE) {
        console.log('other ' + puid + ' peer: ' + peer);
      }
      if (peer == null) {
        // Learned about a new player. This is normal when someone new
        // joins, or when joining a room that already has players.
        let peerType = this.getPeerType(puid);
        switch (peerType) {
        case PeerType.THEY_CALL:
          // If they call, we can actually leave the peer out of our
          // peer set, and it is covered by the "answer from unknown peer"
          // case above.
          // TODO: Is this actually better? Somehow it seems like it
          // would be useful to know about all the peers.
          continue;
          break;

        case PeerType.I_CALL:
          // If I call, and there is an offer available, act on it.
          let encodedOffer = other['s'];
          let ouid = other['ouid'];
          if (encodedOffer !== '' && ouid !== '') {
            let offer = decodeSdp(encodedOffer);
            peer = this.createICallPeer(puid, offer, ouid);
          }
          break;
        }
      }
    }

    // If the server knows of no offer, kick off creation of
    // a new one.
    if (json['ouid']) {
      // Server sends back the offer uid that it has. It could
      // be a stale one (if rejoining), but if we just sent one on
      // this request, then we want that.
      if (this.waitingForOfferUid) {
        this.offerUid = json['ouid'];
        this.waitingForOfferUid = false;
      }
    } else {
      // If the server has no active offer, create a new one.
      this.makeOffer();
    }
  }

  doJoin(room_name : string) {
    requestJSON(SERVER_URL + '/join/' + room_name, {}).
      then(json => {
        this.roomUid = json['room'];
        this.myUid = json['uid'];
        this.mySeq = json['seq'];
        // XXX probably should be done by driver; expose there instead
        window.location.hash =
          this.roomUid + '|' + this.myUid + '|' + this.mySeq;
        if (VERBOSE) {
          console.log('joined!')
          console.log(json);
        }

        this.addSelfPlayer();
        this.makeOffer();
      });
  }
}
