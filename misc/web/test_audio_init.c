/* Actual SDL initialization/shutdown with device and allocation faults mocked.
 * emcc misc/web/test_audio_init.c -O2 -sUSE_SDL=2 -sENVIRONMENT=node
 *   -sWASM_ASYNC_COMPILATION=0 -sASSERTIONS=2 -sSAFE_HEAP=1
 *   -o build_wasm/test_audio_init.cjs
 */
#include <stdlib.h>
#include <assert.h>
static int allocationFails;
static void *audioAlloc(size_t count, size_t size) { return allocationFails ? NULL : calloc(count,size); }
#define calloc audioAlloc
#define SDL_WasInit AudioMock_WasInit
#define SDL_Init AudioMock_Init
#define SDL_QuitSubSystem AudioMock_QuitSubSystem
#define SDL_GetError AudioMock_GetError
#define SDL_GetCurrentAudioDriver AudioMock_GetCurrentAudioDriver
#define SDL_GetNumAudioDevices AudioMock_GetNumAudioDevices
#define SDL_GetAudioDeviceName AudioMock_GetAudioDeviceName
#define SDL_OpenAudioDevice AudioMock_OpenAudioDevice
#define SDL_CloseAudioDevice AudioMock_CloseAudioDevice
#define SDL_PauseAudioDevice AudioMock_PauseAudioDevice
#include "../../src/sdl/sdl_snd.c"
#undef calloc

dma_t dma;
static int initFails, openFails, closes, command, pauses, activeDevice;
static cvar_t variables[8];
static int variableCount;
void Com_Printf(const char *fmt, ...) {}
void Cmd_AddSystemCommand(const char *name, xcommand_t function, const char *description, completionFunc_t complete) { command=1; }
void Cmd_RemoveCommand(const char *name) { command=0; }
cvar_t *Cvar_Get(const char *name, const char *value, cvarFlags_t flags) {
 cvar_t *v=&variables[variableCount++%8];v->integer=atoi(value);v->value=atof(value);return v;
}
void Cvar_Set(const char *name, const char *value) {}
Uint32 SDL_WasInit(Uint32 flags) { return 0; }
int SDL_Init(Uint32 flags) { return initFails ? -1 : 0; }
void SDL_QuitSubSystem(Uint32 flags) { assert(!activeDevice); }
const char *SDL_GetError(void) { return "Fixture fault"; }
const char *SDL_GetCurrentAudioDriver(void) { return "Fixture audio"; }
int SDL_GetNumAudioDevices(int capture) { return 0; }
const char *SDL_GetAudioDeviceName(int index, int capture) { return "Fixture device"; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *name, int capture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int changes) {
 if(openFails)return 0;assert(!activeDevice);*obtained=*desired;activeDevice=17;return activeDevice;
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID device) { assert(device==activeDevice && device!=0);activeDevice=0;closes++; }
void SDL_PauseAudioDevice(SDL_AudioDeviceID device, int paused) {
 assert(device==activeDevice && snd_inited && dma.buffer && dmasize>0);pauses++;
}
int main(void) {
 initFails=1;assert(!SNDDMA_Init());assert(!command && !activeDevice && !snd_inited);
 initFails=0;openFails=1;assert(!SNDDMA_Init());assert(!command && !activeDevice && !snd_inited);
 openFails=0;allocationFails=1;assert(!SNDDMA_Init());
 assert(closes==1 && !command && !activeDevice && !device_id && !dma.buffer && !dmasize && !snd_inited);
 allocationFails=0;assert(SNDDMA_Init());assert(command && snd_inited && pauses==1);
 assert(SNDDMA_Init());assert(pauses==1); // No duplicate device on repeated initialization.
 SNDDMA_Shutdown();assert(closes==2 && !command && !device_id && !dma.buffer && !snd_inited);
 SNDDMA_Shutdown();assert(closes==2); // An already closed device is never closed again.
 assert(SNDDMA_Init());SNDDMA_Shutdown();assert(closes==3);
 puts("Audio initialization: SDL/open/allocation failures clean up, retry succeeds, first callback is ready and repeated shutdown is safe.");
 return 0;
}
