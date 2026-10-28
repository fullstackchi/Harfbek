// Ekrandagi qisqa bildirishnoma (OSD): fokus olmaydigan, shaffof "pill".
// Per-pixel alpha (UpdateLayeredWindow): yumaloq chekka, yumshoq soya va matn alpha-to'g'ri.
// Pill piksel hisobi bilan (SDF) chiziladi — GDI+ yuklanmaydi; matn/glif GDI niqobi orqali
// aralashtiriladi (oddiy GDI matn alpha'ni buzadi). Oyna faqat ko'rsatilganda yaratiladi,
// yo'qolgach DestroyWindow + DIB o'chiriladi (RAM).
#include "app.h"
#include "ui_gfx.h"
#include <math.h>

static const wchar_t* kOsdClass = L"HarfbekOsd";
static const UINT_PTR kHoldTimer = 1;
static const UINT     kHoldMs    = 1500;
static const DWORD    kInMs      = 180;
static const DWORD    kOutMs     = 200;
static const DWORD    kPulseMs   = 160;

enum OsdPhase { PH_IN, PH_HOLD, PH_OUT };

static HWND    s_hOsd = NULL;
static HBITMAP s_bmp = NULL;
static DWORD*  s_bits = NULL;
static int     s_w = 0, s_h = 0;      // oyna (soya chekkasi bilan) o'lchami
static int     s_x = 0, s_y = 0;      // oyna tinch holatdagi joyi
static int     s_dpi = 96;
static OsdPhase s_phase = PH_IN;
static DWORD   s_t0 = 0;              // joriy fazaning boshlanishi
static DWORD   s_pulse0 = 0;          // oxirgi qayta chaqiruv (pulse) vaqti; 0 = yo'q
static bool    s_animTimer = false;
static wchar_t s_text[128];

struct OsdRgbf { float r, g, b, a; };    // premultiplied

static float OsdClamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static float OsdEaseOut(float t) { float u = 1.f - OsdClamp01(t); return 1.f - u * u * u; }

// d ustiga (rang, alpha) ni "over" qoidasi bilan qo'yadi
static void OsdOver(OsdRgbf& d, COLORREF c, float a)
{
    float k = 1.f - a;
    d.r = GetRValue(c) / 255.f * a + d.r * k;
    d.g = GetGValue(c) / 255.f * a + d.g * k;
    d.b = GetBValue(c) / 255.f * a + d.b * k;
    d.a = a + d.a * k;
}

// Yumaloq to'g'ri to'rtburchakning (markazga nisbatan) imzoli masofasi
static float OsdSdBox(float px, float py, float hw, float hh, float r)
{
    float qx = fabsf(px) - (hw - r), qy = fabsf(py) - (hh - r);
    float ox = qx > 0.f ? qx : 0.f, oy = qy > 0.f ? qy : 0.f;
    float in = (qx > qy ? qx : qy);
    if (in > 0.f) in = 0.f;
    return sqrtf(ox * ox + oy * oy) + in - r;
}

// GDI bilan niqobga (oq/qora) chizilgan matn/glifni DIB ustiga rangda aralashtiradi
static void CompositeMask(DWORD* bits, int bw, int bh, const RECT& rc, const wchar_t* s, HFONT font,
                          UINT flags, COLORREF col)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    DWORD* m = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP mb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&m, NULL, 0);
    if (!mb || !m) { if (mb) DeleteObject(mb); DeleteDC(dc); return; }
    HGDIOBJ ob = SelectObject(dc, mb);
    HGDIOBJ of = SelectObject(dc, font);
    ZeroMemory(m, (size_t)bw * bh * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r = rc;
    DrawTextW(dc, s, -1, &r, flags);
    GdiFlush();

    float cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);
    for (int y = rc.top; y < rc.bottom && y < bh; ++y)
    {
        if (y < 0) continue;
        for (int x = rc.left; x < rc.right && x < bw; ++x)
        {
            if (x < 0) continue;
            float c = ((m[(size_t)y * bw + x] >> 8) & 0xFF) / 255.f;   // G kanali = qoplama
            if (c <= 0.f) continue;
            DWORD d = bits[(size_t)y * bw + x];
            float a = (float)((d >> 24) & 0xFF), k = 1.f - c;
            float nr = ((d >> 16) & 0xFF) * k + cr * c;
            float ng = ((d >> 8) & 0xFF) * k + cg * c;
            float nb = (d & 0xFF) * k + cb * c;
            float na = a + (255.f - a) * c;
            bits[(size_t)y * bw + x] = ((DWORD)(na + 0.5f) << 24) | ((DWORD)(nr + 0.5f) << 16) |
                                       ((DWORD)(ng + 0.5f) << 8) | (DWORD)(nb + 0.5f);
        }
    }
    SelectObject(dc, of);
    SelectObject(dc, ob);
    DeleteObject(mb);
    DeleteDC(dc);
}

static void FreeBitmap()
{
    if (s_bmp) { DeleteObject(s_bmp); s_bmp = NULL; }
    s_bits = NULL;
}

// Pillni DIB ga chizadi (s_w, s_h, s_dpi, s_text bo'yicha)
static bool RenderPill(int margin, int pillW, int pillH, const RECT& textRc, const RECT& glyphRc,
                       int circleR, int circleCx, int circleCy, HFONT font, HFONT iconFont)
{
    FreeBitmap();
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s_w;
    bi.bmiHeader.biHeight = -s_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(NULL);
    s_bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, (void**)&s_bits, NULL, 0);
    ReleaseDC(NULL, sdc);
    if (!s_bmp || !s_bits) { FreeBitmap(); return false; }

    const UiTheme& th = Ui_Theme();
    COLORREF fill   = th.dark ? RGB(22, 22, 26) : RGB(252, 252, 254);
    float    fillA  = 0.94f;
    float    shMax  = th.dark ? 0.55f : 0.30f;
    float    bw     = (float)Ui_S(1, s_dpi);
    float    blur   = (float)Ui_S(14, s_dpi);
    float    shDy   = (float)Ui_S(4, s_dpi);

    float cx = s_w * 0.5f, cy = margin + pillH * 0.5f;
    float hw = pillW * 0.5f, hh = pillH * 0.5f, rad = hh;

    for (int y = 0; y < s_h; ++y)
    {
        for (int x = 0; x < s_w; ++x)
        {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            OsdRgbf p = { 0, 0, 0, 0 };

            // yumshoq soya (pastga siljigan)
            float ds = OsdSdBox(px, py - shDy, hw, hh, rad);
            if (ds < blur)
            {
                float t = ds <= 0.f ? 1.f : 1.f - ds / blur;
                OsdOver(p, RGB(0, 0, 0), shMax * t * t);
            }

            // pill: shisha fon + ingichka chegara
            float d = OsdSdBox(px, py, hw, hh, rad);
            float cov = OsdClamp01(0.5f - d);
            if (cov > 0.f)
            {
                OsdOver(p, fill, fillA * cov);
                float inner = OsdClamp01(0.5f - (d + bw));
                float ring = cov - inner;
                if (ring > 0.f) OsdOver(p, th.border, 0.9f * ring);
            }

            // chapdagi accent doira
            float dc = sqrtf((x + 0.5f - circleCx) * (x + 0.5f - circleCx) +
                             (y + 0.5f - circleCy) * (y + 0.5f - circleCy)) - circleR;
            float cc = OsdClamp01(0.5f - dc);
            if (cc > 0.f) OsdOver(p, th.accent, cc);

            DWORD A = (DWORD)(OsdClamp01(p.a) * 255.f + 0.5f);
            DWORD R = (DWORD)(OsdClamp01(p.r) * 255.f + 0.5f);
            DWORD G = (DWORD)(OsdClamp01(p.g) * 255.f + 0.5f);
            DWORD B = (DWORD)(OsdClamp01(p.b) * 255.f + 0.5f);
            s_bits[(size_t)y * s_w + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }

    CompositeMask(s_bits, s_w, s_h, glyphRc, L"\xE765", iconFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, th.onAccent);
    CompositeMask(s_bits, s_w, s_h, textRc, s_text, font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, th.text);
    return true;
}

// Joriy fazaga mos alpha/siljish bilan oynani yangilaydi
static void Present()
{
    if (!s_hOsd || !s_bmp) return;
    DWORD now = GetTickCount();
    float k = 1.f;
    int yoff = 0;
    if (s_phase == PH_IN)
    {
        float e = OsdEaseOut((float)(now - s_t0) / kInMs);
        k = e;
        yoff = (int)((1.f - e) * Ui_S(10, s_dpi));
    }
    else if (s_phase == PH_OUT)
    {
        k = 1.f - OsdEaseOut((float)(now - s_t0) / kOutMs);
    }
    if (s_pulse0)
    {
        float pt = OsdClamp01((float)(now - s_pulse0) / kPulseMs);
        k *= 0.72f + 0.28f * pt;
    }

    HDC sdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(sdc);
    HGDIOBJ ob = SelectObject(mdc, s_bmp);
    POINT dst = { s_x, s_y + yoff }, src = { 0, 0 };
    SIZE sz = { s_w, s_h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(OsdClamp01(k) * 255.f + 0.5f), AC_SRC_ALPHA };
    UpdateLayeredWindow(s_hOsd, sdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, ob);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
}

static void StartAnimTimer()
{
    if (s_hOsd && !s_animTimer) { SetTimer(s_hOsd, UI_ANIM_TIMER, 15, NULL); s_animTimer = true; }
}

static void StopAnimTimer()
{
    if (s_hOsd && s_animTimer) { KillTimer(s_hOsd, UI_ANIM_TIMER); s_animTimer = false; }
}

static void OnAnimTick()
{
    DWORD now = GetTickCount();
    if (s_pulse0 && now - s_pulse0 >= kPulseMs) s_pulse0 = 0;

    if (s_phase == PH_IN && now - s_t0 >= kInMs)
    {
        s_phase = PH_HOLD;
        SetTimer(s_hOsd, kHoldTimer, kHoldMs, NULL);
    }
    else if (s_phase == PH_OUT && now - s_t0 >= kOutMs)
    {
        DestroyWindow(s_hOsd);
        return;
    }
    Present();
    if (s_phase == PH_HOLD && !s_pulse0) StopAnimTimer();
}

static LRESULT CALLBACK OsdProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_TIMER:
        if (wp == UI_ANIM_TIMER) { OnAnimTick(); return 0; }
        if (wp == kHoldTimer)
        {
            KillTimer(h, kHoldTimer);
            if (!Ui_AnimationsEnabled()) { DestroyWindow(h); return 0; }
            s_phase = PH_OUT;
            s_t0 = GetTickCount();
            StartAnimTimer();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(h, kHoldTimer);
        KillTimer(h, UI_ANIM_TIMER);
        s_animTimer = false;
        FreeBitmap();
        s_hOsd = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Osd_Show(const wchar_t* text)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = OsdProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = kOsdClass;
        if (!RegisterClassExW(&wc)) return;
        s_registered = true;
    }

    lstrcpynW(s_text, text ? text : L"", (int)(sizeof(s_text) / sizeof(s_text[0])));

    HDC sdc = GetDC(NULL);
    s_dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    HFONT font = Ui_Font(s_dpi, 17, FW_SEMIBOLD);
    HFONT iconFont = Ui_IconFont(s_dpi, 15);
    HGDIOBJ old = SelectObject(sdc, font);
    RECT tr = { 0, 0, 0, 0 };
    DrawTextW(sdc, s_text, -1, &tr, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(sdc, old);
    ReleaseDC(NULL, sdc);

    // Joylashuv: kursor turgan monitorning ish maydoni, pastki markaz, pastdan ~10% yuqorida
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& wa = mi.rcWork;

    int margin  = Ui_S(18, s_dpi);                  // soya uchun chekka
    int pillH   = Ui_S(48, s_dpi);
    int circleD = Ui_S(28, s_dpi);
    int padL    = Ui_S(10, s_dpi), gap = Ui_S(12, s_dpi), padR = Ui_S(20, s_dpi);
    int textW   = tr.right - tr.left;
    int pillW   = padL + circleD + gap + textW + padR;
    int maxW    = (wa.right - wa.left) - 2 * margin;
    if (pillW > maxW) { textW -= pillW - maxW; pillW = maxW; }
    if (textW < 0) textW = 0;

    s_w = pillW + 2 * margin;
    s_h = pillH + 2 * margin;
    int pillX = wa.left + ((wa.right - wa.left) - pillW) / 2;
    int pillY = wa.bottom - pillH - (wa.bottom - wa.top) / 10;
    s_x = pillX - margin;
    s_y = pillY - margin;

    int circleCx = margin + padL + circleD / 2;
    int circleCy = margin + pillH / 2;
    RECT glyphRc = { circleCx - circleD / 2, circleCy - circleD / 2,
                     circleCx + circleD / 2, circleCy + circleD / 2 };
    RECT textRc  = { margin + padL + circleD + gap, margin, margin + padL + circleD + gap + textW, margin + pillH };

    bool ok = RenderPill(margin, pillW, pillH, textRc, glyphRc, circleD / 2, circleCx, circleCy, font, iconFont);
    DeleteObject(font);
    DeleteObject(iconFont);
    if (!ok) return;

    bool anim = Ui_AnimationsEnabled();
    DWORD now = GetTickCount();

    if (!s_hOsd)
    {
        s_hOsd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            kOsdClass, L"", WS_POPUP, s_x, s_y, s_w, s_h, NULL, NULL, g_hInst, NULL);
        if (!s_hOsd) { FreeBitmap(); return; }
        s_phase = anim ? PH_IN : PH_HOLD;
        s_t0 = now;
        s_pulse0 = 0;
        Present();
        ShowWindow(s_hOsd, SW_SHOWNOACTIVATE);
        if (anim) StartAnimTimer();
        else SetTimer(s_hOsd, kHoldTimer, kHoldMs, NULL);
        return;
    }

    // Qayta chaqiruv: matn yangilanadi, yengil pulse, taymer qayta boshlanadi
    SetWindowPos(s_hOsd, HWND_TOPMOST, s_x, s_y, s_w, s_h, SWP_NOACTIVATE | SWP_NOREDRAW);
    if (s_phase == PH_OUT) { s_phase = PH_HOLD; s_t0 = now; }
    if (s_phase == PH_HOLD)
    {
        KillTimer(s_hOsd, kHoldTimer);
        SetTimer(s_hOsd, kHoldTimer, kHoldMs, NULL);
    }
    if (anim) { s_pulse0 = now; StartAnimTimer(); }
    Present();
}

// ===========================================================================
// Osd_Ask — bosiladigan so'rov popup'i (fokus olmaydi).
// Pill bilan bir xil dizayn tili (SDF kartochka, shisha fon, soya), lekin kattaroq:
// chapda accent glif, 1–2 qator matn, pastda ikki tugma (primary / ghost).
// Alohida oyna sinfi va holat: Osd_Show ga ta'sir qilmaydi.
// ===========================================================================
typedef void (*OsdAskFn)(bool accepted, void* ctx);   // app.h dagi e'lon bilan bir xil
void Osd_Ask(const wchar_t* text, const wchar_t* yes, const wchar_t* no, OsdAskFn cb, void* ctx);

static const wchar_t* kAskClass = L"HarfbekOsdAsk";
static const UINT_PTR kAskTimer = 1;       // avto-yopilish (bir martalik)
static const UINT     kAskMs    = 8000;
static const DWORD    kAskInMs  = 200;
static const DWORD    kAskOutMs = 200;

enum { AB_NONE = -1, AB_YES = 0, AB_NO = 1 };

struct AskState
{
    HWND     hwnd;
    HBITMAP  bmp;
    DWORD*   bits;
    int      w, h, x, y, dpi;
    int      margin, cardW, cardH, radius;
    int      circleCx, circleCy, circleR;
    RECT     glyphRc, textRc, btn[2];
    OsdPhase phase;
    DWORD    t0;
    bool     animTimer, tracking;
    int      hot, press;
    HFONT    fText, fBtn, fIcon;
    OsdAskFn cb;
    void*    ctx;
    wchar_t  text[160], label[2][48];
};
static AskState s_ask;

static void AskFreeBitmap()
{
    if (s_ask.bmp) { DeleteObject(s_ask.bmp); s_ask.bmp = NULL; }
    s_ask.bits = NULL;
}

// Kartochkani DIB ga chizadi (hover/press o'zgarganda qayta chaqiriladi)
static bool AskRender()
{
    AskFreeBitmap();
    AskState& a = s_ask;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = a.w;
    bi.bmiHeader.biHeight = -a.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(NULL);
    a.bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, (void**)&a.bits, NULL, 0);
    ReleaseDC(NULL, sdc);
    if (!a.bmp || !a.bits) { AskFreeBitmap(); return false; }

    const UiTheme& th = Ui_Theme();
    COLORREF fill  = th.dark ? RGB(22, 22, 26) : RGB(252, 252, 254);
    float    fillA = 0.95f;
    float    shMax = th.dark ? 0.55f : 0.30f;
    float    bw    = (float)Ui_S(1, a.dpi);
    float    blur  = (float)Ui_S(16, a.dpi);
    float    shDy  = (float)Ui_S(5, a.dpi);

    float cx = a.w * 0.5f, cy = a.margin + a.cardH * 0.5f;
    float hw = a.cardW * 0.5f, hh = a.cardH * 0.5f, rad = (float)a.radius;
    float br = (float)Ui_S(9, a.dpi);

    for (int y = 0; y < a.h; ++y)
    {
        for (int x = 0; x < a.w; ++x)
        {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            OsdRgbf p = { 0, 0, 0, 0 };

            float ds = OsdSdBox(px, py - shDy, hw, hh, rad);
            if (ds < blur)
            {
                float t = ds <= 0.f ? 1.f : 1.f - ds / blur;
                OsdOver(p, RGB(0, 0, 0), shMax * t * t);
            }

            float d = OsdSdBox(px, py, hw, hh, rad);
            float cov = OsdClamp01(0.5f - d);
            if (cov > 0.f)
            {
                OsdOver(p, fill, fillA * cov);
                float inner = OsdClamp01(0.5f - (d + bw));
                float ring = cov - inner;
                if (ring > 0.f) OsdOver(p, th.border, 0.9f * ring);
            }

            float dc = sqrtf((x + 0.5f - a.circleCx) * (x + 0.5f - a.circleCx) +
                             (y + 0.5f - a.circleCy) * (y + 0.5f - a.circleCy)) - a.circleR;
            float cc = OsdClamp01(0.5f - dc);
            if (cc > 0.f) OsdOver(p, th.accent, cc);

            // tugmalar (yes — primary, no — ghost)
            for (int i = 0; i < 2; ++i)
            {
                const RECT& r = a.btn[i];
                if (x < r.left - 1 || x > r.right || y < r.top - 1 || y > r.bottom) continue;
                float bx = x + 0.5f - (r.left + r.right) * 0.5f, by = y + 0.5f - (r.top + r.bottom) * 0.5f;
                float bd = OsdSdBox(bx, by, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f, br);
                float bc = OsdClamp01(0.5f - bd);
                if (bc <= 0.f) continue;
                bool hot = (a.hot == i), prs = (a.press == i);
                if (i == AB_YES)
                {
                    OsdOver(p, prs ? th.accentPressed : (hot ? th.accentHover : th.accent), bc);
                }
                else
                {
                    if (hot || prs) OsdOver(p, th.text, bc * (prs ? 0.16f : 0.09f));
                    float inner = OsdClamp01(0.5f - (bd + bw));
                    float ring = bc - inner;
                    if (ring > 0.f) OsdOver(p, th.border, 0.9f * ring);
                }
            }

            DWORD A = (DWORD)(OsdClamp01(p.a) * 255.f + 0.5f);
            DWORD R = (DWORD)(OsdClamp01(p.r) * 255.f + 0.5f);
            DWORD G = (DWORD)(OsdClamp01(p.g) * 255.f + 0.5f);
            DWORD B = (DWORD)(OsdClamp01(p.b) * 255.f + 0.5f);
            a.bits[(size_t)y * a.w + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }

    CompositeMask(a.bits, a.w, a.h, a.glyphRc, L"\xE897", a.fIcon,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, th.onAccent);
    CompositeMask(a.bits, a.w, a.h, a.textRc, a.text, a.fText,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS, th.text);
    for (int i = 0; i < 2; ++i)
    {
        COLORREF c = (i == AB_YES) ? th.onAccent : ((a.hot == i || a.press == i) ? th.text : th.textDim);
        CompositeMask(a.bits, a.w, a.h, a.btn[i], a.label[i], a.fBtn,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, c);
    }
    return true;
}

// Joriy fazaga mos alpha/siljish bilan oynani yangilaydi
static void AskPresent()
{
    AskState& a = s_ask;
    if (!a.hwnd || !a.bmp) return;
    DWORD now = GetTickCount();
    float k = 1.f;
    int yoff = 0;
    if (a.phase == PH_IN)
    {
        float e = OsdEaseOut((float)(now - a.t0) / kAskInMs);
        k = e;
        yoff = (int)((1.f - e) * Ui_S(14, a.dpi));
    }
    else if (a.phase == PH_OUT)
    {
        float e = OsdEaseOut((float)(now - a.t0) / kAskOutMs);
        k = 1.f - e;
        yoff = (int)(e * Ui_S(6, a.dpi));
    }
    HDC sdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(sdc);
    HGDIOBJ ob = SelectObject(mdc, a.bmp);
    POINT dst = { a.x, a.y + yoff }, src = { 0, 0 };
    SIZE sz = { a.w, a.h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(OsdClamp01(k) * 255.f + 0.5f), AC_SRC_ALPHA };
    UpdateLayeredWindow(a.hwnd, sdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, ob);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
}

static void AskStartAnim()
{
    if (s_ask.hwnd && !s_ask.animTimer) { SetTimer(s_ask.hwnd, UI_ANIM_TIMER, 15, NULL); s_ask.animTimer = true; }
}

static void AskStopAnim()
{
    if (s_ask.hwnd && s_ask.animTimer) { KillTimer(s_ask.hwnd, UI_ANIM_TIMER); s_ask.animTimer = false; }
}

static int AskHit(LPARAM lp)
{
    POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
    for (int i = 0; i < 2; ++i)
        if (PtInRect(&s_ask.btn[i], pt)) return i;
    return AB_NONE;
}

static void AskRepaint()
{
    if (AskRender()) AskPresent();
}

// Natijani aynan bir marta yetkazadi va yopadi (animatsiya bilan yoki darhol)
static void AskFinish(bool accepted)
{
    AskState& a = s_ask;
    if (!a.hwnd) return;
    OsdAskFn cb = a.cb;
    void* ctx = a.ctx;
    a.cb = NULL;
    KillTimer(a.hwnd, kAskTimer);
    if (a.phase != PH_OUT && Ui_AnimationsEnabled())
    {
        a.phase = PH_OUT;
        a.t0 = GetTickCount();
        a.hot = a.press = AB_NONE;
        AskStartAnim();
        AskRepaint();
    }
    else if (a.phase != PH_OUT)
    {
        DestroyWindow(a.hwnd);
    }
    if (cb) cb(accepted, ctx);
}

static LRESULT CALLBACK AskProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    AskState& a = s_ask;
    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, a.hot != AB_NONE && a.phase != PH_OUT ? IDC_HAND : IDC_ARROW));
        return TRUE;

    case WM_MOUSEMOVE:
    {
        if (a.phase == PH_OUT) return 0;
        if (!a.tracking)
        {
            TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, h, 0 };
            if (TrackMouseEvent(&te)) a.tracking = true;
            KillTimer(h, kAskTimer);            // sichqoncha ustida — taymer to'xtaydi
        }
        int hit = AskHit(lp);
        if (hit != a.hot) { a.hot = hit; AskRepaint(); }
        return 0;
    }

    case WM_MOUSELEAVE:
        a.tracking = false;
        if (a.phase == PH_OUT) return 0;
        if (a.hot != AB_NONE) { a.hot = AB_NONE; AskRepaint(); }
        SetTimer(h, kAskTimer, kAskMs, NULL);  // chiqqanda qayta boshlanadi
        return 0;

    case WM_LBUTTONDOWN:
    {
        if (a.phase == PH_OUT) return 0;
        int hit = AskHit(lp);
        if (hit != AB_NONE) { a.press = hit; a.hot = hit; SetCapture(h); AskRepaint(); }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        int pressed = a.press;
        if (GetCapture() == h) ReleaseCapture();
        a.press = AB_NONE;
        if (a.phase == PH_OUT || pressed == AB_NONE) return 0;
        int hit = AskHit(lp);
        if (hit == pressed) AskFinish(pressed == AB_YES);
        else { a.hot = hit; AskRepaint(); }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (a.press != AB_NONE) { a.press = AB_NONE; if (a.phase != PH_OUT) AskRepaint(); }
        return 0;

    case WM_TIMER:
        if (wp == kAskTimer) { AskFinish(false); return 0; }
        if (wp == UI_ANIM_TIMER)
        {
            DWORD now = GetTickCount();
            if (a.phase == PH_IN && now - a.t0 >= kAskInMs) { a.phase = PH_HOLD; AskStopAnim(); AskPresent(); }
            else if (a.phase == PH_OUT && now - a.t0 >= kAskOutMs) { DestroyWindow(h); }
            else AskPresent();
        }
        return 0;

    case WM_DESTROY:
    {
        // tugallanmagan so'rov (masalan, oyna tashqaridan yopildi) — cb(false) aynan bir marta
        OsdAskFn cb = a.cb;
        void* ctx = a.ctx;
        KillTimer(h, kAskTimer);
        KillTimer(h, UI_ANIM_TIMER);
        AskFreeBitmap();
        if (a.fText) DeleteObject(a.fText);
        if (a.fBtn)  DeleteObject(a.fBtn);
        if (a.fIcon) DeleteObject(a.fIcon);
        ZeroMemory(&a, sizeof(a));
        if (cb) cb(false, ctx);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int AskTextWidth(HDC dc, const wchar_t* s)
{
    RECT r = { 0, 0, 0, 0 };
    DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    return r.right - r.left;
}

void Osd_Ask(const wchar_t* text, const wchar_t* yes, const wchar_t* no, OsdAskFn cb, void* ctx)
{
    // Oldingi so'rov almashtiriladi: darhol yopiladi, uning cb(false) si WM_DESTROY da
    for (int guard = 0; s_ask.hwnd && guard < 4; ++guard)
        DestroyWindow(s_ask.hwnd);

    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = AskProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = kAskClass;
        if (!RegisterClassExW(&wc)) { if (cb) cb(false, ctx); return; }
        s_registered = true;
    }

    Ui_RefreshTheme();                      // joriy mavzuga mos

    AskState& a = s_ask;
    ZeroMemory(&a, sizeof(a));
    a.hot = a.press = AB_NONE;
    a.cb = cb;
    a.ctx = ctx;
    lstrcpynW(a.text, text ? text : L"", (int)(sizeof(a.text) / sizeof(a.text[0])));
    lstrcpynW(a.label[AB_YES], yes ? yes : L"", 48);
    lstrcpynW(a.label[AB_NO],  no  ? no  : L"", 48);

    HDC sdc = GetDC(NULL);
    a.dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    int dpi = a.dpi;
    a.fText = Ui_Font(dpi, 15, FW_SEMIBOLD);
    a.fBtn  = Ui_Font(dpi, 14, FW_SEMIBOLD);
    a.fIcon = Ui_IconFont(dpi, 15);

    int M = Ui_S(18, dpi), pad = Ui_S(16, dpi), circD = Ui_S(30, dpi), gap = Ui_S(12, dpi);
    int textW = Ui_S(264, dpi), btnH = Ui_S(32, dpi), btnGap = Ui_S(8, dpi);
    int rowGap = Ui_S(14, dpi), padB = Ui_S(14, dpi);

    // matn: 1–2 qator (so'z o'rash), ortig'i ko'p nuqta bilan kesiladi
    HGDIOBJ old = SelectObject(sdc, a.fText);
    TEXTMETRICW tm = {};
    GetTextMetricsW(sdc, &tm);
    RECT tr = { 0, 0, textW, 0 };
    DrawTextW(sdc, a.text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int textH = tr.bottom - tr.top;
    if (textH > 2 * tm.tmHeight) textH = 2 * tm.tmHeight;
    if (textH < tm.tmHeight) textH = tm.tmHeight;

    // tugma kengliklari
    SelectObject(sdc, a.fBtn);
    int bw[2];
    int minBw = Ui_S(84, dpi), bpad = Ui_S(18, dpi);
    for (int i = 0; i < 2; ++i)
    {
        bw[i] = AskTextWidth(sdc, a.label[i]) + 2 * bpad;
        if (bw[i] < minBw) bw[i] = minBw;
    }
    SelectObject(sdc, old);
    ReleaseDC(NULL, sdc);

    int rowW = circD + gap + textW;                  // tugmalar qatori kengligi (kartochka ichki kengligi)
    if (bw[0] + btnGap + bw[1] > rowW)
    {
        int each = (rowW - btnGap) / 2;
        if (bw[0] > each) bw[0] = each;
        if (bw[1] > each) bw[1] = each;
    }

    int blockH = textH > circD ? textH : circD;
    a.margin = M;
    a.cardW  = pad + circD + gap + textW + pad;
    a.cardH  = pad + blockH + rowGap + btnH + padB;
    a.radius = Ui_S(16, dpi);
    a.w = a.cardW + 2 * M;
    a.h = a.cardH + 2 * M;

    a.circleR  = circD / 2;
    a.circleCx = M + pad + circD / 2;
    a.circleCy = M + pad + blockH / 2;
    if (textH > circD) a.circleCy = M + pad + circD / 2;   // 2 qatorda glif birinchi qator bilan tekis
    a.glyphRc  = { a.circleCx - circD / 2, a.circleCy - circD / 2, a.circleCx + circD / 2, a.circleCy + circD / 2 };
    int textTop = M + pad + (blockH - textH) / 2;
    a.textRc   = { M + pad + circD + gap, textTop, M + pad + circD + gap + textW, textTop + textH };

    int btnTop = M + pad + blockH + rowGap;
    int right  = M + a.cardW - pad;
    a.btn[AB_YES] = { right - bw[AB_YES], btnTop, right, btnTop + btnH };
    a.btn[AB_NO]  = { a.btn[AB_YES].left - btnGap - bw[AB_NO], btnTop, a.btn[AB_YES].left - btnGap, btnTop + btnH };

    // Joylashuv: asosiy monitor ish maydonining o'ng pastki burchagi (tray yonida).
    // tor ekranda Osd_Show pill'i (pastki markaz) bilan ustma-ust tushmasin — Ask yuqoriroqda.
    POINT origin = { 0, 0 };
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& wa = mi.rcWork;
    int edge = Ui_S(16, dpi);
    int cardX = wa.right - a.cardW - edge;
    int cardY = wa.bottom - a.cardH - edge;
    int pillTop = wa.bottom - (wa.bottom - wa.top) / 10 - Ui_S(48, dpi);
    int midX = wa.left + (wa.right - wa.left) / 2;
    if (cardX < midX + Ui_S(260, dpi) && cardY + a.cardH > pillTop - Ui_S(10, dpi))
        cardY = pillTop - Ui_S(10, dpi) - a.cardH;
    if (cardX < wa.left) cardX = wa.left;
    if (cardY < wa.top) cardY = wa.top;
    a.x = cardX - M;
    a.y = cardY - M;

    if (!AskRender()) goto fail;

    a.hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kAskClass, L"", WS_POPUP, a.x, a.y, a.w, a.h, NULL, NULL, g_hInst, NULL);
    if (!a.hwnd) goto fail;

    {
        bool anim = Ui_AnimationsEnabled();
        a.phase = anim ? PH_IN : PH_HOLD;
        a.t0 = GetTickCount();
        AskPresent();
        ShowWindow(a.hwnd, SW_SHOWNOACTIVATE);
        SetTimer(a.hwnd, kAskTimer, kAskMs, NULL);
        if (anim) AskStartAnim();
    }
    return;

fail:
    {
        // yaratilmadi — cb(false) aynan bir marta, holat tozalanadi
        OsdAskFn fcb = a.cb;
        void* fctx = a.ctx;
        AskFreeBitmap();
        if (a.fText) DeleteObject(a.fText);
        if (a.fBtn)  DeleteObject(a.fBtn);
        if (a.fIcon) DeleteObject(a.fIcon);
        ZeroMemory(&a, sizeof(a));
        if (fcb) fcb(false, fctx);
    }
}
