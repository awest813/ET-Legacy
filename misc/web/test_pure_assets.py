"""Compile production pure-reference code in web and desktop configurations."""
import argparse
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/qcommon/files.c').read_text(encoding='utf-8')
start=source.index('#ifdef __EMSCRIPTEN__\nstatic void FS_WebReferenceStaticModule')
end=source.index('/**\n * @brief FS_ClearPakReferences',start)
functions=source[start:end]
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define BIG_INFO_STRING 8192
#define FS_CGAME_REF 4
#define FS_UI_REF 2
#define FS_GENERAL_REF 1
typedef struct entry { const char *name; struct entry *next; } fileInPack_t;
typedef struct pack { const char *pakGamename; int referenced,pure_checksum,hashSize,pure; fileInPack_t **hashTable; } pack_t;
typedef struct search { pack_t *pack; struct search *next; } searchpath_t;
static searchpath_t *fs_searchpaths;
static int fs_checksumFeed=7;
static int Q_stricmp(const char *a,const char *b) { return strcmp(a,b); }
static int FS_FilenameCompare(const char *a,const char *b) { return strcmp(a,b); }
static int FS_PakIsPure(pack_t *p) { return p->pure; }
static long FS_HashFileName(const char *name,int size) { return 0; }
static void Q_strcat(char *out,size_t size,const char *text) { assert(strlen(out)+strlen(text)<size);strcat(out,text); }
static char *va(const char *format,...) { static char buf[80];va_list args;va_start(args,format);vsnprintf(buf,sizeof(buf),format,args);va_end(args);return buf; }
/* FUNCTIONS */
int main(void) {
 fileInPack_t ui={"ui.mp.wasm32.so",NULL},cg={"cgame.mp.wasm32.so",&ui};fileInPack_t *table[]={&cg},*empty[]={NULL};
 pack_t rejected={"legacy",0,999,1,0,table},wrongGame={"etmain",0,888,1,1,table},absent={"legacy",0,777,1,1,empty},valid={"legacy",1,101,1,1,table},map={"etmain",1,201,1,1,empty};
 searchpath_t paths[5]={{&rejected,&paths[1]},{&wrongGame,&paths[2]},{&absent,&paths[3]},{&valid,&paths[4]},{&map,NULL}};
 fs_searchpaths=paths;
 const char *checksums=FS_ReferencedPakPureChecksums();
#ifdef __EMSCRIPTEN__
 assert(valid.referenced==7);
 assert(!strcmp(checksums,"101 101 @ 101 201 169"));
#else
 assert(valid.referenced==1);
 assert(!strcmp(checksums,"@ 101 201 169"));
#endif
 assert(!rejected.referenced && !wrongGame.referenced && !absent.referenced);
 puts("Pure assets: exact allowed Legacy module entries referenced; missing/disallowed packs rejected; desktop unchanged.");
}
'''
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--compiler',default='emcc');parser.add_argument('--node',default='node');args=parser.parse_args()
    fixture=ROOT/'build_wasm/test_pure_assets.c';fixture.write_text(HARNESS.replace('/* FUNCTIONS */',functions),encoding='utf-8')
    for name,flags in [('web',[]),('desktop',['-U__EMSCRIPTEN__'])]:
        output=ROOT/('build_wasm/test_pure_assets_'+name+'.cjs')
        subprocess.run([args.compiler,str(fixture),'-O2','-sENVIRONMENT=node','-sWASM_ASYNC_COMPILATION=0',*flags,'-o',str(output)],check=True)
        subprocess.run([args.node,str(output)],check=True)
