"""Compile production pure-reference code in web and desktop configurations."""
import argparse
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/qcommon/files.c').read_text(encoding='utf-8')
start=source.index('static qboolean FS_WebReferenceStaticModule')
end=source.index('/**\n * @brief FS_ClearPakReferences',start)
functions=source[start:end]
lookup_start=source.index('int FS_FileIsInPAK(')
lookup_end=source.index('/**',lookup_start)
lookup=source[lookup_start:lookup_end]
server=(ROOT/'src/server/sv_main.c').read_text(encoding='utf-8')
server_start=server.index('static qboolean SV_HasWebClientModulePaks(')
server_end=server.index('static void SVC_Info(',server_start)
advertisement=server[server_start:server_end]
init=(ROOT/'src/server/sv_init.c').read_text(encoding='utf-8')
id_start=init.index('static int SV_NextServerId(')
id_end=init.index('void SV_SpawnServer(',id_start)
next_id=init[id_start:id_end]
commands=(ROOT/'src/server/sv_ccmds.c').read_text(encoding='utf-8')
restart_start=commands.index('static qboolean SV_RestartLoadingClient(')
restart_end=commands.index('/**',restart_start)
restart_loading=commands[restart_start:restart_end]
server_client=(ROOT/'src/server/sv_client.c').read_text(encoding='utf-8')
verify_start=server_client.index('static void SV_VerifyPaks_f(')
verify_end=server_client.index('/**',verify_start)
verify=server_client[verify_start:verify_end]
move_start=server_client.index('\tif (cl->state == CS_PRIMED)',server_client.index('static void SV_UserMove('))
move_end=server_client.index('#ifdef LEGACY_AUTH',move_start)
move_gate=server_client[move_start:move_end]
message_start=server_client.index('\tif (serverId != sv.serverId',server_client.index('void SV_ExecuteClientMessage('))
message_end=server_client.index('\n\t// read optional clientCommand strings',message_start)
message_gate=server_client[message_start:message_end]
client=(ROOT/'src/client/cl_main.c').read_text(encoding='utf-8')
send_start=client.index('void CL_SendPureChecksums(void)')
send_end=client.index('/**',send_start)
send=client[send_start:send_end]
gamestate=(ROOT/'src/client/cl_parse.c').read_text(encoding='utf-8')
feed_read=gamestate.index('clc.checksumFeed = MSG_ReadLong(msg);')
feed_id=gamestate.index('clc.checksumFeedServerId = cl.serverId;',feed_read)
assert feed_read < feed_id < gamestate.index('FS_ConditionalRestart(clc.checksumFeed);',feed_id)
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#define BIG_INFO_STRING 8192
#define MAX_INFO_VALUE 8192
#define Com_sprintf snprintf
#define FS_CGAME_REF 4
#define FS_UI_REF 2
#define FS_GENERAL_REF 1
#define qtrue 1
#define qfalse 0
#define ERR_FATAL 1
#define Sys_GetDLLName(x) x ".mp.x86_64.so"
typedef int qboolean;
typedef struct entry { const char *name; struct entry *next; } fileInPack_t;
typedef struct pack { const char *pakGamename; int referenced,pure_checksum,hashSize,pure; fileInPack_t **hashTable; } pack_t;
typedef struct search { pack_t *pack; struct search *next; } searchpath_t;
static searchpath_t *fs_searchpaths;
static int fs_checksumFeed=7;
static struct { int serverId; } cl;
static struct { int checksumFeedServerId; } clc;
static char reliable[8192];
static void CL_AddReliableCommand(const char *text) { strcpy(reliable,text); }
#define CS_CONNECTED 2
#define CS_PRIMED 3
#define CS_ACTIVE 4
#define Com_DPrintf(...) ((void)0)
typedef struct { int state, pureAuthentic, gotCP, deltaMessage; char *name; struct { int serverTime; } lastUsercmd; int ettvClient,lastSnapshotTime; char downloadName[8],lastClientCommandString[16]; int messageAcknowledge,gamestateMessageNum; } client_t;
static struct { int checksumFeedServerId,checksumFeed,serverId,restartedServerId; } sv={100,7};
#define clc_EOF 0
#define clc_clientCommand 1
#define CS_ZOMBIE 1
static int MSG_ReadByte(void *msg) { return clc_EOF; }
static int SV_ClientCommand(client_t *cl,void *msg,int stale) { return 1; }
static struct { int integer; } pureCvar={1};
#define Com_Memset memset
#define sv_pure (&pureCvar)
static int dropped, entered, resent;
static void SV_DropClient(client_t *cl,const char *reason) { dropped++; }
static void SV_ClientEnterWorld(client_t *cl,int *cmd) { entered++;cl->state=CS_ACTIVE; }
static void SV_SendClientGameState(client_t *cl) { resent++; }
static void SV_SendClientSnapshot(client_t *cl) { }
static char commandBuffer[8192];
static char *commandArgs[1100];
static int commandCount;
static void Cmd_TokenizeString(const char *text) {
 strcpy(commandBuffer,text);commandCount=0;
 for(char *token=strtok(commandBuffer," ");token;token=strtok(NULL," ")) {
  assert(commandCount<1100);commandArgs[commandCount++]=token;
 }
}
static int Cmd_Argc(void) { return commandCount; }
static const char *Cmd_Argv(int index) { return index<commandCount?commandArgs[index]:""; }
static int Q_atoi(const char *text) { return text?atoi(text):0; }
static const char *serverPakList="101 201";
static const char *FS_LoadedPakPureChecksums(void) { return serverPakList; }
#define rc(x) (x)
#define Com_Printf(...) ((void)0)
static int Q_stricmp(const char *a,const char *b) { return strcmp(a,b); }
static int FS_FilenameCompare(const char *a,const char *b) { return strcmp(a,b); }
static int FS_PakIsPure(pack_t *p) { return p->pure; }
static long FS_HashFileName(const char *name,int size) { return 0; }
static void Q_strcat(char *out,size_t size,const char *text) { assert(strlen(out)+strlen(text)<size);strcat(out,text); }
static char *va(const char *format,...) { static char buf[80];va_list args;va_start(args,format);vsnprintf(buf,sizeof(buf),format,args);va_end(args);return buf; }
static void Com_Error(int code,const char *text) { assert(0); }
/* FUNCTIONS */
/* LOOKUP */
/* ADVERTISEMENT */
/* SEND */
/* NEXT ID */
static void movement_gate(client_t *cl) { int cmds[]={0}; /* MOVE GATE */ }
/* RESTART LOADING */
static void message_gate(client_t *cl,int serverId) { void *msg=NULL;int c; /* MESSAGE GATE */ }
/* VERIFY */
static void verify_report(const char *report,int valid) {
 client_t player={CS_PRIMED,0,0,0,"test"};int before=dropped;
 Cmd_TokenizeString(report);SV_VerifyPaks_f(&player);
 assert(player.gotCP && player.pureAuthentic==valid);
 assert(dropped==before+!valid);
}
int main(void) {
 fileInPack_t ui={"ui.mp.wasm32.so",NULL},cg={"cgame.mp.wasm32.so",&ui};fileInPack_t *table[]={&cg},*empty[]={NULL};
 pack_t rejected={"legacy",0,999,1,0,table},wrongGame={"etmain",0,888,1,1,table},absent={"legacy",0,777,1,1,empty},valid={"legacy",1,101,1,1,table},map={"etmain",1,201,1,1,empty};
 searchpath_t paths[5]={{&rejected,&paths[1]},{&wrongGame,&paths[2]},{&absent,&paths[3]},{&valid,&paths[4]},{&map,NULL}};
 fs_searchpaths=paths;
 assert(FS_WebClientModulePaks(NULL,NULL));assert(valid.referenced==1);
 cg.next=NULL;assert(!FS_WebClientModulePaks(NULL,NULL));cg.next=&ui;
 fs_searchpaths=&paths[2];paths[2].next=&paths[4];assert(!FS_WebClientModulePaks(NULL,NULL));paths[2].next=&paths[3];fs_searchpaths=paths;
 const char *checksums=FS_ReferencedPakPureChecksums();
#ifdef __EMSCRIPTEN__
 assert(valid.referenced==7);
 assert(!strcmp(checksums,"101 101 @ 101 201 169"));
#else
 assert(valid.referenced==1);
 assert(!strcmp(checksums,"@ 101 201 169"));
#endif
 assert(!rejected.referenced && !wrongGame.referenced && !absent.referenced);
 assert(!SV_HasWebClientModulePaks()); /* Native DLLs absent. */
 fileInPack_t nativeUI={"ui.mp.x86_64.so",NULL},nativeCg={"cgame.mp.x86_64.so",&nativeUI};
 ui.next=&nativeCg;fs_searchpaths=&paths[3];assert(SV_HasWebClientModulePaks());
 fileInPack_t *nativeTable[]={&nativeCg};pack_t separate={"legacy",0,333,1,1,nativeTable};searchpath_t split={&separate,&paths[3]};
 fs_searchpaths=&split;assert(!SV_HasWebClientModulePaks()); /* A companion wasm PK3 cannot pass native pure checks. */
 fs_searchpaths=&paths[1];assert(!SV_HasWebClientModulePaks()); /* A higher-priority etmain DLL package cannot impersonate Legacy wasm modules. */
 fs_searchpaths=&paths[3];ui.next=NULL;assert(!SV_HasWebClientModulePaks());
 ui.next=&nativeCg;
 verify_report("cp 100 101 101 @ 101 201 169",1);
 verify_report("cp 100 101 101 @ 201 207",1); /* Last allowlist entry. */
 verify_report("cp 100 101 101 @ 999 993",0); /* Valid encoding, unauthorized pack. */
 verify_report("cp 100 101 101 @ 101 999 903",0);
 verify_report("cp 100 101 101 @ 101 101 5",0); /* Duplicate references. */
 verify_report("cp 100 101 101 @ 101 201 170",0); /* Bad encoded checksum. */
 verify_report("cp 100 999 101 @ 101 201 169",0); /* Wrong module pack. */
 serverPakList="";verify_report("cp 100 101 101 @ 101 99",0);serverPakList="101 201";
 client_t pending={CS_PRIMED,0,0,0,"pending"};int before=dropped;
 Cmd_TokenizeString("cp 99 101 101 @ 999 993");SV_VerifyPaks_f(&pending);
 assert(!pending.gotCP && !pending.pureAuthentic && dropped==before);
 dropped=0;
 ui.next=NULL;
 cl.serverId=200;clc.checksumFeedServerId=100;
 CL_SendPureChecksums();assert(strncmp(reliable,"cp 100 ",7)==0);
 cl.serverId=300;CL_SendPureChecksums();assert(strncmp(reliable,"cp 100 ",7)==0);
 clc.checksumFeedServerId=300;CL_SendPureChecksums();assert(strncmp(reliable,"cp 300 ",7)==0);
 assert(SV_NextServerId(100,100)==101);
 assert(SV_NextServerId(101,100)==102);
 assert(SV_NextServerId(102,200)==200);
 client_t loading={CS_CONNECTED,0,0,0,"loading"};movement_gate(&loading);assert(!dropped && !entered && !resent && loading.deltaMessage==-1);
 loading.state=CS_PRIMED;movement_gate(&loading);assert(!dropped && !entered && resent==1);
 loading.gotCP=1;loading.pureAuthentic=1;movement_gate(&loading);assert(!dropped && entered==1 && loading.state==CS_ACTIVE);
 loading.pureAuthentic=0;movement_gate(&loading);assert(dropped==1);
 pureCvar.integer=0;movement_gate(&loading);assert(dropped==1);
 loading.state=CS_CONNECTED;loading.lastUsercmd.serverTime=9000;
 assert(SV_RestartLoadingClient(&loading,0));assert(loading.state==CS_CONNECTED && loading.lastUsercmd.serverTime==0 && resent==2);
 loading.state=CS_PRIMED;assert(SV_RestartLoadingClient(&loading,0));assert(loading.state==CS_PRIMED && resent==3);
 loading.state=CS_ACTIVE;assert(!SV_RestartLoadingClient(&loading,0));assert(resent==3);
 loading.state=CS_CONNECTED;assert(!SV_RestartLoadingClient(&loading,1));assert(resent==3);
 sv.serverId=300;sv.restartedServerId=200;
 loading.state=CS_PRIMED;loading.gamestateMessageNum=10;loading.messageAcknowledge=11;
 message_gate(&loading,200);assert(resent==4 && loading.state==CS_PRIMED);
 message_gate(&loading,299);assert(resent==5); /* Multiple fast restarts while loading. */
 loading.state=CS_CONNECTED;message_gate(&loading,250);assert(resent==6);
 loading.state=CS_PRIMED;loading.messageAcknowledge=10;message_gate(&loading,250);assert(resent==6); /* Await an acknowledgement before retransmitting. */
 loading.messageAcknowledge=11;message_gate(&loading,300);assert(resent==6); /* Current gamestate. */
 loading.state=CS_ACTIVE;message_gate(&loading,250);assert(resent==6); /* Active clients await restart snapshots. */
 loading.state=CS_PRIMED;strcpy(loading.downloadName,"map");message_gate(&loading,250);assert(resent==6);loading.downloadName[0]=0;
 strcpy(loading.lastClientCommandString,"nextdl");message_gate(&loading,250);assert(resent==6);loading.lastClientCommandString[0]=0;
 message_gate(&loading,199);assert(resent==7); /* Previous full-map load still recovers. */
 puts("Pure restart recovery: loading acknowledgements retransmit gamestate; current, active and downloading clients preserved.");
 puts("Pure assets: exact allowed Legacy module entries referenced; missing/disallowed packs rejected; desktop unchanged.");
}
'''
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--compiler',default='emcc');parser.add_argument('--node',default='node');args=parser.parse_args()
    fixture=ROOT/'build_wasm/test_pure_assets.c';fixture.write_text(HARNESS.replace('/* FUNCTIONS */',functions)
        .replace('/* LOOKUP */',lookup).replace('/* ADVERTISEMENT */',advertisement).replace('/* SEND */',send).replace('/* NEXT ID */',next_id).replace('/* MOVE GATE */',move_gate).replace('/* RESTART LOADING */',restart_loading).replace('/* MESSAGE GATE */',message_gate).replace('/* VERIFY */',verify),encoding='utf-8')
    for name,flags in [('web',[]),('desktop',['-U__EMSCRIPTEN__'])]:
        output=ROOT/('build_wasm/test_pure_assets_'+name+'.cjs')
        subprocess.run([args.compiler,str(fixture),'-O2','-sENVIRONMENT=node','-sWASM_ASYNC_COMPILATION=0',*flags,'-o',str(output)],check=True)
        subprocess.run([args.node,str(output)],check=True)
