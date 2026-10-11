#!/usr/bin/env python3
"""One-off: turns the sh3-nx runtime into sh4-nx (names, registry key, the bridges SH4 imports that SH3 did not).
Kept for reference; it has already been applied."""
import glob, re

def sub(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        if a not in s:
            raise SystemExit(f'{path}: missing {a!r}')
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)

files = (glob.glob('src/runtime/*') + glob.glob('src/win32/*') + glob.glob('src/d3d8/*') +
         ['CMakeLists.txt', 'tools/build-switch-local.sh', 'tools/nxlink-wait.sh'])
for f in files:
    s = open(f, encoding='utf-8').read()
    s = re.sub(r'\bsh3', 'sh4', s)  # sh3.exe, sh3.ini, sh3-nx, sh3-progs.bin, sh3.log, sh3nx...
    s = s.replace('SH3_', 'SH4_').replace('C:\\\\SH3', 'C:\\\\SH4').replace('/SH3/', '/SH4/').replace('"SH3"', '"SH4"')
    s = s.replace('HKEY_SH3', 'HKEY_SH4').replace('Silent Hill 3', 'Silent Hill 4').replace('SILENT HILL 3', 'SILENT HILL 4')
    s = s.replace('0x53483350u  /* "SH3P" */', '0x53483450u  /* "SH4P" */')
    open(f, 'w', encoding='utf-8', newline='\n').write(s)

# ---- user32.c: registry key, new bridges ----
sub('src/win32/user32.c', [
    ('if (!subkey || strcasecmp(GSTR(subkey), "SOFTWARE\\\\KONAMI\\\\SILENT HILL 4")) return 2;',
     'if (!subkey || strcasecmp(GSTR(subkey), "SOFTWARE\\\\KONAMI\\\\SILENT HILL 4\\\\1.00.000")) return 2;'),
    ('''    const char *name = ARG(1) ? GSTR(ARG(1)) : "", *v = NULL;
    if (ARG(0) == HKEY_SH4 && (!strcasecmp(name, "data") || !strcasecmp(name, "movie") || !strcasecmp(name, "sound")))
        v = "C:\\\\SH4\\\\";
    if (ARG(0) == HKEY_SH4 && !strcasecmp(name, "save")) v = "C:\\\\SH4\\\\savedata\\\\";
    if (!v) return 2;''',
     '''    const char *name = ARG(1) ? GSTR(ARG(1)) : "", *v = NULL;  /* SH4: no value is kept, the game uses its defaults */
    if (rt_trace) rt_log("  RegQueryValueEx(%s)", name);
    if (!v) return 2;'''),
    ('WINAPI(RegCloseKey, "RegCloseKey", 1) { return 0; }',
     '''WINAPI(RegCloseKey, "RegCloseKey", 1) { return 0; }
WINAPI(RegSetValueExA, "RegSetValueExA", 6) {
    if (rt_trace) rt_log("  RegSetValueEx(%s): dropped", ARG(1) ? GSTR(ARG(1)) : "");
    return 0;
}
WINAPI(RegDeleteValueA, "RegDeleteValueA", 2) { return 0; }'''),
    ('WINAPI(ImmAssociateContext, "ImmAssociateContext", 2) { return 0; }',
     'WINAPI(ImmAssociateContext, "ImmAssociateContext", 2) { return 0; }\n'
     'WINAPI(ImmAssociateContextEx, "ImmAssociateContextEx", 3) { return 1; }'),
    ('static int next_message(uint32_t m, int remove) {',
     '''int rt_messages_pending(void) { rt_pump_events(); return n_posted || quit_posted; }  /* MsgWaitForMultipleObjects */
/* PostThreadMessageA(thread, msg, w, l): the game's own thread, so the loop sees it with hwnd 0. */
WINAPI(PostThreadMessageA, "PostThreadMessageA", 4) {
    if (n_posted == 16) return 0;
    memcpy(posted[n_posted++], (uint32_t[4]){0, ARG(1), ARG(2), ARG(3)}, 16);
    return 1;
}
/* QS_* flags of what is waiting: posted messages are QS_POSTMESSAGE (8), in both halves. */
WINAPI(GetQueueStatus, "GetQueueStatus", 1) { rt_pump_events(); return n_posted || quit_posted ? 0x00080008u : 0; }
static int next_message(uint32_t m, int remove) {'''),
    ('WINAPI(GetWindowRect, "GetWindowRect", 2) {',
     '''WINAPI(GetClientRect, "GetClientRect", 2) {
    uint32_t r[4] = {0, 0, rt_res_w, rt_res_h};
    memcpy(GPTR(ARG(1)), r, sizeof r);
    return 1;
}
/* WINDOWPLACEMENT (44 bytes): length, flags, showCmd, ptMinPosition, ptMaxPosition, rcNormalPosition. */
WINAPI(GetWindowPlacement, "GetWindowPlacement", 2) {
    uint32_t w[11] = {44, 0, 1, 0, 0, 0, 0, 0, 0, rt_res_w, rt_res_h};
    memcpy(GPTR(ARG(1)), w, sizeof w);
    return 1;
}
WINAPI(GetDesktopWindow, "GetDesktopWindow", 0) { return 0x00010000u; }
WINAPI(DestroyWindow, "DestroyWindow", 1) { call_wndproc(ARG(0), 2, 0, 0); return 1; }  /* WM_DESTROY */
WINAPI(UnregisterClassA, "UnregisterClassA", 2) { return 1; }
WINAPI(GetWindowRect, "GetWindowRect", 2) {'''),
])

# ---- kernel32.c: GetVersion, RtlUnwind, MsgWaitForMultipleObjects ----
sub('src/win32/kernel32.c', [
    ('''WINAPI(WaitForMultipleObjects, "WaitForMultipleObjects", 4) {
    uint32_t n = ARG(0), hs = ARG(1), all = ARG(2), ms = ARG(3);
''', '''static uint32_t wait_many(uint32_t n, uint32_t hs, uint32_t all, uint32_t ms) {
'''),
    ('''    pthread_mutex_unlock(&obj_mx);
    return WAIT_TIMEOUT;
}

WINAPI(CreateSemaphoreA''', '''    pthread_mutex_unlock(&obj_mx);
    return WAIT_TIMEOUT;
}
WINAPI(WaitForMultipleObjects, "WaitForMultipleObjects", 4) { return wait_many(ARG(0), ARG(1), ARG(2), ARG(3)); }

/* MsgWaitForMultipleObjects(n, handles, all, ms, mask): WAIT_OBJECT_0 + n when a message is waiting (user32.c's
 * queue), else as WaitForMultipleObjects, in slices so a message posted meanwhile is seen. */
int rt_messages_pending(void);
WINAPI(MsgWaitForMultipleObjects, "MsgWaitForMultipleObjects", 5) {
    uint32_t n = ARG(0), hs = ARG(1), all = ARG(2), ms = ARG(3);
    for (;;) {
        if (rt_messages_pending()) return n;
        uint32_t slice = ms == 0xFFFFFFFFu || ms > 4 ? 4 : ms;
        uint32_t r = WAIT_TIMEOUT;
        if (n) r = wait_many(n, hs, all, slice);
        else if (slice) usleep(slice * 1000);
        if (r != WAIT_TIMEOUT) return r;
        if (ms != 0xFFFFFFFFu) { if (ms <= slice) return WAIT_TIMEOUT; ms -= slice; }
    }
}

WINAPI(CreateSemaphoreA'''),
    ('WINAPI(GetVersionExA, "GetVersionExA", 1) {',
     '''WINAPI(GetVersion, "GetVersion", 0) { return 0x0A280105u; }  /* Windows XP 5.1 build 2600, NT */
/* RtlUnwind(target frame, target ip, exception record, return value): the CRT unwinds SEH frames with it when a
 * C++ exception is thrown. No frame here is registered, so there is nothing to unwind. */
WINAPI(RtlUnwind, "RtlUnwind", 4) { rt_log("RtlUnwind: ignored"); return 0; }
WINAPI(GetVersionExA, "GetVersionExA", 1) {'''),
])
print('edits applied')
