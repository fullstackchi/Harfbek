// =====================================================================
//  Harfbek  -  o'zbek maxsus harflarini oson yozish (Windows)
//
//  Trigger (standart: AltGr = o'ng Alt) + harf:
//     AltGr+o -> ö   AltGr+g -> ğ   AltGr+s -> ş   AltGr+c -> ç   (Shift/Caps -> katta)
//
//  Interfeys: uz / ru / eng. Tray menyu orqali boshqariladi.
//  Xavfsizlik: hech narsa yozib olinmaydi/saqlanmaydi, internetga chiqmaydi.
// =====================================================================
#include "app.h"
#include <shellapi.h>
#include <commctrl.h>   // InitCommonControlsEx

#pragma optimize("s", on)   // tray/menyu — hajm uchun (exe budjeti; ui_gfx.cpp ga qarang)

HINSTANCE g_hInst = NULL;
HWND      g_hWnd  = NULL;

static NOTIFYICONDATAW g_nid = {};
static HICON           g_iconBig   = NULL;
static HICON           g_iconSmall = NULL;

// Explorer qayta ishga tushganda (yoki taskbar qayta yaratilganda) barcha
// oynalarga yuboriladigan xabar — tray ikonkani qayta qo'shish kerak.
static UINT g_wmTaskbarCreated = 0;

// Tray hali tayyor bo'lmasa (Run orqali login paytida Explorer kechikadi)
// NIM_ADD ni har soniyada qayta urinamiz; TaskbarCreated kelsa u ham tiklaydi.
static const int kTrayRetryMax = 30;
static int       g_trayRetries = 0;

// Biror popup menyumiz ochiq (TrackPopupMenu) — ikkinchisini ochmaymiz
static bool g_menuOpen = false;

// Tooltip matni: S_TOOLTIP dagi %s — trigger keycap nomi (contracts.md).
// %s bo'lmagan satr ham xavfsiz (ortiqcha argument e'tiborsiz qoladi).
// wsprintfW 1024 belgidan ko'p yozmaydi — bufer yetarli, keyin szTip ga kesamiz.
// Avto rejimda trigger ishlamaydi — S_TOOLTIP_AUTO formatlashsiz.
static void FormatTrayTip()
{
    if (g_settings.mode == MODE_AUTO) {
        lstrcpynW(g_nid.szTip, T(S_TOOLTIP_AUTO), 128);
        return;
    }
    wchar_t cap[32];
    TriggerKeyCap(g_settings.triggerVk, cap, 32);
    wchar_t buf[1024];
    wsprintfW(buf, T(S_TOOLTIP), cap);
    lstrcpynW(g_nid.szTip, buf, 128);
}

// Holat ikonkasi (Ui_MakeStateIcon): avto — "A" badge, o'chiq — kulrang.
// Faqat holat o'zgarganda yasaladi; trigger + yoqiq — asl g_iconSmall.
static HICON g_iconState = NULL;
static int   g_iconKey   = -1;   // bit 0 — avto, bit 1 — yoqiq (-1 — yasalmagan)

// g_nid.hIcon ni joriy holatga moslaydi. Qaytaradi — eski yasalgan ikonka:
// tray yangisini olgandan KEYIN DestroyIcon qilinadi (yoki NULL).
static HICON PrepareStateIcon()
{
    bool autoMode = g_settings.mode == MODE_AUTO;
    int  key = (autoMode ? 1 : 0) | (g_settings.enabled ? 2 : 0);
    if (key == g_iconKey) return NULL;
    HICON ic  = (key == 2) ? NULL : Ui_MakeStateIcon(g_iconSmall, autoMode, !g_settings.enabled);
    HICON old = g_iconState;
    g_iconState = ic;
    g_iconKey   = (ic || key == 2) ? key : -1;   // yasalmasa keyingi safar yana urinamiz
    g_nid.hIcon = ic ? ic : g_iconSmall;
    return old;
}

void Tray_UpdateIcon()
{
    HICON old = PrepareStateIcon();
    g_nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

// Istisno so'zlarga qo'shib saqlaydi (words — ';' bilan; kichik harfga va
// takrorsizlikka Settings_SetSkipWords keltiradi).
static void AddSkipWords(const wchar_t* words)
{
    wchar_t list[1024 + 4 * 40];
    Settings_GetSkipWords(list, 1024);
    if (list[0]) lstrcatW(list, L";");
    lstrcatW(list, words);
    Settings_SetSkipWords(list);
    Settings_Save();
}

// Popup'da "Qöşiş" bosilgan, lekin o'sha payt Sozlamalar ochiq edi: uning lokal
// ro'yxati saqlanganda ustidan yozardi (R-13). So'zlar shu yerda kutadi va
// Sozlamalar yopilgach (saqlansa ham, bekor qilinsa ham) qo'shiladi. Heap'siz,
// ~4 so'z; sig'masa ortiqchasi tashlanadi.
static wchar_t g_learnPend[4 * 40];

static void FlushLearnPending()
{
    if (!g_learnPend[0] || SettingsUi_IsOpen()) return;
    AddSkipWords(g_learnPend);
    g_learnPend[0] = 0;
}

// Tray yozuvini (tooltip) joriy tilda va joriy trigger bilan yangilaydi
// (ikonka ham — u chaqiriladigan joylarda holat o'zgargan bo'lishi mumkin).
// Sozlamalar yopilgandan keyin ham chaqiriladi — kutayotgan so'zlar shu yerda.
static void UpdateTrayTip()
{
    FlushLearnPending();
    FormatTrayTip();
    HICON old = PrepareStateIcon();
    g_nid.uFlags = NIF_TIP | NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

// Tray ikonkani qo'shadi. Ikonka allaqachon bo'lsa (masalan, DPI o'zgarganda
// ham TaskbarCreated keladi) — NIM_MODIFY bilan yangilaydi.
static bool AddTrayIcon()
{
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    FormatTrayTip();
    return Shell_NotifyIconW(NIM_ADD, &g_nid) ||
           Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// Dastur administrator sifatida ishlasa, UIPI o'rta huquqdagi Explorer'ning
// TaskbarCreated xabarini to'sadi — uni ruxsat etilganlar ro'yxatiga qo'shamiz.
// ChangeWindowMessageFilterEx (Win7+) eski MinGW headerlarida bo'lmasligi
// mumkin, shuning uchun user32.dll dan dinamik olinadi; topilmasa — jim o'tamiz.
static void AllowTaskbarCreated(HWND hWnd)
{
    typedef BOOL (WINAPI *ChangeFilterExFn)(HWND, UINT, DWORD, void*);
    const DWORD kMsgFltAllow = 1;   // MSGFLT_ALLOW
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    ChangeFilterExFn fn = u32 ? (ChangeFilterExFn)(void*)
                          GetProcAddress(u32, "ChangeWindowMessageFilterEx") : NULL;
    if (fn && g_wmTaskbarCreated) fn(hWnd, g_wmTaskbarCreated, kMsgFltAllow, NULL);
}

// Ikonka qo'shilmasa, qayta urinish taymerini ishga tushiradi
static void AddTrayIconOrRetry(HWND hWnd)
{
    if (AddTrayIcon()) {
        KillTimer(hWnd, TRAY_RETRY_TIMER);
        return;
    }
    g_trayRetries = 0;
    SetTimer(hWnd, TRAY_RETRY_TIMER, 1000, NULL);
}

// Modal oyna (Sozlamalar, Qo'llanma, MessageBox...) ochiq bo'lsa, u g_hWnd ni
// o'chirib (disable) qo'yadi. Shunda yangi menyu ochmaymiz — ochiq oynani
// oldinga chiqaramiz. Aks holda Sozlamalar ikki nusxada ochilib, umumiy
// static holatni buzadi.
static bool BringModalToFront(HWND hWnd)
{
    if (IsWindowEnabled(hWnd)) return false;
    HWND pop = GetLastActivePopup(hWnd);
    if (pop && pop != hWnd) {
        if (IsIconic(pop)) ShowWindow(pop, SW_RESTORE);   // Sozlamalar minimallashtirilgan bo'lishi mumkin
        SetForegroundWindow(pop);
    }
    return true;
}

static void SetLanguage(Lang l)
{
    g_settings.lang = l;
    Settings_Save();
    UpdateTrayTip();
}

// Rejim / yoqish o'zgarishi — tray menyu, tezkor tugma va flyout shu ikki
// funksiyadan o'tadi: saqlash, hook holatini tozalash (trigger va avto bufer
// yangi holatga o'tmasin), tooltip, ochiq flyout, so'ng (yoqilgan bo'lsa)
// ekrandagi bildirishnoma.
void ToggleMode()
{
    g_settings.mode = (g_settings.mode == MODE_AUTO) ? MODE_TRIGGER : MODE_AUTO;
    Settings_Save();
    Hook_ResetTriggerState();
    UpdateTrayTip();
    Flyout_Refresh();
    if (g_settings.notify)
        Osd_Show(T(g_settings.mode == MODE_AUTO ? S_OSD_MODE_AUTO : S_OSD_MODE_TRIGGER));
}

void ToggleEnabled()
{
    g_settings.enabled = !g_settings.enabled;
    Settings_Save();
    Hook_ResetTriggerState();
    UpdateTrayTip();
    Flyout_Refresh();
    if (g_settings.notify)
        Osd_Show(T(g_settings.enabled ? S_OSD_ENABLED : S_OSD_DISABLED));
}

// Tray ikonkasining ekrandagi markazi (flyout shu yerga yopishadi).
// Shell_NotifyIconGetRect (Win7+) eski MinGW headerlarida bo'lmasligi mumkin —
// dinamik; topilmasa sichqoncha joyi.
static POINT TrayAnchor(HWND hWnd)
{
    struct NotifyIconId { DWORD cbSize; HWND hWnd; UINT uID; GUID guidItem; };
    typedef HRESULT (WINAPI *GetRectFn)(const NotifyIconId*, RECT*);
    POINT pt; GetCursorPos(&pt);
    HMODULE sh = GetModuleHandleW(L"shell32.dll");
    GetRectFn fn = sh ? (GetRectFn)(void*)GetProcAddress(sh, "Shell_NotifyIconGetRect") : NULL;
    NotifyIconId id = { sizeof(id), hWnd, g_nid.uID, {} };
    RECT r;
    if (fn && SUCCEEDED(fn(&id, &r))) {
        pt.x = (r.left + r.right) / 2;
        pt.y = (r.top + r.bottom) / 2;
    }
    return pt;
}

// Mavzu (yorug'/qorong'i, yuqori kontrast) o'zgardi: ranglar qayta o'qiladi,
// thread'imizning barcha ochiq oynalari WM_THEMECHANGED bilan qayta chiziladi.
static BOOL CALLBACK ThemeChangedProc(HWND w, LPARAM)
{
    if (w != g_hWnd) SendMessageW(w, WM_THEMECHANGED, 0, 0);
    return TRUE;
}

static void OnThemeChanged()
{
    Ui_RefreshTheme();
    EnumThreadWindows(GetCurrentThreadId(), ThemeChangedProc, 0);
}

// Belgilangan matnni o'girish menyusi (4 yo'nalish) — Ctrl+o'ng-tugma va
// tezkor tugma uchun umumiy. Modal oynamiz yoki boshqa menyumiz ochiq bo'lsa
// ochmaymiz. Terminalda ham — u yerda bizning Ctrl+C SIGINT bo'lib jarayonni
// to'xtatadi (Ctrl+o'ng-tugma yo'lini hook o'zi filtrlaydi, hotkey yo'li esa
// faqat shu yerdan o'tadi).
static void ShowConvertMenu(HWND hWnd, int x, int y)
{
    if (g_menuOpen || !IsWindowEnabled(hWnd)) return;
    HWND target = GetForegroundWindow();     // matn shu oynada belgilangan
    if (Hook_IsTerminalWindow(target)) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CMD_CONV_TODIGRAPH, T(S_CONV_TODIGRAPH));
    AppendMenuW(menu, MF_STRING, CMD_CONV_TOSPECIAL, T(S_CONV_TOSPECIAL));
    AppendMenuW(menu, MF_STRING, CMD_CONV_CYR2LAT,   T(S_CONV_CYR2LAT));
    AppendMenuW(menu, MF_STRING, CMD_CONV_LAT2CYR,   T(S_CONV_LAT2CYR));
    SetForegroundWindow(hWnd);
    g_menuOpen = true;
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, hWnd, NULL);
    g_menuOpen = false;
    DestroyMenu(menu);
    PostMessageW(hWnd, WM_NULL, 0, 0);       // menyu to'g'ri yopilishi uchun
    int dir = cmd == CMD_CONV_TODIGRAPH ? CONV_TO_DIGRAPH :
              cmd == CMD_CONV_TOSPECIAL ? CONV_TO_SPECIAL :
              cmd == CMD_CONV_CYR2LAT   ? CONV_CYR_TO_LAT :
              cmd == CMD_CONV_LAT2CYR   ? CONV_LAT_TO_CYR : -1;
    if (dir >= 0) {
        if (target) SetForegroundWindow(target);   // fokusni matn oynasiga qaytaramiz
        TextConvert_Run(dir);
    }
}

// Tezkor tugma bilan o'girish: menyu foreground oynadagi caret ostida
// (GetGUIThreadInfo). Caret'ni tizimga bildirmaydigan ilovalarda (ko'p
// Chromium/Electron, Word) — sichqoncha kursori yonida.
static void ShowConvertMenuAtCaret(HWND hWnd)
{
    if (g_menuOpen || !IsWindowEnabled(hWnd)) return;
    HWND fg = GetForegroundWindow();
    if (Hook_IsTerminalWindow(fg)) return;   // terminal — hech narsa qilmaymiz
    POINT pt;
    GetCursorPos(&pt);
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    if (fg && GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
        POINT c = { gti.rcCaret.left, gti.rcCaret.bottom };
        if (ClientToScreen(gti.hwndCaret, &c)) pt = c;
    }
    ShowConvertMenu(hWnd, pt.x, pt.y);
}

// So'z o'rganish so'rovi. Ikki navbatma-navbat bufer: yangi Osd_Ask oldingisini
// almashtirganda oldingisiga cb(false) chaqiriladi — u o'z so'zini ko'rishi kerak.
static wchar_t g_askWord[2][40];
static int     g_askSlot = 0;

static void LearnAnswer(bool accepted, void* ctx)
{
    const wchar_t* w = (const wchar_t*)ctx;
    if (!accepted) { Hook_LearnDecline(w); return; }
    if (!SettingsUi_IsOpen()) { AddSkipWords(w); return; }
    int n = lstrlenW(g_learnPend), wl = lstrlenW(w);
    if (n + wl + 2 > (int)(sizeof(g_learnPend) / sizeof(g_learnPend[0]))) return;
    if (n) g_learnPend[n++] = L';';
    lstrcpyW(g_learnPend + n, w);
}

static void AskLearn()
{
    wchar_t* w = g_askWord[g_askSlot];
    if (!Hook_TakeLearnWord(w, 40)) return;
    // Sozlamalar ochiq — uning lokal ro'yxati yopilganda ustidan yozadi, so'ramaymiz
    if (!g_settings.learnWords || SettingsUi_IsOpen()) return;
    g_askSlot ^= 1;
    wchar_t text[1024];
    wsprintfW(text, T(S_LEARN_ASK), w);
    Osd_Ask(text, T(S_EX_ADD), T(S_LEARN_NO), LearnAnswer, w);
}

// Menyu buyrug'ini bajaradi
static void HandleCommand(HWND hWnd, int cmd)
{
    switch (cmd)
    {
    case CMD_TOGGLE_ENABLED:
        ToggleEnabled();
        break;
    case CMD_SETTINGS:
        ShowSettingsPage(hWnd, SP_GENERAL);
        UpdateTrayTip();
        break;
    case CMD_PLAYGROUND:
        ShowSettingsPage(hWnd, SP_PLAYGROUND);
        UpdateTrayTip();
        break;
    case CMD_AUTOMODE:
        ToggleMode();
        break;
    case CMD_TUTORIAL:
        ShowTutorial(hWnd);
        UpdateTrayTip();            // tanishtiruvda rejim tanlangan bo'lishi mumkin
        break;
    case CMD_ABOUT:
        ShowAbout(hWnd);
        UpdateTrayTip();            // "Haqida" — Sozlamalar sahifasi, unda rejim/til o'zgargan bo'lishi mumkin
        break;
    case CMD_STARTUP:
        // registrdagi haqiqiy holatdan almashtiramiz (Sozlamalar oynasi ham shundan o'qiydi)
        Startup_Set(!Startup_IsEnabled());
        g_settings.runAtStartup = Startup_IsEnabled();
        Settings_Save();
        break;
    case CMD_LANG_UZ: SetLanguage(LANG_UZ); break;
    case CMD_LANG_RU: SetLanguage(LANG_RU); break;
    case CMD_LANG_EN: SetLanguage(LANG_EN); break;
    case CMD_UNINSTALL:
        if (MessageBoxW(hWnd, T(S_UNINST_CONFIRM), T(S_APP_TITLE),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            Uninstall_CleanData();   // sozlamalar, avtozapusk, yorliq
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        break;
    case CMD_EXIT:
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
        break;
    }
}

// Menyu bandi matni: "Yoqilgan\tCtrl+Shift+F10" (tezkor tugma bo'lsa o'ng
// ustunda ko'rinadi), aks holda faqat nom.
static void MenuTextWithHotkey(StrId id, WORD hk, wchar_t* out, int n)
{
    wchar_t key[64];
    Hotkey_Format(hk, key, 64);
    if (!key[0]) { lstrcpynW(out, T(id), n); return; }
    wchar_t buf[512];
    wsprintfW(buf, L"%s\t%s", T(id), key);
    lstrcpynW(out, buf, n);
}

// Tray menyusini ochadi
static void ShowTrayMenu(HWND hWnd)
{
    POINT pt; GetCursorPos(&pt);

    wchar_t txtEnabled[256], txtAuto[256];
    MenuTextWithHotkey(S_MENU_ENABLED,  g_settings.hkEnable, txtEnabled, 256);
    MenuTextWithHotkey(S_MENU_AUTOMODE, g_settings.hkMode,   txtAuto,    256);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0),
                CMD_TOGGLE_ENABLED, txtEnabled);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_SETTINGS, T(S_MENU_SETTINGS));
    AppendMenuW(menu, MF_STRING | (g_settings.mode == MODE_AUTO ? MF_CHECKED : 0),
                CMD_AUTOMODE, txtAuto);

    HMENU langMenu = CreatePopupMenu();
    AppendMenuW(langMenu, MF_STRING | (g_settings.lang == LANG_UZ ? MF_CHECKED : 0), CMD_LANG_UZ, L"Özbekça");
    AppendMenuW(langMenu, MF_STRING | (g_settings.lang == LANG_RU ? MF_CHECKED : 0), CMD_LANG_RU, L"Русский");
    AppendMenuW(langMenu, MF_STRING | (g_settings.lang == LANG_EN ? MF_CHECKED : 0), CMD_LANG_EN, L"English");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)langMenu, T(S_MENU_LANGUAGE));

    AppendMenuW(menu, MF_STRING, CMD_TUTORIAL, T(S_MENU_TUTORIAL));
    AppendMenuW(menu, MF_STRING, CMD_ABOUT, T(S_MENU_ABOUT));
    AppendMenuW(menu, MF_STRING | (Startup_IsEnabled() ? MF_CHECKED : 0),
                CMD_STARTUP, T(S_MENU_STARTUP));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_UNINSTALL, T(S_MENU_UNINSTALL));
    AppendMenuW(menu, MF_STRING, CMD_EXIT, T(S_MENU_EXIT));

    SetForegroundWindow(hWnd);
    g_menuOpen = true;
    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, NULL);
    g_menuOpen = false;
    DestroyMenu(menu);
    PostMessageW(hWnd, WM_NULL, 0, 0);   // menyu to'g'ri yopilishi uchun
    if (cmd) HandleCommand(hWnd, cmd);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // RegisterWindowMessage qiymati o'zgarmas emas — switch'dan oldin tekshiramiz
    if (g_wmTaskbarCreated && msg == g_wmTaskbarCreated) {
        AddTrayIconOrRetry(hWnd);
        return 0;
    }

    switch (msg)
    {
    case WM_TRAYICON:
        // Chap tugma — flyout panel, o'ng — native menyu. Modal oyna ochiq
        // bo'lsa hech biri ochilmaydi, modal oldinga chiqadi.
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
            if (g_menuOpen || BringModalToFront(hWnd)) return 0;
            if (LOWORD(lp) == WM_LBUTTONUP) Flyout_Toggle(TrayAnchor(hWnd));
            else                            ShowTrayMenu(hWnd);
        }
        return 0;

    case WM_COMMAND:
        // Flyout tugmalari (Sozlamalar, Tanişuv, Çiqiş) — flyout yopilgach keladi.
        // lp != 0 — control bildirishnomasi (bizda yo'q), e'tiborsiz.
        if (lp || BringModalToFront(hWnd)) return 0;
        HandleCommand(hWnd, LOWORD(wp));
        return 0;

    case WM_SETTINGCHANGE:
        if ((lp && lstrcmpiW((const wchar_t*)lp, L"ImmersiveColorSet") == 0) ||
            wp == SPI_SETHIGHCONTRAST)
            OnThemeChanged();
        break;

    case WM_SYSCOLORCHANGE:
        OnThemeChanged();
        break;

    case WM_APP_MODCHECK: {
        // Real modifikator qo'yib yuborildi. Darhol tekshirib bo'lmaydi:
        // bizning inyeksiya qilingan "qayta bosish" hodisamiz hali tizim
        // navbatida bo'lishi mumkin (shunda GetAsyncKeyState "qo'yilgan" deb
        // aldab qo'yadi, keyin esa modifikator osilib qoladi). Shu sababli
        // qisqa taymer qo'yib, hamma hodisa o'tib bo'lgach tekshiramiz.
        UINT vk = (UINT)wp;
        SetTimer(hWnd, MODCHECK_TIMER_BASE + vk, 100, NULL);
        return 0;
    }

    case WM_TIMER:
        if (wp == TRAY_RETRY_TIMER) {
            if (AddTrayIcon() || ++g_trayRetries >= kTrayRetryMax)
                KillTimer(hWnd, TRAY_RETRY_TIMER);
            return 0;
        }
        if (wp >= MODCHECK_TIMER_BASE && wp < MODCHECK_TIMER_BASE + 0x100) {
            KillTimer(hWnd, wp);
            UINT vk = (UINT)(wp - MODCHECK_TIMER_BASE);
            // Fizik tugma qo'yib yuborilganiga qaramay hali "bosilgan" ko'rinsa —
            // bu bizning qayta bosishimizdan osilib qolgan; majburan qo'yib yuboramiz.
            if (GetAsyncKeyState((int)vk) & 0x8000)
                Hook_ForceModUp(vk);
            return 0;
        }
        break;

    case WM_APP_CONVERTMENU:
        // Ctrl+o'ng-tugma bosildi: belgilangan matnni o'girish menyusini ko'rsatamiz.
        // Modal/menyu tekshiruvi ShowConvertMenu ichida (hook buni odatda o'zi
        // tekshiradi; bu — oraliqda holat o'zgargan holat uchun).
        ShowConvertMenu(hWnd, (int)(LONG)wp, (int)(LONG)lp);
        return 0;

    case WM_APP_LEARN:
        AskLearn();
        return 0;

    case WM_HOTKEY:
        // Tezkor tugmalar (hotkeys.cpp). Sozlamalar ochiq paytda ular ro'yxatdan
        // chiqarilgan bo'ladi, shuning uchun bu yerga kelmaydi.
        switch (wp) {
        case HKID_MODE:    ToggleMode();                 break;
        case HKID_ENABLE:  ToggleEnabled();              break;
        case HKID_CONVERT: ShowConvertMenuAtCaret(hWnd); break;
        }
        return 0;

    case WM_DESTROY:
        Hotkeys_Unregister();
        KillTimer(hWnd, TRAY_RETRY_TIMER);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_iconState) { DestroyIcon(g_iconState); g_iconState = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Sozlamalar oynasi tablari va hotkey control'i uchun (comctl32)
    INITCOMMONCONTROLSEX icc = { sizeof(icc),
                                 ICC_TAB_CLASSES | ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    Settings_Load();

    // Ikki nusxa ishlab ketmasligi uchun
    HANDLE once = CreateMutexW(NULL, TRUE, L"HarfbekSingleInstance");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, T(S_ALREADY_RUNNING), T(S_APP_TITLE), MB_ICONINFORMATION);
        return 0;
    }

    // Avtozapusk: registrdagi haqiqiy holat (va exe ko'chirilgan bo'lsa yangi yo'l)
    Startup_Sync();

    // Ikonkalar
    g_iconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                  0, 0, LR_DEFAULTSIZE);
    g_iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!g_iconBig)   g_iconBig   = LoadIconW(NULL, IDI_APPLICATION);
    if (!g_iconSmall) g_iconSmall = g_iconBig;

    // Yashirin oyna
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = g_iconBig;
    wc.lpszClassName = L"HarfbekWnd";
    RegisterClassW(&wc);
    g_hWnd = CreateWindowW(wc.lpszClassName, L"Harfbek",
                           WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInst, NULL);

    // Tray ikonka
    g_wmTaskbarCreated     = RegisterWindowMessageW(L"TaskbarCreated");
    AllowTaskbarCreated(g_hWnd);
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hWnd;
    g_nid.uID              = 1;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = g_iconSmall;
    PrepareStateIcon();             // yoqiq/o'chiq va rejimga mos ikonka bilan qo'shiladi
    AddTrayIconOrRetry(g_hWnd);

    // Global hook
    if (!Hook_Install()) {
        MessageBoxW(NULL, T(S_HOOK_FAILED), T(S_ERROR), MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 1;
    }

    // Tezkor tugmalar. Band bo'lsa jim — foydalanuvchi Sozlamalarda ogohlantiriladi.
    Hotkeys_Apply();

    // Birinchi ishga tushish: til so'raladi + qo'llanma ko'rsatiladi
    if (g_settings.firstRun) {
        ShowLanguageDialog(g_hWnd);
        UpdateTrayTip();
        ShowLicense(g_hWnd);        // litsenziya/ogohlantirish (tanlangan tilda)
        ShowTutorial(g_hWnd);
        UpdateTrayTip();            // tanishtiruvda rejim tanlangan bo'lishi mumkin
        Shortcut_CreateDesktop();   // ish stolida yorliq
        g_settings.firstRun = false;
        Settings_Save();
    }

    // Xabarlar sikli
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Hook_Uninstall();
    if (once) CloseHandle(once);
    return 0;
}
