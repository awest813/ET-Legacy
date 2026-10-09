"""Compile production browser absolute-menu routing and HUD-editor mapping."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(path, name):
    source = (ROOT / path).read_text(encoding='utf-8')
    start = source.index('void ' + name + '(')
    end = source.index('\n}\n', start) + 3
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#define RATIO43 (4.f/3.f)
#define RPRATIO43 (3.f/4.f)
#define SCREEN_WIDTH_F 640.f
#define SCREEN_HEIGHT_F 480.f
#define KEYCATCH_UI 1
#define KEYCATCH_CGAME 2
#define HUD_EDITOR_SIZE_COEFF /* EDITOR SCALE */
static struct { int editingHud,fullScreenHudEditor; } cg;
static struct { int cursorX,cursorY; } cgs;
static struct { int cursorx,cursory; } cgDC;
static struct { struct { float windowAspect; int windowWidth,windowHeight; } glconfig; int keyCatchers; } cls;
static int uivm=1,cgvm=1,uiX,uiY,moves;
static void UI_WebMousePosition(int x,int y) { uiX=x;uiY=y; }
static void CG_MouseEvent(int dx,int dy) { cgs.cursorX+=dx;cgs.cursorY+=dy;moves++; }
/* CG POSITION */
/* CL POSITION */
int main(void) {
 cls.glconfig.windowAspect=RATIO43;cls.glconfig.windowWidth=640;cls.glconfig.windowHeight=480;
 cls.keyCatchers=KEYCATCH_CGAME;
 CL_WebMousePosition(500,100);
 assert(cgs.cursorX==500 && cgs.cursorY==100 && cgDC.cursorx==500 && cgDC.cursory==100);
 CL_WebMousePosition(10,20); /* Immediate click at another location must not use the previous cursor. */
 assert(cgs.cursorX==10 && cgs.cursorY==20 && cgDC.cursorx==10 && cgDC.cursory==20);
 cg.editingHud=1;
 CL_WebMousePosition(600,30); /* Editor side panel, beyond the ordinary 640-unit grid. */
 assert(cgs.cursorX==768 && cgs.cursorY==38 && cgDC.cursorx==768 && cgDC.cursory==38);
 CL_WebMousePosition(100,470); /* Bottom component list. */
 assert(cgs.cursorX==128 && cgs.cursorY==601 && cgDC.cursory==601);
 cg.fullScreenHudEditor=1;CL_WebMousePosition(600,30);
 assert(cgs.cursorX==600 && cgs.cursorY==30 && cgDC.cursorx==600);
 cg.fullScreenHudEditor=0;
 for(int i=0;i<3;i++) {
  const int widths[]={960,1280,1920},heights[]={540,720,1080};
  cls.glconfig.windowAspect=16.f/9.f;cls.glconfig.windowWidth=widths[i];cls.glconfig.windowHeight=heights[i];
  CL_WebMousePosition(widths[i]*9/10,heights[i]/2);
  assert(cgs.cursorX>=982 && cgs.cursorX<=983 && cgs.cursorY==307);
  assert(cgDC.cursorx==cgs.cursorX && cgDC.cursory==cgs.cursorY);
 }
 cg.editingHud=0;CL_WebMousePosition(960,540);
 assert(cgs.cursorX==426 && cgs.cursorY==240); /* Limbo remains unscaled at 1920x1080. */
 cls.keyCatchers=KEYCATCH_UI|KEYCATCH_CGAME;cg.editingHud=1;
 CL_WebMousePosition(960,540);assert(uiX==426 && uiY==240); /* UI wins over stale cgame editor state. */
 int before=moves;cls.glconfig.windowWidth=0;CL_WebMousePosition(20,30);assert(moves==before);
 cls.glconfig.windowWidth=1920;cls.keyCatchers=0;CL_WebMousePosition(20,30);assert(moves==before);
 puts("Menu mouse: HUD editor side/bottom controls, immediate clicks, fullscreen editor, aspect/size changes and ordinary UI/Limbo routing passed.");
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    scale = re.search(r'#define HUD_EDITOR_SIZE_COEFF\s+(\S+)',
                      (ROOT / 'etmain/ui/menudef.h').read_text()).group(1)
    source = HARNESS.replace('/* EDITOR SCALE */', scale).replace('/* CG POSITION */',
        function('src/cgame/cg_newDraw.c', 'CG_WebMousePosition')).replace('/* CL POSITION */',
        function('src/client/cl_input.c', 'CL_WebMousePosition'))
    fixture = ROOT / 'build_wasm/test_menu_mouse.c'
    output = ROOT / 'build_wasm/test_menu_mouse.cjs'
    fixture.write_text(source, encoding='utf-8')
    subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node',
                    '-sWASM_ASYNC_COMPILATION=0', '-o', str(output)], check=True)
    subprocess.run([args.node, str(output)], check=True)
