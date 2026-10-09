// Browser game and bot runtime share one WebAssembly module.
#include "../../vendor/Omnibot/Common/BotExports.h"
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstring>

Bot_EngineFuncs_t g_BotFunctions = {};
IEngineInterface *g_InterfaceFunctions = nullptr;
static bool loaded = false;
static std::string libraryPath;
void Omnibot_Load_PrintMsg(const char *message);
void Omnibot_Load_PrintErr(const char *message);

bool IsOmnibotLoaded() { return loaded; }
const char *Omnibot_GetLibraryPath() { return libraryPath.c_str(); }
const char *Omnibot_FixPath(const char *path) { return path; }
const char *Omnibot_ErrorString(eomnibot_error error)
{
    static const char *errors[] = { "None", "Bot library missing", "Invalid bot exports",
        "Bot initialization failed", "Invalid interface", "Wrong version", "File system initialization failed" };
    return error >= 0 && error < BOT_NUM_ERRORS ? errors[error] : "Unknown error";
}
extern "C" const char *OB_VA(const char *format, ...)
{
    static char buffers[3][1024];
    static unsigned int next;
    char *buffer = buffers[next++ % 3];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, 1024, format, args);
    va_end(args);
    return buffer;
}
eomnibot_error Omnibot_LoadLibrary(int version, const char *, const char *path)
{
    libraryPath = std::string(path && *path ? path : "/omni-bot") + "/omnibot_et.wasm";
    eomnibot_error error = ExportBotFunctionsFromDLL(&g_BotFunctions, sizeof(g_BotFunctions));
    if (error == BOT_ERROR_NONE) error = g_BotFunctions.pfnInitialize(g_InterfaceFunctions, version);
    loaded = error == BOT_ERROR_NONE;
    if (loaded) Omnibot_Load_PrintMsg("Omni-bot Loaded Successfully (WebAssembly)");
    else { Omnibot_Load_PrintErr(Omnibot_ErrorString(error)); Omnibot_FreeLibrary(); }
    return error;
}
void Omnibot_FreeLibrary()
{
    memset(&g_BotFunctions, 0, sizeof(g_BotFunctions));
    delete g_InterfaceFunctions;
    g_InterfaceFunctions = nullptr;
    loaded = false;
}
