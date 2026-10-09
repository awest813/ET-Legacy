"""Exercise production event dispatch and console character filtering."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
common = (ROOT / 'src/qcommon/common.c').read_text(encoding='utf-8')
start = common.index('\t\tcase SE_CHAR:', common.index('int64_t Com_EventLoop('))
dispatch = common[start:common.index('\t\tcase SE_MOUSE:', start)]
keys = (ROOT / 'src/client/cl_keys.c').read_text(encoding='utf-8')
start = keys.index('void CL_CharEvent(int key)')
handler = keys[start:keys.index('/**', start)]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define qfalse 0
#define SE_CHAR 1
#define KEYCATCH_CONSOLE 1
#define KEYCATCH_UI 2
#define KEYCATCH_CGAME 4
#define CA_DISCONNECTED 3
#define UI_KEY_EVENT 0
#define CG_KEY_EVENT 0
#define K_CHAR_FLAG 1024
#define qtrue 1
static int consoleButtonWasPressed, characters[100], count, g_consoleField, uivm, cgvm;
static struct { int keyCatchers,state; } cls={KEYCATCH_CONSOLE,1};
static void Field_CharEvent(int *field,int key) { assert(count<100);characters[count++]=key; }
static void VM_Call(int vm,int call,int key,int down) { assert(0); }
/* HANDLER */
static void text_event(int value) {
 struct { int evType,evValue; } ev={SE_CHAR,value};
 switch(ev.evType) {
 /* DISPATCH */
 }
}
int main(void) {
 const char *command="/bot rollcall";
 consoleButtonWasPressed=1;
 for(const char *p=command;*p;p++) text_event(*p);
#ifdef __EMSCRIPTEN__
 assert(count==strlen(command));
 for(int i=0;i<count;i++) assert(characters[i]==command[i]);
 count=0;consoleButtonWasPressed=1;text_event(0x00e9);text_event(0x4e2d);
 assert(count==2 && characters[0]==0x00e9 && characters[1]==0x4e2d);
#else
 assert(count==strlen(command)-1); /* Preserve desktop's existing suppression. */
 for(int i=0;i<count;i++) assert(characters[i]==command[i+1]);
#endif
 assert(!consoleButtonWasPressed);
 count=0;consoleButtonWasPressed=1;text_event('`');
 text_event('~');text_event(0xac);assert(count==0);
 text_event('/');text_event('a');assert(count==2 && characters[0]=='/' && characters[1]=='a');
 puts("Console text: browser command prefixes/composition preserved; toggle characters filtered; desktop behavior unchanged.");
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    fixture = ROOT / 'build_wasm/test_console_text.c'
    fixture.write_text(HARNESS.replace('/* HANDLER */', handler).replace('/* DISPATCH */', dispatch), encoding='utf-8')
    for name, flags in (('web', []), ('desktop', ['-U__EMSCRIPTEN__'])):
        output = ROOT / ('build_wasm/test_console_text_' + name + '.cjs')
        subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node',
                        '-sWASM_ASYNC_COMPILATION=0', *flags, '-o', str(output)], check=True)
        subprocess.run([args.node, str(output)], check=True)
