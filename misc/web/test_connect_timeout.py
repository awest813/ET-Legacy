"""Compile the actual connectResponse/timeout code with a deterministic clock."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/client/cl_main.c').read_text(encoding='utf-8')
# Mask strings/comments while finding braces, preserving source offsets.
masked = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                lambda match: ' ' * len(match[0]), source)


def block(marker):
    start = source.index(marker)
    opening = masked.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[start:end]


CONNECT = block('if (!Q_stricmp(c, "connectResponse"))')
TIMEOUT = block('void CL_CheckTimeout(void)')
DISCONNECT = block('void CL_Disconnect_f(void)')
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CA_CHALLENGING 4
#define CA_DISCONNECTED 1
#define CA_CONNECTED 5
#define CA_CINEMATIC 9
#define NS_CLIENT 0
#define qtrue 1
#define ERR_DISCONNECT 1
typedef struct { int address; } netadr_t;
typedef struct { int integer; float value; } cvar_t;
static cvar_t paused = {0}, timeout = {0,20};
static cvar_t *cl_paused = &paused, *sv_paused = &paused;
static cvar_t *cl_timeout = &timeout, *cl_freezeDemo = &paused;
static struct { int state, realtime; } cls;
static struct { int lastPacketTime, lastPacketSentTime, netchan;
    netadr_t serverAddress; struct { int playing; } demo; } clc;
static struct { int timeoutcount; } cl;
static int disconnected, setups;
static int departures, cinematicStops, errors;
static void NET_WebUserDisconnect(void) { departures++; }
static void SCR_StopCinematic(void) { cinematicStops++; }
static void Com_Error(int code, const char *message) { assert(code == ERR_DISCONNECT); errors++; }
static int Q_stricmp(const char *a, const char *b) { return strcmp(a,b); }
static void Com_Printf(const char *format, ...) {}
static int NET_CompareAdr(const netadr_t *a, const netadr_t *b) { return a->address == b->address; }
static const char *NET_AdrToString(const netadr_t *a) { return "test"; }
static void Com_CheckUpdateStarted(void) {}
static float Cvar_VariableValue(const char *name) { return 1; }
static void Netchan_Setup(int type, int *channel, const netadr_t *from, float port) { setups++; }
static void Cvar_Set(const char *name, const char *value) {}
static void CL_Disconnect(int showMenu) { disconnected++; }
static void accept(const netadr_t *from) {
    const char *c = "connectResponse";
/* CONNECT */
}
/* TIMEOUT */
/* DISCONNECT */
int main(void) {
    netadr_t server = {1}, outsider = {2};
    cls.state = CA_CHALLENGING; cls.realtime = 100000;
    clc.serverAddress = server; clc.lastPacketTime = 0; cl.timeoutcount = 5;
    accept(&outsider);
    assert(cls.state == CA_CHALLENGING && setups == 0 && clc.lastPacketTime == 0);
    accept(&server);
    assert(cls.state == CA_CONNECTED && setups == 1);
    for (int i=0; i<8; i++) CL_CheckTimeout();
    assert(!disconnected); // a long-running engine still gives a fresh connection its timeout
    assert(clc.lastPacketTime == 100000 && cl.timeoutcount == 0);
    cls.realtime = 119999;
    for (int i=0; i<8; i++) CL_CheckTimeout();
    assert(!disconnected);
    accept(&server); // a duplicate acknowledgement must not extend the timeout
    assert(clc.lastPacketTime == 100000 && setups == 1);
    cls.realtime = 120001;
    for (int i=0; i<6; i++) CL_CheckTimeout();
    assert(disconnected == 1);
    cls.state = 8; CL_Disconnect_f();
    assert(departures == 1 && errors == 1 && cinematicStops == 1);
    cls.state = CA_DISCONNECTED; CL_Disconnect_f();
    cls.state = CA_CINEMATIC; CL_Disconnect_f();
    assert(departures == 1 && errors == 1 && cinematicStops == 3);
    puts("Native connect timeout: validated acceptance resets clock/count; foreign and duplicate replies cannot extend it; stalled connections still time out.");
    return 0;
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    build = ROOT / 'build_wasm'
    build.mkdir(exist_ok=True)
    fixture = build / 'test_connect_timeout.c'
    output = build / 'test_connect_timeout.cjs'
    fixture.write_text(HARNESS.replace('/* CONNECT */', CONNECT).replace('/* TIMEOUT */', TIMEOUT).replace('/* DISCONNECT */', DISCONNECT), encoding='utf-8')
    subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node',
                    '-sWASM_ASYNC_COMPILATION=0', '-o', str(output)], check=True)
    subprocess.run([args.node, str(output)], check=True)
