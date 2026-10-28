// Belgilangan matnni o'girish: nusxalash (Ctrl+C) -> o'girish -> qaytarish (Ctrl+V).
// Ish alohida thread'da bajariladi, shunda hook thread'i (klaviatura) bloklanmaydi.
#include "app.h"
#include <string>
#include <cstring>
#include <cwctype>

// Maxsus harflar (hex — encoding'ga bog'liq emas):
//   ö=00F6 Ö=00D6  ğ=011F Ğ=011E  ş=015F Ş=015E  ç=00E7 Ç=00C7

// Apostrof variantlarini tanish (teskari o'girishda hammasini qabul qilamiz)
static bool IsApo(wchar_t c)
{
    return c == L'\'' || c == L'`' ||
           c == (wchar_t)0x02BB || c == (wchar_t)0x2018 ||
           c == (wchar_t)0x2019 || c == (wchar_t)0x00B4 ||
           c == (wchar_t)0x2032;
}

// Katta harf konteksti (Ş/Ç dan keyin SH/CH yoki Sh/Ch tanlash uchun)
static bool IsUpperCtx(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return true;
    if (c == 0x00D6 || c == 0x011E || c == 0x015E || c == 0x00C7) return true;
    return iswupper(c) != 0;
}

// Harf kontekstimi (so'z oxirini aniqlash uchun)
static bool IsLetterCtx(wchar_t c)
{
    if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) return true;
    return iswalpha(c) != 0;
}

// Ş/Ç katta digraf (SH/CH) bo'lsinmi: keyingi harf katta bo'lsa (ŞAHAR),
// yoki so'z shu harf bilan tugasa (keyingi belgi harf emas) va oldingi harf
// katta bo'lsa (TOŞ). Aks holda Sh/Ch (Şahar, yolg'iz Ş).
static bool UpperDigraph(wchar_t pv, wchar_t nx)
{
    if (IsUpperCtx(nx)) return true;
    return !IsLetterCtx(nx) && IsUpperCtx(pv);
}

// ö ğ ş ç  ->  o' g' sh ch
static std::wstring ToDigraph(const std::wstring& in, wchar_t apo)
{
    std::wstring o;
    o.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c  = in[i];
        wchar_t pv = (i > 0) ? in[i - 1] : 0;
        wchar_t nx = (i + 1 < in.size()) ? in[i + 1] : 0;
        switch (c) {
        case 0x00F6: o += L'o'; o += apo; break;                       // ö
        case 0x00D6: o += L'O'; o += apo; break;                       // Ö
        case 0x011F: o += L'g'; o += apo; break;                       // ğ
        case 0x011E: o += L'G'; o += apo; break;                       // Ğ
        case 0x015F: o += L"sh"; break;                                // ş
        case 0x015E: o += (UpperDigraph(pv, nx) ? L"SH" : L"Sh"); break;// Ş
        case 0x00E7: o += L"ch"; break;                                // ç
        case 0x00C7: o += (UpperDigraph(pv, nx) ? L"CH" : L"Ch"); break;// Ç
        default:     o += c;
        }
    }
    return o;
}

// o' g' sh ch  ->  ö ğ ş ç   (apostrofning barcha variantlari tan olinadi)
static std::wstring ToSpecial(const std::wstring& in)
{
    std::wstring o;
    o.reserve(in.size());
    size_t n = in.size();
    for (size_t i = 0; i < n; ) {
        wchar_t c = in[i];
        wchar_t d = (i + 1 < n) ? in[i + 1] : 0;
        if ((c == L'o' || c == L'O') && IsApo(d)) {
            o += (c == L'O') ? (wchar_t)0x00D6 : (wchar_t)0x00F6; i += 2; continue; }
        if ((c == L'g' || c == L'G') && IsApo(d)) {
            o += (c == L'G') ? (wchar_t)0x011E : (wchar_t)0x011F; i += 2; continue; }
        if ((c == L's' || c == L'S') && (d == L'h' || d == L'H')) {
            o += (c == L'S') ? (wchar_t)0x015E : (wchar_t)0x015F; i += 2; continue; }
        if ((c == L'c' || c == L'C') && (d == L'h' || d == L'H')) {
            o += (c == L'C') ? (wchar_t)0x00C7 : (wchar_t)0x00E7; i += 2; continue; }
        o += c; ++i;
    }
    return o;
}

// --- Kirill -> Lotin (yangi alifbo: ö ğ ş ç, ’ tutuq belgisi) ---

// Kirill harfini kichigiga keltiradi; upper — katta edimi. Kirill bo'lmasa 0.
static wchar_t CyrLower(wchar_t c, bool* upper)
{
    *upper = true;
    if (c >= 0x0410 && c <= 0x042F) return (wchar_t)(c + 0x20);
    switch (c) {
    case 0x0401: return 0x0451;   // Ё
    case 0x040E: return 0x045E;   // Ў
    case 0x049A: return 0x049B;   // Қ
    case 0x0492: return 0x0493;   // Ғ
    case 0x04B2: return 0x04B3;   // Ҳ
    }
    *upper = false;
    if ((c >= 0x0430 && c <= 0x044F) || c == 0x0451 || c == 0x045E ||
        c == 0x049B || c == 0x0493 || c == 0x04B3)
        return c;
    return 0;
}

static bool IsCyrVowel(wchar_t lc)   // lc — kichik kirill harf
{
    switch (lc) {
    case 0x0430: case 0x0435: case 0x0451: case 0x0438: case 0x043E:   // а е ё и о
    case 0x0443: case 0x044D: case 0x044E: case 0x044F: case 0x045E:   // у э ю я ў
        return true;
    }
    return false;
}

static bool IsUpperAny(wchar_t c)
{
    bool up;
    return CyrLower(c, &up) ? up : IsUpperCtx(c);
}

static bool IsLetterAny(wchar_t c)
{
    bool up;
    return CyrLower(c, &up) != 0 || IsLetterCtx(c);
}

// Lotin natija harfining kattasi (a-z va ö ğ ş ç; ’ o'zgarmaydi)
static wchar_t LatUpper(wchar_t c)
{
    if (c >= L'a' && c <= L'z') return (wchar_t)(c - 0x20);
    switch (c) {
    case 0x00F6: return 0x00D6;
    case 0x011F: return 0x011E;
    case 0x015F: return 0x015E;
    case 0x00E7: return 0x00C7;
    }
    return c;
}

// а..я (0x430..0x44F) uchun lotin qiymati (0-2 belgi; pointer jadvali emas —
// ixcham). е va ц kontekstga bog'liq — ToLatin o'zi hal qiladi.
static const wchar_t kCyrLat[32][2] = {
    {'a'}, {'b'}, {'v'}, {'g'}, {'d'}, {'e'}, {'j'}, {'z'},             // а б в г д е ж з
    {'i'}, {'y'}, {'k'}, {'l'}, {'m'}, {'n'}, {'o'}, {'p'},             // и й к л м н о п
    {'r'}, {'s'}, {'t'}, {'u'}, {'f'}, {'x'}, {'s'}, {0x00E7},          // р с т у ф х ц ч
    {0x015F}, {0x015F, 0x00E7}, {0x2019}, {'i'}, {0}, {'e'}, {'y', 'u'}, {'y', 'a'} // ш щ ъ ы ь э ю я
};

// Kirill -> lotin. е: so'z boshida, unli yoki ъ/ь dan keyin "ye", aks holda "e";
// ц: unlidan keyin "ts", aks holda "s". Katta harf: bitta harfli natija — katta;
// ikki harfli (Yo/YO, Ye/YE, Ts, ŞÇ...) — UpperDigraph mantig'i bilan.
// Kirill bo'lmagan belgilar o'zgarmaydi.
static std::wstring ToLatin(const std::wstring& in)
{
    std::wstring o;
    o.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c  = in[i];
        wchar_t pv = (i > 0) ? in[i - 1] : 0;
        wchar_t nx = (i + 1 < in.size()) ? in[i + 1] : 0;
        bool    up;
        wchar_t lc = CyrLower(c, &up);
        wchar_t r[2] = { c, 0 };                      // kirill emas — o'zgarmaydi
        if (lc) {
            bool    pvUp;
            wchar_t pl = pv ? CyrLower(pv, &pvUp) : 0;   // oldingi harf (kichik kirill) yoki 0
            switch (lc) {
            case 0x0451: r[0] = 'y'; r[1] = 'o'; break;  // ё
            case 0x045E: r[0] = 0x00F6; break;           // ў
            case 0x049B: r[0] = 'q';    break;           // қ
            case 0x0493: r[0] = 0x011F; break;           // ғ
            case 0x04B3: r[0] = 'h';    break;           // ҳ
            default:
                r[0] = kCyrLat[lc - 0x0430][0];
                r[1] = kCyrLat[lc - 0x0430][1];
                if (lc == 0x0435 &&                          // е
                    (!IsLetterAny(pv) || IsCyrVowel(pl) || pl == 0x044A || pl == 0x044C)) {
                    r[0] = 'y'; r[1] = 'e';
                } else if (lc == 0x0446 && IsCyrVowel(pl)) { // ц
                    r[1] = 's'; r[0] = 't';
                }
            }
            if (up && r[0]) {
                r[0] = LatUpper(r[0]);
                if (r[1] && (IsUpperAny(nx) || (!IsLetterAny(nx) && IsUpperAny(pv))))
                    r[1] = LatUpper(r[1]);
            }
        }
        o.append(r, r[0] ? (r[1] ? 2 : 1) : 0);         // bitta qo'shish joyi (exe hajmi)
    }
    return o;
}

// --- Clipboard yordamchilari ---

// Clipboard'ni ochadi. Boshqa dastur (masalan, hozirgina Ctrl+C ni bajarayotgan
// ilova) uni band qilgan bo'lsa, bir necha marta qisqa kutib qayta urinamiz.
// Egasi g_hWnd: NULL bilan ochilsa EmptyClipboard egani NULL qiladi va
// keyingi SetClipboardData muvaffaqiyatsiz bo'ladi.
static bool ClipOpen()
{
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(g_hWnd)) return true;
        Sleep(20);
    }
    return false;
}

static std::wstring ClipGet()
{
    std::wstring r;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return r;
    if (!ClipOpen()) return r;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        const wchar_t* p = (const wchar_t*)GlobalLock(hd);
        if (p) { r = p; GlobalUnlock(hd); }
    }
    CloseClipboard();
    return r;
}

// Win+V tarixi va clipboard kuzatuvchilari uchun belgi formatlari
static UINT FmtExcludeMonitor()
{
    static const UINT f = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    return f;
}
static UINT FmtCanIncludeHistory()
{
    static const UINT f = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    return f;
}

// DWORD qiymatli formatni qo'yadi (clipboard ochiq bo'lishi shart)
static void ClipPutDword(UINT fmt, DWORD v)
{
    if (!fmt) return;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!g) return;
    DWORD* p = (DWORD*)GlobalLock(g);
    if (!p) { GlobalFree(g); return; }
    *p = v;
    GlobalUnlock(g);
    if (!SetClipboardData(fmt, g)) GlobalFree(g);
}

// O'girilgan matnni qo'yadi. U vaqtinchalik: Win+V tarixiga tushmaydi va
// clipboard kuzatuvchilari (tarix, bulut sinxronizatsiyasi, menejerlar) uni
// e'tiborsiz qoldiradi.
static bool ClipSet(const std::wstring& s)
{
    if (!ClipOpen()) return false;
    EmptyClipboard();
    size_t  bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool    ok = false;
    if (g) {
        void* p = GlobalLock(g);
        if (p) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(g);
            ok = SetClipboardData(CF_UNICODETEXT, g) != NULL;
        }
        if (!ok) GlobalFree(g);   // muvaffaqiyatli bo'lsa egasi tizim
    }
    if (ok) {
        ClipPutDword(FmtExcludeMonitor(), 0);
        ClipPutDword(FmtCanIncludeHistory(), 0);
    }
    CloseClipboard();
    return ok;
}

// --- Asl clipboard'ni saqlash / tiklash ---
struct ClipItem { UINT fmt; HGLOBAL data; };
struct ClipSnapshot {
    bool  ok = false;               // clipboard ochilib o'qildi
    UINT  total = 0;                // asl formatlar soni (saqlanmaganlari ham)
    UINT  count = 0;                // items dagi elementlar soni
    ClipItem items[128];            // nusxalangan HGLOBAL formatlar (asl tartibda; odatda < 30)
};

// Ma'lumoti HGLOBAL bo'lgan va xavfsiz nusxalanadigan formatmi.
// GDI/handle formatlar (bitmap, palitra, metafayl) nusxalanmaydi — lekin
// CF_DIB/CF_DIBV5 HGLOBAL, rasm ular orqali saqlanadi (CF_BITMAP ni tizim
// keyin o'zi sintez qiladi).
static bool IsHGlobalFormat(UINT f)
{
    switch (f) {
    case CF_BITMAP: case CF_DSPBITMAP: case CF_PALETTE:
    case CF_METAFILEPICT: case CF_DSPMETAFILEPICT:   // ichida HMETAFILE bor
    case CF_ENHMETAFILE:  case CF_DSPENHMETAFILE:
    case CF_OWNERDISPLAY:                             // ma'lumot yo'q
        return false;
    }
    if (f >= CF_PRIVATEFIRST && f <= CF_PRIVATELAST) return false;  // turi noma'lum
    if (f >= CF_GDIOBJFIRST  && f <= CF_GDIOBJLAST)  return false;  // GDI obyekt
    if (f >= 0xC000) {
        // OLE ichki formatlari asl dasturning OLE oynasiga ishora qiladi —
        // boshqa egada tiklansa OleGetClipboard chalg'iydi. Qolgan ma'lumot
        // formatlari (matn, RTF, HTML, fayllar...) baribir tiklanadi.
        wchar_t name[64];
        if (GetClipboardFormatNameW(f, name, 64) &&
            (lstrcmpiW(name, L"DataObject") == 0 ||
             lstrcmpiW(name, L"Ole Private Data") == 0 ||
             lstrcmpiW(name, L"OleClipboardPersistOnFlush") == 0))
            return false;
    }
    return true;
}

// HGLOBAL nusxasi. 0 baytli ma'lumot (odatda faqat mavjudligi muhim bo'lgan
// belgi formatlar) kichik nol blok bilan saqlanadi.
static HGLOBAL DupGlobal(HANDLE h)
{
    SIZE_T sz = GlobalSize(h);
    HGLOBAL d = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sz ? sz : sizeof(DWORD));
    if (!d) return NULL;
    if (sz) {
        const void* src = GlobalLock(h);
        void*       dst = GlobalLock(d);
        if (src && dst) memcpy(dst, src, sz);
        if (dst) GlobalUnlock(d);
        if (src) GlobalUnlock(h);
        if (!src || !dst) { GlobalFree(d); return NULL; }
    }
    return d;
}

static void ClipFree(ClipSnapshot& s)
{
    for (UINT i = 0; i < s.count; ++i) GlobalFree(s.items[i].data);
    s.count = 0;
}

static void ClipSave(ClipSnapshot& s)
{
    if (!ClipOpen()) return;
    s.ok = true;
    for (UINT f = EnumClipboardFormats(0); f; f = EnumClipboardFormats(f)) {
        s.total++;
        if (!IsHGlobalFormat(f)) continue;
        HANDLE h = GetClipboardData(f);
        HGLOBAL copy = h ? DupGlobal(h) : NULL;
        if (copy) {
            s.items[s.count].fmt  = f;
            s.items[s.count].data = copy;
            if (++s.count == sizeof(s.items) / sizeof(s.items[0])) break;   // juda ko'p format — qolgani saqlanmaydi
        }
    }
    CloseClipboard();
}

// Snapshot'ni clipboard'ga qaytaradi (ma'lumot egaligi tizimga o'tadi).
// Tiklab bo'lmaydigan holatda (ochib bo'lmagan yoki asl formatlarning birortasi
// ham saqlanmagan) clipboard'ga tegmaymiz. Asl clipboard bo'sh bo'lgan bo'lsa —
// bo'shatamiz.
static void ClipRestore(ClipSnapshot& s)
{
    if (!s.ok || (s.count == 0 && s.total != 0)) { ClipFree(s); return; }
    if (!ClipOpen()) { ClipFree(s); return; }
    EmptyClipboard();
    bool hasHistFlag = false;
    for (UINT i = 0; i < s.count; ++i) {
        if (s.items[i].fmt == FmtCanIncludeHistory()) hasHistFlag = true;
        if (!SetClipboardData(s.items[i].fmt, s.items[i].data))
            GlobalFree(s.items[i].data);
    }
    s.count = 0;
    // Asl ma'lumot Win+V tarixida allaqachon bor — qayta tiklash uni
    // ikkinchi marta qo'shmasin.
    if (s.total != 0 && !hasHistFlag) ClipPutDword(FmtCanIncludeHistory(), 0);
    CloseClipboard();
}

// Ctrl + <vk> yuborish (masalan Ctrl+C / Ctrl+V). Injected — hook o'tkazib yuboradi.
static void SendCtrlKey(WORD vk)
{
    INPUT in[4] = {};
    for (int i = 0; i < 4; ++i) in[i].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = vk;
    in[2].ki.wVk = vk;         in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

// Bir vaqtda faqat bitta o'girish (ikkitasi clipboard'ni bir-biridan buzadi)
static volatile LONG g_convBusy = 0;

static DWORD WINAPI ConvThread(LPVOID param)
{
    int dir = (int)(INT_PTR)param;   // ConvDir

    // Menyu tezkor tugma (Ctrl+Shift+F11 kabi) bilan ochilgan bo'lsa, foydalanuvchi
    // Shift/Alt/Win ni hali qo'yib yubormagan bo'lishi mumkin — shunda bizning
    // Ctrl+C ilovaga Ctrl+Shift+C (Chrome'da DevTools) bo'lib boradi. Qo'yib
    // yuborilishini kutamiz, lekin 1 s dan ortiq emas.
    for (int i = 0; i < 100; ++i) {
        if (!(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
            !(GetAsyncKeyState(VK_LWIN) & 0x8000)  && !(GetAsyncKeyState(VK_RWIN) & 0x8000))
            break;
        Sleep(10);
    }

    Sleep(40);   // target oyna fokusni olib bo'lsin

    ClipSnapshot saved;
    ClipSave(saved);                             // asl clipboardni (barcha formatlar) saqlaymiz
    DWORD seq = GetClipboardSequenceNumber();

    SendCtrlKey('C');                            // belgilangan matnni nusxalash

    // clipboard yangilanishini kutamiz (~500 ms)
    bool changed = false;
    for (int i = 0; i < 50; ++i) {
        Sleep(10);
        if (GetClipboardSequenceNumber() != seq) { changed = true; break; }
    }

    std::wstring sel, out;
    if (changed) {
        sel = ClipGet();
        if (!sel.empty())
            out = dir == CONV_TO_DIGRAPH ? ToDigraph(sel, g_settings.apo) :
                  dir == CONV_CYR_TO_LAT ? ToLatin(sel) : ToSpecial(sel);
    }

    if (!changed) {
        ClipFree(saved);                         // hech narsa belgilanmagan — clipboard o'zgarmadi
    } else if (sel.empty() || out == sel || !ClipSet(out)) {
        ClipRestore(saved);                      // o'girish yo'q — asl holatga qaytaramiz
    } else {
        DWORD mine = GetClipboardSequenceNumber();
        Sleep(30);
        SendCtrlKey('V');                        // o'girilgan matnni joyiga qo'yish

        // Ilova Ctrl+V ni o'z navbatidan olib, clipboard'ni o'qib bo'lguncha
        // kutamiz. 150 ms band Word / Electron (Chromium) ilovalarida yetmasdi:
        // ular eski (asl) matnni qo'yib yuborardi. 600 ms — sekin ilovalar uchun
        // zaxira bilan, foydalanuvchi uchun esa sezilmas kechikish.
        Sleep(600);

        // Oraliqda foydalanuvchi (yoki boshqa dastur) yangi narsa nusxalagan
        // bo'lsa — uning ma'lumotini asl nusxa bilan ustidan yozmaymiz.
        if (GetClipboardSequenceNumber() == mine) ClipRestore(saved);
        else                                      ClipFree(saved);
    }

    InterlockedExchange(&g_convBusy, 0);
    return 0;
}

void TextConvert_Run(int dir)
{
    if (dir < CONV_TO_DIGRAPH || dir > CONV_CYR_TO_LAT) return;
    if (InterlockedCompareExchange(&g_convBusy, 1, 0) != 0) return;   // oldingisi hali ishlayapti
    HANDLE t = CreateThread(NULL, 0, ConvThread, (LPVOID)(INT_PTR)dir, 0, NULL);
    if (t) CloseHandle(t);
    else   InterlockedExchange(&g_convBusy, 0);
}
