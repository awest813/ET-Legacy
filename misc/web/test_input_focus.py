"""Compile production SDL window/button cases and test browser gameplay focus policy."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/sdl/sdl_input.c').read_text(encoding='utf-8')
masked = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                lambda match: ' ' * len(match[0]), source)
start = source.index('switch (e.window.event)')
end = masked.index('{', start) + 1
depth = 1
while depth:
    depth += (masked[end] == '{') - (masked[end] == '}')
    end += 1
window_switch = source[start:end]
button_cases = source[source.index('case SDL_MOUSEBUTTONDOWN:'):source.index('case SDL_MOUSEWHEEL:')]
action_start = source.index('if ((webInputFlags & 1) && mode != 3)')
action_end = masked.index('{', action_start) + 1
depth = 1
while depth:
    depth += (masked[action_end] == '{') - (masked[action_end] == '}')
    action_end += 1
touch_actions = source[action_start:action_end]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int Uint32;
enum { SDL_WINDOWEVENT_RESIZED, SDL_WINDOWEVENT_MINIMIZED,
 SDL_WINDOWEVENT_SHOWN, SDL_WINDOWEVENT_RESTORED, SDL_WINDOWEVENT_MAXIMIZED,
 SDL_WINDOWEVENT_FOCUS_LOST, SDL_WINDOWEVENT_LEAVE, SDL_WINDOWEVENT_ENTER,
 SDL_WINDOWEVENT_FOCUS_GAINED, SDL_WINDOWEVENT_MOVED };
#define SDL_WINDOW_INPUT_FOCUS 1
#define qtrue 1
static int mainScreen, webInputFlags, clears, unfocused, windowFlags;
static struct { int integer; } minimized;
#define com_minimized (&minimized)
static void Key_ClearStates(void) { clears++; }
static void Cvar_SetValue(const char *name, int value) {
 if (!strcmp(name,"com_unfocused")) unfocused = value;
 else minimized.integer = value;
}
static Uint32 SDL_GetWindowFlags(int screen) { return windowFlags; }
static void SDL_RestoreWindow(int screen) {}
static void SDL_RaiseWindow(int screen) {}
static void IN_WindowFocusLost(void) {}
#define IN_WindowResize(event) ((void)0)
#define IN_WindowMoved(event) ((void)0)
static void browser_event(int event) {
 struct { struct { int event; } window; } e = {{event}};
 int skipLost = 0;
/* SWITCH */
}
enum { SDL_MOUSEBUTTONDOWN=1, SDL_MOUSEBUTTONUP,
 SDL_BUTTON_LEFT=1, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_X1, SDL_BUTTON_X2,
 K_MOUSE1=100, K_MOUSE2, K_MOUSE3, K_MOUSE4, K_MOUSE5, K_AUX1=200, SE_KEY=1, K_ESCAPE=27,
 KEYCATCH_UI=1, KEYCATCH_CGAME=2, KEYCATCH_CONSOLE=4 };
#define qfalse 0
static int catcher, queued, queuedKey, queuedDown, positions;
static struct { int integer; } bypass;
#define cl_bypassMouseInput (&bypass)
static int Key_GetCatcher(void) { return catcher; }
static void CL_WebMousePosition(int x,int y) { positions++; }
static void Com_QueueEvent(int time,int type,int key,int down,int size,void *ptr) {
 queued++;queuedKey=key;queuedDown=down;
}
static void browser_button(int type,int button) {
 struct { int type; struct { int button,x,y; } button; } e={type,{button,10,20}};
 int lasttime=0;
 switch(e.type) {
 /* BUTTON CASES */
 }
}
#undef __EMSCRIPTEN__
enum { CA_ACTIVE=8 };
static struct { int state; } cls;
static char action_commands[128];
static void Cbuf_AddText(const char *text) { assert(strlen(action_commands)+strlen(text)<sizeof(action_commands));strcat(action_commands,text); }
static void browser_actions(int actions,int mode) {
 /* TOUCH ACTIONS */
}
static void desktop_event(int event) {
 struct { struct { int event; } window; } e = {{event}};
 int skipLost = 0;
/* SWITCH */
}
int main(void) {
 webInputFlags = 1; clears = unfocused = 0;
 browser_event(SDL_WINDOWEVENT_LEAVE);
 assert(clears == 0 && unfocused == 0); // Hover exit is not keyboard focus loss.
 browser_event(SDL_WINDOWEVENT_FOCUS_LOST);
 assert(clears == 0 && unfocused == 0); // DOM focus remains authoritative.
 webInputFlags = 0;
 browser_event(SDL_WINDOWEVENT_FOCUS_LOST);
 assert(clears == 1 && unfocused == 1);
 clears = unfocused = 0;
 browser_event(SDL_WINDOWEVENT_LEAVE);
 assert(clears == 1 && unfocused == 1);
 webInputFlags = 1; clears = unfocused = 0;
 desktop_event(SDL_WINDOWEVENT_LEAVE);
 assert(clears == 1 && unfocused == 1); // Desktop behavior remains intact.
 webInputFlags=1;catcher=queued=positions=0;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT);
 assert(queued==0); // Initial unlocked clicks remain capture gestures.
 webInputFlags=1|16;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT);
 assert(queued==1 && queuedKey==K_MOUSE1 && queuedDown==1 && positions==0);
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT);
 assert(queued==1); // Right drag is consumed by the shell.
 webInputFlags=16;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT);assert(queued==1);
 browser_button(SDL_MOUSEBUTTONUP,SDL_BUTTON_LEFT);
 assert(queued==2 && queuedDown==0); // Releases remain valid after focus loss.
 webInputFlags=1;catcher=KEYCATCH_UI;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT);
 assert(queued==3 && queuedKey==K_MOUSE2 && positions==1);
 catcher=KEYCATCH_CONSOLE;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT);assert(queued==3);
 catcher=0;webInputFlags=1|2;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT);assert(queued==4);
 webInputFlags=1|8;
 browser_button(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT);assert(queued==5);
 webInputFlags=1;cls.state=CA_ACTIVE;browser_actions(4|8,0);
 assert(!strcmp(action_commands,"weapnext\nweapalt\n"));
 action_commands[0]=0;browser_actions(16,0);assert(!strcmp(action_commands,"vote yes\n"));
 action_commands[0]=0;browser_actions(32,0);assert(!strcmp(action_commands,"vote no\n"));
 action_commands[0]=0;browser_actions(16|32,0);assert(!action_commands[0]); // Conflicting answers cancel.
 for(int actions=16;actions<=32;actions*=2) {
  for(int mode=1;mode<=3;mode++) {action_commands[0]=0;browser_actions(4|8|actions,mode);assert(!action_commands[0]);}
  action_commands[0]=0;webInputFlags=0;browser_actions(4|8|actions,0);assert(!action_commands[0]);
  webInputFlags=1;cls.state=0;browser_actions(4|8|actions,0);assert(!action_commands[0]);
  cls.state=CA_ACTIVE;
 }
 puts("Input focus: hover retains browser held keys; real focus loss clears them; desktop policy retained.");
 puts("Gameplay mouse: fallback left shots, right-drag isolation, focused capture/menu gates and unfocused releases passed.");
 puts("Touch weapon actions: native alternate/cycle commands require focused active gameplay and cannot run in menus, console or loading.");
 puts("Touch prompt responses: yes/no commands, conflicting answers, inactive/loading/menu/console/focus gates passed.");
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    build = ROOT / 'build_wasm'
    build.mkdir(exist_ok=True)
    fixture = build / 'test_input_focus.c'
    output = build / 'test_input_focus.cjs'
    fixture.write_text(HARNESS.replace('/* SWITCH */', window_switch)
                       .replace('/* BUTTON CASES */', button_cases)
                       .replace('/* TOUCH ACTIONS */', touch_actions), encoding='utf-8')
    subprocess.run([args.compiler, str(fixture), '-O2', '-sENVIRONMENT=node',
                    '-sWASM_ASYNC_COMPILATION=0', '-o', str(output)], check=True)
    subprocess.run([args.node, str(output)], check=True)
