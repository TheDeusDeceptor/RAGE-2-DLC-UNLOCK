/*
 * RAGE 2 - DLC entitlement fix v1.0
 *
 * Config: rage2_dlcfix.ini next to RAGE2.exe (created on first run).
 *
 * Build (MinGW-w64):
 *   x86_64-w64-mingw32-gcc -shared -O2 -o XINPUT9_1_0.dll rage2_dlcfix.c -static-libgcc -Wl,--kill-at
 *
 * Offsets are for the shipped RAGE2.exe (v1.09) and are verified by byte signature at
 * startup; on any mismatch the DLL does nothing.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

/* ---- game offsets (RVA = VA - 0x140000000) ---- */
#define RVA_UI_FUNC    0x00B2C550u  /* UI refresh, sends SetDLCEntitlements to the HUD   */
#define RVA_SETTER     0x00737BB0u  /* CEntitlements: grant(entitlementHash, flag)       */
#define RVA_SINGLETON  0x02D7D630u  /* global pointer to the entitlement object          */

#define ENT_RISE_OF_THE_GHOSTS 0xAB7C8093u /* entitlement hash handled by RVA_SETTER     */
#define KEY_RISE_OF_THE_GHOSTS 0x14AF76B7u /* map key it writes (value 2 = owned)        */

static const uint8_t SIG_UI[18] = {0x48,0x8B,0xC4,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0x78,0xFE,0xFF,0xFF};
static const uint8_t SIG_SETTER[24] = {0x48,0x89,0x5C,0x24,0x08,0x44,0x88,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x30};

typedef uint64_t (__fastcall *UiFn)(void*, void*, void*, void*);
typedef void     (__fastcall *SetterFn)(void*, uint32_t, uint8_t);

static uint8_t *g_base;
static UiFn     g_orig;
static SetterFn g_setter;
static char     g_logpath[MAX_PATH];

static void logf_(const char *fmt, ...)
{
    FILE *f = fopen(g_logpath, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

/* read-only lookup in the game's std::map<uint32,uint8> (same walk the game does) */
static int map_lookup(void *obj, uint32_t key)
{
    uint8_t *head = *(uint8_t**)((uint8_t*)obj + 0x20);
    if (!head) return -2;
    uint8_t *node = *(uint8_t**)(head + 8), *best = head;
    int guard = 0;
    while (node && !node[0x19] && guard++ < 4096) {
        if (*(uint32_t*)(node + 0x1C) < key) node = *(uint8_t**)(node + 0x10);
        else { best = node; node = *(uint8_t**)node; }
    }
    if (best == head || key < *(uint32_t*)(best + 0x1C)) return -1;
    return best[0x20];
}

static volatile LONG g_busy;

/* entitlement hashes accepted by the game's grant function (RVA_SETTER) */
#define ENT_TERRORMANIA        0x12D5B685u
#define KEY_TERRORMANIA        0x8854B50Du
static const uint32_t OTHER_ENTS[] = {
    0x36BCFA14u, 0xB274007Bu, 0x64FC1C53u, 0x9E59175Fu, 0xA3335C07u,
    0xB64C1BC1u, 0xD00E76C4u, 0xEB91533Du, 0xEC0A4760u
};

static int g_cfg_rotg = 1, g_cfg_terror = 1, g_cfg_other = 1, g_others_done = 0;


static void load_config(void)
{
    char ini[MAX_PATH];
    strcpy(ini, g_logpath);
    char *s = strrchr(ini, '\\'); if (s) *(s + 1) = 0;
    strcat(ini, "rage2_dlcfix.ini");
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(ini, "w");
        if (f) {
            fputs("[DLC]\n"
                  "; 1 = mark as owned, 0 = leave to the game\n"
                  "RiseOfTheGhosts=1\n"
                  "Terrormania=1\n"
                  "; other entitlement items the game knows about (Deluxe / pre-order style items)\n"
                  "OtherItems=1\n", f);
            fclose(f);
        }
    }
    g_cfg_rotg   = GetPrivateProfileIntA("DLC", "RiseOfTheGhosts", 1, ini);
    g_cfg_terror = GetPrivateProfileIntA("DLC", "Terrormania", 1, ini);
    g_cfg_other  = GetPrivateProfileIntA("DLC", "OtherItems", 1, ini);
    logf_("Config: RiseOfTheGhosts=%d Terrormania=%d OtherItems=%d", g_cfg_rotg, g_cfg_terror, g_cfg_other);
}

static void try_grant(void)
{
    void *ent = *(void**)(g_base + RVA_SINGLETON);
    if (!ent) return;
    if (g_cfg_rotg && map_lookup(ent, KEY_RISE_OF_THE_GHOSTS) != 2) {
        g_setter(ent, ENT_RISE_OF_THE_GHOSTS, 0);
        logf_("Granted Rise of the Ghosts (state now %d).", map_lookup(ent, KEY_RISE_OF_THE_GHOSTS));
    }
    if (g_cfg_terror && map_lookup(ent, KEY_TERRORMANIA) != 2) {
        g_setter(ent, ENT_TERRORMANIA, 0);
        logf_("Granted Terrormania (state now %d).", map_lookup(ent, KEY_TERRORMANIA));
    }
    if (g_cfg_other && !g_others_done) {
        for (size_t i = 0; i < sizeof OTHER_ENTS / sizeof OTHER_ENTS[0]; i++) g_setter(ent, OTHER_ENTS[i], 0);
        g_others_done = 1;
        logf_("Granted other entitlement items.");
    }
}

static uint64_t __fastcall detour(void *a, void *b, void *c, void *d)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
        try_grant();
        InterlockedExchange(&g_busy, 0);
    }
    return g_orig(a, b, c, d);
}


static int install_hook(void)
{
    uint8_t *target = g_base + RVA_UI_FUNC;
    g_setter = (SetterFn)(g_base + RVA_SETTER);
    if (memcmp(target, SIG_UI, sizeof SIG_UI) || memcmp(g_base + RVA_SETTER, SIG_SETTER, sizeof SIG_SETTER)) {
        logf_("Signature mismatch: different RAGE2.exe build. Doing nothing.");
        return 0;
    }
    uint8_t *tramp = (uint8_t*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return 0;
    memcpy(tramp, target, 18);
    tramp[18] = 0xFF; tramp[19] = 0x25; *(uint32_t*)(tramp + 20) = 0;
    *(uint64_t*)(tramp + 24) = (uint64_t)(target + 18);
    g_orig = (UiFn)tramp;

    DWORD old;
    if (!VirtualProtect(target, 18, PAGE_EXECUTE_READWRITE, &old)) return 0;
    target[0] = 0xFF; target[1] = 0x25; *(uint32_t*)(target + 2) = 0;
    *(uint64_t*)(target + 6) = (uint64_t)detour;
    memset(target + 14, 0x90, 4);
    VirtualProtect(target, 18, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 18);
    logf_("Hook installed.");
    return 1;
}

/* ---- XInput 9.1.0 proxy: forward to the real system DLL ---- */
static HMODULE g_real; static FARPROC g_get, g_set;
static void load_real(void)
{
    if (g_real) return;
    char p[MAX_PATH]; GetSystemDirectoryA(p, MAX_PATH);
    strcat(p, "\\XINPUT9_1_0.dll");
    g_real = LoadLibraryA(p);
    if (g_real) { g_get = GetProcAddress(g_real, "XInputGetState"); g_set = GetProcAddress(g_real, "XInputSetState"); }
}
__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD i, void *s)
{ load_real(); return g_get ? ((DWORD (WINAPI*)(DWORD, void*))g_get)(i, s) : 1167; }
__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD i, void *v)
{ load_real(); return g_set ? ((DWORD (WINAPI*)(DWORD, void*))g_set)(i, v) : 1167; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_base = (uint8_t*)GetModuleHandleA(NULL);
        GetModuleFileNameA(NULL, g_logpath, MAX_PATH);
        char *s = strrchr(g_logpath, '\\'); if (s) *(s + 1) = 0;
        strcat(g_logpath, "rage2_dlcfix.log");
        remove(g_logpath);
        load_config();
        install_hook();
    }
    return TRUE;
}
