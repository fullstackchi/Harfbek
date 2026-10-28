// Avto rejim holat mashinasi: o' g' sh ch -> ö ğ ş ç (autoconv.h).
// Maxsus harflar (hex — encoding'ga bog'liq emas):
//   ö=00F6 Ö=00D6  ğ=011F Ğ=011E  ş=015F Ş=015E  ç=00E7 Ç=00C7
#include "autoconv.h"
#include "settings.h"   // AUTO_RULE_* (faqat makrolar)

// Juftlik holati (prev/undo) — so'z buferiga tegmaydi
static void ResetPair(AutoState& s)
{
    s.prev    = 0;
    s.undo[0] = s.undo[1] = 0;
    s.canUndo = false;
}

static void ResetWord(AutoState& s)
{
    s.origLen = 0;
    s.scrLen  = 0;
    s.lost    = false;
    s.undone  = 0;
}

void Auto_Reset(AutoState& s)
{
    ResetPair(s);
    ResetWord(s);
}

bool Auto_IsApostrophe(wchar_t c)
{
    return c == L'\'' || c == L'`' ||
           c == (wchar_t)0x02BB || c == (wchar_t)0x2019 || c == (wchar_t)0x2018;
}

// prev + ch juftligi maxsus harf beradimi. Registr — birinchi harfniki.
// O'chiq qoida (rules da biti yo'q) bo'yicha juftlik almashmaydi.
static wchar_t Combine(wchar_t prev, wchar_t ch, unsigned rules)
{
    if (Auto_IsApostrophe(ch)) {
        switch (prev) {
        case L'o': return (rules & AUTO_RULE_O) ? (wchar_t)0x00F6 : 0;
        case L'O': return (rules & AUTO_RULE_O) ? (wchar_t)0x00D6 : 0;
        case L'g': return (rules & AUTO_RULE_G) ? (wchar_t)0x011F : 0;
        case L'G': return (rules & AUTO_RULE_G) ? (wchar_t)0x011E : 0;
        }
        return 0;
    }
    if (ch == L'h' || ch == L'H') {
        switch (prev) {
        case L's': return (rules & AUTO_RULE_SH) ? (wchar_t)0x015F : 0;
        case L'S': return (rules & AUTO_RULE_SH) ? (wchar_t)0x015E : 0;
        case L'c': return (rules & AUTO_RULE_CH) ? (wchar_t)0x00E7 : 0;
        case L'C': return (rules & AUTO_RULE_CH) ? (wchar_t)0x00C7 : 0;
        }
    }
    return 0;
}

AutoAction Auto_OnChar(AutoState& s, wchar_t ch, unsigned rules)
{
    AutoAction a = { AUTO_NONE, { 0, 0 }, 0 };
    if (!ch) {                   // so'zga kirmaydigan belgi — so'z qaytarishsiz tugaydi
        ResetPair(s);
        ResetWord(s);
        return a;
    }
    wchar_t sp = Combine(s.prev, ch, rules);

    // So'z buferi: asl belgi qo'shiladi; almashtirishda oldingi ekrandagi
    // belgi endi ikki asl belgini bildiradi (s + h -> ş).
    if (!s.lost) {
        if (s.origLen >= AUTO_WORD_MAX || (sp && s.scrLen == 0)) {
            s.lost = true;       // juda uzun so'z — oxirigacha kuzatilmaydi
        } else {
            s.orig[s.origLen++] = ch;
            if (sp) s.span[s.scrLen - 1] = 2;
            else    s.span[s.scrLen++] = 1;
        }
    }

    if (!sp) {
        s.prev    = ch;
        s.canUndo = false;
        return a;
    }
    a.kind    = AUTO_REPLACE;
    a.text[0] = sp;
    a.len     = 1;
    s.undo[0] = s.prev;
    s.undo[1] = ch;
    s.canUndo = true;
    s.prev    = sp;          // maxsus harf hech narsa bilan juftlashmaydi (shh -> şh, o'' -> ö')
    return a;
}

AutoAction Auto_OnBackspace(AutoState& s)
{
    AutoAction a = { AUTO_NONE, { 0, 0 }, 0 };
    if (s.canUndo) {
        a.kind    = AUTO_UNDO;
        a.text[0] = s.undo[0];
        a.text[1] = s.undo[1];
        a.len     = 2;
        // ş -> sh: ekrandagi bitta belgi yana ikki oddiy belgi (almashtirish emas)
        if (!s.lost && s.scrLen > 0 && s.span[s.scrLen - 1] == 2) {
            s.span[s.scrLen - 1] = 1;
            s.span[s.scrLen++]   = 1;
            s.undone = s.scrLen;
        }
    } else if (!s.lost) {
        if (s.scrLen > 0) s.origLen -= s.span[--s.scrLen];   // oxirgi ekrandagi belgi o'chdi
        else              s.lost = true;                     // so'z boshidan oldingi matn o'chyapti
        if (s.scrLen < s.undone) s.undone = 0;               // undo qilingan juftlik o'chdi
    }
    // Undo'dan keyin ham tozalaymiz: tiklangan "sh" qayta almashmasin
    // (yana "h" -> "shh"), ikkinchi Backspace esa oddiy o'chiradi.
    ResetPair(s);
    return a;
}

// Tekshiruv kaliti: kichik harf (so'zda faqat lotin harflari), apostrof -> '.
// Boshi/oxiridagi apostroflar so'zga kirmaydi: 'shop' (qo'shtirnoq), teachers'.
// key[*b..*e) — kalit; *e > *b bo'lmasa bo'sh.
static void BuildKey(const AutoState& s, wchar_t* key, int* b, int* e)
{
    for (int i = 0; i < s.origLen; ++i) {
        wchar_t c = s.orig[i];
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
        else if (Auto_IsApostrophe(c)) c = L'\'';
        key[i] = c;
    }
    *b = 0; *e = s.origLen;
    while (*b < *e && key[*b] == L'\'')     ++*b;
    while (*e > *b && key[*e - 1] == L'\'') --*e;
}

int Auto_LearnKey(const AutoState& s, wchar_t* key, wchar_t* disp)
{
    if (s.undone <= 0 || s.lost || s.origLen <= 0) return 0;
    wchar_t k[AUTO_WORD_MAX];
    int b, e;
    BuildKey(s, k, &b, &e);
    for (int i = b; i < e; ++i) { key[i - b] = k[i]; disp[i - b] = s.orig[i]; }
    return e > b ? e - b : 0;
}

bool Auto_OnWordEnd(AutoState& s, AutoSkipFn skip, void* ctx, wchar_t* text, int* len, int* bs)
{
    bool revert = false;
    if (!s.lost && s.origLen > 0 && skip) {
        int replaced = 0;
        for (int i = 0; i < s.scrLen; ++i) if (s.span[i] == 2) replaced++;
        if (replaced) {
            wchar_t key[AUTO_WORD_MAX];
            int b, e;
            BuildKey(s, key, &b, &e);
            if (e > b && skip(key + b, e - b, ctx)) {
                for (int i = 0; i < s.origLen; ++i) text[i] = s.orig[i];
                *len   = s.origLen;
                *bs    = s.scrLen;
                revert = true;
            }
        }
    }
    Auto_Reset(s);
    return revert;
}

// Kalit uchun slot: bor bo'lsa o'sha, aks holda bo'sh yoki eng eski (LRU) —
// u tozalanib shu kalitga beriladi. Har holda tick yangilanadi (jadval
// nol bilan boshlanadi; clock 2^32 ga yetmaydi — sessiyada shuncha nomzod bo'lmaydi).
static LearnSlot& LearnFind(LearnTable& t, const wchar_t* key, int len)
{
    if (len > AUTO_WORD_MAX) len = AUTO_WORD_MAX;
    LearnSlot* old = &t.s[0];
    for (int i = 0; i < LEARN_SLOTS; ++i) {
        LearnSlot& x = t.s[i];
        if (x.len == len) {
            int j = 0;
            while (j < len && x.key[j] == key[j]) ++j;
            if (j == len) { x.tick = ++t.clock; return x; }
        }
        if (x.tick < old->tick) old = &x;      // bo'sh slotda tick = 0 — birinchi navbatda
    }
    for (int j = 0; j < len; ++j) old->key[j] = key[j];
    old->len      = (unsigned char)len;
    old->count    = 0;
    old->done     = false;
    old->tick     = ++t.clock;
    return *old;
}

bool Learn_Hit(LearnTable& t, const wchar_t* key, int len)
{
    if (len <= 0) return false;
    LearnSlot& x = LearnFind(t, key, len);
    if (x.count < 255) x.count++;
    if (x.count < 2 || x.done) return false;
    x.done = true;
    return true;
}

void Learn_Decline(LearnTable& t, const wchar_t* key, int len)
{
    if (len > 0) LearnFind(t, key, len).done = true;
}
