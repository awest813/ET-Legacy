/* Browser touch controls. Independent pointers never synthesize held keyboard keys. */
(function() {
 'use strict';
 function createTouch(options) {
  var doc = options.document, win = options.window;
  var toggle = doc.getElementById('touchbtn'), layer = doc.getElementById('touchcontrols');
  var tools = doc.getElementById('touchtools'), pad = doc.getElementById('touchmove');
  var thumb = doc.getElementById('touchthumb'), aim = doc.getElementById('touchlook');
  var pointers = new Map(), move = [0, 0], look = [0, 0], pressed = 0, actions = 0;
  var mode = 3, running = false, enabled;
  // Touchscreen laptops can still use a precise primary mouse/trackpad.
  // Prefer that pointer's mode; retain a capability fallback for older hosts.
  if (typeof options.coarse === 'boolean') enabled = options.coarse;
  else {
   try { enabled = !!win.matchMedia('(pointer: coarse)').matches; }
   catch (e) { enabled = !!(options.navigator && options.navigator.maxTouchPoints > 0); }
  }
  try { var saved = options.storage.getItem('etl.touch'); if (saved === 'true' || saved === 'false') enabled = saved === 'true'; } catch (e) {}
  function held() { var bits = 0; pointers.forEach(function(p) { bits |= p.bit || 0; }); return bits; }
  function reset() {
   var old = Array.from(pointers.entries()); pointers.clear(); move = [0, 0]; look = [0, 0]; pressed = actions = 0;
   thumb.style.transform = 'translate(0px, 0px)';
   old.forEach(function(entry) { try { entry[1].element.releasePointerCapture(entry[0]); } catch (e) {} });
   layer.querySelectorAll('[data-touch-bit]').forEach(function(button) { button.removeAttribute('data-held'); });
  }
  function render() {
   toggle.setAttribute('aria-pressed', String(enabled));
   doc.body.setAttribute('data-touch', String(enabled));
   layer.hidden = !enabled || !running || mode !== 0;
   doc.body.setAttribute('data-touch-playing', String(!layer.hidden));
   tools.hidden = !enabled || !running || mode === 3;
   [tools, layer].forEach(function(parent) { parent.querySelectorAll('[data-touch-action]').forEach(function(button) {
    var action = Number(button.getAttribute('data-touch-action'));
    button.disabled = (action >= 4 && mode !== 0) || (action === 1 && mode === 2);
   }); });
   toggle.textContent = enabled ? 'Touch on' : 'Touch off';
  }
  function stop(event) { event.preventDefault(); event.stopPropagation(); }
  function usable(kind, bit) { return enabled && running && mode !== 3 && (kind === 'action' ? bit < 4 || mode === 0 : mode === 0); }
  function bind(element, kind, bit) {
   element.addEventListener('pointerdown', function(event) {
    if (element.disabled || !usable(kind, bit) || (event.pointerType === 'mouse' && event.button !== 0)) return;
    stop(event); options.gesture();
    if (kind === 'action') { actions |= bit; return; }
    // One move thumb and one look thumb; action buttons can have multiple holders.
    if ((kind === 'move' || kind === 'look') && Array.from(pointers.values()).some(function(p) { return p.kind === kind; })) return;
    var rect = element.getBoundingClientRect();
    var p = {kind: kind, bit: bit || 0, element: element, x: event.clientX, y: event.clientY,
     cx: rect.left + rect.width / 2, cy: rect.top + rect.height / 2, radius: Math.min(rect.width, rect.height) / 2};
    pointers.set(event.pointerId, p);
    try { element.setPointerCapture(event.pointerId); } catch (e) { reset(); return; }
    if (bit) { pressed |= bit; element.setAttribute('data-held', 'true'); }
    if (kind === 'move') updateMove(p, event);
   });
   element.addEventListener('pointermove', function(event) {
    var p = pointers.get(event.pointerId); if (!p || p.element !== element) return;
    stop(event);
    if (p.kind === 'move') updateMove(p, event);
    if (p.kind === 'look') {
     look[0] = Math.max(-500, Math.min(500, look[0] + event.clientX - p.x));
     look[1] = Math.max(-500, Math.min(500, look[1] + event.clientY - p.y));
     p.x = event.clientX; p.y = event.clientY;
    }
   });
   function release(event) {
    var p = pointers.get(event.pointerId); if (!p || p.element !== element) return;
    stop(event); pointers.delete(event.pointerId);
    if (p.kind === 'move') { move = [0, 0]; thumb.style.transform = 'translate(0px, 0px)'; }
    if (p.kind === 'look' && event.type !== 'pointerup') look = [0, 0];
    if (p.bit && !(held() & p.bit)) { element.removeAttribute('data-held'); if (event.type !== 'pointerup') pressed &= ~p.bit; }
    try { element.releasePointerCapture(event.pointerId); } catch (e) {}
   }
   element.addEventListener('pointerup', release);
   element.addEventListener('pointercancel', release);
   element.addEventListener('lostpointercapture', release);
   // Keyboard/assistive activation is one tap; pointer clicks must not duplicate it.
   if (bit) element.addEventListener('click', function(event) {
    if (event.detail === 0 && !element.disabled && usable(kind, bit)) {
     options.gesture(); if (kind === 'action') actions |= bit; else pressed |= bit;
    }
   });
  }
  function updateMove(p, event) {
   var x = (event.clientX - p.cx) / Math.max(1, p.radius), y = (event.clientY - p.cy) / Math.max(1, p.radius);
   var length = Math.hypot(x, y), scale = length > 1 ? 1 / length : 1;
   thumb.style.transform = 'translate(' + x * scale * p.radius * .6 + 'px, ' + y * scale * p.radius * .6 + 'px)';
   var strength = Math.max(0, Math.min(1, (length - .12) / .88));
   move = length ? [x / length * strength, -y / length * strength] : [0, 0];
  }
  bind(pad, 'move'); bind(aim, 'look');
  layer.querySelectorAll('[data-touch-bit]').forEach(function(button) { bind(button, 'button', Number(button.getAttribute('data-touch-bit'))); });
  [tools, layer].forEach(function(parent) { parent.querySelectorAll('[data-touch-action]').forEach(function(button) { bind(button, 'action', Number(button.getAttribute('data-touch-action'))); }); });
  toggle.addEventListener('click', function() {
   reset(); enabled = !enabled; try { options.storage.setItem('etl.touch', String(enabled)); } catch (e) {}
   render(); options.gesture();
  });
   win.addEventListener('blur', reset); win.addEventListener('resize', reset); win.addEventListener('pagehide', reset);
  doc.addEventListener('visibilitychange', function() { if (doc.hidden) reset(); });
  doc.addEventListener('focusout', function(event) { if (event.target === options.canvas) reset(); });
  render();
  return {
   reset: reset,
   frame: function(nextMode, focused, active) {
    if (nextMode !== mode || !focused || !active) reset();
    var changed = mode !== nextMode || running !== active;
    mode = nextMode; running = active; if (changed) render();
   },
   enabled: function() { return enabled; },
   poll: function() {
    var result = [move[0], move[1], look[0], look[1], held() | pressed];
    look = [0, 0]; pressed = 0; return result;
   },
   pollActions: function() { var result = actions; actions = 0; return result; }
  };
 }
 if (typeof module === 'object' && module.exports) module.exports = createTouch;
 else Module['browserTouch'] = createTouch({document: document, window: window, canvas: Module.canvas,
  storage: {getItem: function(key) { return localStorage.getItem(key); }, setItem: function(key, value) { localStorage.setItem(key, value); }},
  navigator: navigator,
  gesture: function() { Module['browserTouchGesture'](); }});
})();
