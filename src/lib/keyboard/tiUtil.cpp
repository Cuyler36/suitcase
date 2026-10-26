#include <lib/textinput/tiUtil.h>
#include <lib/textinput/tiCpData.h>
#include <string.h>
#include <wchar.h>
#if TARGET_PC
#include <cf/screen.h>
#endif

namespace textinput {
namespace util {
static wchar16_t ConvertDakutenEtc(const wchar16_t *map[], wchar16_t ch);

wchar16_t toWLower(wchar16_t ch) {
    const wchar16_t *first = wcschr((wchar16_t *)s_alphabet_map[1], ch);
    if (first != NULL) {
        u32 index = (u32)(first - s_alphabet_map[1]);
        return s_alphabet_map[0][index];
    } else {
        return ch;
    }
}

wchar16_t HankakuToZenkaku(wchar16_t ch) {
    if (ch >= L'a' && ch <= L'z') {
        return static_cast<wchar16_t>(ch + 0xFEE0);
    }
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar16_t>(ch + 0xFEE0);
    }
    if (ch >= L'0' && ch <= L'9') {
        return static_cast<wchar16_t>(ch + 0xFEE0);
    }

    const wchar16_t *map[] = {
        WSTR("，．！？：　＠／＿－"),
        WSTR(",.!?: @/_-"),
    };

    const wchar16_t *first = wcschr((wchar16_t *)map[1], ch);
    if (first != NULL) {
        u32 index = (u32)(first - map[1]);
        return map[0][index];
    } else {
        return ch;
    }
}

static const wchar16_t *s_all_map[] = {
    WSTR("あいうえおかきくけこさしすせそたちつてとはひふへほやゆよわアイウエオカキ")
    WSTR("クケコサシスセソタチツテトハヒフヘホヤユヨワ"),
    WSTR("　　　　　がぎぐげござじずぜぞだぢづでどばびぶべぼ　　　　　　ヴ　　ガギ")
    WSTR("グゲゴザジズゼゾダヂヅデドバビブベボ　　　　"),
    WSTR("　　　　　　　　　　　　　　　　　　　　ぱぴぷぺぽ　　　　　　　　　　　")
    WSTR("　　　　　　　　　　　　　パピプペポ　　　　"),
    WSTR("ぁぃぅぇぉ　　　　　　　　　　　　っ　　　　　　　ゃゅょゎァィゥェォヵ　")
    WSTR("　ヶ　　　　　　　　ッ　　　　　　　ャュョヮ"),
};

static const wchar16_t *s_dakuten_map[] = {s_all_map[0], s_all_map[1]};

static const wchar16_t *s_handaku_map[] = {s_all_map[0], s_all_map[2]};

static const wchar16_t *s_small_map[] = {s_all_map[0], s_all_map[3]};

wchar16_t KBD_ConvertDakuten(wchar16_t ch) {
    if (ch == L'　') {
        return ch;
    }

    if (KBD_IsHandaku(ch)) {
        wchar16_t ch1 = KBD_ConvertHandaku(ch);
        wchar16_t ch2 = ConvertDakutenEtc(s_dakuten_map, ch1);
        if (ch2 != ch1) {
            return ch2;
        } else {
            return ch;
        }
    }

    if (KBD_IsSmall(ch)) {
        wchar16_t ch1 = KBD_ConvertSmall(ch);
        wchar16_t ch2 = ConvertDakutenEtc(s_dakuten_map, ch1);
        if (ch2 != ch1) {
            return ch2;
        } else {
            return ch;
        }
    }

    return ConvertDakutenEtc(s_dakuten_map, ch);
}

wchar16_t KBD_ConvertHandaku(wchar16_t ch) {
    if (ch == L'　') {
        return ch;
    }

    if (KBD_IsDakuten(ch)) {
        wchar16_t ch1 = KBD_ConvertDakuten(ch);
        wchar16_t ch2 = ConvertDakutenEtc(s_handaku_map, ch1);
        if (ch2 != ch1) {
            return ch2;
        } else {
            return ch;
        }
    }

    return ConvertDakutenEtc(s_handaku_map, ch);
}

wchar16_t KBD_ConvertSmall(wchar16_t ch) {
    if (ch == L'　') {
        return ch;
    }

    if (KBD_IsDakuten(ch)) {
        wchar16_t ch1 = KBD_ConvertDakuten(ch);
        wchar16_t ch2 = ConvertDakutenEtc(s_small_map, ch1);
        if (ch2 != ch1) {
            return ch2;
        } else {
            return ch;
        }
    }

    return ConvertDakutenEtc(s_small_map, ch);
}

wchar16_t KBD_ConvertAll(wchar16_t ch) {
    int offset;

    if (ch == L'　') {
        return ch;
    }

    int i, j;
    for (i = 0; i < 4; i++) {
        wchar16_t *first = wcschr((wchar16_t *)s_all_map[i], ch);
        if (first != NULL) {
            offset = first - s_all_map[i];
            break;
        }
    }

    if (i < 4) {
        for (j = 1; j < 4; j++) {
            if (s_all_map[(i + j) % 4][offset] != L'　') {
                return s_all_map[(i + j) % 4][offset];
            }
        }
    } else {
        return ch;
    }

    return ch;
}

BOOL KBD_IsDakuten(wchar16_t ch) {
    wchar16_t *first = wcschr((wchar16_t *)s_dakuten_map[1], ch);
    return first != NULL;
}

BOOL KBD_IsHandaku(wchar16_t ch) {
    wchar16_t *first = wcschr((wchar16_t *)s_handaku_map[1], ch);
    return first != NULL;
}

BOOL KBD_IsSmall(wchar16_t ch) {
    wchar16_t *first = wcschr((wchar16_t *)s_small_map[1], ch);
    return first != NULL;
}

bool strcmp(const char *s1, const char *s2) {
    u32 len = strlen(s1);

    if (len >= 16) {
        len = 16;
    }

    if (strncmp(s1, s2, len) == 0) {
        return true;
    } else {
        return false;
    }
}

void replaceChar(char *dest, u32 destLen, const char *src, int replaceIdx,
                 char newCh) {
    memset(dest, 0, destLen);
    strncpy(dest, src, destLen);
    dest[replaceIdx] = newCh;
}

bool isAlphabet(wchar16_t ch) {
    return true;
}

static wchar16_t ConvertDakutenEtc(const wchar16_t *map[], wchar16_t ch) {
    for (int i = 0; i < 2; i++) {
        const wchar16_t *first = wcschr((wchar16_t *)map[i], ch);

        if (first != NULL) {
            u32 offset = (u32)(first - map[i]);

            if (map[1 - i][offset] != L'　') {
                return map[1 - i][offset];
            } else {
                return ch;
            }
        }
    }
    return ch;
}

f32 hermiteInterporation(f32 arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4,
                         f32 arg5, f32 arg6) {
    f32 var_f26;
    f32 var_f25;
    f32 var_f27;
    f32 var_f28;
    f32 var_f29;
    f32 var_f30;

    f32 result;

    var_f26 = arg0 - arg1;
    var_f29 = arg4 - arg1;
    var_f30 = var_f26 / var_f29;
    var_f25 = var_f30 * var_f30;
    result = var_f30 + var_f30;
    var_f28 = var_f25 - var_f30;
    var_f29 = arg2 - arg5;
    var_f27 = (result * var_f28) - var_f25;
    result = arg3 + (arg3 * var_f28);
    var_f27 = arg2 + (var_f27 * var_f29);
    result += arg6 * var_f28;
    result = (var_f30 * arg3) - result;
    result = var_f27 - (var_f26 * result);

    return result;
}

void getProjectionRect4x3(nw4r::ut::Rect *rect) {
    rect->left = -304.0;
    rect->right = 304.0;
    rect->bottom = 228.0;
    rect->top = -228.0;
}

void getProjectionRect16x9(nw4r::ut::Rect *rect) {
#if TARGET_PC
    rect->left = -cf::screen::WideWidth() / 2.0f;
#else
    rect->left = -416.0;
#endif
#if TARGET_PC
    rect->right = cf::screen::WideWidth() / 2.0f;
#else
    rect->right = 416.0;
#endif
    rect->bottom = 228.0;
    rect->top = -228.0;
}
} // namespace util
} // namespace textinput