// Copyright (c) 2014-2019  Marco Paland (info@paland.com)
// SPDX-License-Identifier: MIT
//
// Copyright (c) 2020  Raspberry Pi (Trading) Ltd.
// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (C) 2025  Luke T. Shumaker <lukeshu@lukeshu.com>
// SPDX-License-Identifier: BSD-3-Clause
//
///////////////////////////////////////////////////////////////////////////////
// \author (c) Marco Paland (info@paland.com)
//             2014-2019, PALANDesign Hannover, Germany
//
// \license The MIT License (MIT)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// \brief Tiny printf, sprintf and (v)snprintf implementation, optimized for speed on
//        embedded systems with a very limited resources. These routines are thread
//        safe and reentrant!
//        Use this instead of the bloated standard/newlib printf cause these use
//        malloc for printf (and may not be thread safe).
//
///////////////////////////////////////////////////////////////////////////////

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/fmt_printf.h"

// PICO_CONFIG: PICO_PRINTF_NTOA_BUFFER_SIZE, Define printf ntoa buffer size, min=0, max=128, default=32, group=pico_printf
// 'ntoa' conversion buffer size, this must be big enough to hold one converted
// numeric number including padded zeros (dynamically created on stack)
#ifndef PICO_PRINTF_NTOA_BUFFER_SIZE
#define PICO_PRINTF_NTOA_BUFFER_SIZE    32U
#endif

// PICO_CONFIG: PICO_PRINTF_FTOA_BUFFER_SIZE, Define printf ftoa buffer size, min=0, max=128, default=32, group=pico_printf
// 'ftoa' conversion buffer size, this must be big enough to hold one converted
// float number including padded zeros (dynamically created on stack)
#ifndef PICO_PRINTF_FTOA_BUFFER_SIZE
#define PICO_PRINTF_FTOA_BUFFER_SIZE    32U
#endif

// PICO_CONFIG: PICO_PRINTF_SUPPORT_FLOAT, Enable floating point printing, type=bool, default=1, group=pico_printf
// support for the floating point type (%f)
#ifndef PICO_PRINTF_SUPPORT_FLOAT
#define PICO_PRINTF_SUPPORT_FLOAT 1
#endif

// PICO_CONFIG: PICO_PRINTF_SUPPORT_EXPONENTIAL, Enable exponential floating point printing, type=bool, default=1, group=pico_printf
// support for exponential floating point notation (%e/%g)
#ifndef PICO_PRINTF_SUPPORT_EXPONENTIAL
#define PICO_PRINTF_SUPPORT_EXPONENTIAL 1
#endif

// PICO_CONFIG: PICO_PRINTF_DEFAULT_FLOAT_PRECISION, Define default floating point precision, min=1, max=16, default=6, group=pico_printf
#ifndef PICO_PRINTF_DEFAULT_FLOAT_PRECISION
#define PICO_PRINTF_DEFAULT_FLOAT_PRECISION  6U
#endif

// PICO_CONFIG: PICO_PRINTF_MAX_FLOAT, Define the largest float suitable to print with %f, min=1, max=1e9, default=1e9, group=pico_printf
#ifndef PICO_PRINTF_MAX_FLOAT
#define PICO_PRINTF_MAX_FLOAT  1e9
#endif

// PICO_CONFIG: PICO_PRINTF_SUPPORT_LONG_LONG, Enable support for long long types (%llu or %p), type=bool, default=1, group=pico_printf
#ifndef PICO_PRINTF_SUPPORT_LONG_LONG
#define PICO_PRINTF_SUPPORT_LONG_LONG 1
#endif

// PICO_CONFIG: PICO_PRINTF_SUPPORT_PTRDIFF_T, Enable support for the ptrdiff_t type (%t), type=bool, default=1, group=pico_printf
// ptrdiff_t is normally defined in <stddef.h> as long or long long type
#ifndef PICO_PRINTF_SUPPORT_PTRDIFF_T
#define PICO_PRINTF_SUPPORT_PTRDIFF_T 1
#endif

///////////////////////////////////////////////////////////////////////////////

// import float.h for DBL_MAX
#if PICO_PRINTF_SUPPORT_FLOAT

#include <float.h>

#endif

///////////////////////////////////////////////////////////////////////////////

// internal flag definitions
#define FMT_FLAG_ZEROPAD   (1U <<  0U)
#define FMT_FLAG_LEFT      (1U <<  1U)
#define FMT_FLAG_PLUS      (1U <<  2U)
#define FMT_FLAG_SPACE     (1U <<  3U)
#define FMT_FLAG_HASH      (1U <<  4U)
#define FMT_FLAG_PRECISION (1U << 10U)

enum fmt_size {
    FMT_SIZE_CHAR,      // "hh"
    FMT_SIZE_SHORT,     // "h"
    FMT_SIZE_DEFAULT,   // ""
    FMT_SIZE_LONG,      // "l"
    FMT_SIZE_LONG_LONG, // "ll"
};

struct ctx {
    fmt_fct_t    fct;
    void        *arg;
    size_t       idx;
};

struct fmt_state {
    // %[flags][width][.precision][size]specifier
    unsigned int         flags;
    unsigned int         width;
    unsigned int         precision;
    enum fmt_size        size;
    char                 specifier;

    struct ctx          *ctx;
};

static inline void out(char character, struct ctx *ctx) {
    if (ctx->fct) {
        ctx->fct(character, ctx->arg);
    }
    ctx->idx++;
}

// internal secure strlen
// \return The length of the string (excluding the terminating 0) limited by 'maxsize'
static inline unsigned int _strnlen_s(const char *str, size_t maxsize) {
    const char *s;
    for (s = str; *s && maxsize--; ++s);
    return (unsigned int) (s - str);
}


// internal test if char is a digit (0-9)
// \return true if char is a digit
static inline bool _is_digit(char ch) {
    return (ch >= '0') && (ch <= '9');
}

static inline bool _is_upper(char ch) {
    return (ch >= 'A') && (ch <= 'Z');
}

// internal ASCII string to unsigned int conversion
static unsigned int _atoi(const char **str) {
    unsigned int i = 0U;
    while (_is_digit(**str)) {
        i = i * 10U + (unsigned int) (*((*str)++) - '0');
    }
    return i;
}


// output the specified string in reverse, taking care of any zero-padding
static void _out_rev(struct fmt_state state, const char *buf, size_t len) {
    const size_t start_idx = state.ctx->idx;

    // pad spaces up to given width
    if (!(state.flags & FMT_FLAG_LEFT) && !(state.flags & FMT_FLAG_ZEROPAD)) {
        for (size_t i = len; i < state.width; i++) {
            out(' ', state.ctx);
        }
    }

    // reverse string
    while (len) {
        out(buf[--len], state.ctx);
    }

    // append pad spaces up to given width
    if (state.flags & FMT_FLAG_LEFT) {
        while (state.ctx->idx - start_idx < state.width) {
            out(' ', state.ctx);
        }
    }
}


// internal itoa format
static void _ntoa_format(struct fmt_state state, char *buf, size_t len, bool negative, unsigned int base) {
    // pad leading zeros
    if (!(state.flags & FMT_FLAG_LEFT)) {
        if (state.width && (state.flags & FMT_FLAG_ZEROPAD) && (negative || (state.flags & (FMT_FLAG_PLUS | FMT_FLAG_SPACE)))) {
            state.width--;
        }
        while ((len < state.precision) && (len < PICO_PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = '0';
        }
        while ((state.flags & FMT_FLAG_ZEROPAD) && (len < state.width) && (len < PICO_PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = '0';
        }
    }

    // handle hash
    if (state.flags & FMT_FLAG_HASH) {
        if (!(state.flags & FMT_FLAG_PRECISION) && len && ((len == state.precision) || (len == state.width))) {
            len--;
            if (len && (base == 16U)) {
                len--;
            }
        }
        if ((base == 16U) && (len < PICO_PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = state.specifier;
        } else if ((base == 2U) && (len < PICO_PRINTF_NTOA_BUFFER_SIZE)) {
            buf[len++] = 'b';
        }
        if (len < PICO_PRINTF_NTOA_BUFFER_SIZE) {
            buf[len++] = '0';
        }
    }

    if (len < PICO_PRINTF_NTOA_BUFFER_SIZE) {
        if (negative) {
            buf[len++] = '-';
        } else if (state.flags & FMT_FLAG_PLUS) {
            buf[len++] = '+';  // ignore the space if the '+' exists
        } else if (state.flags & FMT_FLAG_SPACE) {
            buf[len++] = ' ';
        }
    }

    _out_rev(state, buf, len);
}


// internal itoa for 'long' type
static void _ntoa_long(struct fmt_state state, unsigned long value, bool negative, unsigned long base) {
    char buf[PICO_PRINTF_NTOA_BUFFER_SIZE];
    size_t len = 0U;

    // no hash for 0 values
    if (!value) {
        state.flags &= ~FMT_FLAG_HASH;
    }

    // write if precision != 0 and value is != 0
    if (!(state.flags & FMT_FLAG_PRECISION) || value) {
        do {
            const char digit = (char) (value % base);
            buf[len++] = (char)(digit < 10 ? '0' + digit : (_is_upper(state.specifier) ? 'A' : 'a') + digit - 10);
            value /= base;
        } while (value && (len < PICO_PRINTF_NTOA_BUFFER_SIZE));
    }

    _ntoa_format(state, buf, len, negative, (unsigned int) base);
}


// internal itoa for 'long long' type
#if PICO_PRINTF_SUPPORT_LONG_LONG

static void _ntoa_long_long(struct fmt_state state, unsigned long long value, bool negative, unsigned long long base) {
    char buf[PICO_PRINTF_NTOA_BUFFER_SIZE];
    size_t len = 0U;

    // no hash for 0 values
    if (!value) {
        state.flags &= ~FMT_FLAG_HASH;
    }

    // write if precision != 0 and value is != 0
    if (!(state.flags & FMT_FLAG_PRECISION) || value) {
        do {
            const char digit = (char) (value % base);
            buf[len++] = (char)(digit < 10 ? '0' + digit : (_is_upper(state.specifier) ? 'A' : 'a') + digit - 10);
            value /= base;
        } while (value && (len < PICO_PRINTF_NTOA_BUFFER_SIZE));
    }

    _ntoa_format(state, buf, len, negative, (unsigned int) base);
}

#endif  // PICO_PRINTF_SUPPORT_LONG_LONG


#if PICO_PRINTF_SUPPORT_FLOAT

#define is_nan __builtin_isnan

static bool _float_special(struct fmt_state state, double value) {
    // test for special values
    if (is_nan(value)) {
        _out_rev(state, "nan", 3);
        return true;
    }
    if (value < -DBL_MAX) {
        _out_rev(state, "fni-", 4);
        return true;
    }
    if (value > DBL_MAX) {
        _out_rev(state, (state.flags & FMT_FLAG_PLUS) ? "fni+" : "fni", (state.flags & FMT_FLAG_PLUS) ? 4U : 3U);
        return true;
    }
    return false;
}

// internal ftoa for fixed decimal floating point
static void _ftoa(struct fmt_state state, double value) {
    char buf[PICO_PRINTF_FTOA_BUFFER_SIZE];
    size_t len = 0U;
    double diff = 0.0;

    // powers of 10
    static const double pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

    // check for NaN and special values
    if (_float_special(state, value))
        return;

    // test for negative
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = 0 - value;
    }

    // set default precision, if not set explicitly
    if (!(state.flags & FMT_FLAG_PRECISION)) {
        state.precision = PICO_PRINTF_DEFAULT_FLOAT_PRECISION;
    }
    // limit precision to 9, cause a prec >= 10 can lead to overflow errors
    while ((len < PICO_PRINTF_FTOA_BUFFER_SIZE) && (state.precision > 9U)) {
        buf[len++] = '0';
        state.precision--;
    }

    int whole = (int) value;
    double tmp = (value - whole) * pow10[state.precision];
    unsigned long frac = (unsigned long) tmp;
    diff = tmp - frac;

    if (diff > 0.5) {
        ++frac;
        // handle rollover, e.g. case 0.99 with prec 1 is 1.0
        if (frac >= pow10[state.precision]) {
            frac = 0;
            ++whole;
        }
    } else if (diff < 0.5) {
    } else if ((frac == 0U) || (frac & 1U)) {
        // if halfway, round up if odd OR if last digit is 0
        ++frac;
    }

    if (state.precision == 0U) {
        diff = value - (double) whole;
        if (!((diff < 0.5) || (diff > 0.5)) && (whole & 1)) {
            // exactly 0.5 and ODD, then round up
            // 1.5 -> 2, but 2.5 -> 2
            ++whole;
        }
    } else {
        unsigned int count = state.precision;
        // now do fractional part, as an unsigned number
        while (len < PICO_PRINTF_FTOA_BUFFER_SIZE) {
            --count;
            buf[len++] = (char) (48U + (frac % 10U));
            if (!(frac /= 10U)) {
                break;
            }
        }
        // add extra 0s
        while ((len < PICO_PRINTF_FTOA_BUFFER_SIZE) && (count-- > 0U)) {
            buf[len++] = '0';
        }
        if (len < PICO_PRINTF_FTOA_BUFFER_SIZE) {
            // add decimal
            buf[len++] = '.';
        }
    }

    // do whole part, number is reversed
    while (len < PICO_PRINTF_FTOA_BUFFER_SIZE) {
        buf[len++] = (char) (48 + (whole % 10));
        if (!(whole /= 10)) {
            break;
        }
    }

    // pad leading zeros
    if (!(state.flags & FMT_FLAG_LEFT) && (state.flags & FMT_FLAG_ZEROPAD)) {
        if (state.width && (negative || (state.flags & (FMT_FLAG_PLUS | FMT_FLAG_SPACE)))) {
            state.width--;
        }
        while ((len < state.width) && (len < PICO_PRINTF_FTOA_BUFFER_SIZE)) {
            buf[len++] = '0';
        }
    }

    if (len < PICO_PRINTF_FTOA_BUFFER_SIZE) {
        if (negative) {
            buf[len++] = '-';
        } else if (state.flags & FMT_FLAG_PLUS) {
            buf[len++] = '+';  // ignore the space if the '+' exists
        } else if (state.flags & FMT_FLAG_SPACE) {
            buf[len++] = ' ';
        }
    }

    _out_rev(state, buf, len);
}


#if PICO_PRINTF_SUPPORT_EXPONENTIAL

// internal ftoa variant for exponential floating-point type, contributed by Martijn Jasperse <m.jasperse@gmail.com>
static void _etoa(struct fmt_state state, double value, bool adapt_exp) {
    // check for NaN and special values
    if (_float_special(state, value))
        return;

    // determine the sign
    const bool negative = value < 0;
    if (negative) {
        value = -value;
    }

    // default precision
    if (!(state.flags & FMT_FLAG_PRECISION)) {
        state.precision = PICO_PRINTF_DEFAULT_FLOAT_PRECISION;
    }

    // determine the decimal exponent
    // based on the algorithm by David Gay (https://www.ampl.com/netlib/fp/dtoa.c)
    union {
        uint64_t U;
        double F;
    } conv;

    conv.F = value;
    int expval;
    if (conv.U) {
        int exp2 = (int) ((conv.U >> 52U) & 0x07FFU) - 1023;           // effectively log2
        conv.U = (conv.U & ((1ULL << 52U) - 1U)) | (1023ULL << 52U);  // drop the exponent so conv.F is now in [1,2)
        // now approximate log10 from the log2 integer part and an expansion of ln around 1.5
        expval = (int) (0.1760912590558 + exp2 * 0.301029995663981 + (conv.F - 1.5) * 0.289529654602168);
        // now we want to compute 10^expval but we want to be sure it won't overflow
        exp2 = (int) (expval * 3.321928094887362 + 0.5);
        const double z = expval * 2.302585092994046 - exp2 * 0.6931471805599453;
        const double z2 = z * z;
        conv.U = (uint64_t) (exp2 + 1023) << 52U;
        // compute exp(z) using continued fractions, see https://en.wikipedia.org/wiki/Exponential_function#Continued_fractions_for_ex
        conv.F *= 1 + 2 * z / (2 - z + (z2 / (6 + (z2 / (10 + z2 / 14)))));
        // correct for rounding errors
        if (value < conv.F) {
            expval--;
            conv.F /= 10;
        }
    } else {
        expval = 0;
    }

    // the exponent format is "%+03d" and largest value is "307", so set aside 4-5 characters
    unsigned int minwidth = ((expval < 100) && (expval > -100)) ? 4U : 5U;

    // in "%g" mode, "state.precision" is the number of *significant figures* not decimals
    if (adapt_exp) {
        // do we want to fall-back to "%f" mode?
        if ((conv.U == 0) || ((value >= 1e-4) && (value < 1e6))) {
            if ((int) state.precision > expval) {
                state.precision = (unsigned) ((int) state.precision - expval - 1);
            } else {
                state.precision = 0;
            }
            state.flags |= FMT_FLAG_PRECISION;   // make sure _ftoa respects precision
            // no characters in exponent
            minwidth = 0U;
            expval = 0;
        } else {
            // we use one sigfig for the whole part
            if ((state.precision > 0) && (state.flags & FMT_FLAG_PRECISION)) {
                --state.precision;
            }
        }
    }

    // will everything fit?
    unsigned int fwidth = state.width;
    if (fwidth > minwidth) {
        // we didn't fall-back so subtract the characters required for the exponent
        fwidth -= minwidth;
    } else {
        // not enough characters, so go back to default sizing
        fwidth = 0U;
    }
    if ((state.flags & FMT_FLAG_LEFT) && minwidth) {
        // if we're padding on the right, DON'T pad the floating part
        fwidth = 0U;
    }

    // rescale the float value
    if (expval) {
        value /= conv.F;
    }

    // output the floating part
    const size_t start_idx = state.ctx->idx;
    struct fmt_state substate = {
        .width = fwidth,
        .precision = state.precision,
        .flags = state.flags,
        .specifier = 'f',
        .ctx = state.ctx,
    };
    _ftoa(substate, negative ? -value : value);

    // output the exponent part
    if (minwidth) {
        // output the exponential symbol
        out(_is_upper(state.specifier) ? 'E' : 'e', state.ctx);
        // output the exponent value
        struct fmt_state substate = {
            .width = minwidth - 1,
            .precision = 0,
            .flags = FMT_FLAG_ZEROPAD | FMT_FLAG_PLUS,
            .specifier = 'u',
            .ctx = state.ctx,
        };
        _ntoa_long(substate, (unsigned int)((expval < 0) ? -expval : expval), expval < 0, 10);
        // might need to right-pad spaces
        if (state.flags & FMT_FLAG_LEFT) {
            while (state.ctx->idx - start_idx < state.width) out(' ', state.ctx);
        }
    }
}

#endif  // PICO_PRINTF_SUPPORT_EXPONENTIAL
#endif  // PICO_PRINTF_SUPPORT_FLOAT

int fmt_vfctprintf(fmt_fct_t fct, void *arg, const char *format, va_list va) {
    unsigned int n;
    struct ctx _ctx = {
        .fct = fct,
        .arg = arg,
        .idx = 0,
    };
    struct fmt_state state = {
        .ctx = &_ctx,
    };

    while (*format) {
        // format specifier?  %[flags][width][.precision][length]specifier
        if (*format != '%') {
            // no
            out(*format, state.ctx);
            format++;
            continue;
        } else {
            // yes, evaluate it
            format++;
        }

        // evaluate flags
        state.flags = 0U;
        do {
            switch (*format) {
                case '0':
                    state.flags |= FMT_FLAG_ZEROPAD;
                    format++;
                    n = 1U;
                    break;
                case '-':
                    state.flags |= FMT_FLAG_LEFT;
                    format++;
                    n = 1U;
                    break;
                case '+':
                    state.flags |= FMT_FLAG_PLUS;
                    format++;
                    n = 1U;
                    break;
                case ' ':
                    state.flags |= FMT_FLAG_SPACE;
                    format++;
                    n = 1U;
                    break;
                case '#':
                    state.flags |= FMT_FLAG_HASH;
                    format++;
                    n = 1U;
                    break;
                default :
                    n = 0U;
                    break;
            }
        } while (n);

        // evaluate width field
        state.width = 0U;
        if (_is_digit(*format)) {
            state.width = _atoi(&format);
        } else if (*format == '*') {
            const int w = va_arg(va, int);
            if (w < 0) {
                state.flags |= FMT_FLAG_LEFT;    // reverse padding
                state.width = (unsigned int) -w;
            } else {
                state.width = (unsigned int) w;
            }
            format++;
        }

        // evaluate precision field
        state.precision = 0U;
        if (*format == '.') {
            state.flags |= FMT_FLAG_PRECISION;
            format++;
            if (_is_digit(*format)) {
                state.precision = _atoi(&format);
            } else if (*format == '*') {
                const int prec = (int) va_arg(va, int);
                state.precision = prec > 0 ? (unsigned int) prec : 0U;
                format++;
            }
        }

        // evaluate length field
        state.size = FMT_SIZE_DEFAULT;
        switch (*format) {
            case 'l' :
                state.size = FMT_SIZE_LONG;
                format++;
                if (*format == 'l') {
                    state.size = FMT_SIZE_LONG_LONG;
                    format++;
                }
                break;
            case 'h' :
                state.size = FMT_SIZE_SHORT;
                format++;
                if (*format == 'h') {
                    state.size = FMT_SIZE_CHAR;
                    format++;
                }
                break;
#if PICO_PRINTF_SUPPORT_PTRDIFF_T
            case 't' :
                state.size = (sizeof(ptrdiff_t) == sizeof(long) ? FMT_SIZE_LONG : FMT_SIZE_LONG_LONG);
                format++;
                break;
#endif
            case 'j' :
                state.size = (sizeof(intmax_t) == sizeof(long) ? FMT_SIZE_LONG : FMT_SIZE_LONG_LONG);
                format++;
                break;
            case 'z' :
                state.size = (sizeof(size_t) == sizeof(long) ? FMT_SIZE_LONG : FMT_SIZE_LONG_LONG);
                format++;
                break;
            default :
                break;
        }

        // evaluate specifier
        state.specifier = *format;
        format++;
        switch (state.specifier) {
            case 'd' :
            case 'i' :
            case 'u' :
            case 'x' :
            case 'X' :
            case 'o' :
            case 'b' : {
                // set the base
                unsigned int base;
                if (state.specifier == 'x' || state.specifier == 'X') {
                    base = 16U;
                } else if (state.specifier == 'o') {
                    base = 8U;
                } else if (state.specifier == 'b') {
                    base = 2U;
                } else {
                    base = 10U;
                    state.flags &= ~FMT_FLAG_HASH;   // no hash for dec format
                }

                // no plus or space flag for u, x, X, o, b
                if ((state.specifier != 'i') && (state.specifier != 'd')) {
                    state.flags &= ~(FMT_FLAG_PLUS | FMT_FLAG_SPACE);
                }

                // ignore '0' flag when precision is given
                if (state.flags & FMT_FLAG_PRECISION) {
                    state.flags &= ~FMT_FLAG_ZEROPAD;
                }

                // convert the integer
                if ((state.specifier == 'i') || (state.specifier == 'd')) {
                    // signed
                    switch (state.size) {
#if PICO_PRINTF_SUPPORT_LONG_LONG
                        case FMT_SIZE_LONG_LONG: {
                            const long long value = va_arg(va, long long);
                            _ntoa_long_long(state, (unsigned long long) (value > 0 ? value : 0 - value), value < 0, base);
                            break;
                        }
#else
                        case FMT_SIZE_LONG_LONG: // fall through
#endif
                        case FMT_SIZE_LONG: {
                            const long value = va_arg(va, long);
                            _ntoa_long(state, (unsigned long) (value > 0 ? value : 0 - value), value < 0, base);
                            break;
                        }
                        case FMT_SIZE_DEFAULT: {
                            const int value = va_arg(va, int);
                            _ntoa_long(state, (unsigned int) (value > 0 ? value : 0 - value), value < 0, base);
                            break;
                        }
                        case FMT_SIZE_SHORT: {
                            // 'short' is promoted to 'int' when passed through '...'; so we read it
                            // with va_arg(va, int), but then truncate it with casting.
                            const int value = (short int) va_arg(va, int);
                            _ntoa_long(state, (unsigned int) (value > 0 ? value : 0 - value), value < 0, base);
                            break;
                        }
                        case FMT_SIZE_CHAR: {
                            // 'char' is promoted to 'int' when passed through '...'; so we read it
                            // with va_arg(va, int), but then truncate it with casting.
                            const int value = (char) va_arg(va, int);
                            _ntoa_long(state, (unsigned int) (value > 0 ? value : 0 - value), value < 0, base);
                            break;
                        }
                    }
                } else {
                    // unsigned
                    switch (state.size) {
                        case FMT_SIZE_LONG_LONG:
#if PICO_PRINTF_SUPPORT_LONG_LONG
                            _ntoa_long_long(state, va_arg(va, unsigned long long), false, base);
                            break;
#else
                            // fall through
#endif
                        case FMT_SIZE_LONG:
                            _ntoa_long(state, va_arg(va, unsigned long), false, base);
                            break;
                        case FMT_SIZE_DEFAULT:
                            _ntoa_long(state, va_arg(va, unsigned int), false, base);
                            break;
                        case FMT_SIZE_SHORT:
                            // 'short' is promoted to 'int' when passed through '...'; so we read it
                            // with va_arg(va, unsigned int), but then truncate it with casting.
                            _ntoa_long(state, (unsigned short int) va_arg(va, unsigned int), false, base);
                            break;
                        case FMT_SIZE_CHAR:
                            // 'char' is promoted to 'int' when passed through '...'; so we read it
                            // with va_arg(va, unsigned int), but then truncate it with casting.
                            _ntoa_long(state, (unsigned char) va_arg(va, unsigned int), false, base);
                            break;
                    }
                }
                break;
            }
            case 'f' :
            case 'F' :
#if PICO_PRINTF_SUPPORT_FLOAT
                {
                    double value = va_arg(va, double);
                    // test for very large values
                    // standard printf behavior is to print EVERY whole number digit -- which could be 100s of characters overflowing your buffers == bad
                    if ((value > PICO_PRINTF_MAX_FLOAT && value < DBL_MAX)
                        || (value < -PICO_PRINTF_MAX_FLOAT && value > -DBL_MAX)) {
#if PICO_PRINTF_SUPPORT_EXPONENTIAL
                        _etoa(state, value, false);
#endif
                        break;
                    }
                    _ftoa(state, value);
                }
#else
                for(int i=0;i<2;i++) out('?', state.ctx);
                va_arg(va, double);
#endif
                break;
            case 'e':
            case 'E':
#if PICO_PRINTF_SUPPORT_FLOAT && PICO_PRINTF_SUPPORT_EXPONENTIAL
                _etoa(state, va_arg(va, double), false);
#else
                for(int i=0;i<2;i++) out('?', state.ctx);
                va_arg(va, double);
#endif
                break;
            case 'g':
            case 'G':
#if PICO_PRINTF_SUPPORT_FLOAT && PICO_PRINTF_SUPPORT_EXPONENTIAL
                _etoa(state, va_arg(va, double), true);
#else
                for(int i=0;i<2;i++) out('?', state.ctx);
                va_arg(va, double);
#endif
                break;
            case 'c' : {
                unsigned int l = 1U;
                // pre padding
                if (!(state.flags & FMT_FLAG_LEFT)) {
                    while (l++ < state.width) {
                        out(' ', state.ctx);
                    }
                }
                // char output
                out((char) va_arg(va, int), state.ctx);
                // post padding
                if (state.flags & FMT_FLAG_LEFT) {
                    while (l++ < state.width) {
                        out(' ', state.ctx);
                    }
                }
                break;
            }

            case 's' : {
                const char *p = va_arg(va, char*);
                unsigned int l = _strnlen_s(p, state.precision ? state.precision : (size_t) -1);
                // pre padding
                if (state.flags & FMT_FLAG_PRECISION) {
                    l = (l < state.precision ? l : state.precision);
                }
                if (!(state.flags & FMT_FLAG_LEFT)) {
                    while (l++ < state.width) {
                        out(' ', state.ctx);
                    }
                }
                // string output
                while ((*p != 0) && (!(state.flags & FMT_FLAG_PRECISION) || state.precision--)) {
                    out(*(p++), state.ctx);
                }
                // post padding
                if (state.flags & FMT_FLAG_LEFT) {
                    while (l++ < state.width) {
                        out(' ', state.ctx);
                    }
                }
                break;
            }

            case 'p' : {
                state.width = sizeof(void *) * 2U;
                state.flags |= FMT_FLAG_ZEROPAD;
                state.specifier = 'X';
#if PICO_PRINTF_SUPPORT_LONG_LONG
                const bool is_ll = sizeof(uintptr_t) == sizeof(long long);
                if (is_ll) {
                    _ntoa_long_long(state, (uintptr_t) va_arg(va, void*), false, 16U);
                } else {
#endif
                    _ntoa_long(state, (unsigned long) ((uintptr_t) va_arg(va, void*)), false, 16U);
#if PICO_PRINTF_SUPPORT_LONG_LONG
                }
#endif
                break;
            }

            case '%' :
                out('%', state.ctx);
                break;

            default :
                out(state.specifier, state.ctx);
                break;
        }
    }

    return (int) _ctx.idx;
}
