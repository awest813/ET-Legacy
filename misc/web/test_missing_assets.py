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
parse_source = (ROOT / 'src/client/cl_parse.c').read_text(encoding='utf-8')
parse_start = parse_source.index('void CL_ParseDownload(msg_t *msg)')
parse_end = parse_source.index('\n/**', parse_start)
parse_function = parse_source[parse_start:parse_end]
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#define qfalse 0
#define qtrue 1
#define ERR_DROP 1
#define CA_CONNECTED 5
#define MAX_OSPATH 1024
#define MAX_STRING_CHARS 1024
#define DLTYPE_WWW -1
typedef int qboolean;
typedef struct { int block, reads; const char *location; } msg_t;
static struct { int bWWWDl, bWWWDlAborting, bWWWDlDisconnected; char redirectedList[80], downloadList[80], downloadName[1024]; } dld;
static struct { int state; } cls;
static struct { int integer; } allow;
#define cl_allowDownload (&allow)
static int missing, errors, completed, next;
static char reason[2048], missingCvar[1024];
static int assetNotifications;
static int cl_connectedToPureServer, webModules=1;
static int FS_WebClientModulePaks(int *cg,int *ui) { return webModules; }
static int64_t now;
static int requests;
static char command[1024], printed[4096];
static const char *remote = "etmain/test.pk3";
static int64_t Sys_Milliseconds(void) { return now; }
static void Q_strncpyz(char *to,const char *from,size_t size) { snprintf(to,size,"%s",from); }
static int Q_stricmp(const char *a,const char *b) { return strcasecmp(a,b); }
static void CL_AddReliableCommand(const char *text) { snprintf(command,sizeof(command),"%s",text); if (strstr(text,"download ")==text) requests++; }
static const char *va(const char *format,...) { static char buffer[1024];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);return buffer; }
static void Com_Printf(const char *format,...) { va_list args;va_start(args,format);vsnprintf(printed,sizeof(printed),format,args);va_end(args); }
static int MSG_ReadShort(msg_t *msg) { msg->reads++;return msg->block; }
static const char *MSG_ReadString(msg_t *msg) { msg->reads++;return msg->location; }
static int MSG_ReadLong(msg_t *msg) { msg->reads++;return 2147483647; }
static const char *Cvar_VariableString(const char *name) { return strstr(name,"Names") ? "etmain/test" : "-123"; }
static void NET_WebMissingAssets(const char *missing, const char *names, const char *checksums) {
 assert(strstr(missing,"etmain/test.pk3") && !strcmp(names,"etmain/test") && !strcmp(checksums,"-123"));
 assert(assetNotifications==errors); assetNotifications++;
}
#ifdef __EMSCRIPTEN__
static void Com_WebPackSourceReset(void);
#endif
static void Com_ClearStaticDownload(void) {
#ifdef __EMSCRIPTEN__
 Com_WebPackSourceReset();
#endif
}
static int Com_InitUpdateDownloads(void) { return 0; }
static int FS_ComparePaks(char *buffer, size_t size, int download) {
 if (missing) { if (download) snprintf(buffer,size,"@%s@%s",remote,remote); else snprintf(buffer,size,"etmain/test.pk3\n"); }
 return missing;
}
static void Cvar_Set(const char *name,const char *value) { snprintf(missingCvar,sizeof(missingCvar),"%s",value); }
static void Com_Error(int code,const char *format,...) {
 assert(code==ERR_DROP); errors++; va_list args;va_start(args,format);vsnprintf(reason,sizeof(reason),format,args);va_end(args);
}
static void Com_NextDownload(void) { next++; }
static void Com_DownloadsComplete(void) { completed++; }
/* SOURCE HEADER */
/* FUNCTION */
/* PARSER */
int main(void) {
 missing=1;allow.integer=0;Com_InitDownloads();
#ifdef __EMSCRIPTEN__
 assert(!errors && !assetNotifications && !completed && !next && requests==1);
 assert(!strcmp(command,"download etmain/test.pk3"));
 now=2999;Com_WebPackSourceFrame();assert(!errors);
 now=3000;Com_WebPackSourceFrame();assert(errors==1 && assetNotifications==1 && !completed && !next);
 assert(strstr(reason,"etmain/test.pk3") && strstr(reason,"exact PK3"));
 allow.integer=1;Com_InitDownloads();
 msg_t udp={0,0,"unused"};CL_ParseDownload(&udp);
 assert(errors==2 && !completed && !next && udp.reads==1);
 assert(!strcmp(command,"stopdl")); /* No native file writes or nextdl acknowledgement. */
 Com_InitDownloads();msg_t www={DLTYPE_WWW,0,"https://approved.test/etmain/test.pk3"};CL_ParseDownload(&www);
 assert(errors==3 && assetNotifications==3 && www.reads==4);
 assert(strstr(printed,"Server pack source (etmain/test.pk3): https://approved.test/etmain/test.pk3"));
 Com_InitDownloads();cls.state=0;Com_WebPackSourceFrame();
 now+=10000;Com_WebPackSourceFrame();assert(errors==3);
 www.reads=0;CL_ParseDownload(&www);assert(!www.reads && errors==3); /* Unsolicited and canceled packets are ignored. */
 Com_InitDownloads();Com_ClearStaticDownload();now+=10000;Com_WebPackSourceFrame();assert(errors==3);
 const char *invalid[]={"../test.pk3","legacy/../test.pk3","etmain/test;quit.pk3","etmain/test.pk3 extra","etmain/sub/test.pk3","etmain/test.exe"};
 int count=requests;
 for (int i=0;i<6;i++) { remote=invalid[i];Com_InitDownloads();assert(!webPackSourcePending); }
 assert(requests==count && errors==9 && !completed && !next);
 remote="etmain/test.pk3";Com_InitDownloads();
 www.location="https://approved.test/test.pk3\nquit";printed[0]=0;CL_ParseDownload(&www);
 assert(errors==10 && !strstr(printed,"quit"));
#else
 assert(!errors && completed==1);
 allow.integer=1;Com_InitDownloads();assert(next==1 && cls.state==CA_CONNECTED);
#endif
 missing=0;completed=0;Com_InitDownloads();assert(completed==1 && !missingCvar[0]);
#ifdef __EMSCRIPTEN__
 cl_connectedToPureServer=1;webModules=0;completed=0;int before=errors;Com_InitDownloads();
 assert(errors==before+1 && !completed && strstr(reason,"WebAssembly client modules"));
 webModules=1;Com_InitDownloads();assert(completed==1);
#endif
 puts("Required pack gate: bounded metadata-only redirect discovery, timeout/cancel/unsolicited/path checks; no browser download writes; native behavior preserved.");
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
    header = '#include "' + (ROOT / 'src/qcommon/web_pack_source.h').as_posix() + '"'
    fixture.write_text(HARNESS.replace('/* FUNCTION */', function).replace('/* SOURCE HEADER */', header)
                       .replace('/* PARSER */', '#ifdef __EMSCRIPTEN__\n' + parse_function + '\n#endif'), encoding='utf-8')
    for name, flags in (('web', []), ('desktop', ['-U__EMSCRIPTEN__'])):
        output = build / ('test_missing_assets_' + name + '.cjs')
        subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node', '-sWASM_ASYNC_COMPILATION=0', *flags, '-o', str(output)], check=True)
        subprocess.run([args.node, str(output)], check=True)
