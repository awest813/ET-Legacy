const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
const {webcrypto,createHash} = require('node:crypto');
const names=['etl.html','etl.js','etl.wasm','etl.data','manifest.webmanifest','icon-192.png','icon-512.png'];
const entries=names.map(url=>({url,sha256:createHash('sha256').update(`bundle:${url}`).digest('hex')}));
const source=fs.readFileSync(`${__dirname}/sw.js.in`,'utf8').replace('@PWA_VERSION@','fixture').replace('@PWA_ENTRIES@',entries.map(e=>JSON.stringify(e)).join(',')).replace('@PWA_RECOVERY_JSON@',JSON.stringify(fs.readFileSync(`${__dirname}/pwa/recovery.html`,'utf8')));
function fixture(failure) {
 const fault=typeof failure==='object' ? failure : {kind:failure};
 const events={}, storage=new Map(), messages=[], clients=[{url:'https://game.test/play/?map=radar',postMessage:m=>messages.push(m)}]; let skipped=0, network=0;
 const caches={async open(key) { if(fault.kind==='storage')throw Error('Storage denied'); if(!storage.has(key)) storage.set(key,new Map()); const map=storage.get(key); return {
  async put(url,response) { map.set(url,response.clone()); },async match(url) { return map.get(url)?.clone(); }
 }; },async keys(){return [...storage.keys()];},async delete(key){return storage.delete(key);} };
 const self={location:{href:'https://game.test/play/sw.js'},addEventListener(n,fn){events[n]=fn;},clients:{async matchAll(){return clients;}},async skipWaiting(){skipped++;}};
 vm.runInNewContext(source,{self,caches,URL,Response,crypto:webcrypto,Uint8Array,fetch:async url=>{
  network++; const name=new URL(url).pathname.split('/').pop(); if(fault.kind==='network'&&name==='etl.wasm') throw Error('Disconnected');
  return new Response(fault.kind==='corrupt'&&name==='etl.wasm'?'changed':`bundle:${name}`,{headers:{'Cross-Origin-Embedder-Policy':'require-corp'}});
 }});
 async function dispatch(name,extra={}) {let pending;events[name]({waitUntil(p){pending=p;},...extra});await pending;}
 async function request(path,extra={}) {let pending;events.fetch({request:{url:new URL(path,'https://game.test/play/').href,method:'GET',mode:'cors',headers:new Headers(),...extra},respondWith(p){pending=p;}});return pending?await pending:null;}
 return {storage,messages,clients,dispatch,request,get skipped(){return skipped;},get network(){return network;}};
}
(async()=>{
 const f=fixture();f.storage.set('etl-app-%2Fplay%2F-old',new Map());f.storage.set('unrelated',new Map());
 await f.dispatch('install');assert.equal(f.network,7);await f.dispatch('activate');
 assert.ok(!f.storage.has('etl-app-%2Fplay%2F-old'));assert.ok(f.storage.has('unrelated'));
 assert.equal(await (await f.request('./?map=oasis',{mode:'navigate'})).text(),'bundle:etl.html');
 assert.equal(await (await f.request('etl.data')).text(),'bundle:etl.data');assert.equal(f.network,7,'Engine reads use the coherent cached version');
 assert.equal((await f.request('etl.wasm')).headers.get('Cross-Origin-Embedder-Policy'),'require-corp');
 for(const path of ['assets/pak0.pk3','assets/manifest.json','network/config.json','debug.log','https://elsewhere.test/etl.js']) assert.equal(await f.request(path),null);
 assert.equal(await f.request('etl.wasm',{headers:new Headers({Range:'bytes=0-4'})}),null);
 assert.equal(await f.request('etl.js',{method:'POST'}),null);assert.equal(await f.request('other',{mode:'navigate'}),null);
 await f.dispatch('message',{data:{type:'APP_STATUS'},source:f.clients[0]});assert.equal(f.messages.at(-1).type,'APP_READY');
 f.clients.push({url:'https://game.test/play/etl.html?map=oasis#game',postMessage(){}});await f.dispatch('message',{data:{type:'ACTIVATE_UPDATE'},source:f.clients[0]});
 assert.equal(f.skipped,0);assert.equal(f.messages.at(-1).type,'APP_OTHER_TABS');
 f.clients.pop();await f.dispatch('message',{data:{type:'ACTIVATE_UPDATE'},source:f.clients[0]});assert.equal(f.skipped,1);
 const scoped=fixture(), unrelatedMessages=[];
 for(const url of ['https://game.test/docs/', 'https://game.test/playground/',
                  'https://game.test/play/assets/map-guide.html', 'https://other.test/play/', 'about:blank']) {
  scoped.clients.push({url,postMessage:m=>unrelatedMessages.push(m)});
 }
 await scoped.dispatch('message',{data:{type:'ACTIVATE_UPDATE'},source:scoped.clients[0]});
 assert.equal(scoped.skipped,1,'Unrelated pages on the origin cannot block a game update');
 await scoped.dispatch('install');await scoped.dispatch('activate');
 assert.equal(unrelatedMessages.length,0,'App activation only notifies game pages');
 assert.equal(scoped.messages.at(-1).type,'APP_READY');
 scoped.clients.push({url:'https://game.test/play/network/recovery',postMessage(){}});
 await scoped.dispatch('message',{data:{type:'ACTIVATE_UPDATE'},source:scoped.clients[0]});
 assert.equal(scoped.skipped,1,'An open app recovery page still protects the other game tab');
 assert.equal(scoped.messages.at(-1).type,'APP_OTHER_TABS');
 f.storage.get('etl-app-%2Fplay%2F-fixture').delete('https://game.test/play/etl.wasm');
 await f.dispatch('message',{data:{type:'APP_STATUS'},source:f.clients[0]});assert.equal(f.messages.at(-1).type,'APP_MISSING');
 assert.equal((await f.request('etl.wasm')).status,503,'Missing cached code does not fall back to another version');
 const recovery=await f.request('./',{mode:'navigate'});const html=await recovery.text();
 assert.equal(recovery.status,503);assert.match(html,/Restore offline play/);
 assert.equal(recovery.headers.get('Cross-Origin-Embedder-Policy'),'require-corp');
 // Exercise the actual recovery page script with only browser APIs mocked.
 async function recoveryUI(kind) {
  const events={},elements={retry:{addEventListener(n,fn){this[n]=fn;}},status:{},launcherlink:{}};
  const sw={addEventListener(n,fn){events[n]=fn;},async getRegistration(scope){assert.equal(scope,kind==='standalone'?'../':'./');return registration;}};
  const posts=[],delayed=[],registration={async update(){if(kind==='offline')throw Error('Offline');if(kind==='late')await new Promise(resolve=>delayed.push(resolve));},active:{postMessage(m){posts.push(m);}}};
  if(kind==='update')registration.waiting={postMessage(m){posts.push(m);}};
  if(kind==='storage')registration.active.postMessage=()=>{throw Error('Storage denied');};
  let reloads=0;const timers=new Map();let serial=0;
  const nav={serviceWorker:kind==='noWorker'||kind==='insecure'?undefined:sw};
  if(kind==='blockedWorker')Object.defineProperty(nav,'serviceWorker',{get(){throw Error('SecurityError');}});
  if(kind==='unregistered')sw.getRegistration=async()=>null;
  if(kind==='lateRegistration')sw.getRegistration=()=>new Promise(resolve=>delayed.push(resolve));
  vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],{
   document:{getElementById:id=>elements[id]},navigator:nav,isSecureContext:kind!=='insecure',location:{pathname:kind==='standalone'?'/network/recovery':'/play/',replace(url){assert.equal(url,'../');reloads++;},reload(){reloads++;}},
   setTimeout(fn){const id=++serial;timers.set(id,fn);return id;},clearTimeout(id){timers.delete(id);}
  });
  const clicked=elements.retry.click ? elements.retry.click() : undefined;if(kind!=='late'&&kind!=='lateRegistration')await clicked;
  return {events,e:elements,posts,timers,clicked,delayed,get reloads(){return reloads;}};
 }
 for(const kind of ['noWorker','blockedWorker','insecure']) {
  const unavailable=await recoveryUI(kind);
  assert.equal(unavailable.e.retry.disabled,true,'Unavailable offline APIs cannot leave an inert repair action');
  assert.match(unavailable.e.status.textContent,kind==='insecure'?/HTTPS|localhost/:/browser|offline saving/i);
  assert.equal(unavailable.posts.length,0);assert.equal(unavailable.timers.size,0);
 }
 let unregistered=await recoveryUI('unregistered');
 assert.match(unregistered.e.status.textContent,/launcher.*connected/i);
 assert.equal(unregistered.e.retry.disabled,false);assert.equal(unregistered.timers.size,0);
 const lateRegistration=await recoveryUI('lateRegistration');
 for(const fn of [...lateRegistration.timers.values()])fn();
 const nextRegistration=lateRegistration.e.retry.click();
 lateRegistration.delayed[0](null);await lateRegistration.clicked;
 assert.equal(lateRegistration.e.retry.disabled,true,'A late missing registration cannot cancel a newer repair');
 lateRegistration.delayed[1](null);await nextRegistration;
 assert.equal(lateRegistration.e.retry.disabled,false);assert.equal(lateRegistration.timers.size,0);
 let ui=await recoveryUI('offline');assert.equal(ui.posts.at(-1).type,'REPAIR_CACHE');
 ui.events.message({data:{type:'APP_SAVE_FAILED',requestId:ui.posts.at(-1).requestId}});assert.equal(ui.e.retry.disabled,false);assert.equal(ui.timers.size,0);
 ui=await recoveryUI('repair');ui.events.message({data:{type:'APP_READY',requestId:ui.posts.at(-1).requestId}});assert.equal(ui.reloads,1);
 ui=await recoveryUI('standalone');assert.equal(ui.e.launcherlink.href,'../');ui.events.message({data:{type:'APP_READY',requestId:ui.posts.at(-1).requestId}});assert.equal(ui.reloads,1,'Independent repair returns to the launcher instead of reloading the recovery route');
 ui=await recoveryUI('update');assert.equal(ui.posts.at(-1).type,'ACTIVATE_UPDATE');
 ui.events.message({data:{type:'APP_READY'}});assert.equal(ui.reloads,0,'Update activation must wait for controller replacement');
 ui.events.message({data:{type:'APP_OTHER_TABS'}});assert.equal(ui.e.retry.disabled,false);assert.match(ui.e.status.textContent,/other app tabs/);
 await ui.e.retry.click();ui.events.controllerchange();assert.equal(ui.reloads,1);
 ui=await recoveryUI('storage');assert.equal(ui.e.retry.disabled,false);assert.match(ui.e.status.textContent,/storage/);
 ui=await recoveryUI('repair');for(const fn of [...ui.timers.values()])fn();assert.equal(ui.e.retry.disabled,false);
 const firstRepair=ui.posts.at(-1);
 ui.events.message({data:{type:'APP_READY',requestId:firstRepair.requestId}});assert.equal(ui.reloads,0,'Late repair completion cannot force a timed-out reload');
 await ui.e.retry.click();const nextRepair=ui.posts.at(-1);
 assert.notEqual(firstRepair.requestId,nextRepair.requestId);
 ui.events.message({data:{type:'APP_SAVE_FAILED',requestId:firstRepair.requestId}});
 assert.equal(ui.e.retry.disabled,true,'A stale failure cannot cancel a newer recovery attempt');
 ui.events.message({data:{type:'APP_READY',requestId:firstRepair.requestId}});
 ui.events.message({data:{type:'APP_READY'}});
 assert.equal(ui.reloads,0,'Stale or unrelated replies cannot reload a newer recovery attempt');
 ui.events.message({data:{type:'APP_READY',requestId:nextRepair.requestId}});
 assert.equal(ui.reloads,1,'Only the current repair can reopen the launcher');
 ui=await recoveryUI('late');for(let i=0;i<6;i++)await Promise.resolve();for(const fn of [...ui.timers.values()])fn();
 const second=ui.e.retry.click();for(let i=0;i<6;i++)await Promise.resolve();ui.delayed[0]();await ui.clicked;
 assert.equal(ui.posts.length,0,'A timed-out update cannot send commands during a newer attempt');
 ui.delayed[1]();await second;assert.equal(ui.posts.length,1);
 await f.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:17},source:f.clients[0]});assert.equal(f.messages.at(-1).type,'APP_READY');assert.equal(f.messages.at(-1).requestId,17);
 for(const failure of ['network','corrupt']) {const broken=fixture(failure);broken.storage.set('unrelated',new Map());broken.storage.set('etl-app-%2Fplay%2F-old',new Map());await assert.rejects(broken.dispatch('install'));assert.deepEqual([...broken.storage.keys()],['unrelated','etl-app-%2Fplay%2F-old']);}
 const broken=fixture('network');await broken.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:18},source:broken.clients[0]});assert.equal(broken.messages.at(-1).type,'APP_SAVE_FAILED');assert.equal(broken.messages.at(-1).requestId,18);
 const resumed=fixture();await resumed.dispatch('install');
 const resumedCache=resumed.storage.get('etl-app-%2Fplay%2F-fixture');
 resumedCache.delete('https://game.test/play/icon-512.png');
 await resumed.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:30},source:resumed.clients[0]});
 assert.equal(resumed.network,8,'Repair downloads only the missing file after verifying saved bytes');
 resumedCache.set('https://game.test/play/etl.wasm',new Response('corrupted cached engine'));
 await resumed.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:31},source:resumed.clients[0]});
 assert.equal(resumed.network,9,'A corrupt saved file is replaced instead of being trusted');
 assert.equal(await (await resumed.request('etl.wasm')).text(),'bundle:etl.wasm');
 const concurrent=fixture(), peerMessages=[];
 const peer={url:'https://game.test/play/network/recovery',postMessage:m=>peerMessages.push(m)};
 await Promise.all([
  concurrent.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:40},source:concurrent.clients[0]}),
  concurrent.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:41},source:peer})
 ]);
 assert.equal(concurrent.network,7,'Concurrent repairs share one verified bundle download');
 assert.equal(concurrent.messages.at(-1).requestId,40);
 assert.equal(peerMessages.at(-1).requestId,41);
 assert.equal(peerMessages.at(-1).type,'APP_READY');
 const transientFault={kind:'network'}, transient=fixture(transientFault);
 await Promise.all([40,41].map(requestId=>transient.dispatch('message',{data:{type:'REPAIR_CACHE',requestId},source:transient.clients[0]})));
 assert.equal(transient.network,3,'Failed concurrent repairs also share their transfer');
 assert.equal(transient.messages.filter(m=>m.type==='APP_SAVE_FAILED').length,2);
 transientFault.kind=null;
 await transient.dispatch('message',{data:{type:'REPAIR_CACHE',requestId:42},source:transient.clients[0]});
 assert.equal(transient.network,8,'Retry after a failed shared transfer retains its two verified files');
 assert.equal(transient.messages.at(-1).type,'APP_READY');
 assert.equal(transient.messages.at(-1).requestId,42);
 const denied=fixture('storage');await denied.dispatch('message',{data:{type:'APP_STATUS'},source:denied.clients[0]});assert.equal(denied.messages.at(-1).type,'APP_SAVE_FAILED');
 assert.match(await (await denied.request('./',{mode:'navigate'})).text(),/Restore offline play/);
 console.log('PWA worker: atomic verified bundle, offline navigation/code, version consistency, live network isolation, scoped cleanup, update-tab guard, eviction and repair passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
