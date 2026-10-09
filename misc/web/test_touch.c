/* Test the production usercmd conversion with the real engine types and flags. */
#include "../../src/qcommon/q_shared.h"
#include "../../src/client/web_touch.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
signed char ClampChar(int value) { return value < -128 ? -128 : value > 127 ? 127 : value; }
int main(void)
{
	usercmd_t cmd = {0}; float angles[3] = {0}; float input[4] = {1, .5f, 20, -10};
	WebTouch_Apply(&cmd, angles, input, 1|2|8|16|32, 5, .022f, .022f);
	assert(cmd.rightmove == 127 && cmd.forwardmove == 63 && cmd.upmove == 127);
	assert((cmd.buttons & (BUTTON_ATTACK|BUTTON_SPRINT|BUTTON_ACTIVATE|BUTTON_ANY)) == (BUTTON_ATTACK|BUTTON_SPRINT|BUTTON_ACTIVATE|BUTTON_ANY));
	assert(cmd.wbuttons & WBUTTON_RELOAD); assert(fabsf(angles[YAW]+2.2f)<.001f && fabsf(angles[PITCH]+1.1f)<.001f);
	cmd = (usercmd_t){0}; cmd.forwardmove = 90; cmd.buttons = BUTTON_ATTACK;
	input[0]=input[1]=input[2]=input[3]=0;
	WebTouch_Apply(&cmd, angles, input, 0, 5,.022f,.022f);
	assert(cmd.forwardmove == 90 && cmd.buttons == BUTTON_ATTACK); // Keyboard held state survives touch release.
	cmd = (usercmd_t){0};cmd.wbuttons=WBUTTON_RELOAD;
	WebTouch_Apply(&cmd,angles,input,64,5,.022f,.022f);
	assert((cmd.wbuttons & (WBUTTON_PRONE|WBUTTON_RELOAD)) == (WBUTTON_PRONE|WBUTTON_RELOAD));
	assert(cmd.buttons & BUTTON_ANY);
	cmd=(usercmd_t){0};WebTouch_Apply(&cmd,angles,input,0,5,.022f,.022f);
	assert(!(cmd.wbuttons & WBUTTON_PRONE));
	input[1]=1; WebTouch_Apply(&cmd,angles,input,4,5,.022f,.022f);
	assert(cmd.forwardmove==127 && cmd.upmove==-127);
	cmd=(usercmd_t){0}; cmd.buttons=BUTTON_WALKING; WebTouch_Apply(&cmd,angles,input,0,5,.022f,.022f);
	assert(cmd.forwardmove==64); // Respect the saved run/walk setting.
	input[0]=1.01f; input[1]=-1.01f; input[2]=501; input[3]=-501;
	cmd=(usercmd_t){0}; WebTouch_Apply(&cmd,angles,input,0,5,.022f,.022f);
	assert(cmd.rightmove==0 && cmd.forwardmove==0 && fabsf(angles[YAW]+2.2f)<.001f && fabsf(angles[PITCH]+1.1f)<.001f);
	/* Runtime bit patterns prevent fast-math from folding constant NaN cases. */
	{
		volatile uint32_t bad[] = {0x7fc00001u,0xffc00001u,0x7f800001u,0xff800001u,0x7f800000u,0xff800000u};
		int i,j;
		for(i=0;i<(int)(sizeof(bad)/sizeof(bad[0]));i++)
		{
			uint32_t bits=bad[i], yawBits, pitchBits;
			for(j=0;j<4;j++)memcpy(&input[j],&bits,sizeof(bits));
			cmd=(usercmd_t){0};angles[YAW]=angles[PITCH]=0;
			WebTouch_Apply(&cmd,angles,input,0,5,.022f,.022f);
			memcpy(&yawBits,&angles[YAW],sizeof(yawBits));memcpy(&pitchBits,&angles[PITCH],sizeof(pitchBits));
			assert(cmd.rightmove==0 && cmd.forwardmove==0);
			assert((yawBits & 0x7fffffffu)==0 && (pitchBits & 0x7fffffffu)==0);
		}
	}
	puts("Touch usercmd: analog movement, look, combat bits, mixed keyboard input, clamping and non-finite rejection passed.");
}
