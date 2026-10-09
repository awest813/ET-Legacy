/* Browser System menu staging. GPLv3 or later. */
#ifndef UI_WEB_SETTINGS_H
#define UI_WEB_SETTINGS_H

static const char *const webSizeCvars[2] = {"r_customwidth", "r_customheight"};
static const char *const webSizeStaged[2] = {"ui_r_customwidth", "ui_r_customheight"};

static inline void UI_WebCustomSizeGet(void)
{
	char value[MAX_CVAR_VALUE_STRING];
	int i;
	for (i = 0; i < 2; i++)
	{
		trap_Cvar_LatchedVariableStringBuffer(webSizeCvars[i], value, sizeof(value));
		trap_Cvar_Set(webSizeStaged[i], value);
	}
}

static inline void UI_WebCustomSizeReset(void)
{
	int i;
	for (i = 0; i < 2; i++) trap_Cvar_Set(webSizeStaged[i], "");
}

static inline void UI_WebCustomSizeApply(void)
{
	char value[MAX_CVAR_VALUE_STRING];
	int i;
	for (i = 0; i < 2; i++)
	{
		trap_Cvar_VariableStringBuffer(webSizeStaged[i], value, sizeof(value));
		trap_Cvar_Set(webSizeCvars[i], value);
	}
	UI_WebCustomSizeReset();
}

static inline int UI_WebCustomSizeChanged(void)
{
	return (int)trap_Cvar_VariableValue("ui_r_mode") == -1 &&
		((int)trap_Cvar_VariableValue(webSizeStaged[0]) != (int)trap_Cvar_VariableValue(webSizeCvars[0]) ||
		 (int)trap_Cvar_VariableValue(webSizeStaged[1]) != (int)trap_Cvar_VariableValue(webSizeCvars[1]));
}
#endif
