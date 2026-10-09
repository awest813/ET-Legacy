const assert=require('node:assert/strict'), createApp=require('./pwa.js');
class Element {
 constructor(){this.events={};this.hidden=false;this.textContent='';this.attrs={};}
 addEventListener(n,fn){(this.events[n]||=[]).push(fn);}send(n,e={}){for(const fn of this.events[n]||[])fn(e);}
 setAttribute(k,v){this.attrs[k]=v;}focus(){}showModal(){this.open=true;}close(){this.open=false;this.send('close');}
}
const tick=async()=>{for(let i=0;i<6;i++)await Promise.resolve();};
function fixture(fault={}){
 const elements={},doc=new Element(),win=new Element(),sw=new Element();let pause=0,resume=0,reload=0,requests=0,posts=[];
 doc.getElementById=id=>elements[id]||=new Element();doc.body=new Element();doc.fullscreenEnabled=!fault.unsupported;
 const active={postMessage:m=>{if(fault.post)throw Error('Worker stopped');posts.push(m);}},registration=new Element();registration.active=active;
 if(fault.update){registration.waiting=new Element();registration.waiting.state='installed';registration.waiting.postMessage=m=>{if(fault.updatePost)throw Error('Worker stopped');posts.push(m);};}
 sw.register=async()=>{if(fault.register)throw Error();return registration;};sw.controller=active;
 registration.update=async()=>{registration.updateCalls=(registration.updateCalls||0)+1;if(fault.updateCheck)throw Error('Network unavailable');};
 if(fault.registerSync)sw.register=()=>{throw Error('Registration denied');};
 win.isSecureContext=!fault.insecure;win.matchMedia=()=>({matches:!!fault.installed});
 const nav={serviceWorker:fault.noWorker?null:sw};const timers=new Map();let serial=0;
 if(fault.workerGetter)Object.defineProperty(nav,'serviceWorker',{get(){throw Error('SecurityError');}});
 doc.body.requestFullscreen=()=>{requests++;if(fault.fullscreen==='denied')return Promise.reject(Error());if(fault.fullscreen==='stalled')return;doc.fullscreenElement=doc.body;doc.send('fullscreenchange');return Promise.resolve();};
 doc.exitFullscreen=()=>{doc.fullscreenElement=null;doc.send('fullscreenchange');return Promise.resolve();};
 if(fault.prefixed){
  doc.body.requestFullscreen=undefined;doc.exitFullscreen=undefined;doc.webkitFullscreenEnabled=true;
  doc.body.webkitRequestFullscreen=()=>{requests++;doc.webkitFullscreenElement=doc.body;doc.send('webkitfullscreenchange');};
  doc.webkitExitFullscreen=()=>{doc.webkitFullscreenElement=null;doc.send('webkitfullscreenchange');};
 }
 const app=createApp({document:doc,window:win,navigator:nav,modalOpen(){return !!fault.modal;},pause(){pause++;},gesture(){},resume(){resume++;},reload(){reload++;},setTimeout(fn,ms){fn.delay=ms;assert.equal(this,win,'Browser timers require a Window receiver');const id=++serial;timers.set(id,fn);return id;},clearTimeout(id){assert.equal(this,win);timers.delete(id);}});
 return {app,doc,win,sw,e:elements,registration,timers,posts,get requests(){return requests;},get reload(){return reload;},get pause(){return pause;},get resume(){return resume;}};
}
(async()=>{
 let f=fixture();await tick();f.sw.send('message',{data:{type:'APP_READY'}});assert.match(f.e.appstatus.textContent,/App saved/);
 f.app.gameFilesReady(true);assert.match(f.e.appstatus.textContent,/Offline ready/);
 f.e.launchfullscreen.send('click');await tick();assert.equal(f.e.fullscreenbtn.textContent,'Exit fullscreen');assert.equal(f.e.launchfullscreen.attrs['aria-pressed'],'true');assert.equal(f.timers.size,0);
 f.e.fullscreenbtn.send('click');await tick();assert.equal(f.doc.fullscreenElement,null);assert.ok(f.pause>=2&&f.resume>=2);
 f.e.fullscreenbtn.send('click');await tick();f.doc.pointerLockElement=f.doc.body;f.doc.send('keydown',{key:'Escape'});assert.equal(f.doc.fullscreenElement,f.doc.body);
 f.doc.pointerLockElement=null;f.doc.send('keydown',{key:'Escape'});await tick();assert.equal(f.doc.fullscreenElement,null);
 f.e.launchfullscreen.send('click');await tick();f.app.handleEscape({key:'Escape'});await tick();assert.equal(f.doc.fullscreenElement,null,'Launcher Escape works when SDL filters document key events');
 f.e.appbtn.send('click');assert.equal(f.e.appdialog.open,true);f.e.closeapp.send('click');assert.equal(f.e.appdialog.open,false);
 f=fixture({fullscreen:'denied'});await tick();f.e.fullscreenbtn.send('click');await tick();assert.match(f.e.fullscreenstatus.textContent,/blocked/);assert.equal(f.e.launchfullstatus.textContent,f.e.fullscreenstatus.textContent);assert.equal(f.e.fullscreenbtn.disabled,false);
 f=fixture({fullscreen:'stalled'});await tick();f.e.fullscreenbtn.send('click');f.e.fullscreenbtn.send('click');assert.equal(f.requests,1);for(const fn of [...f.timers.values()])fn();assert.match(f.e.fullscreenstatus.textContent,/blocked/);
 f=fixture({unsupported:true});assert.equal(f.e.fullscreenbtn.disabled,true);
 f=fixture({prefixed:true});await tick();f.e.launchfullscreen.send('click');
 assert.equal(f.doc.webkitFullscreenElement,f.doc.body);assert.equal(f.e.fullscreenbtn.attrs['aria-pressed'],'true');assert.equal(f.timers.size,0);
 f.app.handleEscape({key:'Escape'});assert.equal(f.doc.webkitFullscreenElement,null);assert.equal(f.e.launchfullscreen.attrs['aria-pressed'],'false');
 for(const fault of [{insecure:true},{noWorker:true},{register:true},{registerSync:true},{workerGetter:true}]){
  f=fixture(fault);await tick();assert.match(f.e.appstatus.textContent,/HTTPS|unavailable/);assert.equal(f.reload,0);
  f.e.fullscreenbtn.send('click');await tick();assert.equal(f.doc.fullscreenElement,f.doc.body,'Optional storage failure leaves other controls working');
 }
 f=fixture({registerSync:true});await tick();f.sw.send('message',{data:{type:'APP_READY'}});f.app.gameFilesReady(true);
 f.e.retryapp.send('click');assert.match(f.e.appstatus.textContent,/saved offline app is still available/,'A failed explicit retry preserves the saved app and explains the update failure');
 assert.equal(f.e.retryapp.hidden,false);
 f.sw.send('message',{data:{type:'APP_READY'}});assert.match(f.e.appstatus.textContent,/Could not check/);
 for(const asyncFailure of [false,true]){
  f=fixture();await tick();f.sw.send('message',{data:{type:'APP_READY'}});
  f.sw.register=()=>{if(asyncFailure)return Promise.reject(Error('Offline'));throw Error('Permission denied');};
  f.e.retryapp.send('click');await tick();assert.match(f.e.appstatus.textContent,/Could not check/);
  f.sw.send('message',{data:{type:'APP_READY'}});assert.equal(f.e.retryapp.hidden,false);
  f.sw.register=()=>Promise.resolve(f.registration);f.e.retryapp.send('click');await tick();
  f.sw.send('message',{data:{type:'APP_READY'}});assert.match(f.e.appstatus.textContent,/App saved/);assert.equal(f.e.retryapp.hidden,true);
 }
 f=fixture({registerSync:true});await tick();f.sw.register=()=>Promise.resolve(f.registration);f.e.retryapp.send('click');await tick();
 assert.ok(f.posts.some(m=>m.type==='APP_STATUS'),'A denied registration can be retried after permission changes');
 f=fixture({update:true});await tick();assert.equal(f.e.updateapp.hidden,false);assert.equal(f.e.appbtn.textContent,'Update ready');
 f.sw.send('controllerchange');assert.equal(f.reload,0,'An update cannot automatically interrupt a match');
 f.e.updateapp.send('click');assert.equal(f.posts.at(-1).type,'ACTIVATE_UPDATE');f.sw.send('message',{data:{type:'APP_OTHER_TABS'}});assert.equal(f.e.updateapp.disabled,false);assert.match(f.e.appstatus.textContent,/other app tabs/);
 f.e.updateapp.send('click');f.sw.send('controllerchange');assert.equal(f.reload,1);
 f=fixture();await tick();let prevented=0,prompted=0;
 f.win.send('beforeinstallprompt',{preventDefault(){prevented++;},async prompt(){prompted++;},userChoice:Promise.resolve({outcome:'dismissed'})});assert.equal(f.e.installapp.hidden,false);
 f.e.installapp.send('click');f.e.installapp.send('click');await tick();assert.equal(prompted,1);assert.equal(prevented,1);
 f.win.send('appinstalled');assert.match(f.e.installhint.textContent,/installed/);assert.equal(f.e.installapp.hidden,true);
 f.sw.send('message',{data:{type:'APP_READY'}});f.registration.installing=new Element();f.registration.send('updatefound');
 f.registration.installing.state='redundant';f.registration.installing.send('statechange');assert.match(f.e.appstatus.textContent,/existing offline app/);
 f.e.retryapp.send('click');await tick();assert.ok(!f.posts.some(m=>m.type==='REPAIR_CACHE'),'An update failure does not repair or invalidate the usable old bundle');
 f.sw.send('message',{data:{type:'APP_MISSING'}});assert.equal(f.e.retryapp.hidden,false);f.e.retryapp.send('click');await tick();assert.ok(f.posts.some(m=>m.type==='REPAIR_CACHE'));
 const repairs=f.posts.filter(m=>m.type==='REPAIR_CACHE').length;
 f.e.retryapp.send('click');assert.equal(f.posts.filter(m=>m.type==='REPAIR_CACHE').length,repairs,'Repeated repair clicks share one operation');
 f.sw.send('message',{data:{type:'APP_MISSING'}});assert.equal(f.e.retryapp.disabled,true,'An old status reply cannot cancel an active repair');
 for(const fn of [...f.timers.values()])fn();assert.equal(f.e.retryapp.disabled,false);assert.match(f.e.appstatus.textContent,/three minutes/);
 const oldRepair=f.posts.filter(m=>m.type==='REPAIR_CACHE').at(-1);
 f.e.retryapp.send('click');
 const newRepair=f.posts.filter(m=>m.type==='REPAIR_CACHE').at(-1);
 f.sw.send('message',{data:{type:'APP_SAVE_FAILED',requestId:oldRepair.requestId}});
 assert.equal(f.e.retryapp.disabled,true,'A timed-out repair failure cannot cancel a newer retry');
 f.sw.send('message',{data:{type:'APP_READY',requestId:oldRepair.requestId}});
 assert.equal(f.e.retryapp.disabled,true,'A timed-out repair success cannot finish a newer retry');
 f.sw.send('message',{data:{type:'APP_READY'}});
 assert.equal(f.e.retryapp.disabled,true,'Unrelated status replies cannot finish a repair');
 const beforeProgress=[...f.timers.keys()][0];
 f.sw.send('message',{data:{type:'APP_SAVE_PROGRESS',requestId:newRepair.requestId,loaded:10,total:100}});
 assert.match(f.e.appstatus.textContent,/10%/,'Repair reports actual advancing bytes');
 const afterProgress=[...f.timers.keys()][0];assert.notEqual(afterProgress,beforeProgress,'Advancing bytes renew the inactivity deadline');
 assert.equal(f.timers.get(afterProgress).delay,180000,'Repair is bounded by inactivity rather than a 30-second whole-transfer limit');
 for(const progress of [{loaded:10,total:100},{loaded:9,total:100},{loaded:NaN,total:100},{loaded:101,total:100}]) {
  f.sw.send('message',{data:{type:'APP_SAVE_PROGRESS',requestId:newRepair.requestId,...progress}});
  assert.equal([...f.timers.keys()][0],afterProgress,'Invalid or repeated progress cannot extend a stalled repair');
 }
 f.sw.send('message',{data:{type:'APP_READY',requestId:newRepair.requestId}});assert.equal(f.timers.size,0);assert.equal(f.e.retryapp.hidden,true);
 f.sw.send('message',{data:{type:'APP_SAVE_FAILED',requestId:newRepair.requestId}});
 assert.equal(f.e.retryapp.hidden,true,'Late repair replies cannot replace a completed result');
 f=fixture();await tick();f.sw.send('message',{data:{type:'APP_MISSING'}});f.e.retryapp.send('click');
 const missingRepair=f.posts.filter(m=>m.type==='REPAIR_CACHE').at(-1);
 f.sw.send('message',{data:{type:'APP_MISSING',requestId:missingRepair.requestId}});
 assert.equal(f.e.retryapp.disabled,false,'Missing files after the current repair are recoverable immediately');
 assert.match(f.e.appstatus.textContent,/unavailable/);
 f=fixture({post:true});await tick();assert.match(f.e.appstatus.textContent,/unavailable/);assert.equal(f.e.retryapp.hidden,false);
 f.e.retryapp.send('click');assert.equal(f.e.retryapp.disabled,false);assert.equal(f.timers.size,0,'A failed repair post cannot leave a pending deadline');
 f=fixture({update:true});await tick();f.e.updateapp.send('click');assert.equal(f.e.updateapp.disabled,true);
 for(const fn of [...f.timers.values()])fn();assert.equal(f.e.updateapp.disabled,false);assert.match(f.e.appstatus.textContent,/did not respond/);
 f.sw.send('controllerchange');assert.equal(f.reload,0,'A late update does not force reload after the deadline');
 f.e.updateapp.send('click');f.sw.send('controllerchange');f.sw.send('controllerchange');assert.equal(f.reload,1);assert.equal(f.timers.size,0);
 f=fixture({update:true,updatePost:true});await tick();f.e.updateapp.send('click');assert.equal(f.e.updateapp.disabled,false);assert.equal(f.timers.size,0);
 f=fixture({update:true});await tick();f.e.updateapp.send('click');f.registration.waiting.state='redundant';f.registration.waiting.send('statechange');assert.equal(f.e.updateapp.disabled,false);assert.equal(f.timers.size,0);
 f=fixture();await tick();f.sw.send('message',{data:{type:'APP_READY'}});
 f.registration.installing=new Element();f.registration.installing.state='installing';f.registration.send('updatefound');
 for(let i=0;i<3;i++){f.e.retryapp.send('click');await tick();f.registration.send('updatefound');}
 assert.equal(f.registration.events.updatefound.length,1,'Registration retries do not accumulate update listeners');
 assert.equal(f.registration.installing.events.statechange.length,1,'The same installing worker is watched once');
 assert.equal(f.registration.updateCalls,3,'Retry must explicitly check for a newer worker rather than only registering an existing one');
 f=fixture({updateCheck:true});await tick();f.sw.send('message',{data:{type:'APP_READY'}});
 f.e.retryapp.send('click');await tick();assert.match(f.e.appstatus.textContent,/Could not check the app update/);
 f.sw.send('message',{data:{type:'APP_READY'}});assert.match(f.e.appstatus.textContent,/Could not check/,'A status reply from the previous saved bundle cannot hide an update failure');
 assert.equal(f.e.retryapp.hidden,false);
 const modal={modal:true};f=fixture(modal);await tick();f.e.fullscreenbtn.send('click');await tick();
 f.app.handleEscape({key:'Escape'});assert.equal(f.doc.fullscreenElement,f.doc.body,'Dialog Escape does not also exit fullscreen');
 modal.modal=false;f.app.handleEscape({key:'Escape'});await tick();assert.equal(f.doc.fullscreenElement,null);
 f=fixture();await tick();f.win.send('beforeinstallprompt',{preventDefault(){},prompt(){throw Error('Prompt unavailable');}});
 f.e.installapp.send('click');assert.match(f.e.appstatus.textContent,/Installation did not finish/);assert.equal(f.e.installapp.disabled,false);
 console.log('PWA/fullscreen UI: enter/exit, failure/timeout, unsupported APIs, focus cleanup, install dismissal, saved files, cache recovery and explicit update/reload passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
