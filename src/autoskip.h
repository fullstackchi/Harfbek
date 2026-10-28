#pragma once
// Avto rejimda inglizcha (yoki o'zbekchaga xos bo'lmagan) so'zlarni aniqlash — sof funksiya.
//
// w — KICHIK harfli, apostroflar ' ga keltirilgan asl so'z (masalan shop, o'clock, who's).
// true: ichki inglizcha ro'yxatda bor (qo'shimchalar 's 'll 're 've 'd 'm 't n't olib tashlab
// ham tekshiriladi) YOKI evristika: so'zda w bor yoki "h" dan oldin turmagan c bor.
// Hook thread'idan chaqiriladi: heap yo'q, Win32 yo'q.
bool AutoSkip_IsForeign(const wchar_t* w, int len);