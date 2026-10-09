"""Compile the production GUID validation against reconnect/duplicate cases."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/game/g_client.c').read_text(encoding='utf-8')
start = source.index('\t// check guid\n', source.index('char *ClientConnect('))
validation = source[start:source.index('\n\t// IP filtering', start)]
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_GUID_LENGTH 32
#define ETTV_PROTOCOL_VERSION 999
#define Q_strncmp strncmp
typedef struct { struct { char cl_guid[33]; } pers; } gclient_t;
static gclient_t clients[8];
static struct { int numConnectedClients; gclient_t *clients; int sortedClients[8]; } level={0,clients,{0}};
static struct { int integer; } g_guidCheck={1},g_cheats={0};
static const char *validate(int clientNum,int isBot,int protocol,const char *cs_guid) {
 int i;
 /* VALIDATION */
 return NULL;
}
int main(void) {
 const char *guid="0123456789ABCDEF0123456789ABCDEF";
 strcpy(clients[0].pers.cl_guid,guid);level.numConnectedClients=1;
 assert(validate(0,0,84,guid)==NULL); /* Reuse of the transport's own slot. */
 assert(validate(1,0,84,guid)!=NULL); /* A distinct player cannot share it. */
 strcpy(clients[1].pers.cl_guid,guid);level.sortedClients[1]=1;level.numConnectedClients=2;
 assert(validate(0,0,84,guid)!=NULL); /* Own-slot exclusion must not hide another duplicate. */
 level.numConnectedClients=1;level.sortedClients[0]=5;
 strcpy(clients[5].pers.cl_guid,guid);
 assert(validate(5,0,84,guid)==NULL); /* Sparse slot; roster index differs. */
 assert(validate(0,0,84,guid)!=NULL);
 level.numConnectedClients=0;
 assert(validate(0,0,84,"unknown")!=NULL);
 assert(validate(0,0,84,"NO_GUID")!=NULL);
 assert(validate(0,0,84,"")!=NULL);
 assert(validate(0,0,84,"0123456789ABCDEF0123456789ABCDEG")!=NULL);
 assert(validate(0,0,84,guid)==NULL);
 level.numConnectedClients=1;
 assert(validate(0,1,84,guid)==NULL);
 assert(validate(0,0,ETTV_PROTOCOL_VERSION,guid)==NULL);
 g_cheats.integer=1;assert(validate(0,0,84,guid)==NULL);
 g_cheats.integer=0;g_guidCheck.integer=0;assert(validate(0,0,84,guid)==NULL);
 puts("Client identity: own-slot/sparse-slot reconnect accepted; other duplicates and invalid identities rejected; native exemptions preserved.");
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    build = ROOT / 'build_wasm'
    build.mkdir(exist_ok=True)
    fixture = build / 'test_client_identity.c'
    output = build / 'test_client_identity.cjs'
    fixture.write_text(HARNESS.replace('/* VALIDATION */', validation), encoding='utf-8')
    subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node',
                    '-sWASM_ASYNC_COMPILATION=0', '-o', str(output)], check=True)
    subprocess.run([args.node, str(output)], check=True)
