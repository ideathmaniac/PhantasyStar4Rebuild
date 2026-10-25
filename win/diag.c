/*
 * Crash and hang reports for the Windows build.
 *
 * - A vectored exception handler sees faults before anything else (also
 *   inside window procedures, where Windows may otherwise swallow them), and
 *   the unhandled-exception filter catches the rest. Stack overflows get a
 *   reserved 64 KB to run in, and the report itself is written by a helper
 *   thread with a fresh stack, using only kernel32 calls and static buffers.
 * - crash.txt: exception, faulting module+offset, registers, the 68000 pc,
 *   return addresses found on the stack (exe offsets; build/win/PS4.map
 *   turns them into function names), settings and the last log lines.
 *   crash.dmp next to it (dbghelp minidump) if possible.
 * - A watchdog thread writes hang.txt when the main loop stops for 8 s.
 * - If the exe folder is not writable, the files go to the Desktop.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "win32.h"
#include "diag.h"

extern uint32_t cpu_cur_pc;

const char *(*diag_info)(void);
static wchar_t base_dir[700];
static char version_str[32];
static HWND main_hwnd;
static DWORD main_tid;
static HANDLE main_thread;
static uintptr_t main_stack_base;
static uintptr_t exe_lo, exe_hi;
static volatile uint64_t hb_ms, hb_frame;
static volatile int paused, crashed;

/* last log lines */
#define NOTES 32
static char notes[NOTES][160];
static volatile int note_n;

void diag_note(const char *line)
{
    int i = note_n++ % NOTES;
    strncpy(notes[i], line, sizeof notes[i] - 1);
    notes[i][sizeof notes[i] - 1] = 0;
}

static uint64_t ms(void) { return GetTickCount64(); }
static uintptr_t teb_stack_base(void) { uintptr_t v; __asm__ volatile("mov %%gs:8, %0" : "=r"(v)); return v; }

/* ---------- report text ---------- */
static char rep[24576];
static int rlen;
static void rp(const char *fmt, ...)
{
    if (rlen >= (int)sizeof rep - 2) return;
    va_list a; va_start(a, fmt);
    int n = _vsnprintf(rep + rlen, sizeof rep - 1 - rlen, fmt, a);
    va_end(a);
    if (n < 0 || n > (int)sizeof rep - 1 - rlen) n = (int)sizeof rep - 1 - rlen;
    rlen += n; rep[rlen] = 0;
}

static void module_of(uintptr_t a, char *out, int n, uintptr_t *off)
{
    HMODULE m = 0;
    out[0] = 0; *off = a;
    if (a >= exe_lo && a < exe_hi) { strncpy(out, "PS4.exe", n); *off = a - exe_lo; return; }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m) || !m) {
        strncpy(out, "?", n); return;
    }
    wchar_t w[300]; DWORD k = GetModuleFileNameW(m, w, 299); w[k] = 0;
    wchar_t *s = w; for (wchar_t *p = w; *p; p++) if (*p == L'\\' || *p == L'/') s = p + 1;
    WideCharToMultiByte(65001, 0, s, -1, out, n, 0, 0);
    *off = a - (uintptr_t)m;
}

static void scan_stack(uintptr_t sp, uintptr_t top)
{
    if (!sp || top <= sp) return;
    if (top - sp > 256 * 1024) top = sp + 256 * 1024;
    int shown = 0, other = 0;
    rp("\nreturn addresses on the stack (newest first):\n");
    for (uintptr_t p = sp & ~(uintptr_t)7; p + 8 <= top && shown < 48; p += 8) {
        uintptr_t v = *(uintptr_t *)p;
        if (v >= exe_lo + 0x1000 && v < exe_hi) { rp("  PS4.exe+%llX\n", (unsigned long long)(v - exe_lo)); shown++; }
        else if (other < 6 && v > 0x10000 && (v >> 40) == 0x7F) {
            char mn[80]; uintptr_t off; module_of(v, mn, sizeof mn, &off);
            if (mn[0] && mn[0] != '?') { rp("  %s+%llX\n", mn, (unsigned long long)off); other++; shown++; }
        }
    }
}

static int write_to(const wchar_t *dir, const wchar_t *name, const char *data, int n, wchar_t *path_out)
{
    wchar_t p[800]; wcscpy(p, dir); wcscat(p, name);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0; BOOL ok = WriteFile(h, data, (DWORD)n, &w, 0);
    FlushFileBuffers(h); CloseHandle(h);
    if (path_out) wcscpy(path_out, p);
    return ok && (int)w == n;
}

static void desktop_dir(wchar_t *out)
{
    out[0] = 0;
    DWORD k = GetEnvironmentVariableW(L"USERPROFILE", out, 600);
    if (!k || k > 600) { out[0] = 0; return; }
    wcscat(out, L"\\Desktop\\");
}

/* writes name (or PS4_name on the Desktop); returns where it went */
static int save_report(const wchar_t *name, wchar_t *path_out)
{
    if (write_to(base_dir, name, rep, rlen, path_out)) return 1;
    wchar_t d[700]; desktop_dir(d);
    wchar_t n2[64]; wcscpy(n2, L"PS4_"); wcscat(n2, name);
    if (d[0] && write_to(d, n2, rep, rlen, path_out)) return 1;
    return 0;
}

static void report_head(const char *what)
{
    rlen = 0; rep[0] = 0;
    rp("Phantasy Star IV recomp %s — %s\n", version_str, what);
    rp("68000 pc %06X, frame %llu, main loop last seen %llu ms ago\n", cpu_cur_pc, (unsigned long long)hb_frame,
       (unsigned long long)(ms() - hb_ms));
    if (diag_info) rp("settings: %s\n", diag_info());
}

static void report_tail(void)
{
    rp("\nlast log lines:\n");
    int n = note_n, first = n > NOTES ? n - NOTES : 0;
    for (int i = first; i < n; i++) rp("  %s\n", notes[i % NOTES]);
}

/* ---------- crash ---------- */
typedef struct __attribute__((packed, aligned(4))) { DWORD tid; EXCEPTION_POINTERS *ep; BOOL client; } MDEI;
typedef BOOL (*mdwd_t)(HANDLE, DWORD, HANDLE, int, MDEI *, void *, void *);

static EXCEPTION_POINTERS *crash_ep;
static DWORD crash_tid;
static uintptr_t crash_stack_top;
static wchar_t crash_path[800];
static int crash_saved, dump_saved;
static volatile uint64_t msgbox_done;

static const char *code_name(DWORD c)
{
    switch (c) {
    case 0xC0000005: return "access violation";
    case 0xC00000FD: return "stack overflow";
    case 0xC000001D: return "illegal instruction";
    case 0xC0000094: return "integer divide by zero";
    case 0xC0000096: return "privileged instruction";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun / fast fail";
    case 0xC0000006: return "in-page error";
    case 0x80000003: return "breakpoint";
    default: return "exception";
    }
}

static DWORD report_thread(void *arg)
{
    (void)arg;
    EXCEPTION_RECORD *er = crash_ep->ExceptionRecord;
    uint8_t *ctx = (uint8_t *)crash_ep->ContextRecord;
    uintptr_t addr = (uintptr_t)er->ExceptionAddress, off;
    char mn[80]; module_of(addr, mn, sizeof mn, &off);
    report_head("CRASH");
    rp("\nexception %08lX (%s) in %s+%llX, thread %s\n", (unsigned long)er->ExceptionCode, code_name(er->ExceptionCode), mn,
       (unsigned long long)off, crash_tid == main_tid ? "main" : "other");
    if (er->NumberParameters >= 2 && er->ExceptionCode == 0xC0000005)
        rp("%s of address %llX\n", er->ExceptionInformation[0] == 1 ? "write" : er->ExceptionInformation[0] == 8 ? "execute" : "read",
           (unsigned long long)er->ExceptionInformation[1]);
    if (ctx) {
        static const char *rn[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
        for (int i = 0; i < 16; i++) rp("%s=%016llX%s", rn[i], (unsigned long long)*(uint64_t *)(ctx + 0x78 + i * 8), i % 4 == 3 ? "\n" : "  ");
        rp("rip=%016llX\n", (unsigned long long)*(uint64_t *)(ctx + 0xF8));
        scan_stack((uintptr_t)*(uint64_t *)(ctx + 0x98), crash_stack_top);
    }
    report_tail();
    crash_saved = save_report(L"crash.txt", crash_path);

    wchar_t msg[1200];
    if (crash_saved)
        _snwprintf(msg, 1199, L"Программа аварийно завершилась.\n\nОтчёт записан сюда:\n%s\n\nПришлите его, пожалуйста (и crash.dmp, если он появится рядом).", crash_path);
    else
        wcscpy(msg, L"Программа аварийно завершилась, но записать отчёт не удалось (папка недоступна для записи).");
    MessageBoxW(0, msg, L"Phantasy Star IV", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    msgbox_done = ms();

    /* minidump, best effort (last: loading dbghelp can block if the crash
     * happened while Windows was loading a library) */
    HMODULE dh = LoadLibraryW(L"dbghelp.dll");
    mdwd_t mdwd = dh ? (mdwd_t)GetProcAddress(dh, "MiniDumpWriteDump") : 0;
    if (mdwd && crash_saved) {
        wchar_t dp[800]; wcscpy(dp, crash_path);
        size_t L = wcslen(dp); if (L > 4) wcscpy(dp + L - 4, L".dmp");
        HANDLE h = CreateFileW(dp, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h != INVALID_HANDLE_VALUE) {
            MDEI ei; ei.tid = crash_tid; ei.ep = crash_ep; ei.client = 0;
            dump_saved = mdwd(GetCurrentProcess(), GetCurrentProcessId(), h, 0x40 /* with indirectly referenced memory */, &ei, 0, 0);
            CloseHandle(h);
        }
    }
    return 0;
}

static void crash_report(EXCEPTION_POINTERS *ep)
{
    if (crashed) { TerminateProcess(GetCurrentProcess(), 0xDEAD); }
    crashed = 1;
    crash_ep = ep;
    crash_tid = GetCurrentThreadId();
    crash_stack_top = teb_stack_base();
    /* the report runs on a fresh stack (this one may be overflowed) */
    HANDLE t = CreateThread(0, 1 << 20, report_thread, 0, 0, 0);
    if (!t) report_thread(0);
    else
        while (WaitForSingleObject(t, 200) != 0)
            if (msgbox_done && ms() - msgbox_done > 15000) break;
    TerminateProcess(GetCurrentProcess(), 0xDEAD);
}

static int in_exe(uintptr_t a) { return a >= exe_lo && a < exe_hi; }

static LONG veh(EXCEPTION_POINTERS *ep)
{
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    /* our code never handles exceptions itself, so a fault in it is fatal;
     * faults elsewhere may be handled by their own code (drivers do that) */
    int fatal = c == 0xC00000FD || c == 0xC0000374 || c == 0xC0000409 ||
                (in_exe(a) && (c == 0xC0000005 || c == 0xC000001D || c == 0xC0000094 || c == 0xC0000096 || c == 0xC0000006));
    if (fatal) crash_report(ep);
    return 0;   /* EXCEPTION_CONTINUE_SEARCH */
}

static LONG unhandled(EXCEPTION_POINTERS *ep)
{
    crash_report(ep);
    return 1;
}

/* ---------- hang watchdog ---------- */
static DWORD watchdog(void *arg)
{
    (void)arg;
    diag_thread_init();
    int reported = 0;
    uint64_t hang_from = 0;
    for (;;) {
        Sleep(500);
        if (crashed) continue;
        uint64_t age = ms() - hb_ms;
        if (paused || !hb_ms) { reported = 0; continue; }
        if (age < 8000) {
            if (reported) {
                char b[100]; _snprintf(b, 99, "main loop resumed after a %llu s stop", (unsigned long long)((ms() - hang_from) / 1000));
                diag_note(b);
            }
            reported = 0;
            continue;
        }
        if (reported) continue;
        reported = 1;
        hang_from = hb_ms;
        report_head("HANG (the main loop has not run for 8 seconds)");
        static __attribute__((aligned(16))) uint8_t ctx[0x4D0];
        memset(ctx, 0, sizeof ctx);
        *(DWORD *)(ctx + 0x30) = 0x100003;   /* CONTEXT_AMD64 | CONTROL | INTEGER */
        if (main_thread && SuspendThread(main_thread) != (DWORD)-1) {
            if (GetThreadContext(main_thread, ctx)) {
                uintptr_t rip = (uintptr_t)*(uint64_t *)(ctx + 0xF8), off; char mn[80];
                module_of(rip, mn, sizeof mn, &off);
                rp("\nmain thread is at %s+%llX\n", mn, (unsigned long long)off);
                scan_stack((uintptr_t)*(uint64_t *)(ctx + 0x98), main_stack_base);
            }
            ResumeThread(main_thread);
        }
        report_tail();
        wchar_t p[800];
        save_report(L"hang.txt", p);
    }
    return 0;
}

/* ---------- setup ---------- */
void diag_thread_init(void)
{
    ULONG g = 64 * 1024;
    SetThreadStackGuarantee(&g);
}

void diag_init(const wchar_t *dir, const char *version)
{
    wcscpy(base_dir, dir);
    strncpy(version_str, version, sizeof version_str - 1);
    uint8_t *b = (uint8_t *)GetModuleHandleW(0);
    exe_lo = (uintptr_t)b;
    uint32_t pe = *(uint32_t *)(b + 0x3C);
    exe_hi = exe_lo + *(uint32_t *)(b + pe + 24 + 56);
    main_tid = GetCurrentThreadId();
    main_stack_base = teb_stack_base();
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread, 0, 0, DUPLICATE_SAME_ACCESS);
    diag_thread_init();
    AddVectoredExceptionHandler(1, veh);
    SetUnhandledExceptionFilter(unhandled);
    CreateThread(0, 0, watchdog, 0, 0, 0);
}

void diag_set_window(void *hwnd) { main_hwnd = (HWND)hwnd; }
void diag_heartbeat(uint64_t frame) { hb_ms = ms(); hb_frame = frame; }
void diag_pause(int on) { if (on) paused++; else if (paused > 0) paused--; hb_ms = ms(); }
