/* Shared browser touch-to-usercmd conversion; keyboard state is never modified. */
#ifndef ETL_WEB_TOUCH_H
#define ETL_WEB_TOUCH_H
#include <stdint.h>
#include <string.h>
static float WebTouch_Bound(const float *input, float limit)
{
	uint32_t bits;
	float value;
	/* Release fast-math can assume floating-point comparisons never see NaN. */
	memcpy(&bits, input, sizeof(bits));
	if ((bits & 0x7f800000u) == 0x7f800000u) return 0;
	memcpy(&value, &bits, sizeof(value));
	if (!(value >= -limit && value <= limit)) return 0;
	return value;
}
static void WebTouch_Apply(usercmd_t *cmd, float *angles, const float *input, int buttons,
                           float sensitivity, float yaw, float pitch)
{
	float x = WebTouch_Bound(&input[0], 1), y = WebTouch_Bound(&input[1], 1);
	float dx = WebTouch_Bound(&input[2], 500), dy = WebTouch_Bound(&input[3], 500);
	int speed = (cmd->buttons & BUTTON_WALKING) ? 64 : 127;
	cmd->rightmove = ClampChar(cmd->rightmove + (int)(x * speed));
	cmd->forwardmove = ClampChar(cmd->forwardmove + (int)(y * speed));
	if (buttons & 1) cmd->buttons |= BUTTON_ATTACK;
	if (buttons & 2) cmd->upmove = 127;
	if (buttons & 4) cmd->upmove = -127;
	if (buttons & 8) cmd->buttons |= BUTTON_SPRINT;
	if (buttons & 16) cmd->wbuttons |= WBUTTON_RELOAD;
	if (buttons & 32) cmd->buttons |= BUTTON_ACTIVATE;
	if (buttons & 64) cmd->wbuttons |= WBUTTON_PRONE;
	if (buttons || x || y) cmd->buttons |= BUTTON_ANY;
	angles[YAW] -= dx * sensitivity * yaw;
	angles[PITCH] += dy * sensitivity * pitch;
}
#endif
