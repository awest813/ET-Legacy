/* emcc misc/web/test_mouse.c -O2 -sENVIRONMENT=node
 *      -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_mouse.cjs
 */
#include "../../src/sdl/sdl_web_mouse.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
	webMouseMotion_t motion = {0};
	int dx, dy, sumX, sumY, i, size;
	/* The same CSS travel must yield the same aim at desktop and narrow sizes. */
	const int widths[] = {1280,640,320};
	for (size=0;size<3;size++)
	{
		sumX=sumY=0; WebMouse_Reset(&motion);
		for (i=0;i<100;i++)
		{
			dx=1280/widths[size]; dy=-720/(widths[size]*9/16);
			WebMouse_Convert(&motion,widths[size]/1280.0,(widths[size]*9/16)/720.0,&dx,&dy);
			sumX+=dx; sumY+=dy;
		}
		assert(sumX==100 && sumY==-100);
	}
	WebMouse_Reset(&motion); sumX=0;
	for(i=0;i<100;i++) { dx=1;dy=0; WebMouse_Convert(&motion,.25,.25,&dx,&dy);sumX+=dx; }
	assert(sumX==25 && motion.x==0);
	dx=1;dy=-1; WebMouse_Convert(&motion,.25,.25,&dx,&dy);
	assert(dx==0 && dy==0); WebMouse_Reset(&motion);
	dx=3;dy=-3; WebMouse_Convert(&motion,.25,.25,&dx,&dy);
	assert(dx==0 && dy==0); /* Focus reset cannot leak the old fractional motion. */
	dx=1;dy=-1; WebMouse_Convert(&motion,1,1,&dx,&dy);
	assert(dx==1 && dy==-1); /* Resize also resets stale fractions. */
	puts("Mouse: CSS-scale-independent aim, positive/negative subpixel accumulation, resize and focus reset passed.");
	return 0;
}
