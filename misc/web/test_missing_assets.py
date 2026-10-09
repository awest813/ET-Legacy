"""Run the production pack gate in WebAssembly and desktop configurations."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/qcommon/download.c').read_text(encoding='utf-8')
masked = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), source)
start = source.index('void Com_InitDownloads(void)')
end = masked.index('{', start) + 1
depth = 1
while depth:
    depth += (masked[end] == '{') - (masked[end] == '}')
    end += 1
function = source[start:end]
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define qfalse 0
#define qtrue 1
#define ERR_DROP 1
#define CA_CONNECTED 5
typedef int qboolean;
static struct { int bWWWDl, bWWWDlAborting, bWWWDlDisconnected; char redirectedList[80], downloadList[80]; } dld;
static struct { int state; } cls;
static struct { int integer; } allow;
#define cl_allowDownload (&allow)
static int missing, errors, completed, next;
static char reason[2048], missingCvar[1024];
static int assetNotifications;
static const char *Cvar_VariableString(const char *name) { return strstr(name,"Names") ? "etmain/test" : "-123"; }
static void NET_WebMissingAssets(const char *missing, const char *names, const char *checksums) {
 assert(strstr(missing,"etmain/test.pk3") && !strcmp(names,"etmain/test") && !strcmp(checksums,"-123"));
 assert(assetNotifications==errors); assetNotifications++;
}
static void Com_ClearStaticDownload(void) {}
static int Com_InitUpdateDownloads(void) { return 0; }
static int FS_ComparePaks(char *buffer, size_t size, int download) {
 if (missing) snprintf(buffer,size,"%s",download ? "@etmain/test.pk3@etmain/test.pk3" : "etmain/test.pk3\n");
 return missing;
}
static void Cvar_Set(const char *name,const char *value) { snprintf(missingCvar,sizeof(missingCvar),"%s",value); }
static void Com_Error(int code,const char *format,...) {
 assert(code==ERR_DROP); errors++; va_list args;va_start(args,format);vsnprintf(reason,sizeof(reason),format,args);va_end(args);
}
static void Com_NextDownload(void) { next++; }
static void Com_DownloadsComplete(void) { completed++; }
/* FUNCTION */
int main(void) {
 missing=1;allow.integer=0;Com_InitDownloads();
#ifdef __EMSCRIPTEN__
 assert(errors==1 && assetNotifications==1 && !completed && !next);
 assert(strstr(reason,"etmain/test.pk3") && strstr(reason,"exact PK3"));
 allow.integer=1;Com_InitDownloads();
 assert(errors==2 && !completed && !next); /* Native download flags cannot bypass the web asset gate. */
#else
 assert(!errors && completed==1);
 allow.integer=1;Com_InitDownloads();assert(next==1 && cls.state==CA_CONNECTED);
#endif
 missing=0;completed=0;Com_InitDownloads();assert(completed==1 && !missingCvar[0]);
 puts("Required pack gate: missing browser assets stop before cgame; exact installed packs proceed; native download behavior preserved.");
 return 0;
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    build = ROOT / 'build_wasm'
    fixture = build / 'test_missing_assets.c'
    fixture.write_text(HARNESS.replace('/* FUNCTION */', function), encoding='utf-8')
    for name, flags in (('web', []), ('desktop', ['-U__EMSCRIPTEN__'])):
        output = build / ('test_missing_assets_' + name + '.cjs')
        subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node', '-sWASM_ASYNC_COMPILATION=0', *flags, '-o', str(output)], check=True)
        subprocess.run([args.node, str(output)], check=True)
