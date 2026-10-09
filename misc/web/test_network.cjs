const assert = require('node:assert/strict');
const createNetwork = require('./network.js');
function harness() {
    const sockets = [], timers = new Map(), states = [], failures = []; let clock = 0, id = 0;
    class FakeSocket {
        constructor(url, protocol) { this.url=url; this.protocol=protocol; this.readyState=0; this.bufferedAmount=0; this.sent=[]; sockets.push(this); }
        close() { this.readyState=3; }
        send(bytes) { this.sent.push(Array.from(bytes)); }
        open() { this.readyState=1; this.onopen(); }
        packet(bytes) { this.onmessage({data:Uint8Array.from(bytes).buffer}); }
    }
    const net = createNetwork({WebSocket:FakeSocket, now:()=>clock,
        setTimeout(fn) { timers.set(++id,fn); return id; }, clearTimeout(id) { timers.delete(id); },
        onChange(...args) { states.push(args); }, onFailure(...args) { failures.push(args); }});
    const config = {version:1,enabled:true,relay:'ws://localhost:8082/relay',serverLabel:'Audit ET server',engineAddress:'192.0.2.1:27960'};
    net.configure(config,'http://localhost:8081/');
    return {net,config,sockets,timers,states,failures,advance(ms) {clock+=ms;},connect() {let error='pending';net.connect(e=>{error=e;});sockets.at(-1).open();assert.equal(error,null);return sockets.at(-1);}};
}
const h=harness(), s=h.connect();
assert.equal(h.states.at(-1)[2].serverLabel,'Audit ET server');
assert.equal(h.net.allowed('example.com:27960'),false);
assert.equal(h.net.allowed('192.0.2.1:27960'),true);
assert.equal(h.net.allowed('localhost'),true);
assert.equal(h.net.send(Uint8Array.from([0,255,1])),true);
assert.deepEqual(s.sent,[[0,255,1]]);
s.packet([1,2]);s.packet([3]);
assert.deepEqual(Array.from(h.net.receive(32768)),[1,2]);
assert.deepEqual(Array.from(h.net.receive(32768)),[3]);
assert.equal(h.net.receive(32768),null);
h.net.begin(false); assert.equal(h.net.frame(3,0,''),0);
h.net.frame(8,42,''); assert.equal(h.states.at(-1)[0],'Online');assert.match(h.states.at(-1)[1],/42 ms/);
s.packet([8]);h.advance(250);h.net.frame(8,42,'');
assert.equal(h.states.at(-1)[2].received,3,'Packet counters refresh even when state and ping remain constant');
assert.equal(h.net.frame(1,0,'Server full'),1);assert.equal(h.net.frame(1,0,''),0);assert.match(h.states.at(-1)[1],/Server full/);
assert.equal(s.readyState,3);
assert.equal(h.states.at(-1)[2].serverLabel,'Audit ET server','Failures retain the configured server context');
const slow=harness(), slowSocket=slow.connect();slow.net.begin(false);slow.net.frame(3,0,'');slow.advance(15001);
assert.equal(slow.net.frame(3,0,''),1);assert.match(slow.states.at(-1)[1],/did not finish/);
const retrySocket=slow.connect();slow.net.begin(true);assert.equal(slow.net.frame(3,0,''),2);
slow.net.frame(3,0,''); // Native connect has now taken effect.
slowSocket.packet([99]);assert.equal(slow.net.receive(32768),null,'Old socket cannot inject packets after retry');
retrySocket.packet([42]);assert.deepEqual(Array.from(slow.net.receive(32768)),[42]);
const timeout=harness();let timeoutError;timeout.net.connect(e=>timeoutError=e);
Array.from(timeout.timers.values())[0]();assert.match(timeoutError.message,/did not respond/);
timeout.sockets[0].open();assert.equal(timeout.sockets[0].readyState,3,'Late open is closed');
for (const data of ['text',new ArrayBuffer(0),new ArrayBuffer(32769)]) {
    const bad=harness(), socket=bad.connect();socket.onmessage({data});assert.equal(socket.readyState,3);assert.match(bad.states.at(-1)[1],/invalid game packet/);
}
const congestion=harness(), congested=congestion.connect();congested.bufferedAmount=262144;
assert.equal(congestion.net.send(Uint8Array.of(1)),false);assert.match(congestion.states.at(-1)[1],/send queue/);
const burst=harness(), burstSocket=burst.connect();burst.net.begin(false);
for(let i=0;i<256;i++)burstSocket.packet([i&255]);
assert.equal(burstSocket.readyState,1,'Map loading tolerates a normal snapshot burst');
for(let i=0;i<256;i++)assert.equal(burst.net.receive(32768)[0],i&255,'Loading preserves reliable packet order');
const byteFlood=harness(), byteSocket=byteFlood.connect();
for(let i=0;i<257;i++)byteSocket.onmessage({data:new ArrayBuffer(32768)});
assert.equal(byteSocket.readyState,3,'Receive bytes remain bounded during loading');
const flood=harness(), flooding=flood.connect();for(let i=0;i<1025;i++)flooding.packet([i&255]);
assert.equal(flooding.readyState,3);assert.equal(flood.net.receive(32768),null);
assert.match(flood.states.at(-1)[1],/could not keep up/);
const stalled=harness(), stalledSocket=stalled.connect();stalled.net.begin(false);stalled.net.frame(8,40,'');stalled.advance(6000);
for(let i=0;i<1025;i++)stalledSocket.packet([i&255]);
assert.match(stalled.states.at(-1)[1],/stopped updating.*Return to this tab/,'A stalled frame loop needs recovery guidance, not a misleading unstable-network claim');
const overflow=harness(), large=overflow.connect();large.packet([1,2]);assert.equal(overflow.net.receive(1),null);assert.match(overflow.states.at(-1)[1],/receive buffer/);
const cancelled=harness();let cancelledError;cancelled.net.connect(e=>cancelledError=e);cancelled.net.close();assert.match(cancelledError.message,/cancelled/);
const invalid=harness();for(const config of [{...invalid.config,enabled:false},{...invalid.config,engineAddress:'8.8.8.8:53'},{...invalid.config,serverLabel:'  '},{...invalid.config,relay:'https://example.com/'},{...invalid.config,relay:'ws://localhost/wrong'},{...invalid.config,relay:'ws://localhost/'},{...invalid.config,relay:'ws://user:secret@localhost/relay'}])
    assert.throws(()=>invalid.net.configure(config,'http://localhost:8081/'));
assert.throws(()=>invalid.net.configure(invalid.config,'https://example.com/'),/secure/);
const publicRelay=harness(), publicConfig={...publicRelay.config,publicServers:true,serverId:'a'.repeat(32),relay:'ws://localhost:8082/relay/'+'A'.repeat(100)};
publicRelay.net.configure(publicConfig,'http://localhost:8081/');
assert.equal(publicRelay.connect().url,publicConfig.relay,'Selected signed relay routes reach the transport unchanged');
for (const value of [{...publicConfig,publicServers:false},{...publicConfig,serverId:'host:27960'},{...publicConfig,relay:publicConfig.relay+'?server=127.0.0.1'},{...publicConfig,relay:'ws://localhost:8082/relay/8.8.8.8:27960'}])
    assert.throws(()=>publicRelay.net.configure(value,'http://localhost:8081/'));
const idle=harness();idle.connect();idle.net.begin(false);idle.net.frame(1,0,'');idle.advance(15001);
assert.equal(idle.net.frame(1,0,''),1,'A native client that never begins the handshake times out');
assert.match(idle.states.at(-1)[1],/did not start/);
const replacing=harness();replacing.connect();replacing.net.begin(false);replacing.net.frame(8,30,'');
replacing.net.connect(()=>{});const replacement=replacing.sockets.at(-1);
assert.equal(replacing.states.at(-1)[2].opening,true);
assert.equal(replacing.net.frame(8,30,''),1,'Opening a replacement disconnects the running native client');
replacement.open();replacing.net.begin(true);
assert.equal(replacing.net.frame(8,30,''),2);
assert.equal(replacing.states.at(-1)[0],'Preparing connection','Old match state cannot claim reconnect succeeded');
replacement.packet([7]);assert.equal(replacing.net.receive(32768),null);
assert.equal(replacing.net.send(Uint8Array.of(1)),false,'Old match packets cannot enter the replacement transport');
replacing.net.frame(8,30,'');assert.equal(replacing.states.at(-1)[0],'Preparing connection');
replacing.net.frame(3,0,'');assert.equal(replacing.states.at(-1)[0],'Contacting server');
assert.deepEqual(Array.from(replacing.net.receive(32768)),[7]);
assert.equal(replacing.net.send(Uint8Array.of(1)),true);
const stuckRetry=harness();stuckRetry.connect();stuckRetry.net.begin(true);stuckRetry.net.frame(8,0,'');stuckRetry.advance(15001);
assert.equal(stuckRetry.net.frame(8,0,''),1,'Reconnect command that never executes cannot wait forever');
const failedRetry=harness();failedRetry.connect();failedRetry.net.begin(false);failedRetry.net.frame(8,0,'');
failedRetry.net.connect(()=>{});failedRetry.sockets.at(-1).onerror();
failedRetry.net.userDisconnect();
assert.equal(failedRetry.failures.at(-1)[1],true,'Retry retains knowledge that the native engine is running');
assert.equal(failedRetry.states.at(-1)[2].failed,true,'Automatic native cleanup preserves the transport failure');
assert.equal(failedRetry.net.frame(8,0,''),1);
for(const [reason,code,pattern] of [['Relay is full',1013,/relay is full/],['Packet rate exceeded',1008,/rate limit/],
    ['UDP server unavailable',1011,/could not reach/],['',1009,/exceeded/],['constructor',1000,/disconnected/]]) {
    const closed=harness(), socket=closed.connect();socket.onclose({reason,code});
    assert.match(closed.states.at(-1)[1],pattern);
}
const pingUpdates=harness();pingUpdates.connect();pingUpdates.net.begin(false);pingUpdates.net.frame(8,1,'');
const before=pingUpdates.states.length;
for(let i=0;i<31;i++) {pingUpdates.advance(8);pingUpdates.net.frame(8,i+2,'');}
assert.equal(pingUpdates.states.length,before,'Ping changes do not update the UI every game frame');
pingUpdates.advance(8);pingUpdates.net.frame(8,50,'');assert.equal(pingUpdates.states.length,before+1);
pingUpdates.net.frame(4,0,'');assert.equal(pingUpdates.states.at(-1)[0],'Joining server','Phase changes remain immediate');
const cycling=harness();cycling.connect();cycling.net.begin(false);
for(let i=0;i<9;i++) { cycling.net.frame(i%2 ? 4 : 3,0,''); cycling.advance(10000); }
assert.equal(cycling.net.frame(4,0,''),1,'Changing handshake phases cannot extend the whole attempt indefinitely');
assert.match(cycling.states.at(-1)[1],/too long/);
cycling.connect();cycling.net.begin(true);assert.equal(cycling.net.frame(1,0,''),2);
assert.equal(cycling.net.frame(3,0,''),0,'Retry starts with a fresh overall deadline');
const changingMap=harness();changingMap.connect();changingMap.net.begin(false);changingMap.net.frame(8,0,'');changingMap.advance(120000);
assert.equal(changingMap.net.frame(6,0,''),0,'An online server changing maps gets a fresh loading budget');
changingMap.advance(30001);assert.equal(changingMap.net.frame(6,0,''),1);
assert.match(changingMap.states.at(-1)[1],/map/);
for (const [phase, hint] of [[3,'contacting server'],[4,'joining server'],[5,'receiving game state'],[6,'loading map'],[7,'waiting for match']]) {
    const stalled=harness();stalled.connect();stalled.net.begin(false);stalled.net.frame(phase,0,'');stalled.advance(phase>=5 ? 30001 : 15001);
    assert.equal(stalled.net.frame(phase,0,''),1);
    assert.ok(stalled.states.at(-1)[1].includes(hint),'A stalled connection identifies its failed phase');
}
const departing=harness(), departureSocket=departing.connect();departing.net.begin(false);departing.net.frame(8,0,'');
departing.net.userDisconnect();assert.equal(departureSocket.readyState,1,'Native departure can send its final game packets before transport closes');
assert.equal(departing.net.send(Uint8Array.of(9)),true);
assert.equal(departing.net.frame(1,0,'Disconnected from server'),0,'Intentional native departure needs no redundant disconnect command');
assert.equal(departureSocket.readyState,3);assert.equal(departing.states.at(-1)[0],'Disconnected');assert.equal(departing.failures.length,0);
const leaving=harness(), leavingSocket=leaving.connect();leaving.net.begin(false);leaving.net.frame(8,0,'');let left=false;
leaving.net.close('Left the server',()=>{left=true;});
assert.equal(leavingSocket.readyState,1,'Browser disconnect keeps the original peer for native final packets');
assert.equal(leaving.states.at(-1)[2].closing,true);
assert.equal(leaving.net.frame(8,0,''),1);
leaving.net.userDisconnect();assert.equal(leaving.net.send(Uint8Array.of(9)),true);
assert.equal(left,false,'Returning to the launcher waits for native departure');
leaving.net.frame(1,0,'');assert.equal(leavingSocket.readyState,3);assert.equal(left,true);
assert.equal(leaving.states.at(-1)[1],'Left the server');assert.equal(leaving.timers.size,0);
const stalledClose=harness();stalledClose.connect();stalledClose.net.begin(false);let returned=false;
stalledClose.net.close('',()=>{returned=true;});Array.from(stalledClose.timers.values())[0]();
assert.equal(returned,true,'A stopped engine cannot hang the return action indefinitely');
const cleanup=harness();cleanup.connect();cleanup.net.begin(false);cleanup.net.frame(8,0,'');cleanup.net.connect(()=>{});
cleanup.net.userDisconnect();cleanup.sockets.at(-1).open();cleanup.net.begin(true);
assert.equal(cleanup.net.frame(1,0,''),2,'Automatic native cleanup must not cancel a replacement transport');
cleanup.net.userDisconnect();assert.equal(cleanup.net.frame(3,0,''),0);
assert.equal(cleanup.states.at(-1)[0],'Contacting server');
console.log('Browser network checks passed (binary packets, deadlines, native reconnect isolation, cancellation, close reasons, status throttling, queues, validation).');
