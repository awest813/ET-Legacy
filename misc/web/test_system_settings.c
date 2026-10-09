/* Browser custom render-size staging and restart detection. GPLv3 or later. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_CVAR_VALUE_STRING 256
static const char *names[] = {
	"r_customwidth", "r_customheight", "ui_r_customwidth", "ui_r_customheight", "ui_r_mode",
	"cg_shadows", "cg_skybox", "r_fastsky", "com_maxfps",
	"ui_web_shadows", "ui_web_skybox", "ui_web_fastsky", "ui_web_maxfps",
	"ui_r_picmip", "ui_r_texturemode", "ui_r_ext_texture_filter_anisotropic", "ui_r_dynamiclight"
};
static char values[17][MAX_CVAR_VALUE_STRING] = {"1280", "720", "", "", "-1", "1", "1", "0", "125"};
static const char *pendingWidth;
static int indexOf(const char *name)
{
	int i;
	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) if (!strcmp(name, names[i])) return i;
	assert(0); return -1;
}
static void trap_Cvar_VariableStringBuffer(const char *name, char *output, int size)
{
	snprintf(output, size, "%s", values[indexOf(name)]);
}
static void trap_Cvar_LatchedVariableStringBuffer(const char *name, char *output, int size)
{
	if (pendingWidth && !strcmp(name, "r_customwidth")) snprintf(output, size, "%s", pendingWidth);
	else trap_Cvar_VariableStringBuffer(name, output, size);
}
static void trap_Cvar_Set(const char *name, const char *value)
{
	snprintf(values[indexOf(name)], MAX_CVAR_VALUE_STRING, "%s", value);
}
static float trap_Cvar_VariableValue(const char *name) { return (float)atof(values[indexOf(name)]); }
#include "../../src/ui/ui_web_settings.h"
int main(void)
{
	UI_WebGraphicsGet(); assert(!UI_WebCustomSizeChanged());
	trap_Cvar_Set("ui_r_customwidth", "1024");
	assert(UI_WebCustomSizeChanged()); assert(!strcmp(values[0], "1280"));
	UI_WebGraphicsReset();
	assert(!strcmp(values[0], "1280") && !strcmp(values[1], "720"));
	UI_WebGraphicsGet(); assert(!UI_WebCustomSizeChanged());
	trap_Cvar_Set("ui_r_customheight", "576");
	assert(UI_WebCustomSizeChanged()); assert(!strcmp(values[1], "720"));
	trap_Cvar_Set("ui_r_mode", "-2"); assert(!UI_WebCustomSizeChanged());
	trap_Cvar_Set("ui_r_mode", "-1"); assert(UI_WebCustomSizeChanged());
	UI_WebGraphicsApply();
	assert(!strcmp(values[0], "1280") && !strcmp(values[1], "576"));
	assert(!values[2][0] && !values[3][0]);
	UI_WebGraphicsGet(); assert(!UI_WebCustomSizeChanged());
	trap_Cvar_Set("ui_r_customwidth", "960"); trap_Cvar_Set("ui_r_customheight", "540");
	UI_WebGraphicsApply(); UI_WebGraphicsGet();
	assert(!strcmp(values[0], "960") && !strcmp(values[1], "540") && !UI_WebCustomSizeChanged());
	pendingWidth = "800"; UI_WebGraphicsGet();
	assert(!strcmp(values[2], "800") && !strcmp(values[0], "960") && UI_WebCustomSizeChanged());
	UI_WebGraphicsReset(); assert(!strcmp(pendingWidth, "800"));
	pendingWidth = NULL;
	UI_WebGraphicsGet();
	UI_WebGraphicsPreset(0);
	assert(!strcmp(values[2], "960") && !strcmp(values[3], "540"));
	assert(!strcmp(values[13], "2") && !strcmp(values[15], "0") && !strcmp(values[16], "0"));
	assert(!strcmp(values[5], "1") && !strcmp(values[8], "125")); // no live effects
	assert(!strcmp(values[9], "0") && !strcmp(values[10], "0") && !strcmp(values[11], "1") && !strcmp(values[12], "60"));
	UI_WebGraphicsReset(); UI_WebGraphicsGet();
	assert(!strcmp(values[9], "1") && !strcmp(values[12], "125")); // Back restores effects
	UI_WebGraphicsPreset(0); UI_WebGraphicsApply(); UI_WebGraphicsGet();
	assert(!strcmp(values[5], "0") && !strcmp(values[6], "0") && !strcmp(values[7], "1") && !strcmp(values[8], "60"));
	assert(!UI_WebCustomSizeChanged());
	UI_WebGraphicsPreset(1);
	assert(UI_WebCustomSizeChanged());
	assert(!strcmp(values[2], "1280") && !strcmp(values[3], "720") && !strcmp(values[13], "1"));
	assert(!strcmp(values[15], "4") && !strcmp(values[16], "1") && !strcmp(values[11], "0"));
	UI_WebGraphicsPreset(2);
	assert(!strcmp(values[2], "1920") && !strcmp(values[3], "1080") && !strcmp(values[13], "0"));
	assert(!strcmp(values[15], "16") && !strcmp(values[16], "2"));
	UI_WebGraphicsPreset(-1); UI_WebGraphicsPreset(3);
	assert(!strcmp(values[2], "1920"));
	puts("Browser System settings: staged width/height, Back discard, Apply, custom-mode restart and reopening passed.");
	puts("Browser graphics presets: low power, Balanced, Quality, staged effects and invalid preset rejection passed.");
	return 0;
}
