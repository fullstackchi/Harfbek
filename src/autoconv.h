#pragma once
// Avto rejim holat mashinasi (sof mantiq — Win32 chaqiruvlari yo'q, alohida
// test qilinadi). Hook har yozilgan belgini beradi, mashina nima qilishni
// aytadi: hech narsa / oldingi belgini almashtirish / oxirgi almashtirishni
// bekor qilish (undo).
//
//   o' g' sh ch  ->  ö ğ ş ç   (katta-kichiklik birinchi harf bo'yicha:
//                              O'->Ö, Sh/SH->Ş, sh/sH->ş)
//   almashtirishdan keyin DARHOL Backspace -> asl ikki belgi qaytadi.
//
// So'z kuzatuvi: joriy so'zning (harflar + apostroflar) asl belgilari va
// ekrandagi har belgi nechta asl belgidan iboratligi saqlanadi. So'z
// tugaganda (Auto_OnWordEnd) unda almashtirish bo'lgan bo'lsa va so'z
// istisno bo'lsa (inglizcha, foydalanuvchi so'zi) — asliga qaytarish rejasi
// beriladi: ekrandagi so'z o'chiriladi, asl belgilar qayta yoziladi.

enum AutoActKind {
    AUTO_NONE = 0,   // tugma o'z yo'liga o'tadi
    AUTO_REPLACE,    // tugma yutiladi; 1 belgi o'chiriladi (Backspace), text yoziladi
    AUTO_UNDO,       // Backspace yutiladi; 1 belgi o'chiriladi, text (asl 2 belgi) yoziladi
};

struct AutoAction {
    AutoActKind kind;
    wchar_t     text[2];   // yoziladigan belgilar
    int         len;       // text dagi belgilar soni (REPLACE: 1, UNDO: 2)
};

#define AUTO_WORD_MAX 32   // undan uzun so'z kuzatilmaydi (hech narsa qaytarilmaydi)

struct AutoState {
    wchar_t prev;       // oxirgi yozilgan belgi (0 — noma'lum / almashtirib bo'lmaydi)
    wchar_t undo[2];    // oxirgi almashtirishning asl 2 belgisi
    bool    canUndo;    // oxirgi tugma almashtirish edimi (undo mumkinmi)
    // joriy so'z
    wchar_t       orig[AUTO_WORD_MAX];   // asl yozilgan belgilar (registri bilan)
    unsigned char span[AUTO_WORD_MAX];   // ekrandagi har belgi: 1 — oddiy, 2 — almashtirilgan
    int           origLen;
    int           scrLen;                // ekrandagi so'z belgilari soni
    bool          lost;                  // so'z kuzatuvi uzildi (uzun so'z, so'z boshidan oldinga Backspace)
    int           undone;                // >0 — undo qilingan juftlik ekranda [undone-2, undone) da
                                         // (o'rganish nomzodi); Backspace uni o'chirsa — 0
};

// So'z istisnomi: w — kichik harfli, apostroflar ' ga keltirilgan asl so'z
// (boshi va oxiridagi apostroflarsiz; qaytariladigan matn ularni saqlaydi)
typedef bool (*AutoSkipFn)(const wchar_t* w, int len, void* ctx);

// Holatni (so'z ham) tozalaydi (navigatsiya, sichqoncha, fokus almashishi, Ctrl/Alt/Win...)
void Auto_Reset(AutoState& s);

// Apostrof belgisimi: '  `  ʻ(02BB)  ’(2019)  ‘(2018)
bool Auto_IsApostrophe(wchar_t c);

// Belgili tugma yozildi. ch — yozilgan belgi (harf registri bilan, apostrof);
// boshqa yoki noma'lum belgi uchun 0 (so'z qaytarishsiz tugaydi). rules —
// yoqilgan qoidalar (AUTO_RULE_* bitlari, settings.h): o'chiq qoida juftligi almashmaydi.
AutoAction Auto_OnChar(AutoState& s, wchar_t ch, unsigned rules);

// Backspace bosildi: almashtirishdan darhol keyin bo'lsa — undo, aks holda
// holat tozalanadi va Backspace o'z yo'liga o'tadi. So'z buferi ham moslanadi.
AutoAction Auto_OnBackspace(AutoState& s);

// So'z tugadi (bo'shliq, tinish belgisi, raqam, Enter, Tab). So'zda kamida bitta
// almashtirish qolgan va skip(so'z) true bo'lsa — true: *bs ta ekrandagi belgi
// o'chirilsin, text[0..*len) (asl belgilar, text kamida AUTO_WORD_MAX) yozilsin.
// Har holda so'z va juftlik holati tozalanadi.
bool Auto_OnWordEnd(AutoState& s, AutoSkipFn skip, void* ctx, wchar_t* text, int* len, int* bs);

// So'z o'rganish nomzodi: so'zda undo bo'lgan va kuzatuv uzilmagan bo'lsa —
// kalit (Auto_OnWordEnd dagidek: kichik harf, apostrof ', chetdagi apostroflarsiz)
// va ko'rinishi (asl registr bilan, xuddi shu qism), ikkalasi AUTO_WORD_MAX
// sig'imli. Qaytaradi — uzunlik (0 — nomzod emas). Auto_OnWordEnd dan OLDIN chaqiriladi.
int Auto_LearnKey(const AutoState& s, wchar_t* key, wchar_t* disp);

// O'rganish jadvali: belgilangan o'lcham, LRU (heap'siz). Thread-xavfsiz emas —
// chaqiruvchi qulflaydi.
#define LEARN_SLOTS 16
struct LearnSlot {
    wchar_t       key[AUTO_WORD_MAX];
    unsigned char len;        // 0 — bo'sh
    unsigned char count;      // necha marta nomzod bo'ldi
    bool          done;       // so'raldi yoki "Kerak emas" — qayta so'ralmaydi
    unsigned      tick;       // oxirgi ishlatilgan (LRU)
};
struct LearnTable { LearnSlot s[LEARN_SLOTS]; unsigned clock; };
// Nomzod hisoblagichi +1; true — 2 ga yetdi va hali so'ralmagan: so'rash kerak
// (slot "so'raldi" deb belgilanadi — sessiyada bir marta)
bool Learn_Hit(LearnTable& t, const wchar_t* key, int len);
// So'z rad etildi — jadvalda belgilanadi (yo'q bo'lsa qo'shiladi)
void Learn_Decline(LearnTable& t, const wchar_t* key, int len);
