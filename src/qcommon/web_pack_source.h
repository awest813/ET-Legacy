/* Browser pack-source discovery. Metadata only; never installs native downloads. */
#if defined(__EMSCRIPTEN__) && !defined(DEDICATED)
static char webMissingFiles[1024];
static int64_t webPackSourceDeadline;
static qboolean webPackSourcePending;

static void Com_WebPackSourceReset(void)
{
	webPackSourcePending = qfalse;
	webMissingFiles[0] = '\0';
}

static void Com_WebPackSourceFinish(void)
{
	webPackSourcePending = qfalse;
	CL_AddReliableCommand("stopdl");
	NET_WebMissingAssets(webMissingFiles, Cvar_VariableString("sv_referencedPakNames"), Cvar_VariableString("sv_referencedPaks"));
	Com_Error(ERR_DROP, "Server assets missing:\n%s\nInstall these exact PK3 packs in the local launcher's custom asset directory, then reload and join again.", webMissingFiles);
}

static qboolean Com_WebPackSourceRequest(const char *missing)
{
	char remote[MAX_OSPATH], *begin, *end;
	const char *name, *cursor;
	size_t length;

	Q_strncpyz(webMissingFiles, missing, sizeof(webMissingFiles));
	if (!FS_ComparePaks(dld.downloadList, sizeof(dld.downloadList), qtrue))
	{
		return qfalse;
	}
	begin = dld.downloadList;
	if (*begin == '@') begin++;
	end = strchr(begin, '@');
	if (!end || (length = end - begin) >= sizeof(remote)) return qfalse;
	memcpy(remote, begin, length);
	remote[length] = '\0';
	name = !strncmp(remote, "etmain/", 7) || !strncmp(remote, "legacy/", 7) ? remote + 7 : NULL;
	if (!name || strlen(name) < 5 || strstr(name, "..") || Q_stricmp(name + strlen(name) - 4, ".pk3")) return qfalse;
	for (cursor = name; *cursor; cursor++)
	{
		if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
		      (*cursor >= '0' && *cursor <= '9') || *cursor == '_' || *cursor == '-' || *cursor == '.')) return qfalse;
	}
	// Ask only for the first missing pack's source. No temporary file is opened.
	Q_strncpyz(dld.downloadName, remote, sizeof(dld.downloadName));
	dld.downloadList[0] = '\0';
	cls.state = CA_CONNECTED;
	webPackSourcePending = qtrue;
	webPackSourceDeadline = Sys_Milliseconds() + 3000;
	Com_Printf("Checking server pack source: %s\n", remote);
	CL_AddReliableCommand(va("download %s", remote));
	return qtrue;
}

static void Com_WebPackSourceFrame(void)
{
	if (!webPackSourcePending) return;
	if (cls.state != CA_CONNECTED) Com_WebPackSourceReset();
	else if (Sys_Milliseconds() >= webPackSourceDeadline) Com_WebPackSourceFinish();
}

void Com_WebPackSourceMessage(msg_t *msg)
{
	if (!webPackSourcePending || cls.state != CA_CONNECTED)
	{
		CL_AddReliableCommand("stopdl");
		return;
	}
	if (MSG_ReadShort(msg) == DLTYPE_WWW)
	{
		char location[MAX_STRING_CHARS];
		const char *cursor;
		qboolean printable = qtrue;
		Q_strncpyz(location, MSG_ReadString(msg), sizeof(location));
		MSG_ReadLong(msg); // advertised size: never used to allocate or write
		MSG_ReadLong(msg); // redirect flags: never open a page or disconnect to fetch
		for (cursor = location; *cursor; cursor++)
		{
			if ((unsigned char)*cursor <= 32 || (unsigned char)*cursor >= 127) printable = qfalse;
		}
		if (printable && (!strncmp(location, "https://", 8) || !strncmp(location, "http://", 7)))
		{
			Com_Printf("Server pack source (%s): %s\n", dld.downloadName, location);
		}
	}
	// UDP payloads, fallback URLs and redirects all stop at the same pack gate.
	// The launcher retains its approved-source, ZIP and checksum verification.
	Com_WebPackSourceFinish();
}
#endif
