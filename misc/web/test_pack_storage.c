/* Verify the real Emscripten filesystem path used for immutable pack downloads.
 * emcc misc/web/test_pack_storage.c -O2 -sFORCE_FILESYSTEM=1 -sENVIRONMENT=node
 *   -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_pack_storage.cjs
 */
#include <emscripten.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
 assert(EM_ASM_INT({
  var bytes=new Uint8Array(1024*1024);bytes[0]=80;bytes[bytes.length-1]=91;
  FS.writeFile('/download.pk3',bytes,{canOwn:true});
  var node=FS.lookupPath('/download.pk3').node;
  if(node.contents.buffer!==bytes.buffer || FS.stat('/download.pk3').size!==bytes.length)return 0;
  var stream=FS.open('/download.pk3','r');var tail=new Uint8Array(1);
  FS.read(stream,tail,0,1,bytes.length-1);FS.close(stream);if(tail[0]!==91)return 0;
  // Replacing a stale larger pack must not leave its old tail or allocation.
  var replacement=new Uint8Array([80,75,3,4]);FS.writeFile('/download.pk3',replacement,{canOwn:true});
  if(node.contents.buffer!==replacement.buffer || FS.stat('/download.pk3').size!==4)return 0;
  FS.unlink('/download.pk3');return 1;
 }));
 puts("Pack storage: actual MEMFS retains the download buffer without copying, reads exact bytes, and truncates stale replacements.");
 return 0;
}
