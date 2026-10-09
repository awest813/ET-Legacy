/* Preserve browser mouse sensitivity when CSS scales the SDL canvas.
 * SDL2's Emscripten backend scales both locked and unlocked motion into window
 * coordinates. Convert aiming deltas back to CSS pixels, retaining fractions.
 */
#ifndef SDL_WEB_MOUSE_H
#define SDL_WEB_MOUSE_H
typedef struct
{
	double x, y, scaleX, scaleY;
} webMouseMotion_t;

static void WebMouse_Reset(webMouseMotion_t *motion)
{
	motion->x = motion->y = 0;
}

static void WebMouse_Convert(webMouseMotion_t *motion, double scaleX, double scaleY, int *dx, int *dy)
{
	if (motion->scaleX != scaleX || motion->scaleY != scaleY)
	{
		WebMouse_Reset(motion);
		motion->scaleX = scaleX; motion->scaleY = scaleY;
	}
	motion->x += *dx * scaleX; motion->y += *dy * scaleY;
	*dx = (int) motion->x; *dy = (int) motion->y;
	motion->x -= *dx; motion->y -= *dy;
}
#endif
