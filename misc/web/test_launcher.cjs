// Fault checks for browser startup; no game packs or browser state are modified.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(`${__dirname}/etl_shell.html`, 'utf8')
    .match(/<script type="text\/javascript">([\s\S]*?)<\/script>/)[1];
const packs = ['etloose.pk3', 'pak0.pk3', 'pak1.pk3', 'pak2.pk3'];
const zip = Uint8Array.from([80, 75, 3, 4, ...Array(28).fill(0)]);
function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) {
        crc ^= byte;
        for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ((crc ^ 0xffffffff) >>> 0).toString(16).padStart(8, '0');
}
assert.equal(crc32(Buffer.from('123456789')), 'cbf43926');
const packManifest = {version:1, packs:Object.fromEntries(packs.map(name => [name,
    {size:zip.length, crc32:crc32(zip), sha256:'0'.repeat(64)}]))};

function launcher(fault = {}) {
    const elements = {};
    const files = new Map(packs.map(name => [`/idb/etmain/${name}`, zip]));
    if (fault.missing || fault.download) files.delete('/idb/etmain/pak0.pk3');
    if (fault.corrupt) files.set('/idb/etmain/pak0.pk3', new Uint8Array([1, 2]));
    if (fault.corruptBody) { const bytes = zip.slice(); bytes[bytes.length - 1] ^= 1; files.set('/idb/etmain/pak0.pk3', bytes); }
    if (fault.savedManifest) files.set('/idb/asset-manifest.json', JSON.stringify(packManifest));
    if (fault.navigation) for (const suffix of ['way','gm']) files.set('/omni-bot/et/nav/sample.'+suffix,zip);
    if (fault.cachedCustom && fault.catalog) for (const pack of fault.catalog.packs) files.set('/idb/etmain/'+pack.sha256+'.pk3',zip);
    for (const [name,size] of fault.cacheFiles || []) files.set('/idb/etmain/'+name,{length:size});
    const calls = [], requests = [], events = {};
    const pending = [], timers = new Map(); let timerId = 0, writes = 0;
    const element = id => elements[id] ||= {
        value: '', style: {}, disabled: id === 'playbtn', textContent: '',
        addEventListener(name, callback) { this[name === 'close' ? 'closeEvent' : name] = callback; },
        setAttribute(name, value) { this[name] = value; },
        removeAttribute(name) { delete this[name]; },
        getAttribute(name) { return this[name] == null ? null : String(this[name]); },
        showModal() { this.open=true; }, close() { this.open=false; if (this.closeEvent) this.closeEvent(); },
        reportValidity() { return true; }, focus() { context.document.activeElement = this; }
    };
    const context = vm.createContext({
        document: { getElementById: element, createElement(tag) {
            if (tag !== 'canvas') return {};
            return {getContext(name) {
                assert.equal(name,'webgl2');
                if (fault.graphicsThrow) throw Error('Graphics denied');
                if (fault.noGraphics) return null;
                return {getExtension() { return {loseContext() { calls.push('probe-released'); }}; }};
            }};
        }, hidden:false, hasFocus() { return !this.blurred; },
            addEventListener(name, callback) { events[name] = callback; } },
        window: { location: { search: fault.query || '', href:'http://localhost:8081/etl.html'+(fault.query||'')+(fault.hash||'') },
            history: {state:{launcher:true},replaceState(state, title, value) {
                if (fault.history) throw Error('History denied');
                fault.historyUpdate={state,title,value};
                const url=new URL(value,context.window.location.href);
                context.window.location.href=url.href;context.window.location.search=url.search;
            }},
            addEventListener(name, callback) { events[name] = callback; } },
        localStorage: {
            removeItem(key) { if (fault.storage) throw Error('Denied'); if (key === 'etl.assetResume') fault.assetResume = null; },
            getItem(key) { if (fault.storage) throw Error('Denied'); if (key === 'etl.playerKey') return fault.playerKey || null; return JSON.stringify(key === 'etl.assetResume' ? fault.assetResume || null : key === 'etl.customAssets' ? fault.savedCatalog || null : key === 'etl.sound' ? fault.sound || {} : key === 'etl.engineSettings' ? fault.engineSettings || null : fault.saved || {}); },
            setItem(key, value) { if (fault.storage) throw Error('Denied'); if (key === 'etl.playerKey') fault.playerKey = value; if (key === 'etl.assetResume') fault.assetResume = JSON.parse(value); if (key === 'etl.sound') fault.sound = JSON.parse(value); if (key === 'etl.offlineMatch') fault.saved = JSON.parse(value); if (key === 'etl.engineSettings') fault.engineSettings = JSON.parse(value); }
        },
        location: { reload() { calls.push('retry'); } },
        URL, URLSearchParams, Uint8Array, Uint32Array,
        WebAssembly: fault.noWasm ? undefined : fault.blockedWasm ? {instantiate(){},validate(){throw Error('Blocked');}} : WebAssembly,
        crypto: {getRandomValues(bytes) { bytes.fill(7); return bytes; },subtle:{digest:async (_, bytes)=> {
            const hash=require('node:crypto').createHash('sha256').update(bytes).digest();
            return hash.buffer.slice(hash.byteOffset,hash.byteOffset+hash.byteLength);
        }}},
        setTimeout(callback) { const id = ++timerId; timers.set(id, callback); return id; },
        clearTimeout(id) { timers.delete(id); },
        console: { log() {}, warn() {} },
        XMLHttpRequest: class {
            constructor() { this.upload = {}; }
            open(_, url) { this.url = url; }
            setRequestHeader() {}
            abort() { this.aborted = true; }
            send() {
                requests.push(this.url);
                if (this.url.startsWith('assets/import/')) {
                    this.status=200; this.responseText=JSON.stringify({installed:{maps:['sample']}});
                    if (fault.importLate) { pending.push(()=>this.onload()); return; }
                    return this.onload();
                }
                if (this.url.startsWith('network/assets/')) {
                    this.status = fault.assetFailure ? 502 : 200;
                    const path=this.url.split('?')[0].split('/');
                    this.responseText=JSON.stringify({installed:{game:path[2], checksum:Number(this.url.split('checksum=')[1])}})+'\n';
                    if (fault.assetStream) {
                        this.responseText='';
                        for (const chunk of fault.assetStream) pending.push(()=>{this.responseText+=chunk; this.onprogress();});
                        pending.push(()=>this.onload());return;
                    }
                    if (fault.assetLate) {pending.push(()=>this.onload());return;}
                    return this.onload();
                }
                if (this.url === 'assets/custom.json') {
                    this.status = fault.catalogMissing ? 503 : 200;
                    this.responseText = JSON.stringify(fault.catalog || {version:1,packs:[],errors:[]});
                    if (fault.catalogLate) { pending.push(()=>this.onload()); return; }
                    return this.onload();
                }
                if (this.url === 'network/servers.json') {
                    this.status = fault.publicFailure ? 503 : 200;
                    this.responseText = JSON.stringify(fault.publicCatalog);
                    if (fault.publicLate) { pending.push(()=>this.onload()); return; }
                    return this.onload();
                }
                if (this.url.startsWith('network/config.json')) {
                    this.status = fault.onlineUnavailable ? 503 : 200;
                    this.responseText = JSON.stringify({version:1,enabled:!fault.onlineDisabled,relay:'ws://localhost:8082/relay',serverLabel:'Audit server',engineAddress:'192.0.2.1:27960',autoAssets:!!fault.autoAssets,
                        publicServers:!!fault.publicCatalog,serverId:this.url.includes('?server=') ? this.url.split('?server=')[1] : '',
                        serverInfo:Object.hasOwn(fault,'onlineInfo') ? fault.onlineInfo : {status:'online',compatible:true,hostname:'Audit ET server',map:'oasis',players:8,capacity:16,humans:3,latencyMs:25,password:false}});
                    if (this.url.includes('?server=')) { const config=JSON.parse(this.responseText); config.relay+='/'+'A'.repeat(fault.rotatedTicket ? 101 : 100); this.responseText=JSON.stringify(config); }
                    if (fault.onlineOverride) this.responseText=JSON.stringify({...JSON.parse(this.responseText),...fault.onlineOverride});
                    if (fault.onlineBadJson) this.responseText='not JSON';
                    if (fault.onlineLate) { pending.push(()=>this.onload()); return; }
                    if (fault.onlineNetworkError) return this.onerror();
                    if (fault.onlineTimeout) return this.ontimeout();
                    return this.onload();
                }
                if (this.url === 'assets/manifest.json') {
                    if (fault.manifestLate) { pending.push(() => this.onload()); return; }
                    this.status = fault.manifestMissing ? 503 : 200;
                    this.responseText = JSON.stringify(fault.badManifest ? {version:2, packs:{}} : packManifest);
                    return this.onload();
                }
                if (this.url.startsWith('assets/custom/')) {
                    this.status=200; this.response = fault.customCorrupt ? new Uint8Array(zip.length).buffer : zip.buffer;
                    return this.onload();
                }
                if (fault.download === 'timeout') return this.ontimeout();
                if (fault.download === 'network') return this.onerror();
                this.status = fault.download === '404' ? 404 : 200;
                this.response = fault.download === 'invalid' ? new ArrayBuffer(30) : zip.buffer;
                if (fault.download === 'corrupt') { const bytes = zip.slice(); bytes[bytes.length - 1] ^= 1; this.response = bytes.buffer; }
                if (fault.downloadLate) { pending.push(()=>this.onload()); return; }
                this.onload();
            }
        }
    });
    element('canvas').ownerDocument=context.document;
    element('mapselect').appendChild = option => { (element('mapselect').children ||= []).push(option); };
    element('publicserver').appendChild = option => { (element('publicserver').children ||= []).push(option); };
    context.document.baseURI='http://localhost:8081/';
    if (fault.noDialog) element('helpdialog').showModal=undefined;
    if (fault.noCrypto) context.crypto=undefined;
    if (fault.noSubtle) context.crypto.subtle=undefined;
    if (fault.noDigest) context.crypto.subtle.digest=undefined;
    vm.runInContext(source, context);
    // The probe releases its disposable context without affecting game calls.
    const probeReleased = calls.includes('probe-released'); calls.length=0;
    let relayCallback, networkCloses=0; const networkCalls=[], networkConfigs=[];
    context.Module.browserNetwork={
        configure(value) {networkConfigs.push(value);}, close(reason, done) {
            networkCloses++;
            if (relayCallback) {const done=relayCallback;relayCallback=null;done(Error('Connection cancelled.'));}
            if (done) { if (fault.closeLate) pending.push(done); else done(); }
        }, begin() {return !fault.relayClosed;},
        connect(callback) {
            networkCalls.push('connect');
            if (fault.relayPending) {
                relayCallback=callback;
                context.Module.browserNetworkStatus('Opening relay','Opening the relay…',{sent:0,received:0,ready:false,opening:true,failed:false});
            } else callback(fault.relayError ? Error('Relay unavailable') : null);
        }
    };
    context.Module.FS = {
        filesystems: { IDBFS: {} }, mkdir() {},
        mount() { if (fault.mount) throw Error('Cache mount failed'); },
        syncfs(populate, callback) {
            if (!populate) writes++;
            if (fault.syncHang && populate) { pending.push(() => callback(null)); return; }
            if (fault.syncThrow) throw Error('Cache unavailable');
            callback(fault.syncError ? Error('Quota exhausted') : null);
        },
        analyzePath(path) { return { exists: files.has(path) }; },
        readdir(path) { return [...files.keys()].filter(name=>name.startsWith(path+'/')).map(name=>name.slice(path.length+1)).filter(name=>!name.includes('/')); },
        stat(path) { return { size: files.get(path).length }; },
        open(path) { return path; }, close() {},
        read(path, bytes, target, count, offset) { if (fault.readError) throw Error('Read failed'); const block = files.get(path).slice(offset, offset + count); bytes.set(block, target); return block.length; },
        readFile(path) { if (!files.has(path)) throw Error('Missing pack'); return files.get(path); },
        writeFile(path, data, options) {
            if (fault.copy && path === '/etmain/pak0.pk3') throw Error('Out of memory');
            if (fault.settingsWriteError && path === '/browser/legacy/etconfig.cfg') throw Error('Settings write failed');
            if (path.endsWith('.pk3')) assert.equal(options && options.canOwn,true,'Downloaded immutable packs avoid a second full-size buffer');
            files.set(path, data);
        },
        unlink(path) { files.delete(path); },
        symlink(path, target) { if (fault.copy && target === '/etmain/pak0.pk3') throw Error('Out of memory'); files.set(target, {link:path}); }
    };
    context.Module.callMain = args => {
        calls.push(Array.from(args));
        if (fault.abort) context.Module.onAbort('startup failed');
        if (fault.engine) throw Error('engine failure');
    };
    context.Module.onRuntimeInitialized();
    return { context, elements, files, calls, requests, events, pending, timers, networkCalls, networkConfigs, probeReleased, get writes() { return writes; },
        get networkCloses() { return networkCloses; },
        start() { elements.matchform.submit({ preventDefault() {} }); } };
}

// Compare both actual implementations so launcher and native menu presets
// cannot silently drift. Also exercise the real offline/online startup paths.
const nativeGraphicsHeader=fs.readFileSync(`${__dirname}/../../src/ui/ui_web_settings.h`,'utf8');
const nativePresetRows=Array.from(nativeGraphicsHeader.match(/values\[3\]\[11\] = \{([\s\S]*?)\n\t\};/)[1].matchAll(/\{([^}]+)\}/g),row=>Array.from(row[1].matchAll(/"([^"]+)"/g),value=>value[1]));
const effectNames={ui_web_shadows:'cg_shadows',ui_web_skybox:'cg_skybox',ui_web_fastsky:'r_fastsky',ui_web_maxfps:'com_maxfps'};
const nativePresetNames=Array.from(nativeGraphicsHeader.match(/void UI_WebGraphicsPreset[\s\S]*?names\[\] = \{([\s\S]*?)\};/)[1].matchAll(/"([^"]+)"/g),name=>effectNames[name[1]]||name[1].replace(/^ui_/,''));
for (const [index,preset] of ['chromebook','balanced','quality'].entries()) {
    for (const online of [false,true]) {
        const page=launcher();
        page.elements.graphicspreset.value=preset;page.elements.graphicspreset.change();
        const args=Array.from(page.context.graphicsStartupArgs(preset));
        assert.deepEqual(args.filter((_,i)=>i%3===1),nativePresetNames);
        assert.deepEqual(args.filter((_,i)=>i%3===2),nativePresetRows[index]);
        assert.ok(args.every((value,i)=>i%3!==0||value==='+set'));
        if (online) { page.elements.onlinebtn.click();page.elements.joinbtn.click(); }
        else page.start();
        const boot=page.calls[0],at=boot.indexOf('r_mode')-1;
        assert.deepEqual(boot.slice(at,at+args.length),args);
        assert.ok(at<boot.indexOf(online?'+connect':'+map'),'Preset precedes the first map/connection');
        assert.equal(page.elements.graphicspreset.disabled,true,'Graphics cannot change during startup');
    }
}
for (const invalid of ['saved','__proto__','constructor','quality; quit','unknown']) {
    assert.equal(launcher().context.graphicsStartupArgs(invalid).length,0,'Only named presets produce commands');
}
const savedGraphics=launcher({saved:{map:'radar',bots:0,difficulty:6,graphics:'quality'},engineSettings:{version:1,config:'// generated by ET Legacy\nseta r_customwidth "1024"\n'}});
assert.equal(savedGraphics.elements.graphicspreset.value,'saved','Previous preset selection cannot overwrite customized native settings');
savedGraphics.start();
assert.ok(!savedGraphics.calls[0].includes('r_mode'),'Use saved settings leaves graphics cvars untouched');
assert.match(savedGraphics.files.get('/browser/legacy/etconfig.cfg'),/1024/);
const cancelledGraphics=launcher({manifestLate:true});
cancelledGraphics.elements.graphicspreset.value='chromebook';cancelledGraphics.start();
assert.equal(cancelledGraphics.elements.graphicspreset.disabled,true);
cancelledGraphics.elements.cancelload.click();
assert.equal(cancelledGraphics.elements.graphicspreset.disabled,true,'Cancelled startup remains disabled while the launcher reloads');
cancelledGraphics.pending.shift()();assert.deepEqual(cancelledGraphics.calls,['retry'],'Late file checks cannot boot a cancelled preset');
const transientGraphics=launcher({storage:true});transientGraphics.elements.graphicspreset.value='chromebook';transientGraphics.start();
assert.ok(transientGraphics.calls[0].includes('960'),'Preset works with unavailable browser storage');
console.log('Launcher graphics checks passed (native preset parity, offline/online startup ordering, saved customization, cancellation and storage denial).');

const engineLoader=fs.readFileSync(`${__dirname}/etl_shell.html`,'utf8').match(/<template id="enginecode">[\s\S]*?<\/template>\s*<script>([\s\S]*?)<\/script>/)[1];
for (const [fault,message] of [[{noWasm:true},/WebAssembly/],[{blockedWasm:true},/blocked/],
    [{noGraphics:true},/WebGL 2/],[{graphicsThrow:true},/WebGL 2/],[{noDialog:true},/menus/]]) {
    const page=launcher(fault);
    assert.equal(page.context.Module.browserCanStart,false);
    assert.match(page.elements.etl_status.textContent,message);
    assert.equal(page.elements.playbtn.textContent,'RECHECK SUPPORT');
    assert.equal(page.elements.matchsettings.disabled,true);
    assert.equal(page.elements.launchtools.style.display,'none');
    assert.deepEqual(page.requests,[],'Unsupported browsers never fetch assets or catalogs');
    page.context.document.body={appendChild(){assert.fail('Unsupported browsers must not load the engine');}};
    vm.runInContext(engineLoader,page.context);
    assert.equal(page.elements.playbtn.textContent,'RECHECK SUPPORT','A late runtime callback cannot remove the support failure');
    page.start();assert.deepEqual(page.calls,['retry']);
}
const supportedBrowser=launcher();
assert.equal(supportedBrowser.probeReleased,true);
let engineLoads=0;
supportedBrowser.elements.enginecode={content:{querySelector(selector){assert.equal(selector,'script');return {cloneNode(deep){assert.equal(deep,true);return {src:'etl.js',async:true};}};}}};
supportedBrowser.context.document.body={appendChild(script){assert.equal(script.src,'etl.js');engineLoads++;}};
vm.runInContext(engineLoader,supportedBrowser.context);assert.equal(engineLoads,1);

for (const fault of [{}, { mount: true }, { syncThrow: true }, { syncError: true }, { storage: true }]) {
    const page = launcher(fault);
    page.start();
    assert.equal(page.calls.length, 1, `Boot survives ${JSON.stringify(fault)}`);
    assert.equal(page.elements.overlay.style.display, 'none');
}
for (const fault of [{ copy: true }, { engine: true }, { abort: true },
                    {readError:true}, {badManifest:true}, ...['timeout', 'network', '404', 'invalid', 'corrupt'].map(download => ({ download }))]) {
    const page = launcher(fault);
    page.start();
    assert.equal(page.elements.playbtn.textContent, 'RETRY', JSON.stringify(fault));
    assert.equal(page.elements.overlay.style.display, 'flex');
    page.context.Module.onRuntimeInitialized();
    assert.equal(page.elements.playbtn.textContent, 'RETRY');
    page.start();
    assert.equal(page.calls.at(-1), 'retry');
}
const corrupt = launcher({ corrupt: true });
corrupt.start();
assert.deepEqual(corrupt.requests, ['assets/custom.json', 'assets/manifest.json', 'assets/pak0.pk3']);
assert.equal(corrupt.calls.length, 1);
const configured = launcher({ query: '?map=radar&bots=6&difficulty=6', saved: { map: 'oasis', bots: 4 } });
configured.start();
assert.equal(configured.calls[0].at(-1), 'radar');
assert.match(configured.files.get('/omni-bot/et/user/omni-bot.cfg'), /MinBots = 6\nMaxBots = 6/);
assert.match(configured.files.get('/omni-bot/et/user/omni-bot.cfg'), /CurrentDifficulty = 6/);
for (const map of ['oasis','goldrush','battery','fueldump','radar','railgun']) {
    const match=launcher({query:'?map='+map});match.start();
    const args=match.calls[0], mode=args.indexOf('g_gametype');
    assert.ok(mode>0 && mode<args.indexOf('+map'),'Offline objective mode is set before map startup');
    assert.equal(args[mode-1],'+set');assert.equal(args[mode+1],'2');
    assert.equal(args.at(-1),map,'The selected map remains the match map');
}
const invalid = launcher({ query: '?map=../../bad&bots=-2&difficulty=999' });
assert.equal(invalid.elements.mapselect.value, 'oasis');
assert.equal(invalid.elements.botcount.value, 4);
assert.equal(invalid.elements.difficulty.value, '4');
const extraPack={name:'sample.pk3',game:'etmain',maps:['sample'],size:zip.length,crc32:crc32(zip),
    sha256:require('node:crypto').createHash('sha256').update(zip).digest('hex')};
const customList={version:1,packs:[extraPack],errors:[]};
for (const fault of [{noCrypto:true},{noSubtle:true},{noDigest:true}]) {
    const bundled=launcher(fault);bundled.start();assert.equal(bundled.calls.length,1,'Bundled play works without secure hashing');
    const custom=launcher({...fault,catalog:customList,query:'?map=sample'});custom.start();
    assert.match(custom.elements.etl_status.textContent,/secure verification.*localhost or HTTPS/);
    assert.deepEqual(custom.requests,['assets/custom.json'],'Custom verification fails before loading packs');
    assert.equal(custom.calls.length,0);
}
for (const cachedCustom of [false,true]) {
    const blockedDigest=launcher({catalog:customList,query:'?map=sample',cachedCustom});
    blockedDigest.context.crypto.subtle.digest=()=>{throw Error('SecurityError');};blockedDigest.start();
    assert.equal(blockedDigest.calls.length,0);
    assert.match(blockedDigest.elements.etl_status.textContent,/verification is unavailable.*localhost or HTTPS/);
}
console.log('Browser support checks passed (engine gating, WebAssembly/WebGL/dialog failures, disposable graphics probe and secure custom-pack hashing).');
const soloCustom=launcher({catalog:customList,query:'?map=sample&bots=6'});
assert.equal(soloCustom.elements.mapselect.value,'sample');
assert.equal(soloCustom.elements.botcount.value,0);
assert.equal(soloCustom.elements.botcount.disabled,true);
assert.match(soloCustom.elements.mapbrief.textContent,/No bot navigation/);
soloCustom.elements.mapselect.value='radar';soloCustom.elements.mapselect.change();
assert.equal(soloCustom.elements.botcount.disabled,false);
assert.equal(soloCustom.elements.botcount.value,6,'Returning to bundled maps restores the requested bot count');
const navCustom=launcher({catalog:customList,query:'?map=sample&bots=6',navigation:true});
assert.equal(navCustom.elements.botcount.value,6);
assert.equal(navCustom.elements.botcount.disabled,false);
const offlineCatalog=launcher({catalogMissing:true,savedCatalog:customList,query:'?map=sample'});
assert.equal(offlineCatalog.elements.mapselect.value,'sample');
assert.match(offlineCatalog.elements.customstatus.textContent,/saved map list/);
const badCustom=launcher({catalog:{version:1,packs:[{...extraPack,name:'../evil.pk3'}]}});
assert.equal(badCustom.elements.mapselect.value,'oasis');
const slowCatalog=launcher({catalog:customList,catalogLate:true});
const fatalCatalog=launcher({catalog:customList,catalogLate:true});
const fatalCatalogRequest=fatalCatalog.context.catalogRequest;
fatalCatalog.context.showFatal('Runtime stopped during map lookup');
assert.equal(fatalCatalogRequest.aborted,true,'Fatal errors abort map catalog requests');
fatalCatalog.pending.shift()();
assert.equal(fatalCatalog.elements.playbtn.disabled,false,'A late map catalog cannot disable fatal-error Retry');
assert.equal(fatalCatalog.elements.playbtn.textContent,'RETRY');
const fatalImport=launcher({importLate:true});
const completedImport=launcher({importLate:true,catalog:customList});
completedImport.elements.customfile.files=[{name:'sample.pk3',size:zip.length}];completedImport.elements.customfile.change();
completedImport.context.catalogRequest.upload.onprogress({lengthComputable:true,loaded:16,total:32});
assert.match(completedImport.elements.customstatus.textContent,/Uploading sample.pk3 · 50%/);
completedImport.context.catalogRequest.upload.onload();assert.match(completedImport.elements.customstatus.textContent,/Verifying/);
completedImport.pending.shift()();
assert.equal(completedImport.elements.mapselect.value,'sample','A completed import still selects its installed map');
assert.equal(completedImport.elements.playbtn.disabled,false);
assert.equal(completedImport.elements.customfile.disabled,false);
assert.equal(completedImport.context.catalogRequest,null);
assert.equal(completedImport.context.catalogBusy,false);
fatalImport.elements.customfile.files=[{name:'sample.pk3',size:zip.length}];fatalImport.elements.customfile.change();
const fatalImportRequest=fatalImport.context.catalogRequest, importMap=fatalImport.context.initialMap;
fatalImport.context.showFatal('Runtime stopped during map import');
assert.equal(fatalImportRequest.aborted,true,'Fatal errors abort imports');
const importStatus=fatalImport.elements.customstatus.textContent;
fatalImportRequest.upload.onprogress({lengthComputable:true,loaded:zip.length,total:zip.length});
fatalImportRequest.upload.onload();fatalImport.pending.shift()();fatalImportRequest.ontimeout();
assert.equal(fatalImport.elements.customstatus.textContent,importStatus,'Late import progress cannot rewrite fatal recovery state');
assert.equal(fatalImport.context.initialMap,importMap,'A stale import cannot change the selected map');
assert.equal(fatalImport.elements.playbtn.disabled,false);
assert.equal(fatalImport.elements.playbtn.textContent,'RETRY');
assert.equal(fatalImport.context.catalogBusy,false);
fatalImport.elements.refreshmaps.click();assert.equal(fatalImport.context.catalogRequest,null,'Fatal recovery cannot restart map discovery');
fatalImport.start();assert.equal(fatalImport.calls.at(-1),'retry');
const interruptedImportFault={importLate:true}, interruptedImport=launcher(interruptedImportFault);
interruptedImport.elements.customfile.files=[{name:'sample.pk3',size:zip.length}];interruptedImport.elements.customfile.change();
const oldImport=interruptedImport.context.catalogRequest;oldImport.ontimeout();
interruptedImportFault.catalogLate=true;interruptedImport.elements.refreshmaps.click();
const newCatalog=interruptedImport.context.catalogRequest, refreshStatus=interruptedImport.elements.customstatus.textContent;
oldImport.upload.onprogress({lengthComputable:true,loaded:1,total:2});oldImport.upload.onload();interruptedImport.pending.shift()();
assert.equal(interruptedImport.elements.customstatus.textContent,refreshStatus,'A timed-out upload cannot overwrite a newer refresh');
assert.equal(interruptedImport.context.catalogRequest,newCatalog);
assert.equal(interruptedImport.context.catalogBusy,true,'A stale import cannot finish a newer request');
interruptedImport.pending.shift()();assert.equal(interruptedImport.elements.playbtn.disabled,false);
const refreshFocusFault={}, refreshFocus=launcher(refreshFocusFault);refreshFocus.context.document.body={};
refreshFocusFault.catalogLate=true;refreshFocus.elements.refreshmaps.focus();refreshFocus.elements.refreshmaps.click();
refreshFocus.context.document.activeElement=refreshFocus.context.document.body;refreshFocus.pending.shift()();
assert.equal(refreshFocus.context.document.activeElement,refreshFocus.elements.refreshmaps,'Map refresh restores focus lost when its button was disabled');
refreshFocus.elements.refreshmaps.click();refreshFocus.elements.botcount.focus();refreshFocus.pending.shift()();
assert.equal(refreshFocus.context.document.activeElement,refreshFocus.elements.botcount,'Map refresh respects a player who moved to another control');
refreshFocus.elements.refreshmaps.focus();refreshFocus.elements.refreshmaps.click();refreshFocus.elements.onlinebtn.click();
refreshFocus.context.document.activeElement=refreshFocus.context.document.body;refreshFocus.pending.shift()();
assert.equal(refreshFocus.context.document.activeElement,refreshFocus.context.document.body,'Map refresh cannot focus a hidden control after switching to Online');
slowCatalog.start();assert.equal(slowCatalog.calls.length,0,'Map discovery prevents racing engine startup');
slowCatalog.pending.shift()();assert.equal(slowCatalog.elements.playbtn.disabled,false);
(async()=>{
    for(const cachedCustom of [false,true]) {
        const page=launcher({catalog:customList,query:'?map=sample',cachedCustom});page.start();
        for(let n=0;n<12;n++)await Promise.resolve();
        assert.equal(page.calls.length,1,'Verified custom map boots');
        assert.equal(page.calls[0].at(-1),'sample');
        assert.deepEqual(page.files.get('/etmain/sample.pk3'),{link:'/idb/etmain/'+extraPack.sha256+'.pk3'});
        assert.equal(page.requests.some(url=>url.startsWith('assets/custom/')),!cachedCustom,'Offline cached custom packs require no download');
    }
    const corruptCustom=launcher({catalog:customList,query:'?map=sample',customCorrupt:true});corruptCustom.start();
    for(let n=0;n<12;n++)await Promise.resolve();
    assert.equal(corruptCustom.calls.length,0);assert.match(corruptCustom.elements.etl_status.textContent,/integrity check/);
    const wrongHash=launcher({catalog:{version:1,packs:[{...extraPack,sha256:'0'.repeat(64)}]},query:'?map=sample'});wrongHash.start();
    for(let n=0;n<12;n++)await Promise.resolve();
    assert.equal(wrongHash.calls.length,0,'Matching CRC cannot bypass the SHA-256 check');
    assert.match(wrongHash.elements.etl_status.textContent,/integrity check/);
    const otherPacks=['1','2'].map((c,n)=>({...extraPack,name:'other'+n+'.pk3',maps:['other'+n],sha256:c.repeat(64),size:268435456}));
    const cacheBudget=launcher({catalog:{version:1,packs:[extraPack,...otherPacks]},query:'?map=sample',
        cacheFiles:[...otherPacks.map(pack=>[pack.sha256+'.pk3',pack.size]),['3'.repeat(64)+'.pk3',100]]});cacheBudget.start();
    for(let n=0;n<12;n++)await Promise.resolve();
    assert.equal(cacheBudget.calls.length,1);
    assert.equal(cacheBudget.files.has('/idb/etmain/'+otherPacks[0].sha256+'.pk3'),false,'Evict unused custom packs to reserve space within 512 MB');
    assert.equal(cacheBudget.files.has('/idb/etmain/'+otherPacks[1].sha256+'.pk3'),true,'Keep other installed maps when they fit');
    assert.equal(cacheBudget.files.has('/idb/etmain/'+'3'.repeat(64)+'.pk3'),false,'Remove obsolete versions only from the managed cache');
    console.log('Custom-map checks passed (catalog validation, navigation fallback, cached offline loading, CRC and SHA-256 failures).');
})().catch(error=>{console.error(error);process.exitCode=1;});
const runtime = launcher();
runtime.start();
runtime.events.error({ error: { name: 'RuntimeError', message: 'null function' } });
assert.equal(runtime.elements.overlay.style.display, 'flex');
assert.equal(runtime.elements.playbtn.textContent, 'RETRY');
console.log('Launcher fault checks passed (cache, downloads, retry, options, runtime errors).');
const corruptBody = launcher({corruptBody:true}); corruptBody.start();
assert.equal(corruptBody.requests.at(-1), 'assets/pak0.pk3');
const cached = launcher({savedManifest:true}); cached.start();
assert.equal(cached.writes, 0, 'Unchanged cache is not persisted again');
assert.equal(cached.files.get('/etmain/pak0.pk3').link, '/idb/etmain/pak0.pk3');
const offline = launcher({savedManifest:true, manifestMissing:true}); offline.start();
assert.equal(offline.calls.length, 1, 'Verified cached manifest supports offline assets');
const unavailable = launcher({manifestMissing:true}); unavailable.start();
assert.equal(unavailable.elements.playbtn.textContent, 'RETRY');
const stale = launcher({manifestLate:true}); stale.start();
stale.context.showFatal('Stopped'); stale.pending[0]();
assert.equal(stale.calls.length, 0, 'Late callback cannot boot after failure');
assert.equal(stale.elements.playbtn.textContent, 'RETRY');
const hung = launcher({syncHang:true}); hung.start();
Array.from(hung.timers.values())[0]();
assert.equal(hung.calls.length, 1, 'Cache timeout still permits isolated in-memory play');
assert.equal(hung.files.get('/etmain/pak0.pk3').link, '/session-assets/pak0.pk3');
hung.pending[0](); assert.equal(hung.calls.length, 1, 'Late IDB completion cannot boot twice');
const twice = launcher(); twice.start(); twice.context.Module.onRuntimeInitialized(); twice.start();
assert.equal(twice.calls.length, 1, 'Engine initializes once');
console.log('Full-byte integrity and asynchronous lifecycle checks passed.');

for (const fault of [{manifestLate:true},{syncHang:true},{download:true,downloadLate:true}]) {
    const cancelling=launcher(fault);cancelling.start();
    assert.equal(cancelling.elements.cancelload.hidden,false,'Preparing files offers a way back to match setup');
    const request=cancelling.context.activeRequest;
    cancelling.elements.cancelload.click();cancelling.elements.cancelload.click();
    assert.deepEqual(cancelling.calls,['retry'],'Repeated cancellation performs one navigation and never boots');
    if (request) assert.equal(request.aborted,true);
    for (const late of cancelling.pending) late();
    assert.deepEqual(cancelling.calls,['retry'],'Late cache and file responses cannot boot a cancelled match');
}
const cancelRunning=launcher();cancelRunning.start();cancelRunning.elements.cancelload.click();assert.equal(cancelRunning.calls.length,1,'Cancel loading cannot interrupt an active match');

const onlineUI=launcher(); onlineUI.elements.onlinebtn.click();
assert.equal(onlineUI.elements.servername.textContent,'Audit server');
assert.equal(onlineUI.elements.serverhostname.textContent,'Audit ET server');
assert.match(onlineUI.elements.serverpopulation.textContent,/8 \/ 16.*3 humans/);
assert.equal(onlineUI.elements.servermap.textContent,'Map: oasis');
assert.match(onlineUI.elements.serverresponse.textContent,/25 ms from the relay host/);
assert.doesNotMatch(onlineUI.elements.onlinestatus.textContent,/Ready to join through/,'Configuration alone cannot establish readiness');
for(const onlineInfo of [null,{status:'unavailable'},{status:'online',compatible:'yes'}]) {
    const unknown=launcher({onlineInfo});unknown.elements.onlinebtn.click();
    assert.match(unknown.elements.onlinestatus.textContent,/not been verified/);
    assert.equal(unknown.elements.joinbtn.textContent,'TRY JOINING SERVER');
    unknown.elements.joinbtn.click();assert.equal(unknown.calls.length,1,'Hidden or nonresponsive query servers can still accept game connections');
}
const incompatibleInfo={status:'online',compatible:false,hostname:'^1Other mod',map:'sample',players:16,capacity:16,humans:8,latencyMs:10};
const incompatibleServer=launcher({onlineInfo:incompatibleInfo});incompatibleServer.elements.onlinebtn.click();
assert.match(incompatibleServer.elements.serverpopulation.textContent,/Full/);
assert.match(incompatibleServer.elements.serverrules.textContent,/Incompatible/);
incompatibleServer.elements.joinbtn.click();assert.equal(incompatibleServer.calls.length,0);
assert.equal(incompatibleServer.networkCalls.length,0,'Incompatible servers do not open a relay or load game files');
incompatibleServer.elements.offlinebtn.click();incompatibleServer.start();assert.equal(incompatibleServer.calls.length,1);
const privateInfo={...incompatibleInfo,compatible:true,password:true,hostname:'<img src=x onerror=bad> Audit',latencyMs:NaN};
const passwordServer=launcher({onlineInfo:privateInfo});passwordServer.elements.onlinebtn.click();
assert.equal(passwordServer.elements.serverhostname.textContent,privateInfo.hostname,'Server names are rendered as text');
assert.equal(passwordServer.elements.serverpassword.open,true);
assert.equal(passwordServer.elements.joinbtn.textContent,'ENTER SERVER PASSWORD');
passwordServer.elements.joinbtn.click();assert.equal(passwordServer.networkCalls.length,0);
for(const password of ['a"bad','a\\bad','a;bad','a\nbad','x'.repeat(65)]) {
    passwordServer.elements.onlinepassword.value=password;passwordServer.elements.onlinepassword.input();
    assert.equal(passwordServer.elements.joinbtn.disabled,true);assert.equal(passwordServer.elements.onlinepassword['aria-invalid'],'true');
}
passwordServer.elements.onlinepassword.value='a+complex password.dm_84';passwordServer.elements.onlinepassword.input();
assert.equal(passwordServer.elements.joinbtn.disabled,false);
passwordServer.elements.joinbtn.click();assert.equal(passwordServer.calls.length,1);
assert.equal(passwordServer.elements.onlinepassword.value,'');assert.equal(passwordServer.context.matchOptions.password,'');
assert.equal(passwordServer.files.get('/browser/legacy/browser-connect.cfg'),'set password "a+complex password.dm_84"\n');
assert.ok(passwordServer.calls[0].includes('browser-connect.cfg'));assert.ok(!passwordServer.calls[0].includes('a+complex password.dm_84'),'Passwords never become command-line commands');
const privateResume=launcher({onlineInfo:privateInfo,autoAssets:true,assetResume:{version:1,relay:'ws://localhost:8082/relay',required:[{game:'etmain',name:'sample.pk3',checksum:123}],attempts:1,expires:Date.now()+10000}});
assert.equal(privateResume.calls.length,0,'Asset recovery cannot auto-join without a required password');
privateResume.elements.onlinepassword.value='audit';privateResume.elements.onlinepassword.input();privateResume.elements.joinbtn.click();
assert.equal(privateResume.context.serverPackSelection[0].name,'sample.pk3','Manual password entry preserves exact recovered pack selection');
const passwordClear=launcher();passwordClear.elements.onlinebtn.click();passwordClear.elements.onlinepassword.value='audit';passwordClear.elements.offlinebtn.click();
assert.equal(passwordClear.elements.onlinepassword.value,'','Switching offline discards the transient password');
const passwordSettings={};const passwordProfile=launcher(passwordSettings);
passwordProfile.context.Module.browserSaveSettings('// generated by ET\nseta password "audit-secret"\nseta cg_fov "90"\n');passwordProfile.context.flushSettings();
assert.doesNotMatch(passwordSettings.engineSettings.config,/audit-secret|seta password/,'An archived native password cannot enter browser storage');
assert.match(passwordSettings.engineSettings.config,/cg_fov "90"/);
console.log('Server browser UI checks passed (live details, unavailable/incompatible/full servers, password validation and transient delivery, asset-recovery resume and offline fallback).');
assert.equal(onlineUI.elements.matchform.hidden,true);
assert.equal(onlineUI.elements.onlinepanel.hidden,false);
assert.equal(onlineUI.elements.joinbtn.disabled,false);
onlineUI.elements.joinbtn.click();onlineUI.elements.joinbtn.click();
assert.equal(onlineUI.calls.length,1,'Repeated joins cannot boot twice');
assert.ok(onlineUI.calls[0].includes('+connect'));
assert.equal(onlineUI.calls[0][onlineUI.calls[0].lastIndexOf('cl_allowDownload') + 1], '0', 'Native download installation stays disabled');
assert.equal(onlineUI.calls[0][onlineUI.calls[0].lastIndexOf('cl_wwwDownload') + 1], '1', 'Online userinfo permits metadata-only redirect discovery');
assert.equal(onlineUI.calls[0].at(-1),'192.0.2.1:27960');
assert.equal(onlineUI.calls[0].includes('+map'),false);
assert.equal(onlineUI.calls[0].includes('g_gametype'),false,'The online server owns its match mode');
assert.equal(onlineUI.files.has('/omni-bot/et/user/omni-bot.cfg'),false,'Online boot does not start local bots');
assert.equal(onlineUI.elements.networkbtn.hidden,false);
const loadingOnline=launcher({manifestLate:true});loadingOnline.elements.onlinebtn.click();loadingOnline.elements.joinbtn.click();
assert.equal(loadingOnline.elements.joinbtn.textContent,'LOADING GAME…','Game-file loading has a distinct online action state');
assert.equal(loadingOnline.elements.joinbtn.disabled,true);
assert.equal(loadingOnline.elements.joinbtn['aria-busy'],'true');
for(const fault of [{onlineDisabled:true},{onlineUnavailable:true},{onlineTimeout:true}]) {
    const unavailableUI=launcher(fault);unavailableUI.elements.onlinebtn.click();
    assert.equal(unavailableUI.elements.joinbtn.disabled,true);
    assert.equal(unavailableUI.calls.length,0);
    unavailableUI.elements.offlinebtn.click();unavailableUI.start();
    assert.equal(unavailableUI.calls.length,1,'Online config failure preserves offline boot');
}
const lateOnline=launcher({onlineLate:true});lateOnline.elements.onlinebtn.click();lateOnline.elements.offlinebtn.click();lateOnline.pending[0]();
assert.equal(lateOnline.context.onlineConfigured,false,'Old mode cannot apply a late online configuration');
const checkingOnline=launcher({onlineLate:true});checkingOnline.elements.onlinebtn.click();
assert.equal(checkingOnline.elements.refreshonline.disabled,true,'Configuration checks cannot be duplicated');
assert.equal(checkingOnline.elements.refreshonline.textContent,'Checking…');
checkingOnline.elements.refreshonline.click();assert.equal(checkingOnline.requests.filter(url=>url==='network/config.json').length,1);
checkingOnline.pending[0]();assert.equal(checkingOnline.elements.refreshonline.disabled,false);
assert.equal(checkingOnline.elements.refreshonline.textContent,'Check again');
checkingOnline.elements.refreshonline.focus();checkingOnline.elements.refreshonline.click();
checkingOnline.context.document.body={};checkingOnline.context.document.activeElement=checkingOnline.context.document.body;
checkingOnline.pending[1]();assert.equal(checkingOnline.context.document.activeElement,checkingOnline.elements.refreshonline,'A completed check restores keyboard focus lost when its button was disabled');
checkingOnline.elements.refreshonline.click();checkingOnline.elements.onlinebtn.focus();checkingOnline.pending[2]();
assert.equal(checkingOnline.context.document.activeElement,checkingOnline.elements.onlinebtn,'Check completion cannot steal focus from another action');
const rejectedRelay=launcher({relayError:true});rejectedRelay.elements.onlinebtn.click();rejectedRelay.elements.joinbtn.click();
const duplicatePlayer=launcher();duplicatePlayer.elements.onlinebtn.click();duplicatePlayer.elements.joinbtn.click();
const passwordRetry=launcher();passwordRetry.elements.onlinebtn.click();passwordRetry.elements.joinbtn.click();
const badPasswordStats={failed:true,ready:false,opening:false,sent:1,received:1};
passwordRetry.context.Module.browserNetworkStatus('Connection failed','Invalid password',badPasswordStats);
passwordRetry.context.Module.browserNetworkFailure('Invalid password',true);
assert.equal(passwordRetry.elements.retrypasswordfield.hidden,false);
assert.equal(passwordRetry.context.document.activeElement,passwordRetry.elements.retrypassword);
const passwordAttempts=passwordRetry.networkCalls.length;
for (const invalid of ['', 'bad"quote', 'bad\\slash', 'bad;command', 'bad\nline', 'x'.repeat(65)]) {
 passwordRetry.elements.retrypassword.value=invalid;passwordRetry.elements.retryconnection.click();
 assert.equal(passwordRetry.networkCalls.length,passwordAttempts,'Invalid replacement passwords cannot open a relay');
 assert.equal(passwordRetry.elements.retrypassword['aria-invalid'],'true');
}
passwordRetry.elements.retrypassword.value='new +safe password';passwordRetry.elements.retryconnection.click();
assert.equal(passwordRetry.files.get('/browser/legacy/browser-connect.cfg'),'set password "new +safe password"\n');
assert.equal(passwordRetry.elements.retrypassword.value,'');
assert.equal(passwordRetry.elements.retrypasswordfield.hidden,true);
assert.equal(passwordRetry.networkCalls.length,passwordAttempts+1);
passwordRetry.context.Module.browserNetworkStatus('Connection failed','Invalid password',badPasswordStats);
passwordRetry.elements.retrypassword.value='discard-me';passwordRetry.elements.networkdialog.close();
assert.equal(passwordRetry.elements.retrypassword.value,'','Closing the connection dialog clears entered credentials');
passwordRetry.context.Module.browserNetworkStatus('Online','Connected',{failed:false,ready:true,sent:2,received:2});
assert.equal(passwordRetry.elements.retrypasswordfield.hidden,true);
const passwordWriteFailure=launcher();passwordWriteFailure.elements.onlinebtn.click();passwordWriteFailure.elements.joinbtn.click();
passwordWriteFailure.context.Module.browserNetworkStatus('Connection failed','Invalid password',badPasswordStats);
passwordWriteFailure.elements.retrypassword.value='safe';
passwordWriteFailure.context.Module.FS.writeFile=()=>{throw Error('Read only');};
passwordWriteFailure.elements.retryconnection.click();
assert.equal(passwordWriteFailure.networkCalls.length,1);assert.match(passwordWriteFailure.elements.connectionstatus.textContent,/Could not update the password/);
const playerIdentity=duplicatePlayer.files.get('/browser/etmain/etkey');
duplicatePlayer.context.Module.browserNetworkStatus('Disconnected','Bad GUID: Duplicate etkey.',{failed:true,sent:1,received:1});
assert.match(duplicatePlayer.elements.connectionhint.textContent,/Close or disconnect.*then retry/);
assert.match(duplicatePlayer.elements.connectionhint.textContent,/previous connection.*old server session to time out/);
assert.equal(duplicatePlayer.files.get('/browser/etmain/etkey'),playerIdentity,'Duplicate-player recovery must preserve the saved identity');
duplicatePlayer.context.Module.browserNetworkStatus('Online','Packets flowing',{failed:false,ready:true,sent:2,received:2});
assert.doesNotMatch(duplicatePlayer.elements.connectionhint.textContent,/Separate players/,'Successful reconnection clears duplicate-player guidance');
assert.equal(rejectedRelay.calls.length,0);
assert.equal(rejectedRelay.elements.joinbtn.disabled,false);
assert.equal(rejectedRelay.elements.offlinebtn.disabled,false);
assert.match(rejectedRelay.elements.onlinestatus.textContent,/Relay unavailable/);
const closedRelay=launcher({relayClosed:true});closedRelay.elements.onlinebtn.click();closedRelay.elements.joinbtn.click();
assert.equal(closedRelay.calls.length,0);
assert.equal(closedRelay.elements.playbtn.textContent,'RETRY');
assert.equal(closedRelay.elements.matchform.hidden,false,'Pre-boot relay loss exposes the restart action');
assert.equal(closedRelay.elements.playbtn['aria-describedby'],'etl_status','Fatal retry explains the error to keyboard and screen-reader users');
const pendingJoin=launcher({relayPending:true});pendingJoin.elements.onlinebtn.click();pendingJoin.elements.joinbtn.focus();pendingJoin.elements.joinbtn.click();
assert.equal(pendingJoin.context.document.activeElement,pendingJoin.elements.canceljoin,'A pending keyboard join exposes its cancellation action');
assert.equal(pendingJoin.elements.joinbtn.textContent,'CONNECTING…');
assert.equal(pendingJoin.elements.canceljoin.hidden,false,'Initial relay opening exposes cancellation');
assert.equal(pendingJoin.elements.retryconnection.disabled,true);
assert.equal(pendingJoin.elements.disconnectonline.disabled,false);
assert.equal(pendingJoin.elements.disconnectonline.textContent,'Cancel connection');
pendingJoin.elements.retryconnection.click();assert.equal(pendingJoin.networkCalls.length,1);
pendingJoin.elements.canceljoin.focus();pendingJoin.elements.canceljoin.click();assert.equal(pendingJoin.calls.length,0);
assert.equal(pendingJoin.context.document.activeElement,pendingJoin.elements.joinbtn,'Cancelling restores focus to a visible action');
assert.equal(pendingJoin.elements.joinbtn.textContent,'JOIN SERVER');
assert.equal(pendingJoin.elements.canceljoin.hidden,true);
assert.equal(pendingJoin.elements.joinbtn.disabled,false);
assert.equal(pendingJoin.elements.offlinebtn.disabled,false);
pendingJoin.elements.offlinebtn.click();pendingJoin.start();assert.equal(pendingJoin.calls.length,1);
onlineUI.context.Module.browserNetworkStatus('Opening relay','Opening…',{sent:0,received:0,ready:false,opening:true,failed:false});
const attempts=onlineUI.networkCalls.length;onlineUI.elements.retryconnection.click();
assert.equal(onlineUI.networkCalls.length,attempts,'A pending retry cannot be replaced by repeated clicks');
onlineUI.context.Module.browserNetworkStatus('Connection failed','Lost relay',{sent:1,received:1,ready:false,opening:false,failed:true});
onlineUI.context.Module.browserNetworkStatus('Connection failed','Lost relay',{sent:1,received:1,ready:false,opening:false,failed:true,serverLabel:'Audit ET server'});
assert.equal(onlineUI.elements.connectiontarget.hidden,false);
assert.equal(onlineUI.elements.connectiontarget.textContent,'Server: Audit ET server');
onlineUI.context.Module.browserNetworkStatus('Disconnected','Offline',{sent:0,received:0,ready:false,opening:false,failed:false,serverLabel:''});
assert.equal(onlineUI.elements.connectiontarget.hidden,true);
assert.equal(onlineUI.elements.connectiontarget.textContent,'');
assert.equal(onlineUI.elements.retryconnection.disabled,false);
assert.equal(onlineUI.elements.disconnectonline.disabled,true);
assert.equal(onlineUI.elements.retryconnection.hidden,false);
assert.equal(onlineUI.elements.disconnectonline.hidden,true);
onlineUI.elements.networkbtn.click();onlineUI.elements.retryconnection.click();
assert.equal(onlineUI.elements.networkdialog.open,true,'Relay opening must not dismiss server connection progress');
assert.equal(onlineUI.context.document.activeElement,onlineUI.elements.closenetwork,'Retry retains focus on an enabled dialog action');
onlineUI.context.Module.browserNetworkStatus('Joining server','Joining…',{sent:1,received:1,ready:true,opening:false,failed:false});
assert.equal(onlineUI.elements.networkdialog.open,true);
assert.match(onlineUI.elements.connectionhint.textContent,/Disconnect to cancel/,'Joining guidance refers to the available cancel action');
onlineUI.context.Module.browserNetworkStatus('Online','Audit server · 12 ms',{sent:2,received:2,ready:true,opening:false,failed:false});
assert.equal(onlineUI.elements.networkdialog.open,false,'A successful retry returns to the game only when the match is online');
assert.equal(onlineUI.context.document.activeElement,onlineUI.elements.canvas);
onlineUI.elements.networkbtn.click();
onlineUI.context.Module.browserNetworkStatus('Online','Audit server · 13 ms',{sent:3,received:3,ready:true,opening:false,failed:false});
assert.equal(onlineUI.elements.connectionstatus.textContent,'Connected to the game server.','Ping updates do not repeat in the live status region');
assert.match(onlineUI.elements.networkstats.textContent,/13 ms/);
assert.equal(onlineUI.elements.retryconnection.hidden,true);
assert.equal(onlineUI.elements.disconnectonline.hidden,false);
assert.equal(onlineUI.elements.networkdialog.open,true,'Normal status updates must not close a dialog opened by the player');
onlineUI.elements.closenetwork.click();
onlineUI.context.Module.browserNetworkStatus('Connection failed','Rejected',{sent:3,received:3,ready:false,opening:false,failed:true});
onlineUI.context.Module.browserNetworkFailure('Rejected',true);
onlineUI.elements.retryconnection.click();onlineUI.elements.closenetwork.click();
onlineUI.elements.helpbtn.click();
onlineUI.context.Module.browserNetworkStatus('Online','Audit server · 14 ms',{sent:4,received:4,ready:true,opening:false,failed:false});
assert.equal(onlineUI.elements.helpdialog.open,true,'A retry finishing after Close cannot replace the player’s current dialog');
onlineUI.elements.closehelp.click();
onlineUI.context.Module.browserNetworkFailure('Rejected',true);
onlineUI.elements.retryconnection.click();
onlineUI.context.Module.browserNetworkFailure('Rejected again',true);
assert.equal(onlineUI.elements.networkdialog.open,true,'Retry failure keeps recovery actions visible');
console.log('Online UI checks passed (configuration, isolated online boot, disabled relay, timeout, stale replies, retry, offline fallback).');

const disconnectFocus=launcher({closeLate:true});disconnectFocus.elements.onlinebtn.click();disconnectFocus.elements.joinbtn.click();
disconnectFocus.context.Module.browserNetworkStatus('Online','12 ms',{sent:2,received:2,ready:true,opening:false,failed:false});
disconnectFocus.elements.networkbtn.click();disconnectFocus.elements.disconnectonline.click();
assert.equal(disconnectFocus.context.document.activeElement,disconnectFocus.elements.closenetwork,'Disconnect retains a visible focus target while native departure finishes');
disconnectFocus.context.Module.browserNetworkStatus('Disconnected','Disconnected by you',{sent:2,received:2,ready:false,opening:false,failed:false});
disconnectFocus.pending.shift()();
assert.equal(disconnectFocus.context.document.activeElement,disconnectFocus.elements.retryconnection,'Completed departure focuses the available retry action');
const disconnectElsewhere=launcher({closeLate:true});disconnectElsewhere.elements.onlinebtn.click();disconnectElsewhere.elements.joinbtn.click();
disconnectElsewhere.elements.networkbtn.click();disconnectElsewhere.elements.disconnectonline.click();
disconnectElsewhere.elements.backoffline.focus();
disconnectElsewhere.context.Module.browserNetworkStatus('Disconnected','Disconnected by you',{sent:0,received:0,ready:false,opening:false,failed:false});
disconnectElsewhere.pending.shift()();
assert.equal(disconnectElsewhere.context.document.activeElement,disconnectElsewhere.elements.backoffline,'Departure completion respects a player who moved to another action');

const publicEntries=[{id:'a'.repeat(32),host:'8.8.8.8',port:27960,info:{status:'online',compatible:true,hostname:'Public Oasis',map:'oasis',players:8,capacity:16,humans:3,latencyMs:25,password:false}},
    {id:'b'.repeat(32),host:'1.1.1.1',port:27960,info:{status:'online',compatible:true,hostname:'Public Radar',map:'radar',players:2,capacity:12,humans:0,latencyMs:40,password:true}}];
const publicCatalog={version:1,status:'ready',total:5,checked:5,servers:publicEntries};
function publicReady(fault) {
    const page=launcher(fault);page.elements.onlinebtn.click();
    page.elements.publicserver.value=publicEntries[0].id;page.elements.publicserver.change();return page;
}
const initialTicketFault={publicCatalog},initialTicket=publicReady(initialTicketFault);initialTicketFault.rotatedTicket=true;
initialTicket.elements.joinbtn.click();assert.equal(initialTicket.networkConfigs.at(-1).relay,'ws://localhost:8082/relay/'+'A'.repeat(101));
assert.equal(initialTicket.networkCalls.length,1,'Join renews a ticket after time spent in the launcher');
const initialFailureFault={publicCatalog},initialFailure=publicReady(initialFailureFault);initialFailureFault.onlineUnavailable=true;
initialFailure.elements.joinbtn.click();assert.equal(initialFailure.networkCalls.length,0);assert.equal(initialFailure.calls.length,0);
assert.equal(initialFailure.elements.joinbtn.disabled,false);assert.equal(initialFailure.elements.offlinebtn.disabled,false);
assert.match(initialFailure.elements.onlinestatus.textContent,/Could not refresh/);
const initialCancelFault={publicCatalog},initialCancel=publicReady(initialCancelFault);initialCancelFault.onlineLate=true;
initialCancel.elements.joinbtn.click();initialCancel.elements.canceljoin.click();initialCancel.pending.shift()();
assert.equal(initialCancel.networkCalls.length,0,'Cancel stops a late initial ticket request before opening the relay');
assert.equal(initialCancel.elements.offlinebtn.disabled,false);assert.equal(initialCancel.elements.joinbtn.disabled,false);
function retryPublic(fault={publicCatalog}) {
    const page=publicReady(fault);page.elements.joinbtn.click();
    page.context.Module.browserNetworkStatus('Connection failed','Lost connection',{sent:2,received:2,ready:false,opening:false,failed:true});
    page.context.Module.browserNetworkFailure('Lost connection',true);
    return page;
}
const renewedFault={publicCatalog},renewed=retryPublic(renewedFault);
renewedFault.rotatedTicket=true;const checksBefore=renewed.requests.filter(url=>url.startsWith('network/config.json')).length;
renewed.elements.retryconnection.click();
assert.equal(renewed.requests.filter(url=>url.startsWith('network/config.json')).length,checksBefore+1);
assert.equal(renewed.networkConfigs.at(-1).relay,'ws://localhost:8082/relay/'+'A'.repeat(101));
assert.equal(renewed.networkConfigs.at(-1).serverId,publicEntries[0].id);
assert.equal(renewed.networkCalls.length,2,'Retry refreshes the same public selection before reconnecting');
for(const changed of [{onlineUnavailable:true},{onlineTimeout:true},{onlineNetworkError:true},{onlineBadJson:true},
    {onlineOverride:{enabled:false}},{onlineOverride:{serverId:publicEntries[1].id}},
    {onlineOverride:{relay:'wss://other.example.org/relay/'+'B'.repeat(100)}},
    {onlineOverride:{serverInfo:{compatible:false}}}]) {
    const fault={publicCatalog},page=retryPublic(fault);Object.assign(fault,changed);page.elements.retryconnection.click();
    assert.equal(page.networkCalls.length,1,'A failed or changed selection cannot reuse the old ticket');
    assert.equal(page.elements.retryconnection.disabled,false);assert.equal(page.elements.retryconnection.hidden,false);
    assert.notEqual(page.elements.connectionstatus.textContent,'Refreshing the selected server connection…');
}
const refreshFault={publicCatalog},refreshCancelled=retryPublic(refreshFault);refreshFault.onlineLate=true;
refreshCancelled.elements.retryconnection.click();refreshCancelled.elements.retryconnection.click();
assert.equal(refreshCancelled.pending.length,1,'Repeated retry shares one ticket refresh');
refreshCancelled.elements.backoffline.click();refreshCancelled.pending.shift()();
assert.equal(refreshCancelled.networkCalls.length,1,'Leaving the game cancels a late ticket refresh');
const closeRefreshFault={publicCatalog},closeRefresh=retryPublic(closeRefreshFault);closeRefreshFault.onlineLate=true;
closeRefresh.elements.retryconnection.click();closeRefresh.elements.closenetwork.click();closeRefresh.elements.helpbtn.click();
closeRefresh.pending.shift()();assert.equal(closeRefresh.networkCalls.length,2,'Closing the dialog lets the requested connection finish');
closeRefresh.context.Module.browserNetworkStatus('Online','12 ms',{sent:2,received:2,ready:true,opening:false,failed:false});
assert.equal(closeRefresh.elements.helpdialog.open,true,'A completed refresh respects the player’s newer dialog');
const publicUI=launcher({publicCatalog}); publicUI.elements.onlinebtn.click();
assert.equal(publicUI.elements.publicbrowser.hidden,false);
assert.match(publicUI.elements.publicstatus.textContent,/2 matching compatible/);
publicUI.elements.serverfilter.value='radar'; publicUI.elements.serverfilter.input();
assert.equal(publicUI.context.renderPublicList(),1);
publicUI.elements.onlinepassword.value='unsaved'; publicUI.elements.publicserver.value=publicEntries[1].id; publicUI.elements.publicserver.change();
assert.equal(publicUI.elements.onlinepassword.value,'','Changing servers clears a password intended for another server');
assert.ok(publicUI.requests.includes('network/config.json?server='+publicEntries[1].id));
assert.equal(publicUI.context.selectedServerId,publicEntries[1].id);
publicUI.elements.serverfilter.value='no matches'; publicUI.elements.serverfilter.input();
assert.equal(publicUI.context.renderPublicList(),0);
assert.match(publicUI.elements.publicstatus.textContent,/No servers match this search.*Clear the search/);
assert.equal(publicUI.elements.publicserver.value,publicEntries[1].id,'Filtering preserves the selected destination');
publicUI.elements.publicserver.focus();
const pickerOptions=publicUI.elements.publicserver.children.length;
publicUI.context.renderPublicList();
assert.equal(publicUI.elements.publicserver.children.length,pickerOptions,'Background discovery cannot rebuild a focused native picker');
publicUI.context.document.activeElement=publicUI.elements.serverfilter;publicUI.elements.publicserver.blur();
assert.ok(publicUI.elements.publicserver.children.length>pickerOptions,'Leaving the picker applies the latest public options');
assert.equal(publicUI.elements.publicserver.value,publicEntries[1].id);
for (const status of ['checking','ready','unavailable']) {
    const page=launcher({publicCatalog:{...publicCatalog,status,servers:[],checked:status==='checking'?2:5}});
    page.elements.onlinebtn.click();
    const before=page.elements.publicstatus.textContent;
    page.elements.serverfilter.value='oasis';page.elements.serverfilter.input();
    assert.equal(page.elements.publicstatus.textContent,before,'Search preserves empty-catalog scan/error status');
    assert.match(before,status==='checking'?/2\/5 checked.*Checking/:status==='ready'?/No browser-compatible public servers found.*5\/5 checked.*Native-only/:/master is unavailable.*No recent compatible/);
}
const scanProgress=launcher({publicCatalog:{...publicCatalog,status:'checking',checked:3}});
scanProgress.elements.onlinebtn.click();scanProgress.elements.serverfilter.value='no match';scanProgress.elements.serverfilter.input();
assert.match(scanProgress.elements.publicstatus.textContent,/3\/5 checked.*Checking.*Clear the search/);
const stalePublic=launcher({publicCatalog:{...publicCatalog,status:'unavailable'}});
stalePublic.elements.onlinebtn.click();stalePublic.elements.serverfilter.value='no match';stalePublic.elements.serverfilter.input();
assert.match(stalePublic.elements.publicstatus.textContent,/master is unavailable.*Recent verified.*Clear the search/);
const refreshPublicFault={publicCatalog},refreshPublicUI=launcher(refreshPublicFault);
refreshPublicUI.elements.onlinebtn.click();refreshPublicFault.publicFailure=true;refreshPublicUI.elements.refreshpublic.click();
refreshPublicUI.elements.serverfilter.value='no match';refreshPublicUI.elements.serverfilter.input();
assert.match(refreshPublicUI.elements.publicstatus.textContent,/Could not refresh.*last list.*Clear the search/);
assert.equal(refreshPublicUI.context.publicEntries.length,2,'Refresh failure retains the last validated list');
refreshPublicFault.publicFailure=false;refreshPublicUI.elements.refreshpublic.click();
assert.doesNotMatch(refreshPublicUI.elements.publicstatus.textContent,/Could not refresh/,'A successful retry clears the error');
const publicLate=launcher({publicCatalog,publicLate:true}); publicLate.elements.onlinebtn.click();
publicLate.elements.refreshpublic.click();assert.equal(publicLate.requests.filter(url=>url==='network/servers.json').length,1,'Simultaneous refresh shares the pending request');
publicLate.elements.offlinebtn.click();publicLate.pending.shift()();
assert.equal(publicLate.context.publicEntries.length,0,'A late public list cannot overwrite offline state');
const publicInvalid=launcher({publicCatalog:{...publicCatalog,servers:[{...publicEntries[0],info:{...publicEntries[0].info,compatible:false}}]}}); publicInvalid.elements.onlinebtn.click();
assert.match(publicInvalid.elements.publicstatus.textContent,/Could not refresh/);
assert.equal(publicInvalid.context.publicEntries.length,0,'Malformed public lists cannot offer an incompatible destination');

function missingAssets(page, names='legacy/legacy_v2.86 etmain/sample', checksums='-123 456', missing='etmain/sample.pk3\n') {
    page.context.Module.browserMissingAssets(missing,names,checksums);
    page.context.Module.browserNetworkFailure('Server assets missing',true);
}
const recoveredFault={autoAssets:true};
const recovered=launcher(recoveredFault);recovered.elements.onlinebtn.click();recovered.elements.joinbtn.click();missingAssets(recovered);
assert.equal(recovered.calls.at(-1),'retry','Verified assets reload the engine to mount exact packs');
assert.equal(recoveredFault.assetResume.attempts,1);
assert.equal(recoveredFault.assetResume.required.length,2);
assert.ok(recovered.requests.includes('network/assets/etmain/sample.pk3?checksum=456'));
const resumed=launcher(recoveredFault);
assert.equal(resumed.networkCalls.length,1,'Asset recovery resumes online only after runtime and catalog readiness');
assert.equal(resumed.context.serverPackSelection.length,2);
const publicRecoverFault={autoAssets:true,publicCatalog};
const publicRecover=launcher(publicRecoverFault);publicRecover.elements.onlinebtn.click();publicRecover.elements.publicserver.value=publicEntries[0].id;publicRecover.elements.publicserver.change();publicRecover.elements.joinbtn.click();missingAssets(publicRecover);
assert.equal(publicRecoverFault.assetResume.serverId,publicEntries[0].id);
const publicResumed=launcher({...publicRecoverFault,rotatedTicket:true});
assert.equal(publicResumed.networkCalls.length,1,'A refreshed ticket rejoins the same public server after loading packs');
assert.equal(publicResumed.elements.publicserver.value,publicEntries[0].id,'Auto-rejoin keeps the selected server visible while the public list loads');
assert.ok(publicResumed.requests.includes('network/config.json?server='+publicEntries[0].id));
const publicWrongRelay=launcher({...publicRecoverFault,assetResume:{...publicRecoverFault.assetResume,relay:'ws://other:8082/relay/'+'A'.repeat(100)}});
assert.equal(publicWrongRelay.networkCalls.length,0,'A saved public ID cannot silently move to another relay operator');
const wrongRelay=launcher({...recoveredFault,assetResume:{...recoveredFault.assetResume,relay:'ws://other:8082/relay'}});
assert.equal(wrongRelay.networkCalls.length,0,'Changing the configured relay cannot autojoin a different server');
const limitFault={autoAssets:true,assetResume:{...recoveredFault.assetResume,attempts:4}};
const limited=launcher(limitFault);missingAssets(limited);
assert.equal(limited.requests.some(url=>url.startsWith('network/assets/')),false,'Changing or incompatible assets cannot loop forever');
assert.match(limited.elements.connectionstatus.textContent,/keep changing/);
const cancelled=launcher({autoAssets:true,assetLate:true});cancelled.elements.onlinebtn.click();cancelled.elements.joinbtn.click();missingAssets(cancelled);
assert.equal(cancelled.elements.retryconnection.disabled,true);
assert.equal(cancelled.elements.retryconnection.hidden,true);
assert.equal(cancelled.elements.disconnectonline.hidden,false);
assert.match(cancelled.elements.connectionhint.textContent,/Cancel to stop/);
cancelled.elements.disconnectonline.click();cancelled.pending[0]();
assert.equal(cancelled.calls.length,1,'A late cancelled download cannot reload or rejoin');
const fatalDownload=launcher({autoAssets:true,assetLate:true});fatalDownload.elements.onlinebtn.click();fatalDownload.elements.joinbtn.click();missingAssets(fatalDownload);
const fatalRequest=fatalDownload.context.assetRequest;
fatalDownload.context.showFatal('Native runtime stopped');
assert.equal(fatalRequest.aborted,true,'Fatal errors must cancel the active server pack request');
assert.equal(fatalDownload.elements.serverdownload.hidden,true);fatalDownload.pending[0]();assert.equal(fatalDownload.calls.length,1);
const storageFault={autoAssets:true,assetLate:true};const noResumeStorage=launcher(storageFault);noResumeStorage.elements.onlinebtn.click();noResumeStorage.elements.joinbtn.click();missingAssets(noResumeStorage);
storageFault.storage=true;noResumeStorage.pending[0]();
assert.equal(noResumeStorage.elements.reloadassets.hidden,false,'Verified packs have an explicit reload action if rejoin storage fails');
assert.equal(noResumeStorage.elements.networkbtn.textContent,'Packs ready');
assert.match(noResumeStorage.elements.connectionhint.textContent,/Reload to load/);
noResumeStorage.context.Module.browserNetworkStatus('Disconnected','Late status',{failed:true,sent:0,received:0});
assert.match(noResumeStorage.elements.connectionstatus.textContent,/Browser storage is unavailable/);
noResumeStorage.elements.reloadassets.click();assert.equal(noResumeStorage.calls.at(-1),'retry');
const cancelledStorageFault={...recoveredFault,assetLate:true};const cancelledWithStorage=launcher(cancelledStorageFault);
cancelledStorageFault.storage=true;cancelledWithStorage.context.cancelServerAssets();
assert.equal(cancelledWithStorage.context.assetResume,null,'Unavailable storage cannot retain a cancelled in-memory rejoin');
const unavailableAssets=launcher({autoAssets:true,assetFailure:true});unavailableAssets.elements.onlinebtn.click();unavailableAssets.elements.joinbtn.click();missingAssets(unavailableAssets);
assert.equal(unavailableAssets.calls.length,1);
assert.match(unavailableAssets.elements.connectionstatus.textContent,/could not be verified/);
assert.equal(unavailableAssets.elements.retryconnection.hidden,false);
assert.equal(unavailableAssets.context.document.activeElement,unavailableAssets.elements.retryconnection);
const badRefs=launcher({autoAssets:true});badRefs.elements.onlinebtn.click();badRefs.elements.joinbtn.click();missingAssets(badRefs,'etmain/../evil','456','etmain/../evil.pk3\n');
assert.equal(badRefs.requests.some(url=>url.startsWith('network/assets/')),false);
const disabledAssets=launcher();disabledAssets.elements.onlinebtn.click();disabledAssets.elements.joinbtn.click();missingAssets(disabledAssets);
assert.equal(disabledAssets.requests.some(url=>url.startsWith('network/assets/')),false);
const largeLibrary=[1,2,3].map((n)=>({...extraPack,name:'map'+n+'.pk3',maps:['map'+n],sha256:String(n).repeat(64),size:268435456,checksum:n}));
const serverAssets={...extraPack,game:'legacy',name:'legacy_v2.86.pk3',maps:[],size:34825712,sha256:'4'.repeat(64),checksum:4};
const largeCatalog={version:1,packs:[...largeLibrary,serverAssets],errors:[]};
const boundedOnline=launcher({autoAssets:true,catalog:largeCatalog,syncHang:true});boundedOnline.elements.onlinebtn.click();boundedOnline.elements.joinbtn.click();
assert.equal(boundedOnline.context.bootFailed,false,'Unrelated installed maps cannot block an automatic online handshake');
assert.ok(boundedOnline.context.PAKS.some(pack=>pack.name===serverAssets.name),'Initial online loading reserves room for Legacy assets');
assert.ok(boundedOnline.context.PAKS.filter(pack=>pack.custom).reduce((total,pack)=>total+pack.entry.size,0)<=536870912);
const exactResume=launcher({autoAssets:true,catalog:largeCatalog,syncHang:true,assetResume:{version:1,relay:'ws://localhost:8082/relay',required:[{game:'etmain',name:'map3.pk3',checksum:3},{game:'legacy',name:serverAssets.name,checksum:4}],attempts:1,expires:Date.now()+60000}});
assert.deepEqual(Array.from(exactResume.context.PAKS.filter(pack=>pack.custom),pack=>pack.name),['map3.pk3',serverAssets.name],'Recovery mounts the exact server requirements regardless of unrelated map sizes');
const requiredTooLarge=launcher({autoAssets:true,catalog:largeCatalog,syncHang:true,assetResume:{version:1,relay:'ws://localhost:8082/relay',required:largeLibrary.map(pack=>({game:pack.game,name:pack.name,checksum:pack.checksum})),attempts:1,expires:Date.now()+60000}});
assert.equal(requiredTooLarge.context.bootFailed,true,'Actual server requirements must still obey the memory limit');
const streaming=launcher({autoAssets:true,assetStream:['{"stage":"downloading","loaded":1048576,', '"total":2097152}\n', '{"stage":"verifying"}\n', '{"installed":{"game":"etmain","checksum":456}}\n']});
streaming.elements.onlinebtn.click();streaming.elements.joinbtn.click();missingAssets(streaming);
streaming.pending.shift()(); assert.equal(streaming.calls.length,1,'Partial progress frames cannot trigger a reload');
streaming.pending.shift()(); assert.equal(streaming.elements.serverpackprogress.value,50);
assert.match(streaming.elements.serverpackdetail.textContent,/1.0 \/ 2.0 MB · 50%/);
streaming.context.Module.browserNetworkStatus('Disconnected','Old network status',{failed:true,sent:0,received:0});
assert.match(streaming.elements.connectionstatus.textContent,/Downloading/,'Network updates cannot overwrite pack progress');
streaming.pending.shift()(); assert.equal(streaming.elements.serverpackprogress.value,undefined,'Verification uses indeterminate progress');
streaming.pending.shift()();streaming.pending.shift()();assert.equal(streaming.calls.at(-1),'retry');
const streamError=launcher({autoAssets:true,assetStream:['{"error":"checksum","message":"private operator URL"}\n']});
streamError.elements.onlinebtn.click();streamError.elements.joinbtn.click();missingAssets(streamError);streamError.pending.shift()();
assert.match(streamError.elements.connectionstatus.textContent,/sample.pk3:.*different pack version/);
assert.doesNotMatch(streamError.elements.connectionstatus.textContent,/private operator/);
assert.equal(streamError.elements.retryconnection.disabled,false);
assert.equal(streamError.elements.serverdownload.hidden,true);
streamError.context.Module.browserNetworkStatus('Disconnected','Late native status',{failed:true,sent:0,received:0});
assert.match(streamError.elements.connectionstatus.textContent,/different pack version/,'Late native status must retain the actionable pack error');
streamError.pending.shift()();assert.equal(streamError.calls.length,1);
const truncatedStream=launcher({autoAssets:true,assetStream:['{"installed":{"game":"etmain","checksum":456}}']});
truncatedStream.elements.onlinebtn.click();truncatedStream.elements.joinbtn.click();missingAssets(truncatedStream);
truncatedStream.pending.shift()();truncatedStream.pending.shift()();assert.equal(truncatedStream.calls.length,1,'Truncated streams cannot mount unconfirmed packs');
for (const value of ['null\n','{"stage":"__proto__"}\n','[]\n']) {
    const invalidStream=launcher({autoAssets:true,assetStream:[value]});invalidStream.elements.onlinebtn.click();invalidStream.elements.joinbtn.click();missingAssets(invalidStream);
    invalidStream.pending.shift()();invalidStream.pending.shift()();assert.equal(invalidStream.calls.length,1);assert.equal(invalidStream.elements.retryconnection.disabled,false);
}
console.log('Server asset recovery checks passed (exact references, rejoin, relay changes, cancellation, failure, bounds and loop limit).');
const delayedDeparture=launcher({closeLate:true});delayedDeparture.start();delayedDeparture.context.returnToLauncher();
delayedDeparture.context.returnToLauncher();
assert.equal(delayedDeparture.pending.length,1,'Repeated returns cannot queue multiple reloads');
assert.equal(delayedDeparture.calls.length,1,'Return waits until native departure sends its final packets');
delayedDeparture.pending[0]();assert.equal(delayedDeparture.calls.at(-1),'retry');
const identityFault={};const identityPage=launcher(identityFault);identityPage.start();
assert.match(identityFault.playerKey,/^0000001002\d{18}$/);
const identityReload=launcher(identityFault);identityReload.start();
assert.equal(identityReload.files.get('/browser/etmain/etkey'),identityPage.files.get('/browser/etmain/etkey'),'Asset reloads preserve the native player identity');
const invalidIdentity=launcher({playerKey:'broken'});invalidIdentity.start();
assert.match(invalidIdentity.files.get('/browser/etmain/etkey'),/^0000001002\d{18}$/);

const matchPreferences={};
const linkedPreferences={query:'?map=goldrush&bots=12&difficulty=6&extra=keep',hash:'#controls'};
const linkedMatch=launcher(linkedPreferences);
linkedMatch.elements.mapselect.value='battery';linkedMatch.elements.botcount.value='4';linkedMatch.elements.difficulty.value='4';
linkedMatch.start();
assert.equal(linkedPreferences.historyUpdate.value,'/etl.html?map=battery&bots=4&difficulty=4&extra=keep#controls');
assert.equal(linkedPreferences.historyUpdate.state,linkedMatch.context.window.history.state,'Replacing match parameters preserves history state');
linkedMatch.elements.matchbtn.click();linkedMatch.elements.returnlauncher.click();
const linkedReload=launcher({query:linkedMatch.context.window.location.search,saved:linkedPreferences.saved});
assert.equal(linkedReload.elements.mapselect.value,'battery');assert.equal(linkedReload.elements.botcount.value,4);assert.equal(linkedReload.elements.difficulty.value,'4');
const deniedMatchStorage=launcher({storage:true});deniedMatchStorage.elements.mapselect.value='radar';deniedMatchStorage.elements.botcount.value='0';deniedMatchStorage.start();
assert.match(deniedMatchStorage.context.window.location.search,/map=radar&bots=0/,'URL state survives denied local storage');
const deniedMatchHistory=launcher({history:true});deniedMatchHistory.elements.mapselect.value='railgun';deniedMatchHistory.start();
assert.equal(deniedMatchHistory.calls[0].at(-1),'railgun','Denied history cannot block match startup');
const invalidMatchURL={query:'?map=goldrush&bots=12&difficulty=6'};
const invalidMatchForm=launcher(invalidMatchURL);invalidMatchForm.elements.matchform.reportValidity=()=>false;
invalidMatchForm.elements.mapselect.value='battery';invalidMatchForm.start();
assert.equal(invalidMatchURL.historyUpdate,undefined,'Invalid forms cannot replace deep-link choices');
assert.equal(invalidMatchForm.calls.length,0);
const onlineMatchURL={query:'?map=goldrush&bots=12&difficulty=6'};
const onlineLinkedMatch=launcher(onlineMatchURL);onlineLinkedMatch.elements.onlinebtn.click();onlineLinkedMatch.elements.joinbtn.click();
assert.equal(onlineMatchURL.historyUpdate,undefined,'Online joins do not rewrite offline match choices');
const returning=launcher(matchPreferences);
returning.elements.matchbtn.click(); assert.equal(returning.elements.matchdialog.open,undefined,'Match return is unavailable before boot');
returning.elements.mapselect.value='battery'; returning.elements.botcount.value='0'; returning.start();
returning.elements.helpbtn.click(); returning.elements.matchbtn.click();
assert.equal(returning.elements.helpdialog.open,false,'Match setup replaces the controls dialog');
assert.equal(returning.elements.matchdialog.open,true);
assert.equal(returning.context.document.activeElement,returning.elements.resumematch,'Keep playing is the initial keyboard action');
assert.equal(returning.calls.length,1,'Opening match setup does not end the match');
returning.elements.resumematch.click(); assert.equal(returning.elements.matchdialog.open,false);
assert.equal(returning.context.document.activeElement,returning.elements.canvas);
returning.elements.matchbtn.click(); returning.elements.returnlauncher.click();
assert.equal(returning.networkCloses,1,'Returning releases any transport');
assert.equal(returning.context.audioPageHidden,true,'Returning silences audio before navigation');
assert.equal(returning.calls.at(-1),'retry');
const savedMatch=launcher(matchPreferences);
assert.equal(savedMatch.elements.mapselect.value,'battery'); assert.equal(savedMatch.elements.botcount.value,0);
savedMatch.start(); assert.equal(savedMatch.calls[0].at(-1),'battery');
assert.match(savedMatch.files.get('/omni-bot/et/user/omni-bot.cfg'),/MinBots = 0\nMaxBots = 0/);
onlineUI.elements.matchbtn.click();
onlineUI.context.Module.browserNetworkFailure('Lost relay',true);
assert.equal(onlineUI.elements.matchdialog.open,false,'A connection failure replaces match setup');
assert.equal(onlineUI.elements.networkdialog.open,true);
onlineUI.elements.matchbtn.click(); assert.equal(onlineUI.elements.networkdialog.open,false);
onlineUI.elements.returnlauncher.click(); assert.equal(onlineUI.calls.at(-1),'retry','An online match can return to setup');
const fatalReturn=launcher(); fatalReturn.start(); fatalReturn.elements.matchbtn.click(); fatalReturn.context.showFatal('Stopped');
assert.equal(fatalReturn.elements.matchdialog.open,false,'Fatal cleanup closes match setup');
fatalReturn.elements.matchbtn.click(); assert.equal(fatalReturn.elements.matchdialog.open,false);
console.log('Match return checks passed (cancel, focus, transport/audio cleanup, saved solo setup, online failure and fatal recovery).');

const modalUI=launcher(); modalUI.start();
modalUI.elements.helpbtn.click(); modalUI.context.Module.browserAppOpen();
assert.equal(modalUI.context.Module.browserModalOpen(),false);
modalUI.elements.appdialog.showModal(); modalUI.elements.appdialog.focus();
assert.equal(modalUI.context.Module.browserModalOpen(),true);
assert.equal(modalUI.elements.helpdialog.open,false,'App setup replaces the controls dialog');
modalUI.context.resumeBrowserGame();
assert.equal(modalUI.context.document.activeElement,modalUI.elements.appdialog,'Resume cannot steal focus from an open app dialog');
modalUI.context.Module.browserNetworkFailure('Relay lost',true);
assert.equal(modalUI.elements.appdialog.open,false,'Network failure replaces app setup');
assert.equal(modalUI.elements.networkdialog.open,true);
modalUI.context.Module.browserAppResume();
modalUI.elements.helpdialog.closeEvent(); modalUI.elements.matchdialog.closeEvent();
assert.equal(modalUI.context.document.activeElement,modalUI.elements.retryconnection,'Queued close events cannot steal focus from the recovery action');
assert.equal(modalUI.elements.retryconnection['data-primary'],'true');
modalUI.elements.closenetwork.click();
assert.equal(modalUI.context.document.activeElement,modalUI.elements.canvas);
let appKeys=[]; modalUI.context.Module.browserApp={handleEscape(event){appKeys.push(event.key);}};
modalUI.events.keydown({target:modalUI.elements.mapselect,key:'Escape',stopImmediatePropagation(){}});
assert.deepEqual(appKeys,['Escape'],'Launcher keys reach app handling before the SDL input filter');
modalUI.elements.appdialog.showModal(); modalUI.context.showFatal('Stopped');
assert.equal(modalUI.elements.appdialog.open,false,'Fatal recovery closes the app modal');
assert.equal(modalUI.context.document.activeElement,modalUI.elements.playbtn,'Retry remains reachable after a fatal error inside app setup');
modalUI.context.Module.browserAppResume();
assert.equal(modalUI.context.document.activeElement,modalUI.elements.playbtn);
console.log('Cross-feature modal checks passed (app transitions, queued close events, fullscreen key forwarding and fatal retry focus).');

const lostGraphics=launcher();lostGraphics.start();let graphicsPrevented=0;
lostGraphics.elements.canvas.webglcontextlost({preventDefault(){graphicsPrevented++;}});
assert.equal(graphicsPrevented,1);
assert.match(lostGraphics.elements.etl_status.textContent,/graphics context was lost/);
assert.equal(lostGraphics.elements.playbtn.textContent,'RETRY');
assert.equal(lostGraphics.elements.wrap.inert,true);
assert.equal(lostGraphics.elements.gamebar.hidden,true);
lostGraphics.context.Module.onRuntimeInitialized();assert.equal(lostGraphics.calls.length,1);
lostGraphics.start();assert.equal(lostGraphics.calls.at(-1),'retry');

const setupUI=launcher();
setupUI.elements.botcount.value='0'; setupUI.elements.botcount.input();
assert.equal(setupUI.elements.difficulty.disabled,true);
assert.match(setupUI.elements.matchsummary.textContent,/Solo exploration/);
setupUI.elements.botcount.value='1'; setupUI.elements.botcount.input();
assert.equal(setupUI.elements.difficulty.disabled,false);
assert.match(setupUI.elements.matchsummary.textContent,/1 bot · Normal/);
setupUI.elements.botcount.value=''; setupUI.elements.botcount.input();
assert.match(setupUI.elements.matchsummary.textContent,/0–12 bots/);
assert.equal(setupUI.elements.botcount['aria-invalid'],'true');
setupUI.elements.botcount.value='4'; setupUI.elements.botcount.input();
assert.equal(setupUI.elements.botcount['aria-invalid'],'false','Correcting the bot count clears its accessible error state');
setupUI.elements.mapselect.value='railgun'; setupUI.elements.mapselect.change();
assert.match(setupUI.elements.mapbrief.textContent,/railway gun/);
setupUI.elements.botcount.value='4'; setupUI.start();
assert.equal(setupUI.elements.canvas.tabindex,0);
assert.equal(setupUI.elements.canvas['aria-hidden'],'false');
assert.equal(setupUI.elements.wrap.inert,false);
setupUI.context.Module.browserInputFrame(0);
setupUI.elements.canvas.keydown({key:'Tab',shiftKey:true,preventDefault(){},stopImmediatePropagation(){}});
assert.equal(setupUI.context.document.activeElement,setupUI.elements.helpbtn);
setupUI.elements.helpbtn.click();
assert.equal(setupUI.elements.helpdialog.open,true);
assert.equal(setupUI.context.document.activeElement,setupUI.elements.closehelp);
assert.equal(setupUI.elements.keyboardhelp.open,true);assert.equal(setupUI.elements.touchhelp.open,false);
setupUI.elements.closehelp.click();setupUI.context.Module.browserTouch={enabled:()=>true,frame(){}};setupUI.elements.helpbtn.click();
assert.equal(setupUI.elements.keyboardhelp.open,false);assert.equal(setupUI.elements.touchhelp.open,true);
assert.match(setupUI.elements.helpstart.textContent,/Tap Team/);
assert.equal(setupUI.elements.mousehelp.hidden,true);
assert.match(setupUI.elements.settingshelp.textContent,/Tap Menu/);
setupUI.elements.closehelp.click();
assert.equal(setupUI.elements.helpdialog.open,false);
assert.equal(setupUI.context.document.activeElement,setupUI.elements.canvas);
let stopped=0;
setupUI.events.keydown({target:setupUI.elements.volume,stopImmediatePropagation(){stopped++;}});
setupUI.events.keydown({target:setupUI.elements.canvas,stopImmediatePropagation(){stopped++;}});
setupUI.events.keypress({target:setupUI.elements.helpbtn,stopImmediatePropagation(){stopped++;}});
setupUI.events.keyup({target:setupUI.elements.closehelp,stopImmediatePropagation(){stopped++;}});
assert.equal(stopped,3, 'Toolbar keydown, keypress and keyup stay outside SDL');
setupUI.context.showFatal('Stopped');
assert.equal(setupUI.elements.wrap.inert,true);
assert.equal(setupUI.elements.canvas.tabindex,-1);
assert.equal(setupUI.elements.etl_status.role,'alert');
console.log('Match summary, solo setup, keyboard help, canvas focus and error accessibility checks passed.');

// Permission requests must belong to a gesture; menus and focus loss release
// capture. Exercise the actual shell code, including asynchronous rejection.
(async function browserControls() {
    const primed = launcher({manifestLate:true});
    let contexts=0, closes=0;
    primed.context.window.AudioContext = class {
        constructor() {contexts++; this.state='suspended'; this.resumes=0;}
        resume() {this.resumes++; this.state='running'; return Promise.resolve();}
        close() {closes++; this.state='closed'; return Promise.resolve();}
    };
    primed.start(); primed.start();
    assert.equal(contexts,1, 'PLAY primes only one SDL audio context before async assets');
    assert.equal(primed.context.Module.SDL2.audioContext.resumes,1);
    assert.equal(primed.calls.length,0, 'Priming does not bypass asset verification');
    primed.context.showFatal('Unavailable assets');
    assert.equal(closes,1, 'Failed startup releases its audio context');
    const prefixedAudio=launcher();
    prefixedAudio.context.window.webkitAudioContext=primed.context.window.AudioContext;
    prefixedAudio.start();assert.equal(prefixedAudio.calls.length,1);
    assert.equal(prefixedAudio.context.Module.SDL2.audioContext.resumes,1,'Prefixed Web Audio starts within the Play gesture');
    const blockedAudio=launcher();
    blockedAudio.context.window.AudioContext=class {constructor(){throw Error('Audio disabled');}};
    blockedAudio.start();assert.equal(blockedAudio.calls.length,1,'Denied audio does not block gameplay');
    const cancelled=launcher(); cancelled.start();
    let rejectLock;
    cancelled.elements.canvas.requestPointerLock=()=>new Promise((resolve,reject)=>{rejectLock=reject;});
    cancelled.context.Module.browserInputFrame(0);
    cancelled.elements.canvas.mousedown({preventDefault(){},stopImmediatePropagation(){}});
    cancelled.context.Module.browserInputFrame(1); rejectLock(Error('Late denial')); await Promise.resolve();
    cancelled.context.Module.browserInputFrame(1);
    assert.match(cancelled.elements.focusstatus.textContent,/Menu/, 'Late denial cannot replace menu status');
    assert.equal(cancelled.context.capturePending,false);
    const timed=launcher(); timed.start(); timed.context.Module.browserInputFrame(0);
    timed.elements.canvas.requestPointerLock=()=>{};
    timed.elements.canvas.mousedown({preventDefault(){},stopImmediatePropagation(){}});
    Array.from(timed.timers.values())[0](); timed.context.Module.browserInputFrame(0);
    assert.match(timed.elements.focusstatus.textContent,/timed out/);
    assert.equal(timed.context.dragFallback,true, 'Stalled permission also offers drag aiming');
    let staleExits=0;
    timed.context.document.exitPointerLock=()=>{staleExits++; timed.context.document.pointerLockElement=null;};
    timed.context.document.pointerLockElement=timed.elements.canvas; timed.events.pointerlockchange();
    assert.equal(staleExits,1, 'A timed-out capture cannot acquire the mouse later');
    let retryRequests=0;
    timed.elements.canvas.requestPointerLock=()=>{retryRequests++;};
    timed.elements.capturebtn.click();
    assert.equal(retryRequests,1,'Capture mouse explicitly retries after fallback');
    timed.context.document.pointerLockElement=timed.elements.canvas;timed.events.pointerlockchange();
    assert.equal(timed.context.Module.browserInputFrame(0)&2,2,'A successful toolbar retry returns to captured gameplay');
    const escaped=launcher(); escaped.start(); escaped.context.Module.browserInputFrame(0);
    escaped.elements.canvas.requestPointerLock=()=>{};
    escaped.elements.canvas.mousedown({preventDefault(){},stopImmediatePropagation(){}});
    escaped.elements.canvas.keydown({key:'Escape'});
    escaped.context.document.exitPointerLock=()=>{escaped.context.document.pointerLockElement=null;};
    escaped.context.document.pointerLockElement=escaped.elements.canvas; escaped.events.pointerlockchange();
    assert.equal(escaped.context.document.pointerLockElement,null, 'Escape cancels a late pointer-lock grant');
    const page = launcher(); page.start();
    const {context:c, elements:e, events} = page, doc = c.document;
    let requests = 0, exits = 0;
    e.canvas.requestPointerLock = () => { requests++; };
    doc.exitPointerLock = () => { exits++; doc.pointerLockElement = null; events.pointerlockchange(); };
    const frame = mode => c.Module.browserInputFrame(mode);
    const gesture = () => ({preventDefault() {}, stopImmediatePropagation() {}});
    assert.equal(frame(0) & 1, 1);
    for (let i=0; i<120; i++) frame(0);
    assert.equal(requests, 0, 'Frame loop never requests pointer lock');
    e.canvas.mousedown(gesture()); e.canvas.mousedown(gesture());
    assert.equal(requests, 1, 'Only one pending gesture request');
    doc.pointerLockElement = e.canvas; events.pointerlockchange();
    assert.equal(frame(0) & 2, 2);
    assert.equal(e.capturebtn.disabled, true);
    assert.equal(frame(1) & 2, 0, 'Opening menus releases capture');
    assert.equal(exits, 1);
    e.canvas.mousedown(gesture()); assert.equal(requests, 1, 'Menu click is not a capture request');
    frame(0);
    e.canvas.requestPointerLock = () => { requests++; return Promise.reject(Error('Blocked')); };
    e.canvas.mousedown(gesture()); await Promise.resolve();
    frame(0); assert.match(e.focusstatus.textContent, /Capture blocked/);
    assert.equal(frame(0)&16,16,'Fallback explicitly permits stationary left-button input');
    assert.equal(frame(1)&16,0,'Menu mode cannot grant fallback gameplay buttons');frame(0);
    assert.equal(e.capturebtn.disabled, false, 'Failure permits retry');
    assert.equal(c.bootFailed, false, 'Capture denial cannot stop the engine');
    let stoppedFire=false;
    e.canvas.mousedown({button:0,preventDefault(){stoppedFire=true;},stopImmediatePropagation(){stoppedFire=true;}});
    assert.equal(stoppedFire,false, 'Stationary left click reaches gameplay after capture denial');
    assert.equal(requests,2, 'Stationary firing does not retry denied capture');
    e.canvas.mousedown({...gesture(),button:2});
    assert.equal(frame(0) & 8, 8, 'Right drag provides aiming after capture denial');
    assert.equal(requests,2, 'Drag fallback never retries lock on right press');
    e.canvas.mousedown({...gesture(),button:0});
    assert.equal(requests,2, 'Left click while dragging reaches gameplay');
    events.mouseup({button:2,target:e.helpbtn,stopImmediatePropagation(){}});
    assert.equal(frame(0) & 12,4, 'Right release stops aiming and clears held actions');
    e.canvas.mousedown({...gesture(),button:2}); e.volume.focus();
    events.focusout({target:e.canvas}); e.canvas.focus();
    assert.equal(frame(0) & 12,4, 'Toolbar focus cancels held drag aim before returning to the game');
    e.canvas.mousedown({...gesture(),button:2}); e.canvas.pointercancel();
    assert.equal(frame(0) & 12,4, 'Pointer cancellation releases aim and held input');
    doc.hidden = true; events.visibilitychange();
    const hiddenFlags=frame(0);
    assert.equal(hiddenFlags&16,0,'Fallback buttons are disabled when the page loses focus');
    assert.equal(hiddenFlags & 5, 4, 'Hidden page clears keys and loses focus');
    assert.equal(frame(0) & 4, 0, 'Reset consumed exactly once');
    doc.hidden = false; e.volume.focus();
    assert.equal(frame(0) & 1, 0, 'Toolbar focus is not engine keyboard focus');
    e.canvas.focus(); doc.blurred = true; events.blur();
    assert.equal(frame(0) & 1, 0, 'Window blur wins over active canvas');
    doc.blurred = false;

    const gains = [], processors = [];
    function audio() {
        const processor = {connections:[], disconnect() { this.connections=[]; }, connect(node) {this.connections.push(node);} };
        const ctx = {state:'suspended',currentTime:0,destination:{}, resumes:0,
            resume() {this.resumes++; this.state='running'; return Promise.resolve();},
            createGain() {const node={gain:{value:0, calls:[],cancelScheduledValues() {},setValueAtTime(value) {this.value=value;},setTargetAtTime(value) {this.value=value;this.calls.push(value);}},connect() {},disconnect() {this.disconnected=true;}}; gains.push(node); return node;}}
        c.Module.SDL2 = {audioContext:ctx,audio:{scriptProcessorNode:processor}};
        processors.push(processor); return ctx;
    }
    const first = audio(); frame(0);
    assert.equal(e.soundstatus['data-audio-state'], 'suspended');
    e.soundbtn.click(); frame(0);
    assert.equal(first.state, 'running'); assert.equal(gains[0].gain.value, .8);
    assert.equal(processors[0].connections.length, 1, 'One output path, not doubled audio');
    const ramps = gains[0].gain.calls.length; for(let i=0;i<120;i++) frame(0);
    assert.equal(gains[0].gain.calls.length, ramps, 'No per-frame audio automation buildup');
    e.soundbtn.click(); frame(0); assert.equal(gains[0].gain.value, 0);
    e.soundbtn.click(); e.volume.value=35; e.volume.input(); e.canvas.focus(); frame(0);
    assert.equal(gains[0].gain.value, .35);
    doc.blurred=true; events.blur(); frame(0); assert.equal(gains[0].gain.value, 0);
    doc.blurred=false; e.canvas.focus(); frame(0); assert.equal(gains[0].gain.value,.35);
    const second = audio(); frame(0);
    assert.equal(gains[0].disconnected, true, 'Audio restart releases old gain');
    assert.equal(processors[1].connections.length, 1);
    second.resume = () => Promise.reject(Error('Denied'));
    e.soundbtn.click(); await Promise.resolve(); frame(0);
    assert.match(e.soundstatus.textContent, /retry/, 'Resume denial remains recoverable');
    c.showFatal('Stopped'); assert.equal(gains[1].gain.value,0); assert.equal(e.gamebar.hidden,true);
    console.log('Browser capture, focus, audio resume/mute/volume/restart checks passed.');
})().catch(error => { console.error(error); process.exitCode=1; });

{
    const config = '// generated by ET do not modify\nunbindall\nbind F8 "+scores"\nseta sensitivity "3.25"\nseta r_gamma "1.4"\n';
    const saved = {engineSettings:{version:1,config}};
    const page = launcher(saved); page.start();
    assert.equal(page.files.get('/browser/legacy/etconfig.cfg'),config, 'Native settings restored before engine startup');
    assert.ok(page.calls[0].includes('/browser'), 'Native configs use the dedicated browser home directory');
    const updated = config.replace('3.25','4.5');
    page.context.Module.browserSaveSettings(updated);
    page.context.Module.browserSaveSettings(updated);
    assert.equal(page.timers.size,1, 'Rapid native settings writes share one storage timer');
    assert.equal(saved.engineSettings.config,config, 'Storage is deferred until the current snapshot is ready');
    page.elements.matchbtn.click(); page.elements.returnlauncher.click();
    assert.equal(saved.engineSettings.config,updated, 'Returning flushes pending settings before reload');
    assert.equal(page.timers.size,0);
    assert.match(page.elements.settingsstatus.textContent,/saved/);
    const reloaded = launcher(saved); reloaded.start();
    assert.equal(reloaded.files.get('/browser/legacy/etconfig.cfg'),updated);
    const later = updated.replace('4.5','2.75');
    reloaded.context.Module.browserSaveSettings(later); reloaded.events.pagehide();
    assert.equal(saved.engineSettings.config,later, 'Page exit also flushes pending settings');
    const deniedFault = {storage:true}; const denied = launcher(deniedFault); denied.start();
    denied.context.Module.browserSaveSettings(config); denied.context.flushSettings();
    assert.match(denied.elements.settingsstatus.textContent,/only for this page/);
    assert.equal(denied.calls.length,1, 'Storage denial cannot prevent gameplay');
    deniedFault.storage=false; denied.context.flushSettings();
    assert.equal(deniedFault.engineSettings.config,config, 'Failed saves remain available for retry');
    for (const bad of [{version:2,config}, {version:1,config:'exec unexpected.cfg'}, {version:1,config:config+'\0'}, {version:1,config:config+'x'.repeat(256*1024)}]) {
        const invalid = launcher({engineSettings:bad}); invalid.start();
        assert.equal(invalid.files.has('/browser/legacy/etconfig.cfg'),false);
        assert.equal(invalid.calls.length,1);
        const original = invalid.context.settingsConfig;
        invalid.context.Module.browserSaveSettings(bad.config);
        if (bad.version === 1) assert.equal(invalid.context.settingsConfig,original, 'Invalid native snapshots cannot replace current settings');
    }
    const unwritable = launcher({engineSettings:{version:1,config},settingsWriteError:true}); unwritable.start();
    assert.equal(unwritable.calls.length,1);
    assert.match(unwritable.elements.settingsstatus.textContent,/could not be restored/);
    for (const type of ['mousedown','mouseup','mousemove','wheel']) {
        let stopped = 0;
        page.events[type]({target:page.elements.helpbtn,stopImmediatePropagation(){stopped++;}});
        assert.equal(stopped,1, 'Toolbar/dialog mouse events are isolated from SDL');
        page.events[type]({target:page.elements.canvas,stopImmediatePropagation(){stopped++;}});
        assert.equal(stopped,1, 'Native canvas mouse events still reach SDL');
    }
    page.context.draggingAim=true;
    page.events.mouseup({target:page.elements.helpbtn,stopImmediatePropagation(){}});
    assert.equal(page.context.draggingAim,false,'Releasing outside the canvas still clears drag aiming');
    console.log('Native settings checks passed (reload, bindings/cvars, debounce, return/page-exit flush, invalid data and unavailable storage/filesystem).');
}

(async function audioFaults() {
    function device(page, options={}) {
        const listeners = new Set(), nodes = [];
        const processor = {connections:[{direct:true}],disconnect() {this.connections=[];},connect(node) {this.connections.push(node);}};
        const ctx = {state:options.state || 'running',currentTime:0,sampleRate:48000,resumes:0,destination:{},
            addEventListener(type, listener) {listeners.add(listener);}, removeEventListener(type, listener) {listeners.delete(listener);},
            change(state) {this.state=state; for(const listener of listeners) listener();},
            resume() {this.resumes++; if(options.pending) return options.pending; this.change('running'); return Promise.resolve();},
            createGain() {
                const node={gain:{value:0,cancels:0,cancelScheduledValues() {this.cancels++;},setValueAtTime(value) {this.value=value;},setTargetAtTime(value) {if(options.rampFailure) throw Error('Invalid parameter'); this.value=value;}},
                    connect() {if(options.connectFailure) throw Error('Graph failed');},disconnect() {this.disconnected=true;}};
                nodes.push(node); return node;
            }};
        page.context.Module.SDL2={audioContext:ctx,audio:{scriptProcessorNode:processor}};
        page.context.Module.browserInputFrame(1);
        return {ctx,processor,nodes,listeners};
    }
    const settings={sound:{volume:.45,muted:true}};
    const muted=launcher(settings); muted.start(); const a=device(muted,{state:'suspended'});
    muted.elements.volume.value=60; muted.elements.volume.input();
    assert.equal(settings.sound.muted,true, 'Changing volume preserves explicit mute in storage');
    assert.equal(muted.elements.soundbtn.textContent,'Unmute');
    assert.equal(a.nodes[0].gain.value,0, 'Resuming a context never clears saved mute');
    a.ctx.change('interrupted');
    muted.elements.canvas.pointerdown(); await Promise.resolve();
    assert.equal(a.ctx.state,'running'); assert.equal(a.nodes[0].gain.value,0, 'Touch/pointer recovery retains mute');
    muted.elements.soundbtn.click();
    assert.equal(a.nodes[0].gain.value,.6);
    muted.elements.volume.value=0; muted.elements.volume.input();
    muted.elements.soundbtn.click();
    assert.equal(muted.elements.volume.value,60, 'Unmute from zero restores the last positive volume');
    muted.elements.soundbtn.click();
    const restarted=device(muted);
    assert.equal(restarted.nodes[0].gain.value,0, 'A fresh running SDL device keeps an explicit mute');
    assert.equal(muted.elements.soundbtn.textContent,'Unmute');
    muted.elements.soundbtn.click();
    muted.elements.volume.value=0; muted.elements.volume.input();
    const reloaded=launcher(settings); reloaded.start(); const restored=device(reloaded);
    reloaded.elements.soundbtn.click();
    assert.equal(restored.nodes[0].gain.value,.6, 'The last positive volume survives a page reload at zero');
    muted.elements.soundbtn.click();
    muted.events.pagehide(); assert.equal(restarted.nodes[0].gain.value,0, 'Pagehide silences even before document.hidden changes');
    muted.context.Module.browserInputFrame(1); assert.equal(restarted.nodes[0].gain.value,0);
    muted.events.pageshow(); assert.equal(restarted.nodes[0].gain.value,.6);
    muted.elements.volume.focus(); muted.events.focusout();
    assert.equal(restarted.nodes[0].gain.value,0, 'Leaving the canvas cancels pending gain ramps immediately');
    muted.elements.canvas.focus(); muted.events.focusin();
    assert.equal(restarted.nodes[0].gain.value,.6, 'Returning focus restores audio without waiting for a game frame');
    assert.equal(muted.elements.soundstatus.title,'Audio output: 48000 Hz. Sound pauses when the game loses focus.');
    const malformed=launcher({sound:{volume:'loud',muted:'false',lastVolume:-1}});
    assert.equal(malformed.context.soundVolume,.8); assert.equal(malformed.context.soundMuted,false);

    const broken=launcher({sound:{volume:.6}}); broken.start(); const fault={connectFailure:true}; const b=device(broken,fault);
    assert.equal(b.processor.connections.length,0, 'A partial graph failure cannot bypass master controls');
    assert.equal(b.nodes[0].disconnected,true, 'Partial gain nodes are released');
    for(let i=0;i<120;i++) broken.context.Module.browserInputFrame(1);
    assert.equal(b.nodes.length,1, 'Graph failures do not retry and allocate on every frame');
    assert.equal(broken.elements.soundbtn.textContent,'Retry sound');
    fault.connectFailure=false; broken.elements.soundbtn.click();
    assert.equal(b.processor.connections.length,1); assert.equal(b.nodes[1].gain.value,.6);
    assert.equal(broken.context.soundMuted,false, 'Retry sound does not toggle mute');
    const quietBroken=launcher({sound:{volume:.6,muted:true}}); quietBroken.start();
    const quietFault={connectFailure:true}; const quietDevice=device(quietBroken,quietFault);
    quietFault.connectFailure=false; quietBroken.elements.soundbtn.click();
    assert.equal(quietBroken.context.soundMuted,true, 'Retrying graph setup retains an explicit mute');
    assert.equal(quietDevice.nodes.at(-1).gain.value,0);
    fault.rampFailure=true; broken.elements.volume.value=40; broken.elements.volume.input();
    assert.equal(b.processor.connections.length,0, 'Automation failure disconnects output without crashing the engine');
    fault.rampFailure=false; broken.elements.soundbtn.click();
    assert.equal(b.nodes.at(-1).gain.value,.4);
    const lastGain=b.nodes.at(-1); lastGain.gain.cancelScheduledValues=()=>{throw Error('Closed context');};
    broken.context.showFatal('Stopped');
    assert.equal(lastGain.gain.value,0, 'Fatal shutdown still silences when automation cancellation fails');
    assert.equal(lastGain.disconnected,true);

    let rejectOld;
    const delayed=launcher(); delayed.start();
    const old=device(delayed,{state:'suspended',pending:new Promise((resolve,reject)=>{rejectOld=reject;})});
    delayed.elements.canvas.pointerdown(); delayed.elements.canvas.touchstart(); delayed.elements.soundbtn.click();
    assert.equal(old.ctx.resumes,1, 'Repeated gestures share one outstanding resume request');
    const next=device(delayed);
    rejectOld(Error('Late denial')); await Promise.resolve();
    assert.equal(old.listeners.size,0, 'Restart removes the old context state listener');
    assert.equal(delayed.elements.soundstatus.textContent,'Sound on', 'An old resume rejection cannot poison a restarted device');
    next.ctx.change('interrupted');
    assert.equal(delayed.elements.soundbtn.textContent,'Enable sound');
    assert.equal(next.nodes[0].gain.value,0);
    delayed.elements.canvas.touchstart(); await Promise.resolve();
    assert.equal(next.ctx.state,'running'); assert.equal(delayed.elements.soundstatus.textContent,'Sound on');
    const stalled=launcher(); stalled.start();
    const waiting=device(stalled,{state:'suspended',pending:new Promise(()=>{})});
    stalled.elements.canvas.pointerdown();
    for(const timer of Array.from(stalled.timers.values())) timer();
    assert.match(stalled.elements.soundstatus.textContent,/retry/, 'An unresolved autoplay resume times out with a recoverable status');
    stalled.elements.canvas.touchstart();
    assert.equal(waiting.ctx.resumes,2, 'A later trusted gesture can retry after a stalled resume');
    device(stalled);
    assert.equal(stalled.timers.size,0, 'Restart cancels pending resume deadlines');
    const closing=launcher({sound:{volume:.45,muted:true}}); closing.start();
    let rejectClosing;
    const closedDevice=device(closing,{state:'suspended',pending:new Promise((resolve,reject)=>{rejectClosing=reject;})});
    closing.elements.canvas.pointerdown();
    closedDevice.processor.onaudioprocess=()=>{throw Error('Freed native device');};
    closing.context.Module.SDL2.audio.silenceTimer=23;
    let clearedTimer;
    closing.context.clearInterval=id=>{clearedTimer=id;};
    const nativeClose=fs.readFileSync(`${__dirname}/../../src/sdl/sdl_snd.c`,'utf8')
        .match(/EM_JS\(void, SNDDMA_WebAudioClosing, \(\), \{([\s\S]*?)\n\}\);/)[1];
    vm.runInContext(nativeClose,closing.context);
    assert.equal(closedDevice.processor.onaudioprocess,null,'Native shutdown detaches queued callbacks before freeing their device');
    assert.equal(clearedTimer,23,'Native shutdown cancels suspended-device timer callbacks');
    assert.equal(closedDevice.processor.connections.length,0);
    assert.equal(closedDevice.nodes[0].disconnected,true);
    assert.equal(closedDevice.listeners.size,0);
    assert.equal(closing.timers.size,0,'Native device shutdown cancels the shell resume deadline');
    const replacement=device(closing);
    rejectClosing(Error('Late closed-device denial')); await Promise.resolve();
    assert.equal(replacement.nodes[0].gain.value,0,'Explicit mute survives native device shutdown/replacement');
    assert.equal(closing.elements.soundstatus.textContent,'Muted','Old resume rejection cannot affect a replacement after native shutdown');
    console.log('Audio fault checks passed (mute persistence, interruption, graph/automation failure, gesture deduplication, stale restart promises and shutdown).');
})().catch(error => { console.error(error); process.exitCode=1; });
