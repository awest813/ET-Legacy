/* Browser System menu staging. GPLv3 or later. */
#ifndef UI_WEB_SETTINGS_H
#define UI_WEB_SETTINGS_H

static const char *const webSizeCvars[2] = {"r_customwidth", "r_customheight"};
static const char *const webSizeStaged[2] = {"ui_r_customwidth", "ui_r_customheight"};
static const char *const webEffectCvars[4] = {"cg_shadows", "cg_skybox", "r_fastsky", "com_maxfps"};
static const char *const webEffectStaged[4] = {"ui_web_shadows", "ui_web_skybox", "ui_web_fastsky", "ui_web_maxfps"};

static inline void UI_WebGraphicsGet(void)
{
	char value[MAX_CVAR_VALUE_STRING];
	int i;
	for (i = 0; i < 2; i++)
	{
		trap_Cvar_LatchedVariableStringBuffer(webSizeCvars[i], value, sizeof(value));
		trap_Cvar_Set(webSizeStaged[i], value);
	}
	for (i = 0; i < 4; i++)
	{
		trap_Cvar_LatchedVariableStringBuffer(webEffectCvars[i], value, sizeof(value));
		trap_Cvar_Set(webEffectStaged[i], value);
	}
}

static inline void UI_WebGraphicsReset(void)
{
	int i;
	for (i = 0; i < 2; i++) trap_Cvar_Set(webSizeStaged[i], "");
	for (i = 0; i < 4; i++) trap_Cvar_Set(webEffectStaged[i], "");
}

static inline void UI_WebGraphicsApply(void)
{
	char value[MAX_CVAR_VALUE_STRING];
	int i;
	for (i = 0; i < 2; i++)
	{
		trap_Cvar_VariableStringBuffer(webSizeStaged[i], value, sizeof(value));
		trap_Cvar_Set(webSizeCvars[i], value);
	}
	for (i = 0; i < 4; i++)
	{
		trap_Cvar_VariableStringBuffer(webEffectStaged[i], value, sizeof(value));
		trap_Cvar_Set(webEffectCvars[i], value);
	}
	UI_WebGraphicsReset();
}

/* Presets only edit staged graphics values. Audio, input and networking are
 * deliberately independent. Individual controls remain editable afterward. */
static inline void UI_WebGraphicsPreset(int preset)
{
	static const char *const names[] = {
		"ui_r_mode", "ui_r_customwidth", "ui_r_customheight", "ui_r_picmip",
		"ui_r_texturemode", "ui_r_ext_texture_filter_anisotropic", "ui_r_dynamiclight",
		"ui_web_shadows", "ui_web_skybox", "ui_web_fastsky", "ui_web_maxfps"
	};
	static const char *const values[3][11] = {
		{"-1", "960", "540", "2", "GL_LINEAR_MIPMAP_NEAREST", "0", "0", "0", "0", "1", "60"},
		{"-1", "1280", "720", "1", "GL_LINEAR_MIPMAP_LINEAR", "4", "1", "1", "1", "0", "60"},
		{"-1", "1920", "1080", "0", "GL_LINEAR_MIPMAP_LINEAR", "16", "2", "1", "1", "0", "60"}
	};
	int i;
	if (preset < 0 || preset > 2) return;
	for (i = 0; i < 11; i++) trap_Cvar_Set(names[i], values[preset][i]);
}

static inline int UI_WebCustomSizeChanged(void)
{
	return (int)trap_Cvar_VariableValue("ui_r_mode") == -1 &&
		((int)trap_Cvar_VariableValue(webSizeStaged[0]) != (int)trap_Cvar_VariableValue(webSizeCvars[0]) ||
		 (int)trap_Cvar_VariableValue(webSizeStaged[1]) != (int)trap_Cvar_VariableValue(webSizeCvars[1]));
}
#endif
