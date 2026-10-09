"""Compile production video-mode selection, including invalid browser settings."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/sdl/sdl_glimp.c').read_text(encoding='utf-8')
start = source.index('qboolean GLimp_GetModeInfo(')
handler = source[start:source.index('#define GLimp_ResolutionToFraction', start)]
HARNESS = r'''
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#define qfalse 0
#define qtrue 1
typedef int qboolean;
typedef union { float f; unsigned int ui; } floatint_t;
typedef struct { int width, height; float pixelAspect; } vidmode_t;
typedef struct { int integer; float value; } cvar_t;
static vidmode_t glimp_vidModes[] = {{640,480,1}, {1920,1080,1}};
static int s_numVidModes = 2;
cvar_t cw = {960,0}, ch = {540,0}, ca = {0,1};
static cvar_t *r_customwidth = &cw, *r_customheight = &ch, *r_customaspect = &ca;
/* HANDLER */
int main(void) {
    int width, height;
    float aspect;
    assert(GLimp_GetModeInfo(&width,&height,&aspect,-1));
    assert(width==960 && height==540 && fabsf(aspect-16.0f/9)<0.001f);
    assert(!GLimp_GetModeInfo(&width,&height,&aspect,-2));
    assert(!GLimp_GetModeInfo(&width,&height,&aspect,2));
#ifdef __EMSCRIPTEN__
    for(int i=0;i<2;i++) {
        cw.integer=i ? -1 : 0;
        assert(!GLimp_GetModeInfo(&width,&height,&aspect,-1));
        cw.integer=960; ch.integer=i ? -1 : 0;
        assert(!GLimp_GetModeInfo(&width,&height,&aspect,-1));
        ch.integer=540;
    }
    float invalid[] = {0,-1,NAN,INFINITY,FLT_MAX,FLT_TRUE_MIN};
    for(int i=0;i<6;i++) {
        ca.value=invalid[i];
        assert(!GLimp_GetModeInfo(&width,&height,&aspect,-1));
    }
    ca.value=1;
#else
    // Desktop behavior is outside this browser-specific validation change.
    cw.integer=0;
    assert(GLimp_GetModeInfo(&width,&height,&aspect,-1));
    cw.integer=960;
#endif
    assert(GLimp_GetModeInfo(&width,&height,&aspect,0));
    assert(width==640 && height==480 && fabsf(aspect-4.0f/3)<0.001f);
    assert(GLimp_GetModeInfo(&width,&height,&aspect,1));
    assert(width==1920 && height==1080);
    puts("Video mode checks passed: custom resolution, invalid browser dimensions/aspect, stock fallback, desktop parity.");
}
'''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    build = ROOT / 'build_wasm'
    build.mkdir(exist_ok=True)
    fixture = build / 'test_video_mode.c'
    production, main = HARNESS.split('int main(void)', 1)
    fixture.write_text(production.replace('/* HANDLER */', handler), encoding='utf-8')
    driver = build / 'test_video_mode_driver.c'
    driver.write_text(HARNESS.split('static vidmode_t', 1)[0] +
                      'extern cvar_t cw, ch, ca;\n' +
                      'qboolean GLimp_GetModeInfo(int*, int*, float*, int);\n' +
                      'int main(void)' + main, encoding='utf-8')
    for platform, flags in [('web', []), ('desktop', ['-U__EMSCRIPTEN__'])]:
        output = build / ('test_video_mode_' + platform + '.cjs')
        obj = build / ('test_video_mode_' + platform + '.o')
        # Match production optimization, but keep adversarial NaN/infinity test
        # inputs in a separate translation unit without fast-math assumptions.
        subprocess.run([args.compiler, str(fixture), '-O3', '-ffast-math', *flags,
                        '-c', '-o', str(obj)], check=True)
        subprocess.run([args.compiler, str(driver), str(obj), '-O2', '-sENVIRONMENT=node',
                        '-sWASM_ASYNC_COMPILATION=0', *flags, '-o', str(output)], check=True)
        subprocess.run([args.node, str(output)], check=True)
