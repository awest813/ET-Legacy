/* Browser app installation, explicit updates, and fullscreen with recoverable errors. */
(function() {
 'use strict';
 function createApp(options) {
  var doc = options.document, win = options.window, nav = options.navigator;
  var dialog = doc.getElementById('appdialog'), status = doc.getElementById('appstatus');
  var fullStatuses = [doc.getElementById('fullscreenstatus'), doc.getElementById('launchfullstatus')], install = doc.getElementById('installapp');
  var update = doc.getElementById('updateapp'), retry = doc.getElementById('retryapp');
  var fullButtons = [doc.getElementById('fullscreenbtn'), doc.getElementById('launchfullscreen')];
  var registration = null, prompt = null, saved = false, packs = false, installing = false, updateRequested = false;
  var fullscreenPending = false, fullscreenTimer = null, fullEpoch = 0;
  var updateTimer = null, repairTimer = null, repairing = false, updateFailed = false;
  var workers = new WeakSet(), registrations = new WeakSet();
  var installed = !!(win.matchMedia && win.matchMedia('(display-mode: standalone)').matches) || !!nav.standalone;
  var message = '', fullMessage = '';
  var serviceWorker = null;
  // Some private or sandboxed contexts throw even when reading this property.
  try { serviceWorker = nav.serviceWorker; } catch (e) {}
  function fullElement() { return doc.fullscreenElement || doc.webkitFullscreenElement; }
  function render() {
   status.textContent = message || (saved ? packs ? 'Offline ready in this browser.' : 'App saved. Play offline once to save the game files.' : 'Saving app for offline use…');
   install.hidden = !prompt || installed; install.disabled = installing;
   doc.getElementById('installhint').textContent = installed ? 'App installed.' : prompt ? 'Install for a separate app window. Offline play becomes available after the game files are saved.' : 'To install, use your browser menu: Install app or Add to Home Screen. Browser support varies.';
   update.hidden = !registration || !registration.waiting; update.disabled = updateRequested;
   retry.disabled = repairing;
   fullStatuses.forEach(function(element) { element.textContent = fullMessage; });
   ['appbtn','launchapp'].forEach(function(id) { doc.getElementById(id).textContent = registration && registration.waiting ? 'Update ready' : 'App & offline'; });
   fullButtons.forEach(function(button) {
    var supported = !!(doc.body.requestFullscreen || doc.body.webkitRequestFullscreen) && (doc.body.requestFullscreen ? doc.fullscreenEnabled !== false : doc.webkitFullscreenEnabled !== false);
    button.disabled = fullscreenPending || !supported;
    button.textContent = fullElement() ? 'Exit fullscreen' : fullscreenPending ? 'Opening…' : 'Fullscreen';
    button.setAttribute('aria-pressed', String(!!fullElement()));
    button.title = supported ? 'Keep game controls visible in fullscreen' : 'Fullscreen unavailable here; use the browser fullscreen option';
   });
  }
  function fullFinished(error, epoch) {
   if (epoch !== fullEpoch) return;
   options.clearTimeout.call(win, fullscreenTimer); fullscreenPending = false;
   fullMessage = error ? 'Fullscreen was blocked. Try again, or use the browser fullscreen option.' : '';
   render(); options.resume();
  }
  function fullscreen() {
   if (fullscreenPending) return;
   options.pause(); options.gesture();
   var exiting = !!fullElement(), epoch = ++fullEpoch;
   fullscreenPending = true; fullMessage = ''; render();
   fullscreenTimer = options.setTimeout.call(win, function() { fullFinished(!!fullElement() === exiting, epoch); }, 4000);
   try {
    var target = exiting ? doc : doc.body;
    var method = exiting ? doc.exitFullscreen || doc.webkitExitFullscreen : doc.body.requestFullscreen || doc.body.webkitRequestFullscreen;
    var result = method.call(target);
    if (result && result.then) result.then(function() { fullFinished(false, epoch); }, function() { fullFinished(true, epoch); });
   } catch (e) { fullFinished(true, epoch); }
  }
  fullButtons.forEach(function(button) { button.addEventListener('click', fullscreen); });
  ['fullscreenchange','webkitfullscreenchange'].forEach(function(name) { doc.addEventListener(name, function() {
   options.pause(); fullFinished(false, fullEpoch);
  }); });
  ['fullscreenerror','webkitfullscreenerror'].forEach(function(name) { doc.addEventListener(name, function() { fullFinished(true,fullEpoch); }); });
  function handleEscape(event) {
   // The first Escape releases a captured mouse; a subsequent Escape exits fullscreen.
   if (event.key === 'Escape' && fullElement() && !doc.pointerLockElement && !doc.webkitPointerLockElement && !dialog.open && !(options.modalOpen && options.modalOpen())) fullscreen();
  }
  doc.addEventListener('keydown', handleEscape, true);
  function show() { options.pause(); if (options.open) options.open(); if (!dialog.open) dialog.showModal(); doc.getElementById('closeapp').focus(); }
  ['appbtn','launchapp'].forEach(function(id) { doc.getElementById(id).addEventListener('click', show); });
  doc.getElementById('closeapp').addEventListener('click', function() { dialog.close(); });
  dialog.addEventListener('close', function() { options.resume(); });
  win.addEventListener('beforeinstallprompt', function(event) { event.preventDefault(); prompt = event; render(); });
  win.addEventListener('appinstalled', function() { installed = true; prompt = null; render(); });
  install.addEventListener('click', function() {
   if (!prompt || installing) return;
   var current = prompt; prompt = null; installing = true; render();
   try { Promise.resolve(current.prompt()).then(function() { return current.userChoice; }).then(function() {
    installing = false; render();
   }, function() { installing = false; message = 'Installation did not finish. Use your browser menu to try again.'; render(); }); }
   catch (e) { installing = false; message = 'Installation did not finish. Use your browser menu to try again.'; render(); }
  });
  update.addEventListener('click', function() {
   if (!registration || !registration.waiting || updateRequested) return;
   options.pause(); updateRequested = true; message = 'Reloading the app ends the current match. Applying the saved update…'; render();
   updateTimer = options.setTimeout.call(win, function() {
    clearUpdate(); message = 'The update did not respond. Try again when ready; your current match can continue.'; render();
   }, 8000);
   try { registration.waiting.postMessage({type:'ACTIVATE_UPDATE'}); }
   catch (e) { clearUpdate(); message = 'The update is no longer waiting. Reload from your browser when ready.'; render(); }
  });
  function clearUpdate() { options.clearTimeout.call(win, updateTimer); updateTimer = null; updateRequested = false; }
  function clearRepair() { options.clearTimeout.call(win, repairTimer); repairTimer = null; repairing = false; }
  function requestStatus(worker) {
   try { worker.postMessage({type:'APP_STATUS'}); }
   catch (e) { if (!saved) { message = 'Offline saving is unavailable. Reconnect and retry when ready.'; retry.hidden = false; render(); } }
  }
  function watch(worker) {
   if (!worker || workers.has(worker)) return;
   workers.add(worker);
   var activated = worker.state === 'activated';
   worker.addEventListener('statechange', function() {
    if (worker.state === 'activated') activated = true;
    if (worker.state === 'installed') { updateFailed = false; render(); if (registration.waiting) message = 'An app update is saved. Reload when you are ready to end the current match.'; render(); }
    if (worker.state === 'redundant' && !activated) { clearUpdate(); updateFailed = true; message = saved ? 'The app update could not be saved. Your existing offline app is still available. Check storage and retry.' : 'Offline saving did not finish. Check available storage and retry; connected play is still available.'; retry.hidden = false; render(); }
   });
  }
  function register(forceUpdate) {
   retry.hidden = true;
   if (!win.isSecureContext || !serviceWorker) { message = 'Offline app installation needs HTTPS or localhost and browser permission. Connected play is still available.'; render(); return; }
   message = ''; render();
   function failed() { if (repairing) return; message = saved ? '' : 'Offline saving is unavailable. Connected play is still available; retry when ready.'; retry.hidden = saved; render(); }
   try { serviceWorker.register('sw.js', {scope:'./', updateViaCache:'none'}).then(function(value) {
    registration = value; watch(value.installing); watch(value.waiting);
    if (!registrations.has(value)) { registrations.add(value); value.addEventListener('updatefound', function() { watch(value.installing); }); }
    if (value.active && !repairing) requestStatus(value.active);
    if (value.waiting) message = 'An app update is saved. Reload when you are ready to end the current match.';
    render();
    if (forceUpdate && typeof value.update === 'function') {
     function updateUnavailable() { updateFailed = true; message = saved ? 'Could not check the app update. Your saved offline app is still available. Reconnect and retry.' : 'Could not check the app update. Connected play is still available. Reconnect and retry.'; retry.hidden = false; render(); }
     try { Promise.resolve(value.update()).catch(updateUnavailable); } catch (e) { updateUnavailable(); }
    }
   }, failed); } catch (e) { failed(); }
  }
  if (serviceWorker) {
   serviceWorker.addEventListener('message', function(event) {
    if (!event.data) return;
    if (event.data.type === 'APP_READY') { clearRepair(); saved = true; retry.hidden = !updateFailed; if (!updateFailed && (!registration || !registration.waiting)) message = ''; render(); }
    if (event.data.type === 'APP_SAVE_FAILED' || (event.data.type === 'APP_MISSING' && !repairing)) { clearRepair(); saved = false; message = 'Some offline app files are unavailable. Reconnect and retry offline saving.'; retry.hidden = false; render(); }
    if (event.data.type === 'APP_OTHER_TABS') { clearUpdate(); message = 'Close the other app tabs or windows, then reload to update. Your current match can continue.'; render(); }
   });
   serviceWorker.addEventListener('controllerchange', function() { if (updateRequested) { clearUpdate(); options.reload(); } });
   if (serviceWorker.controller) requestStatus(serviceWorker.controller);
  }
  retry.addEventListener('click', function() {
   if (repairing) return;
   updateFailed = false;
   if (registration && registration.active && !saved) {
    // Also discover a newer bundle if this cache belongs to an older deployment.
    register(true); repairing = true; retry.hidden = false; message = 'Saving app files for offline play…'; render();
    repairTimer = options.setTimeout.call(win, function() { clearRepair(); message = 'Saving did not finish. Check your connection and available storage, then retry.'; retry.hidden = false; render(); }, 30000);
    try { registration.active.postMessage({type:'REPAIR_CACHE'}); }
    catch (e) { clearRepair(); message = 'Offline saving is unavailable. Reconnect and retry when ready.'; retry.hidden = false; render(); }
    return;
   }
   register(true);
  }); render(); register();
  return {gameFilesReady: function(value) { packs = !!value; render(); }, show:show, handleEscape:handleEscape};
 }
 if (typeof module === 'object' && module.exports) module.exports = createApp;
 else Module['browserApp'] = createApp({document:document, window:window, navigator:navigator,
  setTimeout:setTimeout, clearTimeout:clearTimeout, reload:function() { location.reload(); },
  gesture:function() { Module['browserTouchGesture'](); }, pause:function() { Module['browserAppPause'](); },
  open:function() { Module['browserAppOpen'](); }, modalOpen:function() { return Module['browserModalOpen'](); }, resume:function() { Module['browserAppResume'](); }});
})();
