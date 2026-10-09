/*
 * ET: Legacy WebAssembly build - static mod modules.
 *
 * The engine normally loads cgame/ui/qagame as native shared libraries
 * via Sys_LoadGameDll(). There is no dlopen in a browser, so the mod
 * sources are linked into the main binary as static archives whose
 * symbols are prefixed (cg_ / ui_ / qa_) while compiling using the
 * versioned module_symbols headers to avoid collisions between the
 * modules and the engine (q_math.c etc. are compiled into both).
 *
 * WebAssembly indirect calls are signature-typed, so the real prefixed
 * vmMain (12 args) is wrapped in a full-arity trampoline matching
 * VM_EntryPoint_t (17 args) - the same arity the engine's native DLL
 * loading tolerated implicitly through the C ABI.
 *
 * Sys_LoadGameDll() (see sys_main.c) resolves module names against this
 * table at runtime.
 */
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

// Renamed entry points (see cmake/ETLBuildMod.cmake compile definitions)
extern intptr_t cg_vmMain(intptr_t command, intptr_t arg0, intptr_t arg1, intptr_t arg2, intptr_t arg3,
                          intptr_t arg4, intptr_t arg5, intptr_t arg6, intptr_t arg7, intptr_t arg8,
                          intptr_t arg9, intptr_t arg10, intptr_t arg11);
extern void     cg_dllEntry(intptr_t (QDECL *syscallptr)(intptr_t arg, ...));

extern intptr_t ui_vmMain(intptr_t command, intptr_t arg0, intptr_t arg1, intptr_t arg2, intptr_t arg3,
                          intptr_t arg4, intptr_t arg5, intptr_t arg6, intptr_t arg7, intptr_t arg8,
                          intptr_t arg9, intptr_t arg10, intptr_t arg11);
extern void     ui_dllEntry(intptr_t (QDECL *syscallptr)(intptr_t arg, ...));

extern intptr_t qa_vmMain(intptr_t command, intptr_t arg0, intptr_t arg1, intptr_t arg2, intptr_t arg3,
                          intptr_t arg4, intptr_t arg5, intptr_t arg6);
extern void     qa_dllEntry(intptr_t (QDECL *syscallptr)(intptr_t arg, ...));

// Full-arity trampolines matching VM_EntryPoint_t (int command, 16 x intptr_t)
static intptr_t cg_vmMain_full(int command, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4,
                               intptr_t a5, intptr_t a6, intptr_t a7, intptr_t a8, intptr_t a9, intptr_t a10,
                               intptr_t a11, intptr_t a12, intptr_t a13, intptr_t a14, intptr_t a15)
{
	(void) a12; (void) a13; (void) a14; (void) a15;
	return cg_vmMain(command, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

static intptr_t ui_vmMain_full(int command, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4,
                               intptr_t a5, intptr_t a6, intptr_t a7, intptr_t a8, intptr_t a9, intptr_t a10,
                               intptr_t a11, intptr_t a12, intptr_t a13, intptr_t a14, intptr_t a15)
{
	(void) a12; (void) a13; (void) a14; (void) a15;
	return ui_vmMain(command, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

static intptr_t qa_vmMain_full(int command, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4,
                               intptr_t a5, intptr_t a6, intptr_t a7, intptr_t a8, intptr_t a9, intptr_t a10,
                               intptr_t a11, intptr_t a12, intptr_t a13, intptr_t a14, intptr_t a15)
{
	(void) a7; (void) a8; (void) a9; (void) a10; (void) a11;
	(void) a12; (void) a13; (void) a14; (void) a15;
	// qagame's vmMain takes command + 7 args
	return qa_vmMain(command, a0, a1, a2, a3, a4, a5, a6);
}

typedef struct
{
	const char *name;
	VM_EntryPoint_t entryPoint;
	void (QDECL *dllEntry)(intptr_t (QDECL *syscallptr)(intptr_t arg, ...));
} staticMod_t;

static const staticMod_t staticMods[] =
{
	{ "cgame",  (VM_EntryPoint_t) cg_vmMain_full, cg_dllEntry },
	{ "ui",     (VM_EntryPoint_t) ui_vmMain_full,  ui_dllEntry },
	{ "qagame", (VM_EntryPoint_t) qa_vmMain_full,  qa_dllEntry },
};

/*
 * Replacement for the native library load path. Returns an opaque
 * non-NULL handle on success and wires the syscall trampoline exactly
 * like the native Sys_LoadGameDll does.
 */
void *Sys_LoadGameDllStatic(const char *name, VM_EntryPoint_t *entryPoint,
                            void (QDECL **dllEntry)(intptr_t (QDECL *)(intptr_t, ...)),
                            intptr_t (QDECL *systemcalls)(intptr_t, ...))
{
	size_t i;

	for (i = 0; i < ARRAY_LEN(staticMods); i++)
	{
		if (!strcmp(staticMods[i].name, name))
		{
			Com_DPrintf("Sys_LoadGameDllStatic: resolving '%s' to statically linked module\n", name);
			*entryPoint = staticMods[i].entryPoint;
			*dllEntry   = staticMods[i].dllEntry;
			staticMods[i].dllEntry(systemcalls);
			return (void *) (intptr_t) (i + 1);
		}
	}

	return NULL;
}
