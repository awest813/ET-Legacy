/* Exercise the actual SDL DMA callback with bounded memory.
 * emcc misc/web/test_audio.c -O2 -sUSE_SDL=2 -sENVIRONMENT=node
 *      -sWASM_ASYNC_COMPILATION=0 -sASSERTIONS=2 -sSAFE_HEAP=1
 *      -o build_wasm/test_audio.cjs
 */
#include "../../src/sdl/sdl_snd.c"
#include <assert.h>

dma_t dma;

int main(void)
{
	unsigned char ring[16], output[96];
	int bits, size, start, length, i;

	/* Include exact-end, one-wrap, and many-wrap callbacks at both depths.
	 * Sentinel bytes detect writes beyond the requested output range. */
	for (bits = 8; bits <= 16; bits += 8)
	{
		int bytes = bits / 8;
		for (size = 4; size <= 16; size *= 2)
		{
			for (i = 0; i < size; ++i) ring[i] = i + 1;
			dma.buffer = ring; dma.samplebits = bits; dma.samples = size / bytes;
			dmasize = size; snd_inited = qtrue;
			for (start = 0; start < size; start += bytes)
				for (length = bytes; length <= 80; length += bytes)
				{
					memset(output, 0xcd, sizeof(output)); dmapos = start / bytes;
					SNDDMA_AudioCallback(NULL, output + 1, length);
					assert(output[0] == 0xcd && output[length + 1] == 0xcd);
					for (i = 0; i < length; ++i) assert(output[i + 1] == ring[(start + i) % size]);
					assert(dmapos == ((start + length) % size) / bytes);
				}
		}
		snd_inited = qfalse;
		SNDDMA_AudioCallback(NULL, output, sizeof(output));
		for (i = 0; i < sizeof(output); ++i) assert(output[i] == (bits == 8 ? 128 : 0));
	}
	puts("Audio DMA: 8/16-bit ring boundaries, repeated wraps, output bounds and silence passed.");
	return 0;
}
