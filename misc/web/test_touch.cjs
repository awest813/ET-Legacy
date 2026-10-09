const assert = require('node:assert/strict');
const createTouch = require('./touch.js');
class Element {
 constructor(attrs = {}) { this.attrs = attrs; this.events = {}; this.style = {}; this.children = []; this.hidden = false; this.captures = new Set(); }
 addEventListener(name, fn) { (this.events[name] ||= []).push(fn); }
 setAttribute(name, value) { this.attrs[name] = String(value); }
 getAttribute(name) { return this.attrs[name]; }
 removeAttribute(name) { delete this.attrs[name]; }
 querySelectorAll(selector) { const attr=selector.slice(1,-1); return this.children.filter(child=>Object.hasOwn(child.attrs,attr)); }
 getBoundingClientRect() { return {left:0, top:0, width:100, height:100}; }
 setPointerCapture(id) { this.captures.add(id); }
 releasePointerCapture(id) { this.captures.delete(id); }
 send(type, id = 1, x = 50, y = 50, extra = {}) {
  const event = {type, pointerId:id, clientX:x, clientY:y, pointerType:'touch', button:0, detail:1,
   preventDefault() {}, stopPropagation() {}, ...extra};
  for (const fn of this.events[type] || []) fn(event);
 }
}
function fixture(coarse = true, saved, denied = false, primaryPointer) {
 const elements = Object.fromEntries(['touchbtn','touchcontrols','touchtools','touchmove','touchthumb','touchlook','canvas'].map(id => [id,new Element()]));
 elements.touchcontrols.children = [1,2,4,8,16,32,64].map(bit => new Element({'data-touch-bit':bit}));
 const alt = new Element({'data-touch-action':8});elements.touchcontrols.children.push(alt);
 elements.touchtools.children = [1,2,4,16,32].map(bit => new Element({'data-touch-action':bit}));
 const doc = new Element(), win = new Element(); doc.body = new Element(); doc.getElementById = id => elements[id];
 if (primaryPointer) win.matchMedia = query => { assert.equal(query,'(pointer: coarse)'); if (primaryPointer.denied) throw Error('blocked'); return {matches:primaryPointer.coarse}; };
 let gestures = 0;
 const touch = createTouch({document:doc, window:win, canvas:elements.canvas, coarse:primaryPointer ? undefined : coarse,
  navigator:{maxTouchPoints:primaryPointer ? primaryPointer.points : 0},
  storage:{getItem() { if (denied) throw Error(); return saved; },setItem() { if (denied) throw Error(); }}, gesture() { gestures++; }});
 touch.frame(0,true,true);
 return {touch, doc, win, ...elements, fire:elements.touchcontrols.children[0], jump:elements.touchcontrols.children[1], prone:elements.touchcontrols.children[6],alt, team:elements.touchtools.children[0], gestures:() => gestures};
}
let f = fixture();
f.touchmove.send('pointerdown',1,50,0); f.touchlook.send('pointerdown',2,70,50); f.fire.send('pointerdown',3);
f.touchlook.send('pointermove',2,90,40);
assert.deepEqual(f.touch.poll(),[0,1,20,-10,1]);
assert.deepEqual(f.touch.poll(),[0,1,0,0,1]); // Look is consumed once, movement/fire remain held.
f.fire.send('pointerdown',4); f.fire.send('pointerup',3);
assert.equal(f.touch.poll()[4],1); // Releasing one finger does not release another.
f.fire.send('pointerup',4); f.touchmove.send('pointerup',1); f.touchlook.send('pointerup',2);
assert.deepEqual(f.touch.poll(),[0,0,0,0,0]);
f.jump.send('pointerdown',5); f.jump.send('pointerup',5);
assert.equal(f.touch.poll()[4],2); assert.equal(f.touch.poll()[4],0); // Short taps survive until next usercmd.
f.fire.send('pointerdown',6); f.fire.send('pointercancel',6);
assert.equal(f.touch.poll()[4],0); assert.equal(f.fire.captures.size,0);
f.touchlook.send('pointerdown',20); f.touchlook.send('pointermove',20,90,80); f.touchlook.send('pointercancel',20);
assert.deepEqual(f.touch.poll(),[0,0,0,0,0]);
f.touchmove.send('pointerdown',7,53,50); assert.equal(f.touch.poll()[0],0); // Dead zone.
f.touchmove.send('pointermove',7,500,-500);
let input = f.touch.poll(); assert.ok(Math.abs(Math.hypot(input[0], input[1])-1) < 1e-9);
f.touchmove.send('lostpointercapture',7); assert.deepEqual(f.touch.poll(),[0,0,0,0,0]);
f.team.send('pointerdown',8); assert.equal(f.touch.pollActions(),1); assert.equal(f.touch.pollActions(),0);
f.team.send('click',8,50,50,{detail:1}); assert.equal(f.touch.pollActions(),0);
f.team.send('click',8,50,50,{detail:0}); assert.equal(f.touch.pollActions(),1);
for (const button of f.touchcontrols.querySelectorAll('[data-touch-bit]')) {
 button.send('click',8,50,50,{detail:0}); assert.equal(f.touch.poll()[4],button.getAttribute('data-touch-bit'));
 assert.equal(f.touch.poll()[4],0,'Keyboard activation is consumed once');
 button.send('click',8,50,50,{detail:1}); assert.equal(f.touch.poll()[4],0,'Pointer click does not duplicate a tap');
}
f.alt.send('pointerdown',30);f.alt.send('pointerup',30);f.alt.send('click',30,50,50,{detail:1});
assert.equal(f.touch.pollActions(),8,'An alternate-fire tap produces one native action');assert.equal(f.touch.pollActions(),0);
f.alt.send('click',30,50,50,{detail:0});assert.equal(f.touch.pollActions(),8);
for (const button of f.touchtools.children.slice(3)) {
 button.send('pointerdown',32);button.send('pointerup',32);button.send('click',32,50,50,{detail:1});
 assert.equal(f.touch.pollActions(),button.getAttribute('data-touch-action'),'Prompt tap produces one native response');
 assert.equal(f.touch.pollActions(),0);
 button.send('click',32,50,50,{detail:0});assert.equal(f.touch.pollActions(),button.getAttribute('data-touch-action'));
}
f.prone.send('pointerdown',31);f.prone.send('pointerup',31);assert.equal(f.touch.poll()[4],64);assert.equal(f.touch.poll()[4],0);
for (const cancel of [() => f.win.send('blur'), () => f.win.send('resize'), () => f.win.send('pagehide'),
 () => {f.doc.hidden=true;f.doc.send('visibilitychange');f.doc.hidden=false;},
 () => f.doc.send('focusout',1,0,0,{target:f.canvas}), () => f.touch.frame(0,false,true), () => f.touch.frame(1,true,true)]) {
 f.touch.frame(0,true,true); f.fire.send('pointerdown',10); f.touchmove.send('pointerdown',11,0,0);
 f.touchlook.send('pointerdown',12); f.touchlook.send('pointermove',12,100,100); f.team.send('pointerdown',13);f.alt.send('pointerdown',14);f.prone.send('pointerdown',15);
 f.touchtools.children[3].send('pointerdown',16); // A queued prompt answer must be discarded too.
 cancel(); assert.deepEqual(f.touch.poll(),[0,0,0,0,0]); assert.equal(f.touch.pollActions(),0); assert.equal(f.fire.captures.size,0);
}
assert.equal(f.touchcontrols.hidden,true); assert.equal(f.touchtools.hidden,false);
f.touchtools.children[2].send('pointerdown',14); assert.equal(f.touch.pollActions(),0); // Weapon unavailable in menus.
f.fire.send('pointerdown',14); assert.equal(f.touch.poll()[4],0); // Native menus cannot shoot.
f.fire.send('click',14,50,50,{detail:0}); assert.equal(f.touch.poll()[4],0);
for (const mode of [1,2,3]) {
 f.touch.frame(mode,true,true);f.alt.send('pointerdown',30);f.alt.send('click',30,50,50,{detail:0});
 f.prone.send('pointerdown',31);f.prone.send('click',31,50,50,{detail:0});
 for (const button of f.touchtools.children.slice(3)) {
  assert.equal(button.disabled,true);button.send('pointerdown',32);button.send('click',32,50,50,{detail:0});
 }
 assert.equal(f.touch.pollActions(),0,'Menu, console and loading states cannot change alternate weapon mode');
 assert.equal(f.touch.poll()[4],0,'Menu, console and loading states cannot toggle prone');
}
f.touch.frame(2,true,true); f.team.send('pointerdown',14); assert.equal(f.touch.pollActions(),0); // Console cannot open class selection.
f.touch.frame(3,true,true); assert.equal(f.touchtools.hidden,true);
f.team.send('pointerdown',15); assert.equal(f.touch.pollActions(),0);
f.touch.frame(0,true,true); f.touchbtn.send('click'); assert.equal(f.touch.enabled(),false);
assert.equal(f.touchcontrols.hidden,true); assert.deepEqual(f.touch.poll(),[0,0,0,0,0]);
assert.equal(fixture(false).touch.enabled(),false); assert.equal(fixture(true,'false').touch.enabled(),false);
assert.equal(fixture(false,'true').touch.enabled(),true); assert.equal(fixture(true,undefined,true).touch.enabled(),true);
assert.equal(fixture(false,undefined,false,{coarse:false,points:10}).touch.enabled(),false,'Touchscreen laptop retains primary mouse/trackpad controls');
assert.equal(fixture(false,undefined,false,{coarse:true,points:5}).touch.enabled(),true,'Primary coarse pointer defaults to touch controls');
assert.equal(fixture(false,'true',false,{coarse:false,points:10}).touch.enabled(),true,'Saved touch preference overrides a fine primary pointer');
assert.equal(fixture(false,'false',false,{coarse:true,points:5}).touch.enabled(),false,'Saved mouse preference overrides a coarse primary pointer');
assert.equal(fixture(false,undefined,false,{denied:true,points:5}).touch.enabled(),true,'Touch capability is the fallback when pointer queries fail');
assert.equal(fixture(false,undefined,false,{denied:true,points:0}).touch.enabled(),false);
assert.ok(f.gestures()>0);
console.log('Touch: primary-pointer defaults, touchscreen laptop preferences, independent move/aim/fire pointers, quick taps, cancellation, dead zone, menu/loading gates and focus/resize resets passed.');
