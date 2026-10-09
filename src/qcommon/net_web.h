/* Browser-only fixed-server implementation; included by net_ip.c. GPLv3 or later. */
#include <emscripten.h>

EM_JS(void, NET_WebSend, (const void *data, int length), {
    if (Module['browserNetwork']) Module['browserNetwork'].send(HEAPU8.slice(data, data + length));
});

EM_JS(int, NET_WebReceive, (void *data, int limit), {
    var packet = Module['browserNetwork'] && Module['browserNetwork'].receive(limit);
    if (!packet) return 0;
    HEAPU8.set(packet, data);
    return packet.length;
});

EM_JS(int, NET_WebConnectAllowed, (const char *server), {
    var net = Module['browserNetwork'];
    if (net && net.allowed(UTF8ToString(server))) return 1;
    if (net) net.blocked();
    return 0;
});

EM_JS(int, NET_WebFrame, (int state, int ping, const char *message), {
    return Module['browserNetwork'] ? Module['browserNetwork'].frame(state, ping,
        // EM_JS passes through a C string: preserve the regex's literal caret.
        UTF8ToString(message).replace(/\\^[0-9a-z]/gi, "").slice(0, 512)) : 0;
});

EM_JS(void, NET_WebUserDisconnect, (), {
    if (Module['browserNetwork']) Module['browserNetwork'].userDisconnect();
});

EM_JS(void, NET_WebMissingAssets, (const char *missing, const char *names, const char *checksums), {
    if (Module['browserMissingAssets']) Module['browserMissingAssets'](UTF8ToString(missing), UTF8ToString(names), UTF8ToString(checksums));
});

void NET_WebClientFrame(int state, int ping, const char *message)
{
    int action = NET_WebFrame(state, ping, message);
    if (action == 1) Cbuf_AddText("disconnect\n");
    else if (action == 2) Cbuf_AddText("exec browser-connect.cfg\nconnect 192.0.2.1:27960\n");
}

static void NET_WebSleep(void)
{
    byte data[MAX_MSGLEN];
    netadr_t from;
    msg_t message;
    int length, count;

    Com_Memset(&from, 0, sizeof(from));
    from.type = NA_IP;
    from.ip[0] = 192; from.ip[1] = 0; from.ip[2] = 2; from.ip[3] = 1;
    from.port = BigShort(PORT_SERVER);
    // Bound per-frame work; excess packets remain in the bounded JS queue.
    for (count = 0; count < 64; count++)
    {
        length = NET_WebReceive(data, sizeof(data));
        if (!length) break;
        MSG_Init(&message, data, sizeof(data));
        message.cursize = length;
        CL_PacketEvent(&from, &message);
    }
}
