/* Exercise the actual EM_JS bridge after C-string and JS code generation.
 * emcc misc/web/test_net_bridge.c -O2 -sENVIRONMENT=node
 *      -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_net_bridge.cjs
 */
#include "../../src/qcommon/q_shared.h"
#include "../../src/qcommon/qcommon.h"
#include "../../src/qcommon/net_web.h"
#include <assert.h>
#include <string.h>

static char command[64];
void Cbuf_AddText(const char *text)
{
    strcpy(command, text);
}

EM_JS(void, setupBridgeCheck, (), {
    Module.browserNetwork = {frame:function(state, ping, message) {
        if (message !== 'Game server error') throw new Error('Network bridge corrupted message: ' + message);
        return state === 8 ? 2 : 1;
    }, userDisconnect:function() { Module.departureNotified = true; }};
});

int main(void)
{
    setupBridgeCheck();
    NET_WebClientFrame(1, 0, "Game server error");
    assert(!strcmp(command, "disconnect\n"));
    NET_WebClientFrame(8, 0, "^1Game ^7server error");
    assert(!strcmp(command, "exec browser-connect.cfg\nconnect 192.0.2.1:27960\n"));
    NET_WebUserDisconnect();
    assert(EM_ASM_INT({ return Module.departureNotified === true; }));
    puts("Native network bridge: complete plain/color messages and disconnect/reconnect commands passed.");
    return 0;
}
