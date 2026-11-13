// Nightcap: turns the monitors off when the session locks, and keeps them off.
// Build: make
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <winhttp.h>

#ifndef VERSION
#define VERSION L"dev"  // release builds get the real version from the Makefile; dev builds never check for updates
#endif

// Tuning knobs: real monitors and drivers vary.
#define REASSERT_MS  30000  // while locked, re-send monitor-off after this long with no input

#define UPDATE_FIRST_MS 60000                  // first update check a minute after start, so the network is up
#define UPDATE_EVERY_MS (24 * 60 * 60 * 1000)  // then once a day

#define WM_TRAY (WM_APP + 1)
#define WM_UPDATE (WM_APP + 2)
enum { TIMER_OFF = 1, TIMER_UPDATE };
enum { IDI_TRAY_DARK = 2, IDI_TRAY_LIGHT };  // icon IDs in nightcap.rc; names say which taskbar they are for
enum { ID_ENABLED = 1, ID_AUTOSTART, ID_EXIT, ID_UPDATE, ID_UPDATE_CHECK, ID_DELAY = 100 };  // ID_DELAY + index into DELAYS

// Lock -> first monitor-off. Chosen from the tray menu, saved in the registry.
static const struct { DWORD ms; const wchar_t *label; } DELAYS[] = {
    { 2000, L"2 seconds" }, { 5000, L"5 seconds" }, { 15000, L"15 seconds" },
    { 30000, L"30 seconds" }, { 60000, L"1 minute" }, { 300000, L"5 minutes" },
};
static DWORD delay_ms = 2000;

static const wchar_t APP[] = L"Nightcap";
static const wchar_t CFG_KEY[] = L"Software\\Nightcap";
static const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t RELEASES_URL[] = L"https://github.com/sethryder/nightcap/releases/latest";

static BOOL enabled = TRUE, locked, first_off;
static NOTIFYICONDATAW nid;
static UINT wm_taskbar_created;
static DWORD update_check = 1;
static wchar_t found[32];   // written by the update thread
static wchar_t latest[32];  // newer version already announced; empty if none

// Is dotted version a newer than b? Compares number by number, so 0.10.0 > 0.9.0.
static BOOL newer(const wchar_t *a, const wchar_t *b) {
    for (;;) {
        wchar_t *ea, *eb;
        unsigned long x = wcstoul(a, &ea, 10), y = wcstoul(b, &eb, 10);
        if (x != y) return x > y;
        if (*ea != L'.' && *eb != L'.') return FALSE;
        a = *ea == L'.' ? ea + 1 : ea;
        b = *eb == L'.' ? eb + 1 : eb;
    }
}

// Asks GitHub which release is the latest. /releases/latest answers with a redirect to /releases/tag/vX.Y.Z,
// so the Location header is all we need: no download, no parsing of a response body.
// Nothing that comes back is ever run or opened. A newer version only raises a notification that links
// to the fixed RELEASES_URL, and the person installs it themselves.
static DWORD WINAPI update_thread(LPVOID hwnd) {
    wchar_t loc[512];
    DWORD len = sizeof loc, no_redirect = WINHTTP_DISABLE_REDIRECTS;
    HINTERNET s = WinHttpOpen(L"Nightcap/" VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"HEAD", L"/sethryder/nightcap/releases/latest", NULL, NULL, NULL,
                                         WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirect, sizeof no_redirect)
          && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)
          && WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, NULL, loc, &len, NULL)) {
        const wchar_t *v = wcsstr(loc, L"/releases/tag/v");
        if (v && newer(v += 15, VERSION)) {
            int i = 0;  // keep digits and dots only: this text is shown to the user
            for (; i < 31 && ((*v >= L'0' && *v <= L'9') || *v == L'.'); v++) found[i++] = *v;
            found[i] = 0;
            PostMessageW(hwnd, WM_UPDATE, 0, 0);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return 0;
}

static void open_releases(void) {
    ShellExecuteW(NULL, L"open", RELEASES_URL, NULL, NULL, SW_SHOWNORMAL);
}

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
    if (latest[0]) {
        wchar_t label[64];
        wsprintfW(label, L"Download version %s...", latest);
        AppendMenuW(m, 0, ID_UPDATE, label);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(m, enabled ? MF_CHECKED : 0, ID_ENABLED, L"Enabled");
    HMENU d = CreatePopupMenu();  // destroyed along with m
    for (int i = 0; i < (int)ARRAYSIZE(DELAYS); i++)
        AppendMenuW(d, DELAYS[i].ms == delay_ms ? MF_CHECKED : 0, ID_DELAY + i, DELAYS[i].label);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)d, L"Delay after lock");
    AppendMenuW(m, autostart() ? MF_CHECKED : 0, ID_AUTOSTART, L"Start at login");
    AppendMenuW(m, update_check ? MF_CHECKED : 0, ID_UPDATE_CHECK, L"Check for updates");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, 0, ID_EXIT, L"Exit");
    SetForegroundWindow(h);  // without this the menu does not dismiss on click-away
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
    DestroyMenu(m);
    if (cmd == ID_ENABLED) enabled = !enabled;
    else if (cmd == ID_AUTOSTART) toggle_autostart();
    else if (cmd == ID_EXIT) DestroyWindow(h);
    else if (cmd == ID_UPDATE) open_releases();
    else if (cmd == ID_UPDATE_CHECK) {
        update_check = !update_check;
        RegSetKeyValueW(HKEY_CURRENT_USER, CFG_KEY, L"UpdateCheck", REG_DWORD, &update_check, sizeof update_check);
    }
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
        if (w == TIMER_UPDATE) {
            SetTimer(h, TIMER_UPDATE, UPDATE_EVERY_MS, NULL);
            HANDLE t = update_check ? CreateThread(NULL, 0, update_thread, h, 0, NULL) : NULL;
            if (t) CloseHandle(t);
            return 0;
        }
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
        else if (l == NIN_BALLOONUSERCLICK) open_releases();
        return 0;
    case WM_UPDATE:
        if (lstrcmpW(latest, found)) {  // announce each new version once per run; the menu item stays
            lstrcpyW(latest, found);
            NOTIFYICONDATAW n = nid;  // a copy, so later icon updates do not show the notification again
            n.uFlags |= NIF_INFO;
            n.dwInfoFlags = NIIF_INFO;
            lstrcpyW(n.szInfoTitle, L"Nightcap update available");
            wsprintfW(n.szInfo, L"Version %s is ready. Click here to download it.", latest);
            Shell_NotifyIconW(NIM_MODIFY, &n);
        }
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

#ifdef SELFTEST  // make test: checks the version comparison, then does one real update check
#include <stdio.h>
int main(void) {
    static const struct { const wchar_t *a, *b; BOOL want; } cases[] = {
        { L"0.2.0", L"0.1.0", TRUE }, { L"0.1.0", L"0.1.0", FALSE }, { L"0.1.0", L"0.2.0", FALSE },
        { L"0.10.0", L"0.9.0", TRUE }, { L"1.0", L"1.0.1", FALSE }, { L"1.0.1", L"1.0", TRUE },
        { L"0.1.0\r\n", L"0.1.0", FALSE }, { L"junk", L"0.1.0", FALSE },
    };
    for (int i = 0; i < (int)ARRAYSIZE(cases); i++)
        if (newer(cases[i].a, cases[i].b) != cases[i].want) { printf("FAIL newer(%ls, %ls)\n", cases[i].a, cases[i].b); return 1; }
    update_thread(NULL);
    printf("version comparison ok; running %ls, newer release found: %ls\n", VERSION, found[0] ? found : L"none");
    (void)wndproc;
    return 0;
}
#else
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    (void)prev; (void)cmdline; (void)show;
    CreateMutexW(NULL, FALSE, APP);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    DWORD size = sizeof delay_ms;  // stays at the default if the value is missing
    RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, L"DelayMs", RRF_RT_REG_DWORD, NULL, &delay_ms, &size);
    size = sizeof update_check;
    RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, L"UpdateCheck", RRF_RT_REG_DWORD, NULL, &update_check, &size);

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
    lstrcpyW(nid.szTip, L"Nightcap " VERSION);
    Shell_NotifyIconW(NIM_ADD, &nid);
    if (VERSION[0] >= L'0' && VERSION[0] <= L'9') SetTimer(h, TIMER_UPDATE, UPDATE_FIRST_MS, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) DispatchMessageW(&m);
    return 0;
}
#endif
