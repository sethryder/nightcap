// Nightcap: turns the monitors off when the session locks, and keeps them off.
// Build: make
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>

// Tuning knobs: real monitors and drivers vary.
#define REASSERT_MS  30000  // while locked, re-send monitor-off after this long with no input

#define WM_TRAY (WM_APP + 1)
#define TIMER_OFF 1
enum { IDI_TRAY_DARK = 2, IDI_TRAY_LIGHT };  // icon IDs in nightcap.rc; names say which taskbar they are for
enum { ID_ENABLED = 1, ID_AUTOSTART, ID_EXIT, ID_DELAY = 100 };  // ID_DELAY + index into DELAYS

// Lock -> first monitor-off. Chosen from the tray menu, saved in the registry.
static const struct { DWORD ms; const wchar_t *label; } DELAYS[] = {
    { 2000, L"2 seconds" }, { 5000, L"5 seconds" }, { 15000, L"15 seconds" },
    { 30000, L"30 seconds" }, { 60000, L"1 minute" }, { 300000, L"5 minutes" },
};
static DWORD delay_ms = 2000;

static const wchar_t APP[] = L"Nightcap";
static const wchar_t CFG_KEY[] = L"Software\\Nightcap";
static const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static BOOL enabled = TRUE, locked, first_off;
static NOTIFYICONDATAW nid;
static UINT wm_taskbar_created;

// Tray icon matching the taskbar: Windows records a light taskbar as SystemUsesLightTheme = 1.
static HICON tray_icon(void) {
    DWORD light = 0, size = sizeof light;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &light, &size);
    return LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(light ? IDI_TRAY_LIGHT : IDI_TRAY_DARK), IMAGE_ICON,
                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
}

static BOOL autostart(void) {
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP, RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}

static void toggle_autostart(void) {
    if (autostart()) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP);
        return;
    }
    wchar_t path[MAX_PATH + 3] = L"\"";
    DWORD n = GetModuleFileNameW(NULL, path + 1, MAX_PATH);
    path[n + 1] = L'"';
    path[n + 2] = 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP, REG_SZ, path, (n + 3) * sizeof(wchar_t));
}

static void tray_menu(HWND h) {
    POINT p;
    GetCursorPos(&p);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, enabled ? MF_CHECKED : 0, ID_ENABLED, L"Enabled");
    AppendMenuW(m, autostart() ? MF_CHECKED : 0, ID_AUTOSTART, L"Start at login");
    HMENU d = CreatePopupMenu();  // destroyed along with m
    for (int i = 0; i < (int)ARRAYSIZE(DELAYS); i++)
        AppendMenuW(d, DELAYS[i].ms == delay_ms ? MF_CHECKED : 0, ID_DELAY + i, DELAYS[i].label);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)d, L"Delay after lock");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, 0, ID_EXIT, L"Exit");
    SetForegroundWindow(h);  // without this the menu does not dismiss on click-away
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
    DestroyMenu(m);
    if (cmd == ID_ENABLED) enabled = !enabled;
    else if (cmd == ID_AUTOSTART) toggle_autostart();
    else if (cmd == ID_EXIT) DestroyWindow(h);
    else if (cmd >= ID_DELAY) {
        delay_ms = DELAYS[cmd - ID_DELAY].ms;
        RegSetKeyValueW(HKEY_CURRENT_USER, CFG_KEY, L"DelayMs", REG_DWORD, &delay_ms, sizeof delay_ms);
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_LOCK && enabled) {
            locked = first_off = TRUE;
            SetTimer(h, TIMER_OFF, delay_ms, NULL);
        } else if (w == WTS_SESSION_UNLOCK) {
            locked = FALSE;
            KillTimer(h, TIMER_OFF);
        }
        return 0;
    case WM_TIMER: {
        if (!locked || !enabled) { KillTimer(h, TIMER_OFF); return 0; }
        // The idle check keeps us from blanking the screen while someone is typing a password.
        LASTINPUTINFO li = { .cbSize = sizeof li };
        GetLastInputInfo(&li);
        if (first_off || GetTickCount() - li.dwTime >= REASSERT_MS)
            DefWindowProcW(h, WM_SYSCOMMAND, SC_MONITORPOWER, 2);  // 2 = off
        first_off = FALSE;
        SetTimer(h, TIMER_OFF, REASSERT_MS, NULL);
        return 0;
    }
    case WM_SETTINGCHANGE:  // covers a light/dark theme switch
        nid.hIcon = tray_icon();
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        break;
    case WM_TRAY:
        if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) tray_menu(h);
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        WTSUnRegisterSessionNotification(h);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == wm_taskbar_created) Shell_NotifyIconW(NIM_ADD, &nid);  // Explorer restarted
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    (void)prev; (void)cmdline; (void)show;
    CreateMutexW(NULL, FALSE, APP);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    DWORD size = sizeof delay_ms;  // stays at the default if the value is missing
    RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, L"DelayMs", RRF_RT_REG_DWORD, NULL, &delay_ms, &size);

    WNDCLASSW wc = { .lpfnWndProc = wndproc, .hInstance = inst, .lpszClassName = APP };
    RegisterClassW(&wc);
    // Hidden top-level window, not message-only: message-only windows miss the TaskbarCreated broadcast.
    HWND h = CreateWindowExW(0, APP, APP, 0, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!h || !WTSRegisterSessionNotification(h, NOTIFY_FOR_THIS_SESSION)) return 1;

    wm_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    nid.cbSize = sizeof nid;
    nid.hWnd = h;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = tray_icon();
    lstrcpyW(nid.szTip, APP);
    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) DispatchMessageW(&m);
    return 0;
}
