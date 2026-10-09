// Binary datagram transport for the configured/selected ET relay. No UDP destination
// comes from browser packets, server redirects, or a console command.
(function() {
    function createNetwork(options) {
        var socket = null, epoch = 0, timer = null, queue = [], queuedBytes = 0;
        var config = null, ready = false, opening = false, engineStarted = false, action = 0;
        var phase = -1, deadline = 0, seenConnection = false, failed = false;
        var awaitingConnect = false;
        var connectionDeadline = 0, userLeaving = false;
        var closing = false, closingReason = '', closeCallbacks = [];
        var received = 0, sent = 0, text = '', label = 'Offline', pending = null;
        var lastReport = 0, reportedReceived = -1, reportedSent = -1;
        var lastFrame = 0;
        function report(nextLabel, nextText) {
            label = nextLabel; text = nextText;
            lastReport = options.now(); reportedReceived = received; reportedSent = sent;
            if (options.onChange) options.onChange(label, text, {received:received, sent:sent, ready:ready, opening:opening, failed:failed,
                serverLabel:config ? config.serverLabel : '', closing:closing});
        }
        function clear() {
            epoch++;
            if (timer !== null) options.clearTimeout(timer);
            timer = null; queue = []; queuedBytes = 0; ready = false; opening = false; awaitingConnect = false;
            connectionDeadline = 0; userLeaving = false;
            closing = false;
            var old = socket; socket = null;
            if (old) try { old.close(); } catch (e) {}
        }
        function fail(reason) {
            if (closing) { finishClose(); return; }
            if (failed) return;
            failed = true; clear();
            if (engineStarted) action = 1;
            report('Connection failed', reason);
            var callback = pending; pending = null;
            if (callback) callback(new Error(reason));
            if (options.onFailure) options.onFailure(reason, engineStarted);
        }
        function finishClose() {
            var callbacks = closeCallbacks; closeCallbacks = [];
            var reason = closingReason; closingReason = '';
            clear(); engineStarted = false;
            report('Disconnected', reason || 'Disconnected. Retry, or return to offline play.');
            callbacks.forEach(function(callback) { callback(); });
        }
        return {
            configure: function(value, pageURL) {
                if (!value || value.version !== 1 || value.enabled !== true ||
                    value.engineAddress !== '192.0.2.1:27960' ||
                    typeof value.serverLabel !== 'string' || !value.serverLabel.trim() || value.serverLabel.length > 160)
                    throw new Error('Online server configuration is unavailable.');
                var url = new URL(value.relay), page = new URL(pageURL);
                var selected = value.publicServers === true && typeof value.serverId === 'string' && /^[a-f0-9]{32}$/.test(value.serverId);
                var validPath = url.pathname === '/relay' || (selected && /^\/relay\/[A-Za-z0-9_-]{60,220}$/.test(url.pathname));
                if (!['ws:', 'wss:'].includes(url.protocol) || !validPath || url.username || url.password || url.search || url.hash)
                    throw new Error('The relay address is invalid.');
                if (page.protocol === 'https:' && url.protocol !== 'wss:')
                    throw new Error('This secure page requires a secure (wss) relay.');
                config = {relay:url.href, serverLabel:value.serverLabel.trim()};
                report('Ready to join', 'Join ' + config.serverLabel + ' through the configured relay.');
            },
            connect: function(callback) {
                if (closing) { callback(new Error('The previous connection is still disconnecting.')); return; }
                if (!config) { callback(new Error('Configure an ET server relay first.')); return; }
                var previous = pending; pending = null; clear();
                if (previous) previous(new Error('Connection attempt replaced.'));
                // A retry replaces transport, but the native client is still running.
                // Disconnect it even if the replacement relay cannot open.
                failed = false; opening = true; action = engineStarted ? 1 : 0; seenConnection = false;
                received = sent = 0; phase = -1; pending = callback;
                report('Opening relay', 'Opening the relay for ' + config.serverLabel + '…');
                var attempt = epoch;
                try {
                    var current = socket = new options.WebSocket(config.relay, 'et-udp-v1');
                    current.binaryType = 'arraybuffer';
                    timer = options.setTimeout(function() { if (attempt === epoch) fail('The relay did not respond. Check that it is running, then retry.'); }, 8000);
                    current.onopen = function() {
                        if (attempt !== epoch || current !== socket) { try { current.close(); } catch (e) {} return; }
                        if (current.protocol !== 'et-udp-v1') { fail('The relay uses an incompatible protocol.'); return; }
                        options.clearTimeout(timer); timer = null; ready = true; opening = false;
                        report('Relay ready', 'Relay connected. Preparing the game…');
                        var done = pending; pending = null; if (done) done(null);
                    };
                    current.onmessage = function(event) {
                        if (attempt !== epoch || current !== socket || !ready || closing) return;
                        if (!(event.data instanceof ArrayBuffer) || !event.data.byteLength || event.data.byteLength > 32768) {
                            fail('The relay sent an invalid game packet.'); return;
                        }
                        // Map/media registration yields to browser events while the
                        // native client cannot drain snapshots. Retain a bounded
                        // loading burst without discarding reliable fragments.
                        if (queue.length >= 1024 || queuedBytes + event.data.byteLength > 8388608) {
                            fail(engineStarted && options.now() - lastFrame > 5000 ?
                                'The game stopped updating. Return to this tab and retry the connection.' :
                                'The game could not keep up with incoming updates. Retry the connection.'); return;
                        }
                        queue.push(new Uint8Array(event.data)); queuedBytes += event.data.byteLength; received++;
                    };
                    current.onerror = function() { if (attempt === epoch) fail('Could not reach the relay. Check its address and allowed page origin, then retry.'); };
                    current.onclose = function(event) {
                        if (attempt !== epoch) return;
                        var reason = event && event.reason;
                        var reasons = {
                            'Relay is full':'The relay is full. Try again when a slot is available.',
                            'Packet rate exceeded':'The relay packet rate limit was reached. Retry when the connection is stable.',
                            'UDP server unavailable':'The relay could not reach its ET server. Check the server, then retry.',
                            'UDP unavailable or receive queue full':'The relay lost its ET server or its receive queue filled. Check the server, then retry.',
                            'ET relay protocol required':'The relay uses an incompatible protocol.',
                            'Binary ET packets required':'The relay rejected an invalid game packet.'
                        };
                        fail((Object.prototype.hasOwnProperty.call(reasons, reason) && reasons[reason]) || (event && event.code === 1009 ?
                            'A game packet exceeded the relay limit. Check server compatibility, then retry.' :
                            'The relay disconnected. Check the game server and relay, then retry.'));
                    };
                } catch (error) { fail('Could not open the relay: ' + error.message); }
            },
            begin: function(reconnect) {
                if (!ready) return false;
                engineStarted = true; phase = -1; seenConnection = false;
                lastFrame = options.now();
                deadline = options.now() + 15000;
                connectionDeadline = options.now() + 90000;
                if (reconnect) { action = 2; awaitingConnect = true; }
                report('Preparing connection', 'Starting the connection to ' + config.serverLabel + '…');
                return true;
            },
            userDisconnect: function() {
                // Native disconnect sends its final packets before the next frame.
                // Ignore automatic cleanup while a replacement is opening/starting.
                if (ready && engineStarted && !awaitingConnect) userLeaving = true;
            },
            allowed: function(server) {
                return server === 'localhost' || (server === '192.0.2.1:27960' && ready);
            },
            blocked: function() {
                report('Relay required', 'Use Online in the launcher to select a server. Direct UDP addresses and server redirects are unavailable in this browser build.');
            },
            send: function(bytes) {
                if (!ready || awaitingConnect || !socket || socket.readyState !== 1 || !bytes.length || bytes.length > 32768) return false;
                if (socket.bufferedAmount + bytes.length > 262144) {
                    fail('The relay send queue filled. Retry when the connection is stable.'); return false;
                }
                try { socket.send(bytes); sent++; return true; }
                catch (error) { fail('Sending to the relay failed. Retry the connection.'); return false; }
            },
            receive: function(limit) {
                if (!ready || awaitingConnect || !queue.length) return null;
                var packet = queue.shift(); queuedBytes -= packet.length;
                if (packet.length > limit) { fail('A game packet exceeded the engine receive buffer.'); return null; }
                return packet;
            },
            frame: function(state, ping, message) {
                lastFrame = options.now();
                var command = action; action = 0;
                if (closing) {
                    if (state === 1 || state === 0) finishClose();
                    return command;
                }
                if (!engineStarted || failed || !ready) return command;
                if (userLeaving && state === 1) {
                    clear(); engineStarted = false;
                    report('Disconnected', 'Disconnected by you. Retry, or return to offline play.');
                    return 0;
                }
                // The reconnect command runs after this frame; its state still
                // belongs to the old match and must not be reported as Online.
                if (command === 2) return command;
                if (awaitingConnect) {
                    if (state === 3 || state === 4) awaitingConnect = false;
                    else if (options.now() <= deadline) return command;
                    else {
                        fail('The game did not start its connection. Retry, or return to offline play.');
                        command = action; action = 0; return command;
                    }
                }
                if (state < 3 && !seenConnection && options.now() > deadline) {
                    fail('The game did not start its connection. Retry, or return to offline play.');
                    command = action; action = 0; return command;
                }
                if (state >= 3 && state <= 8) seenConnection = true;
                // Active play ends this attempt. A later server map change gets
                // its own loading budget rather than inheriting an expired one.
                if (state === 8) connectionDeadline = 0;
                else if (state >= 3 && state <= 7) {
                    if (!connectionDeadline) connectionDeadline = options.now() + 90000;
                    if (options.now() >= connectionDeadline) {
                        fail('The connection took too long. Check the ET server and relay, then retry or return to offline play.');
                        command = action; action = 0; return command;
                    }
                }
                if (phase !== state) {
                    phase = state;
                    deadline = options.now() + (state >= 5 ? 30000 : 15000);
                }
                var labels = {3:'Contacting server',4:'Joining server',5:'Receiving game state',6:'Loading map',7:'Waiting for match',8:'Online'};
                if (state >= 3 && state <= 7 && options.now() > deadline) {
                    fail('The ET server did not finish connecting (' + labels[state].toLowerCase() + '). Check that it supports this Legacy build and the installed game packs.');
                    command = action; action = 0; return command;
                }
                if (state === 1 && seenConnection) {
                    fail(message || 'Disconnected from the ET server. Retry, or return to offline play.');
                    command = action; action = 0; return command;
                }
                var nextLabel = labels[state] || 'Preparing connection';
                var nextText = state === 8 ? config.serverLabel + ' · ' + Math.max(0, ping) + ' ms' :
                    (message || nextLabel + ' · ' + config.serverLabel);
                if (nextLabel !== label || (options.now() - lastReport >= 250 &&
                    (nextText !== text || reportedReceived !== received || reportedSent !== sent)))
                    report(nextLabel, nextText);
                return command;
            },
            close: function(reason, done) {
                if (done) closeCallbacks.push(done);
                if (closing) return;
                var callback = pending; pending = null; failed = false;
                closingReason = reason || '';
                if (engineStarted && ready) {
                    // Keep the original UDP peer alive until native disconnect
                    // sends its final packets. Otherwise immediate retry can
                    // collide with the old server session's persistent GUID.
                    closing = true; action = 1;
                    report('Disconnecting', 'Leaving the game server…');
                    timer = options.setTimeout(finishClose, 2000);
                } else {
                    if (engineStarted) action = 1;
                    finishClose();
                }
                if (callback) callback(new Error('Connection cancelled.'));
            }
        };
    }
    if (typeof module === 'object' && module.exports) module.exports = createNetwork;
    else Module['browserNetwork'] = createNetwork({
        WebSocket:WebSocket,
        setTimeout:function(fn, ms) { return setTimeout(fn, ms); },
        clearTimeout:function(id) { clearTimeout(id); }, now:function() { return performance.now(); },
        onChange:function(label, text, stats) { if (Module['browserNetworkStatus']) Module['browserNetworkStatus'](label, text, stats); },
        onFailure:function(reason, started) { if (Module['browserNetworkFailure']) Module['browserNetworkFailure'](reason, started); }
    });
})();
