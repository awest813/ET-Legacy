/* Actual renderer color mapping/upload logic, without a GL context.
 * emcc misc/web/test_gamma.c src/qcommon/q_math.c -Isrc/webgl -O2 -sENVIRONMENT=node
 *      -sWASM_ASYNC_COMPILATION=0 -o build_wasm/test_gamma.cjs
 */
#include "../../src/renderer/tr_image.c"
#include <assert.h>

trGlobals_t tr;
glconfig_t glConfig;
refimport_t ri;
cvar_t *r_gamma, *r_intensity, *r_overBrightBits;
static int ramps;
static void gammaRamp(unsigned char red[256], unsigned char green[256], unsigned char blue[256]) { ramps++; }

int main(void)
{
	cvar_t gamma = {0}, intensity = {0}, overbright = {0};
	unsigned int pixels[2];
	byte original[8] = {32,64,128,37, 96,160,224,191};
	int hardware, screen, i;
	r_gamma = &gamma; r_intensity = &intensity; r_overBrightBits = &overbright;
	gamma.value = 1.5f; intensity.value = 1.25f; overbright.integer = 1;
	glConfig.colorBits = 24; glConfig.isFullscreen = qtrue;
	ri.GLimp_SetGamma = gammaRamp;
	for (hardware = 0; hardware <= 1; hardware++)
		for (screen = 0; screen <= 1; screen++)
		{
			glConfig.deviceSupportsGamma = hardware; tr.gammaProgramUsed = screen;
			ramps = 0; R_SetColorMappings();
			assert(tr.overbrightBits == ((hardware || screen) ? 1 : 0));
			assert(ramps == (hardware && !screen));
			memcpy(pixels, original, sizeof(pixels));
			R_LightScaleTexture(pixels, 2, 1, qtrue);
			for (i = 0; i < 8; i++)
				assert(((byte *)pixels)[i] == (i % 4 == 3 || hardware || screen ? original[i] : s_gammatable[original[i]]));
			memcpy(pixels, original, sizeof(pixels));
			R_LightScaleTexture(pixels, 2, 1, qfalse);
			for (i = 0; i < 8; i++)
			{
				byte expected = i % 4 == 3 ? original[i] : s_intensitytable[original[i]];
				if (i % 4 != 3 && !hardware && !screen) expected = s_gammatable[expected];
				assert(((byte *)pixels)[i] == expected);
			}
		}
	puts("Gamma: hardware/screen/software paths, single correction, overbright, intensity and alpha passed.");
	return 0;
}
