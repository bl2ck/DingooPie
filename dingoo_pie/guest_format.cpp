#include "sdk_hle.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "native_runtime.h"
#include "guest_format.h"
#include "emulated_memory.h"

#include <float.h>

typedef char* my_va_list;

template <typename T>
static T _va_arg_value(my_va_list* va)
{
    T value = *((T*)*va);
    *va += sizeof(T);
    return value;
}

template <typename UnsignedT, typename SignedT>
static UnsignedT _signed_magnitude(SignedT value)
{
    return value < 0 ? (UnsignedT)0 - (UnsignedT)value : (UnsignedT)value;
}

// printf flag bits
#define FLAGS_ZEROPAD   (1U <<  0U)
#define FLAGS_LEFT      (1U <<  1U)
#define FLAGS_PLUS      (1U <<  2U)
#define FLAGS_SPACE     (1U <<  3U)
#define FLAGS_HASH      (1U <<  4U)
#define FLAGS_UPPERCASE (1U <<  5U)
#define FLAGS_CHAR      (1U <<  6U)
#define FLAGS_SHORT     (1U <<  7U)
#define FLAGS_LONG      (1U <<  8U)
#define FLAGS_LONG_LONG (1U <<  9U)
#define FLAGS_PRECISION (1U << 10U)
#define FLAGS_ADAPT_EXP (1U << 11U)

#define PRINTF_NTOA_BUFFER_SIZE    32U
#define PRINTF_FTOA_BUFFER_SIZE    32U
#define PRINTF_MAX_FLOAT  1e9
#define PRINTF_DEFAULT_FLOAT_PRECISION  6U
#define PRINTF_SUPPORT_FLOAT
#define PRINTF_SUPPORT_LONG_LONG
#define PRINTF_SUPPORT_PTRDIFF_T

// Bounded strlen for formatter inputs.
static inline unsigned int _strnlen_s(const char* str, uint32_t maxsize)
{
    const char* s;
    for (s = str; *s && maxsize--; ++s);
    return (unsigned int)(s - str);
}

// internal test if char is a digit (0-9)
// \return true if char is a digit
static inline bool _is_digit(char ch)
{
    return (ch >= '0') && (ch <= '9');
}

// internal ASCII string to unsigned int conversion
static unsigned int _atoi(const char** str)
{
    unsigned int i = 0U;
    while (_is_digit(**str)) {
        i = i * 10U + (unsigned int)(*((*str)++) - '0');
    }
    return i;
}

// internal buffer output
static void _out_buffer(char character, void* buffer, uint32_t idx, uint32_t maxlen)
{
    if (idx < maxlen) {
        ((char*)buffer)[idx] = character;
    }
}

static void _out_null(char character, void* buffer, uint32_t idx, uint32_t maxlen)
{
    (void)character;
    (void)buffer;
    (void)idx;
    (void)maxlen;
}

static const char* _guest_string_or_empty(uint32_t guestAddress)
{
    const char* text = (const char*)toHostPtr(guestAddress);
    return text ? text : "";
}

// output function type
typedef void (*out_fct_type)(char character, void* buffer, uint32_t idx, uint32_t maxlen);

// output the specified string in reverse, taking care of any zero-padding
static int32_t _out_rev(
    out_fct_type out,
    char* buffer,
    int32_t idx,
    int32_t maxlen,
    const char* buf,
    int32_t len,
    unsigned int width,
    unsigned int flags)
{
    const int32_t start_idx = idx;
    int32_t i;

    // pad spaces up to given width
    if (!(flags & FLAGS_LEFT) && !(flags & FLAGS_ZEROPAD)) {
        for (i = len; i < width; i++) {
            out(' ', buffer, idx++, maxlen);
        }
    }

    // reverse string
    while (len) {
        out(buf[--len], buffer, idx++, maxlen);
    }

    // append pad spaces up to given width
    if (flags & FLAGS_LEFT) {
        while (idx - start_idx < width) {
            out(' ', buffer, idx++, maxlen);
        }
    }

    return idx;
}

// internal itoa format
static int32_t _ntoa_format(
    out_fct_type out,
    char* buffer,
    int32_t idx,
    int32_t maxlen,
    char* buf,
    int32_t len,
    bool negative,
    unsigned int base,
    unsigned int prec,
    unsigned int width,
    unsigned int flags)
{
    // pad leading zeros
    if (!(flags & FLAGS_LEFT)) {
        if (width && (flags & FLAGS_ZEROPAD) && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE)))) {
            width--;
        }
        while ((len < prec) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = '0';
        }
        while ((flags & FLAGS_ZEROPAD) && (len < width) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = '0';
        }
    }

    // handle hash
    if (flags & FLAGS_HASH) {
        if (!(flags & FLAGS_PRECISION) && len && ((len == prec) || (len == width))) {
            len--;
            if (len && (base == 16U)) {
                len--;
            }
        }
        if ((base == 16U) && !(flags & FLAGS_UPPERCASE) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = 'x';
        }
        else if ((base == 16U) && (flags & FLAGS_UPPERCASE) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = 'X';
        }
        else if ((base == 2U) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = 'b';
        }
        if (len < PRINTF_NTOA_BUFFER_SIZE) {
            buf[len++] = '0';
        }
    }

    if (len < PRINTF_NTOA_BUFFER_SIZE) {
        if (negative) {
            buf[len++] = '-';
        }
        else if (flags & FLAGS_PLUS) {
            buf[len++] = '+';  // ignore the space if the '+' exists
        }
        else if (flags & FLAGS_SPACE) {
            buf[len++] = ' ';
        }
    }

    return _out_rev(out, buffer, idx, maxlen, buf, len, width, flags);
}

// internal itoa for 'long' type
static int32_t _ntoa_long(
    out_fct_type out,
    char* buffer,
    int32_t idx,
    int32_t maxlen,
    unsigned long value,
    bool negative,
    unsigned long base,
    unsigned int prec,
    unsigned int width,
    unsigned int flags)
{
    char buf[PRINTF_NTOA_BUFFER_SIZE];
    int32_t len = 0U;

    // no hash for 0 values
    if (!value) {
        flags &= ~FLAGS_HASH;
    }

    // write if precision != 0 and value is != 0
    if (!(flags & FLAGS_PRECISION) || value) {
        do {
            const char digit = (char)(value % base);
            buf[len++] = digit < 10 ? '0' + digit : (flags & FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10;
            value /= base;
        } while (value && (len < PRINTF_NTOA_BUFFER_SIZE));
    }

    return _ntoa_format(
        out, buffer, idx, maxlen, buf, len, negative, (unsigned int)base, prec, width, flags);
}

static int32_t _ntoa_long_long(
    out_fct_type out,
    char* buffer,
    int32_t idx,
    int32_t maxlen,
    unsigned long long value,
    bool negative,
    unsigned long long base,
    unsigned int prec,
    unsigned int width,
    unsigned int flags)
{
    char buf[PRINTF_NTOA_BUFFER_SIZE];
    int32_t len = 0U;

    // no hash for 0 values
    if (!value) {
        flags &= ~FLAGS_HASH;
    }

    // write if precision != 0 and value is != 0
    if (!(flags & FLAGS_PRECISION) || value) {
        do {
            const char digit = (char)(value % base);
            buf[len++] = digit < 10 ? '0' + digit : (flags & FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10;
            value /= base;
        } while (value && (len < PRINTF_NTOA_BUFFER_SIZE));
    }

    return _ntoa_format(
        out, buffer, idx, maxlen, buf, len, negative, (unsigned int)base, prec, width, flags);
}

// internal ftoa for fixed decimal floating point
static int32_t _ftoa(
    out_fct_type out,
    char* buffer,
    int32_t idx,
    int32_t maxlen,
    double value,
    unsigned int prec,
    unsigned int width,
    unsigned int flags)
{
    char buf[PRINTF_FTOA_BUFFER_SIZE];
    int32_t len = 0U;
    double diff = 0.0;
    bool negative = false;
    int whole;
    double tmp;
    unsigned long frac;

    // powers of 10
    static const double pow10[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };

    // test for special values
    if (value != value)
        return _out_rev(out, buffer, idx, maxlen, "nan", 3, width, flags);
    if (value < -DBL_MAX)
        return _out_rev(out, buffer, idx, maxlen, "fni-", 4, width, flags);
    if (value > DBL_MAX)
        return _out_rev(out, buffer, idx, maxlen, (flags & FLAGS_PLUS) ? "fni+" : "fni", (flags & FLAGS_PLUS) ? 4U : 3U, width, flags);

    // Avoid emitting hundreds of whole-number digits into the bounded buffer.
    if ((value > PRINTF_MAX_FLOAT) || (value < -PRINTF_MAX_FLOAT)) {
#if defined(PRINTF_SUPPORT_EXPONENTIAL)
        return _etoa(out, buffer, idx, maxlen, value, prec, width, flags);
#else
        return 0U;
#endif
    }

    // test for negative
    if (value < 0) {
        negative = true;
        value = 0 - value;
    }

    // set default precision, if not set explicitly
    if (!(flags & FLAGS_PRECISION)) {
        prec = PRINTF_DEFAULT_FLOAT_PRECISION;
    }
    // limit precision to 9, cause a prec >= 10 can lead to overflow errors
    while ((len < PRINTF_FTOA_BUFFER_SIZE) && (prec > 9U)) {
        buf[len++] = '0';
        prec--;
    }

    whole = (int)value;
    tmp = (value - whole) * pow10[prec];
    frac = (unsigned long)tmp;
    diff = tmp - frac;

    if (diff > 0.5) {
        ++frac;
        // handle rollover, e.g. case 0.99 with prec 1 is 1.0
        if (frac >= pow10[prec]) {
            frac = 0;
            ++whole;
        }
    }
    else if (diff < 0.5) {
    }
    else if ((frac == 0U) || (frac & 1U)) {
        // if halfway, round up if odd OR if last digit is 0
        ++frac;
    }

    if (prec == 0U) {
        diff = value - (double)whole;
        if ((!(diff < 0.5) || (diff > 0.5)) && (whole & 1)) {
            // exactly 0.5 and ODD, then round up
            // 1.5 -> 2, but 2.5 -> 2
            ++whole;
        }
    }
    else {
        unsigned int count = prec;
        // now do fractional part, as an unsigned number
        while (len < PRINTF_FTOA_BUFFER_SIZE) {
            --count;
            buf[len++] = (char)(48U + (frac % 10U));
            if (!(frac /= 10U)) {
                break;
            }
        }
        // add extra 0s
        while ((len < PRINTF_FTOA_BUFFER_SIZE) && (count-- > 0U)) {
            buf[len++] = '0';
        }
        if (len < PRINTF_FTOA_BUFFER_SIZE) {
            // add decimal
            buf[len++] = '.';
        }
    }

    // do whole part, number is reversed
    while (len < PRINTF_FTOA_BUFFER_SIZE) {
        buf[len++] = (char)(48 + (whole % 10));
        if (!(whole /= 10)) {
            break;
        }
    }

    // pad leading zeros
    if (!(flags & FLAGS_LEFT) && (flags & FLAGS_ZEROPAD)) {
        if (width && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE)))) {
            width--;
        }
        while ((len < width) && (len < PRINTF_FTOA_BUFFER_SIZE)) {
            buf[len++] = '0';
        }
    }

    if (len < PRINTF_FTOA_BUFFER_SIZE) {
        if (negative) {
            buf[len++] = '-';
        }
        else if (flags & FLAGS_PLUS) {
            buf[len++] = '+';  // ignore the space if the '+' exists
        }
        else if (flags & FLAGS_SPACE) {
            buf[len++] = ' ';
        }
    }

    return _out_rev(out, buffer, idx, maxlen, buf, len, width, flags);
}

// internal vsnprintf
static int my_vsnprintf(
    NativeRuntime* runtime,
    out_fct_type out,
    char* buffer,
    const uint32_t maxlen,
    const char* format,
    my_va_list va)
{
    unsigned int flags, width, precision, n;
    int32_t idx = 0U;

    if (!format) {
        return -1;
    }

    if (!buffer || !out) {
        out = _out_null;
    }

    while (*format)
    {
        if (*format != '%') {
            out(*format, buffer, idx++, maxlen);
            format++;
            continue;
        }
        format++;

        // evaluate flags
        flags = 0U;
        do {
            switch (*format) {
            case '0':
                flags |= FLAGS_ZEROPAD;
                format++;
                n = 1U;
                break;
            case '-':
                flags |= FLAGS_LEFT;
                format++;
                n = 1U;
                break;
            case '+':
                flags |= FLAGS_PLUS;
                format++;
                n = 1U;
                break;
            case ' ':
                flags |= FLAGS_SPACE;
                format++;
                n = 1U;
                break;
            case '#':
                flags |= FLAGS_HASH;
                format++;
                n = 1U;
                break;
            default:
                n = 0U;
                break;
            }
        } while (n);

        // evaluate width field
        width = 0U;
        if (_is_digit(*format)) {
            width = _atoi(&format);
        }
        else if (*format == '*') {
            int w = _va_arg_value<int>(&va);
            if (w < 0) {
                flags |= FLAGS_LEFT;
                width = _signed_magnitude<unsigned int>(w);
            }
            else {
                width = (unsigned int)w;
            }
            format++;
        }

        // evaluate precision field
        precision = 0U;
        if (*format == '.') {
            flags |= FLAGS_PRECISION;
            format++;
            if (_is_digit(*format)) {
                precision = _atoi(&format);
            }
            else if (*format == '*') {
                int prec = _va_arg_value<int>(&va);
                precision = prec > 0 ? (unsigned int)prec : 0U;
                format++;
            }
        }

        // evaluate length field
        switch (*format) {
        case 'l':
            flags |= FLAGS_LONG;
            format++;
            if (*format == 'l') {
                flags |= FLAGS_LONG_LONG;
                format++;
            }
            break;
        case 'h':
            flags |= FLAGS_SHORT;
            format++;
            if (*format == 'h') {
                flags |= FLAGS_CHAR;
                format++;
            }
            break;
#if defined(PRINTF_SUPPORT_PTRDIFF_T)
        case 't':
            flags |= (sizeof(ptrdiff_t) == sizeof(long) ? FLAGS_LONG : FLAGS_LONG_LONG);
            format++;
            break;
#endif
        case 'j':
            flags |= (sizeof(intmax_t) == sizeof(long) ? FLAGS_LONG : FLAGS_LONG_LONG);
            format++;
            break;
        case 'z':
            flags |= (sizeof(int32_t) == sizeof(long) ? FLAGS_LONG : FLAGS_LONG_LONG);
            format++;
            break;
        default:
            break;
        }

        // evaluate specifier
        switch (*format) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'b': {
            // set the base
            unsigned int base;
            if (*format == 'x' || *format == 'X') {
                base = 16U;
            }
            else if (*format == 'o') {
                base = 8U;
            }
            else if (*format == 'b') {
                base = 2U;
            }
            else {
                base = 10U;
                flags &= ~FLAGS_HASH;   // no hash for dec format
            }
            // uppercase
            if (*format == 'X') {
                flags |= FLAGS_UPPERCASE;
            }

            // no plus or space flag for u, x, X, o, b
            if ((*format != 'i') && (*format != 'd')) {
                flags &= ~(FLAGS_PLUS | FLAGS_SPACE);
            }

            // ignore '0' flag when precision is given
            if (flags & FLAGS_PRECISION) {
                flags &= ~FLAGS_ZEROPAD;
            }

            // convert the integer
            if ((*format == 'i') || (*format == 'd')) {
                // signed
                if (flags & FLAGS_LONG_LONG) {
#if defined(PRINTF_SUPPORT_LONG_LONG)
                    long long value = _va_arg_value<long long>(&va);
                    unsigned long long magnitude = _signed_magnitude<unsigned long long>(value);
                    idx = _ntoa_long_long(
                        out, buffer, idx, maxlen, magnitude, value < 0, base, precision, width, flags);
#endif
                }
                else if (flags & FLAGS_LONG) {
                    long value = _va_arg_value<long>(&va);
                    unsigned long magnitude = _signed_magnitude<unsigned long>(value);
                    idx = _ntoa_long(
                        out, buffer, idx, maxlen, magnitude, value < 0, base, precision, width, flags);
                }
                else {
                    int value = 0;
                    if (flags & FLAGS_CHAR) {
                        value = _va_arg_value<int>(&va);
                    }
                    else if (flags & FLAGS_SHORT) {
                        value = (short int)_va_arg_value<int>(&va);
                    }
                    else {
                        value = _va_arg_value<int>(&va);
                    }

                    unsigned int magnitude = _signed_magnitude<unsigned int>(value);
                    idx = _ntoa_long(
                        out, buffer, idx, maxlen, magnitude, value < 0, base, precision, width, flags);
                }
            }
            else {
                // unsigned
                if (flags & FLAGS_LONG_LONG) {
#if defined(PRINTF_SUPPORT_LONG_LONG)
                    unsigned long long value = _va_arg_value<unsigned long long>(&va);
                    idx = _ntoa_long_long(out, buffer, idx, maxlen, value, false, base, precision, width, flags);
#endif
                }
                else if (flags & FLAGS_LONG) {
                    unsigned long value = _va_arg_value<unsigned long>(&va);
                    idx = _ntoa_long(out, buffer, idx, maxlen, value, false, base, precision, width, flags);
                }
                else {
                    unsigned int value = 0;

                    if (flags & FLAGS_CHAR) {
                        unsigned int v = _va_arg_value<unsigned int>(&va);
                        value = (unsigned char)v;
                    }
                    else if (flags & FLAGS_SHORT) {
                        unsigned int v = _va_arg_value<unsigned int>(&va);
                        value = (unsigned short int)v;
                    }
                    else {
                        value = _va_arg_value<unsigned int>(&va);
                    }
                    idx = _ntoa_long(out, buffer, idx, maxlen, value, false, base, precision, width, flags);
                }
            }
            format++;
            break;
        }
#if defined(PRINTF_SUPPORT_FLOAT)
        case 'f':
        case 'F':
        {
            if (*format == 'F') flags |= FLAGS_UPPERCASE;
            double value = _va_arg_value<double>(&va);
            idx = _ftoa(out, buffer, idx, maxlen, value, precision, width, flags);
            format++;
            break;
        }
#if defined(PRINTF_SUPPORT_EXPONENTIAL)
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            if ((*format == 'g') || (*format == 'G')) flags |= FLAGS_ADAPT_EXP;
            if ((*format == 'E') || (*format == 'G')) flags |= FLAGS_UPPERCASE;
            idx = _etoa(out, buffer, idx, maxlen, _va_arg_value<double>(&va), precision, width, flags);
            format++;
            break;
#endif  // PRINTF_SUPPORT_EXPONENTIAL
#endif  // PRINTF_SUPPORT_FLOAT
        case 'c': {
            unsigned int paddedLength = 1U;
            if (!(flags & FLAGS_LEFT)) {
                while (paddedLength++ < width) {
                    out(' ', buffer, idx++, maxlen);
                }
            }

            int value = _va_arg_value<int>(&va);
            out((char)value, buffer, idx++, maxlen);

            if (flags & FLAGS_LEFT) {
                while (paddedLength++ < width) {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            format++;
            break;
        }

        case 's': {
            uint32_t guestString = _va_arg_value<uint32_t>(&va);
            const char* text = _guest_string_or_empty(guestString);
            unsigned int paddedLength = _strnlen_s(text, UINT32_MAX);
            if (flags & FLAGS_PRECISION) {
                paddedLength = (paddedLength < precision ? paddedLength : precision);
            }

            if (!(flags & FLAGS_LEFT)) {
                while (paddedLength++ < width) {
                    out(' ', buffer, idx++, maxlen);
                }
            }

            while ((*text != 0) && (!(flags & FLAGS_PRECISION) || precision--)) {
                out(*(text++), buffer, idx++, maxlen);
            }

            if (flags & FLAGS_LEFT) {
                while (paddedLength++ < width) {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            format++;
            break;
        }

        case 'p': {
            uint32_t value = _va_arg_value<uint32_t>(&va);
            width = sizeof(void*) * 2U;
            flags |= FLAGS_ZEROPAD | FLAGS_UPPERCASE;
#if defined(PRINTF_SUPPORT_LONG_LONG)
            idx = _ntoa_long_long(out, buffer, idx, maxlen, value, false, 16U, precision, width, flags);
#else
            idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned long)value, false, 16U, precision, width, flags);
#endif
            format++;
            break;
        }

        case '%':
            out('%', buffer, idx++, maxlen);
            format++;
            break;

        default:
            out(*format, buffer, idx++, maxlen);
            format++;
            break;
        }
    }

    // termination
    out((char)0, buffer, idx < maxlen ? idx : maxlen - 1U, maxlen);

    // return written chars without terminating \0
    return (int)idx;
}

void my_sprintf(NativeRuntime* runtime)
{
    uint32_t a0, a1, a2, a3;
    uint32_t sp;

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &a0);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &a1);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &a2);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &a3);

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &sp);

    char* buffer = (char*)toHostPtr((a0));
    char* format = (char*)toHostPtr((a1));

    uint32_t s1, s2;

    sp -= 40;

    s1 = ((uint32_t*)toHostPtr((sp + 48)))[0];
    s2 = ((uint32_t*)toHostPtr((sp + 52)))[0];

    *((uint32_t*)toHostPtr((sp + 48))) = a2;
    *((uint32_t*)toHostPtr((sp + 52))) = a3;
    my_va_list va = (my_va_list)toHostPtr((sp + 48));

    uint32_t ret = my_vsnprintf(runtime, _out_buffer, buffer, (uint32_t)-1, format, va);

    *((uint32_t*)toHostPtr((sp + 48))) = s1;
    *((uint32_t*)toHostPtr((sp + 52))) = s2;

    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_V0, &ret);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}

void dingoo_debug(NativeRuntime* runtime)
{
    uint32_t a0, a1, a2, a3;
    uint32_t sp;

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &a0);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &a1);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &a2);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &a3);

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &sp);

    char buffer[128] = {};
    char* format = (char*)toHostPtr((a0));

    *(uint32_t*)toHostPtr((sp + 4))     = a1;
    *(uint32_t*)toHostPtr((sp + 8))     = a2;
    *(uint32_t*)toHostPtr((sp + 0xc))   = a3;

    my_va_list va = (my_va_list)toHostPtr((sp + 4));

    my_vsnprintf(runtime, _out_buffer, buffer, (uint32_t)-1, format, va);

    printf("guest_debug: %s", buffer);

    uint32_t pc;
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &pc);
    nativeRuntimeWriteRegister(runtime, RUNTIME_REG_PC, &pc);
}
