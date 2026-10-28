// Avto rejimda inglizcha so'zlarni aniqlash (sof funksiya: Win32 yo'q, heap yo'q).
// Hook thread'idan chaqiriladi. Qarang: autoskip.h.
#include "autoskip.h"

// Ichki inglizcha ro'yxat: avto rejimda buziladigan (ichida sh/ch yoki o'/g' bor) eng ko'p
// ishlatiladigan so'zlar va ularning keng tarqalgan shakllari. Faqat ASCII kichik harf va '.
// Ixcham saqlanadi: bitta NUL-ajratilgan satr + unsigned short offsetlar (pointer massivi emas).
//
// ATAYLAB CHIQARILGAN (o'zbekcha so'z bilan to'qnashadi yoki shubhali — kiritmang!):
//   chat, chin, chip(s), chop, chap, charm, sham, gash, shah/shoh/shod/shox, osh, bosh, tosh,
//   qosh, ish, kech, och, uch, ich, kuch, finish, chest, ash, mesh, mush, lash, nash, tush,
//   josh, gosh, shun, shush.
// Ro'yxatda w yoki yolg'iz c bor so'zlar yo'q: ularni evristika ushlaydi (AutoSkip_IsForeign).
// Umumiy "-ing/-s/-ed" kesish ATAYLAB qilinmagan: "shuning" -> "shun" kabi o'zbekcha
// so'zlar noto'g'ri ushlanib qolardi. Shakllar ro'yxatda alohida turadi.

// ---- GENERATSIYA QILINGAN MA'LUMOT (scratchpad/build/light/autoskip/gen.ps1) ----
// 533 ta so'z, 4182 bayt. Saralangan (ordinal), takrorsiz; har biri NUL bilan tugaydi.
static const char kWords[] =
    "abolish\0ache\0ached\0aches\0aching\0admonish\0anguish\0approach\0approached\0"
    "approaches\0arch\0arched\0arches\0ashamed\0ashes\0ashore\0attach\0attached\0"
    "attaches\0attachment\0attachments\0banish\0bash\0batch\0batched\0batches\0beach\0"
    "beaches\0bench\0benches\0bishop\0bleach\0blemish\0blush\0branch\0branches\0brash\0"
    "breach\0british\0brunch\0brush\0bunch\0bunches\0bush\0bushes\0butcher\0chain\0"
    "chains\0chair\0chairman\0chairs\0chalk\0challenge\0challenged\0challenges\0"
    "challenging\0chamber\0chambers\0champion\0champions\0championship\0change\0changed\0"
    "changes\0changing\0channel\0channels\0chaos\0chapter\0chapters\0charge\0charged\0"
    "charger\0chargers\0charges\0charging\0charity\0charming\0chart\0charted\0charts\0"
    "chase\0chased\0chases\0chasing\0chasm\0chatbot\0chatgpt\0chatted\0chatting\0cheap\0"
    "cheaper\0cheapest\0cheat\0cheated\0cheating\0cheats\0cheddar\0cheek\0cheeks\0cheer\0"
    "cheered\0cheerful\0cheering\0cheerleader\0cheers\0cheese\0cheesy\0cheetah\0chef\0"
    "chefs\0chemist\0chemistry\0cherish\0cherry\0chess\0chief\0chihuahua\0child\0"
    "childhood\0childish\0childlike\0children\0chill\0chilled\0chilli\0chilling\0chills\0"
    "chilly\0chimney\0china\0chinese\0chirp\0choir\0choke\0choose\0chooses\0choosing\0"
    "choppy\0chord\0chords\0chores\0chorus\0chose\0chosen\0christ\0christian\0"
    "christianity\0christians\0christmas\0chrome\0chromebook\0chromium\0chromosome\0"
    "chubby\0chunk\0chunks\0church\0churches\0churn\0churning\0chute\0dash\0dashboard\0"
    "dashboards\0dashed\0demolish\0detach\0detached\0diminish\0dish\0dishes\0dishonest\0"
    "dispatch\0dispatched\0dispatcher\0dispatches\0drench\0each\0english\0enrich\0"
    "enriched\0establish\0established\0establishing\0establishment\0extinguish\0fashion\0"
    "fashioned\0fashions\0fetch\0fetched\0fetches\0fetching\0fish\0fisherman\0fishes\0"
    "fishing\0flash\0flashed\0flashing\0flashlight\0flourish\0flush\0flushed\0flushes\0"
    "flushing\0foolish\0french\0fresh\0fresher\0freshly\0freshman\0freshness\0furnish\0"
    "g'day\0garnish\0gibberish\0girlish\0gulch\0harsh\0hash\0hashed\0hashes\0hashing\0"
    "hashtag\0hashtags\0hatch\0hatched\0hatches\0hellish\0hunch\0hunched\0impoverish\0"
    "inch\0inches\0itch\0itches\0itchy\0kitchen\0kitchens\0languish\0launch\0launched\0"
    "launcher\0launches\0launching\0lavish\0leash\0leech\0lunch\0lunches\0lunchtime\0"
    "lurch\0machine\0machinery\0machines\0macho\0march\0marched\0marches\0marching\0"
    "marshes\0mashed\0mashup\0match\0matched\0matches\0matching\0matchmaker\0mismatch\0"
    "mismatched\0much\0munch\0mushroom\0mushrooms\0notch\0notched\0notches\0nourish\0"
    "o'brian\0o'brien\0o'donnell\0o'er\0o'grady\0o'hara\0o'leary\0o'malley\0o'neil\0"
    "o'neill\0o'reilly\0o'sullivan\0ostrich\0overreach\0parish\0parishes\0patch\0"
    "patched\0patches\0patching\0perish\0pinch\0pinched\0pitch\0pitched\0pitcher\0"
    "pitches\0plush\0polish\0polished\0polishing\0porch\0porches\0preach\0preacher\0"
    "publish\0published\0publisher\0publishers\0publishing\0punch\0punched\0punches\0"
    "punish\0punished\0punishment\0purchase\0purchased\0purchases\0purchasing\0push\0"
    "pushed\0pusher\0pushes\0pushing\0pushup\0radish\0ranch\0ranches\0rash\0rashes\0"
    "ravish\0reach\0reached\0reaches\0reaching\0reddish\0refresh\0refreshed\0refresher\0"
    "refreshes\0refreshing\0relish\0research\0researched\0researcher\0researchers\0"
    "researching\0reshape\0reshuffle\0retch\0rich\0riches\0rush\0rushed\0rushes\0"
    "rushing\0schedule\0scheduled\0scheduler\0schedules\0scheduling\0schema\0schemas\0"
    "scheme\0schemes\0scholar\0scholars\0scholarship\0school\0schooled\0schooling\0"
    "schools\0search\0searched\0searcher\0searches\0searching\0selfish\0shade\0shaft\0"
    "shake\0shakespeare\0shall\0shame\0shanghai\0shape\0share\0sharepoint\0shark\0sharp\0"
    "shave\0shazam\0she\0shed\0sheep\0sheepish\0sheet\0sheikh\0shelf\0shell\0shells\0"
    "shelter\0shelves\0shepherd\0sheriff\0shield\0shift\0shine\0shiny\0ship\0shipped\0"
    "shipping\0ships\0shirt\0shirts\0shoe\0shoebox\0shoes\0shoot\0shooting\0shop\0"
    "shopify\0shopping\0shops\0shore\0short\0shorter\0shorthand\0shortlist\0shorts\0"
    "should\0shoulder\0shout\0shouted\0shouting\0shouts\0shove\0shred\0shrimp\0shrine\0"
    "shrink\0shrub\0shrubs\0shrug\0shudder\0shuffle\0shut\0shutter\0shutting\0shuttle\0"
    "shy\0shyness\0sketch\0sketched\0sketches\0sketchy\0slash\0slashed\0slashes\0"
    "slashing\0slavish\0slush\0smash\0smashed\0smashes\0smashing\0snatch\0snitch\0"
    "spanish\0speech\0speeches\0splash\0splashed\0splashes\0splashing\0squash\0squashed\0"
    "squashes\0squish\0ssh\0sshd\0stash\0stashed\0stashes\0stitch\0stitched\0stitches\0"
    "stomach\0stomachs\0stretch\0stretched\0stretches\0stretching\0stylish\0such\0"
    "tarnish\0teach\0teacher\0teachers\0teaches\0teaching\0thrash\0torch\0torches\0"
    "touch\0touched\0touches\0touching\0touchpad\0touchy\0trash\0trashed\0trashes\0"
    "trashing\0trench\0turkish\0vanish\0vanished\0vanquish\0varnish\0yiddish\0zsh\0";

static const unsigned short kOff[533] = {
    0, 8, 13, 19, 25, 32, 41, 49, 58, 69, 80, 85, 92, 99, 107, 113,
    120, 127, 136, 145, 156, 168, 175, 180, 186, 194, 202, 208, 216, 222, 230, 237,
    244, 252, 258, 265, 274, 280, 287, 295, 302, 308, 314, 322, 327, 334, 342, 348,
    355, 361, 370, 377, 383, 393, 404, 415, 427, 435, 444, 453, 463, 476, 483, 491,
    499, 508, 516, 525, 531, 539, 548, 555, 563, 571, 580, 588, 597, 605, 614, 620,
    628, 635, 641, 648, 655, 663, 669, 677, 685, 693, 702, 708, 716, 725, 731, 739,
    748, 755, 763, 769, 776, 782, 790, 799, 808, 820, 827, 834, 841, 849, 854, 860,
    868, 878, 886, 893, 899, 905, 915, 921, 931, 940, 950, 959, 965, 973, 980, 989,
    996, 1003, 1011, 1017, 1025, 1031, 1037, 1043, 1050, 1058, 1067, 1074, 1080, 1087, 1094, 1101,
    1107, 1114, 1121, 1131, 1144, 1155, 1165, 1172, 1183, 1192, 1203, 1210, 1216, 1223, 1230, 1239,
    1245, 1254, 1260, 1265, 1275, 1286, 1293, 1302, 1309, 1318, 1327, 1332, 1339, 1349, 1358, 1369,
    1380, 1391, 1398, 1403, 1411, 1418, 1427, 1437, 1449, 1462, 1476, 1487, 1495, 1505, 1514, 1520,
    1528, 1536, 1545, 1550, 1560, 1567, 1575, 1581, 1589, 1598, 1609, 1618, 1624, 1632, 1640, 1649,
    1657, 1664, 1670, 1678, 1686, 1695, 1705, 1713, 1719, 1727, 1737, 1745, 1751, 1757, 1762, 1769,
    1776, 1784, 1792, 1801, 1807, 1815, 1823, 1831, 1837, 1845, 1856, 1861, 1868, 1873, 1880, 1886,
    1894, 1903, 1912, 1919, 1928, 1937, 1946, 1956, 1963, 1969, 1975, 1981, 1989, 1999, 2005, 2013,
    2023, 2032, 2038, 2044, 2052, 2060, 2069, 2077, 2084, 2091, 2097, 2105, 2113, 2122, 2133, 2142,
    2153, 2158, 2164, 2173, 2183, 2189, 2197, 2205, 2213, 2221, 2229, 2239, 2244, 2252, 2259, 2267,
    2276, 2283, 2291, 2300, 2311, 2319, 2329, 2336, 2345, 2351, 2359, 2367, 2376, 2383, 2389, 2397,
    2403, 2411, 2419, 2427, 2433, 2440, 2449, 2459, 2465, 2473, 2480, 2489, 2497, 2507, 2517, 2528,
    2539, 2545, 2553, 2561, 2568, 2577, 2588, 2597, 2607, 2617, 2628, 2633, 2640, 2647, 2654, 2662,
    2669, 2676, 2682, 2690, 2695, 2702, 2709, 2715, 2723, 2731, 2740, 2748, 2756, 2766, 2776, 2786,
    2797, 2804, 2813, 2824, 2835, 2847, 2859, 2867, 2877, 2883, 2888, 2895, 2900, 2907, 2914, 2922,
    2931, 2941, 2951, 2961, 2972, 2979, 2987, 2994, 3002, 3010, 3019, 3031, 3038, 3047, 3057, 3065,
    3072, 3081, 3090, 3099, 3109, 3117, 3123, 3129, 3135, 3147, 3153, 3159, 3168, 3174, 3180, 3191,
    3197, 3203, 3209, 3216, 3220, 3225, 3231, 3240, 3246, 3253, 3259, 3265, 3272, 3280, 3288, 3297,
    3305, 3312, 3318, 3324, 3330, 3335, 3343, 3352, 3358, 3364, 3371, 3376, 3384, 3390, 3396, 3405,
    3410, 3418, 3427, 3433, 3439, 3445, 3453, 3463, 3473, 3480, 3487, 3496, 3502, 3510, 3519, 3526,
    3532, 3538, 3545, 3552, 3559, 3565, 3572, 3578, 3586, 3594, 3599, 3607, 3616, 3624, 3628, 3636,
    3643, 3652, 3661, 3669, 3675, 3683, 3691, 3700, 3708, 3714, 3720, 3728, 3736, 3745, 3752, 3759,
    3767, 3774, 3783, 3790, 3799, 3808, 3818, 3825, 3834, 3843, 3850, 3854, 3859, 3865, 3873, 3881,
    3888, 3897, 3906, 3914, 3923, 3931, 3941, 3951, 3962, 3970, 3975, 3983, 3989, 3997, 4006, 4014,
    4023, 4030, 4036, 4044, 4050, 4058, 4066, 4075, 4084, 4091, 4097, 4105, 4113, 4122, 4129, 4137,
    4144, 4153, 4162, 4170, 4178,
};
// ---- /GENERATSIYA ----

static const int kWordCount = (int)(sizeof(kOff) / sizeof(kOff[0]));
static const int kMaxLen = 24;   // ro'yxatdagi eng uzun so'zdan uzunrog'i hech qachon topilmaydi

// w[0..len) ni ro'yxatdan ikkilik qidiruv bilan izlaydi
static bool InList(const wchar_t* w, int len)
{
    if (len <= 0 || len > kMaxLen) return false;
    int lo = 0, hi = kWordCount - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        const char* e = kWords + kOff[mid];
        int cmp = 0, i = 0;
        for (; i < len; ++i)
        {
            unsigned c = (unsigned)w[i], d = (unsigned char)e[i];
            if (d == 0) { cmp = 1; break; }         // yozuv qisqaroq -> so'z katta
            if (c != d) { cmp = c < d ? -1 : 1; break; }
        }
        if (cmp == 0 && e[i] != 0) cmp = -1;        // yozuv uzunroq -> so'z kichik
        if (cmp == 0) return true;
        if (cmp < 0) hi = mid - 1; else lo = mid + 1;
    }
    return false;
}

bool AutoSkip_IsForeign(const wchar_t* w, int len)
{
    if (!w || len <= 0) return false;

    // Evristika: o'zbek lotin yozuvida w yo'q, "h" siz yolg'iz c ham yo'q
    for (int i = 0; i < len; ++i)
    {
        if (w[i] == L'w') return true;
        if (w[i] == L'c' && (i + 1 >= len || w[i + 1] != L'h')) return true;
    }

    if (InList(w, len)) return true;

    // Inglizcha qo'shimchalarni olib tashlab ham tekshiramiz: 's 'd 'm 't 'll 're 've n't
    if (len >= 3 && w[len - 2] == L'\'')
    {
        wchar_t c = w[len - 1];
        if ((c == L's' || c == L'd' || c == L'm' || c == L't') && InList(w, len - 2)) return true;
        if (c == L't' && len >= 4 && w[len - 3] == L'n' && InList(w, len - 3)) return true;   // n't
    }
    if (len >= 4 && w[len - 3] == L'\'')
    {
        wchar_t a = w[len - 2], b = w[len - 1];
        if (((a == L'l' && b == L'l') || (a == L'r' && b == L'e') || (a == L'v' && b == L'e')) &&
            InList(w, len - 3))
            return true;
    }
    return false;
}