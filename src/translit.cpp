// Lotin -> kirill o'girish (sof funksiya, Win32'siz).
// Kirish: yangi (ö ğ ş ç) va eski (o' g' sh ch) lotin yozuvi.
//
// Ma'lum cheklov: lotincha xorijiy so'zlar (Windows, Chrome) ham o'giriladi —
// bu kutilgan; foydalanuvchi o'giradigan matnni o'zi belgilaydi. Yolg'iz `c`
// va `w` o'zgarishsiz qoladi (o'zbek kirilida ularga mos harf yo'q).
#include "translit.h"
#include <cwctype>

// Apostrof variantlari (convert.cpp IsApo bilan bir xil to'plam)
static bool TlApo(wchar_t c)
{
    return c == L'\'' || c == L'`' ||
           c == (wchar_t)0x02BB || c == (wchar_t)0x2018 ||
           c == (wchar_t)0x2019 || c == (wchar_t)0x00B4 ||
           c == (wchar_t)0x2032;
}

// Kichik harfga o'tkazadi (lotin harflari va ö ğ ş ç); up — katta bo'lganmi
static wchar_t TlLower(wchar_t c, bool* up)
{
    *up = false;
    if (c >= L'A' && c <= L'Z') { *up = true; return (wchar_t)(c + 32); }
    if (c == 0x00D6 || c == 0x00C7) { *up = true; return (wchar_t)(c + 0x20); }
    if (c == 0x011E || c == 0x015E) { *up = true; return (wchar_t)(c + 1); }
    return c;
}

// Kirill kichik harfni katta qiladi
static wchar_t TlCyrUpper(wchar_t c)
{
    if (c == 0x0451 || c == 0x045E) return (wchar_t)(c - 0x50);   // ё ў
    if (c == 0x049B || c == 0x0493 || c == 0x04B3) return (wchar_t)(c - 1); // қ ғ ҳ
    return (wchar_t)(c - 0x20);                                    // а..я
}

static bool TlLetter(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || iswalpha(c) != 0;
}

// a..z (kichik) uchun kirill; 0 — o'zgarishsiz (c, w)
static wchar_t TlMap(wchar_t l)
{
    switch (l) {
    case L'a': return 0x0430; case L'b': return 0x0431; case L'd': return 0x0434;
    case L'f': return 0x0444; case L'g': return 0x0433; case L'h': return 0x04B3;
    case L'i': return 0x0438; case L'j': return 0x0436; case L'k': return 0x043A;
    case L'l': return 0x043B; case L'm': return 0x043C; case L'n': return 0x043D;
    case L'o': return 0x043E; case L'p': return 0x043F; case L'q': return 0x049B;
    case L'r': return 0x0440; case L's': return 0x0441; case L't': return 0x0442;
    case L'u': return 0x0443; case L'v': return 0x0432; case L'x': return 0x0445;
    case L'y': return 0x0439; case L'z': return 0x0437; case L'e': return 0x0435;
    case 0x00F6: return 0x045E;   // ö -> ў
    case 0x011F: return 0x0493;   // ğ -> ғ
    case 0x015F: return 0x0448;   // ş -> ш
    case 0x00E7: return 0x0447;   // ç -> ч
    }
    return 0;
}

// i dan oldingi lotin unlisi (a e i o u ö, o')mi
static bool TlVowelBefore(const std::wstring& s, size_t i)
{
    if (i == 0) return false;
    bool up;
    wchar_t p = TlLower(s[i - 1], &up);
    if (p == L'a' || p == L'e' || p == L'i' || p == L'o' || p == L'u' || p == 0x00F6) return true;
    if (TlApo(s[i - 1]) && i >= 2)                       // o' = ў (unli)
        return TlLower(s[i - 2], &up) == L'o';
    return false;
}

// i dan oldin undosh harf bormi (unli ham, apostrof ham emas)
static bool TlConsBefore(const std::wstring& s, size_t i)
{
    if (i == 0 || !TlLetter(s[i - 1])) return false;
    return !TlVowelBefore(s, i);
}

std::wstring ToCyrillic(const std::wstring& in)
{
    std::wstring o;
    o.reserve(in.size());
    const size_t n = in.size();
    for (size_t i = 0; i < n; ++i) {
        wchar_t c = in[i];
        bool    up;
        wchar_t l = TlLower(c, &up);

        // Tutuq belgisi: harfdan keyin va harfdan oldin turgan apostrof -> ъ
        if (TlApo(c)) {
            bool su;                                    // s'h -> сҳ (ajratuvchi apostrof tushadi)
            if (i > 0 && i + 1 < n && TlLower(in[i - 1], &su) == L's' &&
                TlLower(in[i + 1], &su) == L'h') continue;
            bool pvL = !o.empty() && TlLetter(o.back());
            bool nxL = (i + 1 < n) && TlLetter(in[i + 1]);
            if (pvL && nxL) o.push_back((wchar_t)0x044A);
            else            o.push_back(c);
            continue;
        }

        wchar_t r = 0;          // natija (kichik kirill)
        size_t  used = 1;
        wchar_t n1 = (i + 1 < n) ? in[i + 1] : 0;
        bool    u1;
        wchar_t l1 = TlLower(n1, &u1);

        if ((l == L'o' || l == L'g') && TlApo(n1)) {            // o' g'
            r = (l == L'o') ? 0x045E : 0x0493; used = 2;
        } else if (l == L's' && l1 == L'h') {                   // sh
            r = 0x0448; used = 2; up = up || u1;
        } else if (l == L'c' && l1 == L'h') {                   // ch
            r = 0x0447; used = 2; up = up || u1;
        } else if (l == L'y' && (l1 == L'o' || l1 == L'u' || l1 == L'a' || l1 == L'e')) {
            // yo' -> й + ў (yo -> ё emas); aks holda yo ё, yu ю, ya я, ye е
            if (l1 == L'o' && i + 2 < n && TlApo(in[i + 2])) {
                r = 0x0439;
            } else if (l1 == L'e' && TlConsBefore(in, i)) {   // obyekt -> объект
                o.push_back(up || u1 ? (wchar_t)0x042A : (wchar_t)0x044A);
                r = 0x0435; used = 2; up = up || u1;
            } else {
                r = l1 == L'o' ? 0x0451 : l1 == L'u' ? 0x044E : l1 == L'a' ? 0x044F : 0x0435;
                used = 2; up = up || u1;
            }
        } else if (l == L'e') {                                  // so'z boshida э
            bool start = o.empty() || !TlLetter(in[i - 1]);
            r = (start || TlVowelBefore(in, i)) ? 0x044D : 0x0435;
        } else {
            r = TlMap(l);
        }

        if (!r) { o.push_back(c); continue; }                    // lotin bo'lmagan / c, w
        o.push_back(up ? TlCyrUpper(r) : r);
        i += used - 1;
    }
    return o;
}
