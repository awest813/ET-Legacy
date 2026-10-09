/* Nonblocking browser frame pacing. GPLv3 or later. */
#ifndef ET_WEB_FRAME_H
#define ET_WEB_FRAME_H
#include <stdint.h>

typedef struct
{
	int64_t next, period;
	int initialized;
} webFrameClock_t;

static int64_t Web_FramePeriod(int fps)
{
	return fps > 0 ? 1000000 / fps : fps < 0 ? 1000 : 0;
}

static int Web_FrameDue(webFrameClock_t *clock, int64_t now, int64_t period)
{
	// A small tolerance absorbs RAF timestamp jitter at matching refresh rates.
	if (!clock->initialized || clock->period != period || !period)
	{
		clock->initialized = 1;
		clock->period = period;
		clock->next = now + period;
		return 1;
	}
	if (now + 500 < clock->next) return 0;
	clock->next += period;
	// A stalled/hidden tab must resume without a burst of catch-up frames.
	if (clock->next < now) clock->next = now + period;
	return 1;
}
#endif
