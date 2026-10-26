/*
 * Hemlock Parser - gpu fn OpenCL C code generation
 *
 * Compiles a validated `gpu fn` kernel to an OpenCL C program whose results
 * match CPU emulation (@stdlib/gpu_emu), which runs the kernel under
 * Hemlock's own rules. C's rules differ in many places, so nothing is left
 * to C's implicit conversions:
 *
 * - Every expression is statically typed with Hemlock's promotion table
 *   (hml_tk_promote, shared with both backends). Binary operands are cast
 *   explicitly to the promoted type, then the operation runs in that type.
 * - i32/i64 + - * and negation are overflow-checked; i8/i16 and unsigned
 *   arithmetic wraps; `/` always yields a float and integer division by zero
 *   fails; `%` follows Hemlock (MIN % -1 == 0, zero divisor fails); shifts
 *   use Hemlock's count rules; `switch` uses Hemlock's loose equality.
 * - Conversions (explicit, and into typed locals/params/buffers) are the
 *   interpreter's convert_to_type: range-checked, NaN/Infinity rejected.
 * - Buffer accesses are bounds-checked against a length passed per buffer.
 *
 * Errors: a failing operation records (site, detail) in the work-item's
 * hml_ctx; the statement finishes without side effects (stores check the
 * flag), then the work-item reports and returns. Reporting keeps the error
 * with the lowest linear work-item id (a 64-bit atomic min on
 * id << HML_GPU_SITE_BITS | site), which is the error the emulator, running
 * work-items in order, stops at. Within a statement the lowest site wins:
 * sites are numbered in Hemlock evaluation order, and assignments are
 * statement-level only, so C's unspecified operand order cannot change
 * which error is reported. The detail value (bad index, out-of-range value)
 * is recorded in one of HML_GPU_ERR_SLOTS slots; the host formats messages
 * from the site table.
 *
 * Floating point: FP_CONTRACT is off (no fused multiply-add). f32 division
 * and sqrt go through double when the device has fp64, which rounds exactly
 * like the emulator; f32 transcendentals likewise (otherwise they may differ
 * in the last ulp, as may f64 transcendentals).
 */

#include "gpu_internal.h"
#include "type_promotion.h"
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <float.h>
#include <math.h>

// Offsets of the per-conversion error sites (one block of sites per conversion)
enum {
    CONV_SITE_NAN,
    CONV_SITE_POS_INF,
    CONV_SITE_NEG_INF,
    CONV_SITE_FLOAT_RANGE,
    CONV_SITE_VALUE_RANGE,
    CONV_SITE_COUNT
};

// Largest integer literal compared exactly by the switch lowering (2^53):
// below it, Hemlock's double-based switch equality is plain integer equality.
#define GPU_EXACT_DOUBLE_INT (INT64_C(1) << 53)

// ========== STRING BUILDER ==========

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} SBuf;

static void sb_init(SBuf *b) {
    b->cap = 256;
    b->len = 0;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
}

static void sb_vprintf(SBuf *b, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    if (n < 0) { va_end(ap2); return; }
    if ((size_t)n >= b->cap - b->len) {
        while ((size_t)n >= b->cap - b->len) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
        vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2);
    }
    va_end(ap2);
    b->len += (size_t)n;
}

static void sb_printf(SBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(b, fmt, ap);
    va_end(ap);
}

static void sb_append(SBuf *b, const char *s) { sb_printf(b, "%s", s); }

static char *sb_take(SBuf *b) { return b->data; }

// ========== TYPES ==========

static HmlTypeKind tk_of(TypeKind k) {
    switch (k) {
        case TYPE_I8:  return HML_TK_I8;
        case TYPE_I16: return HML_TK_I16;
        case TYPE_I32: return HML_TK_I32;
        case TYPE_I64: return HML_TK_I64;
        case TYPE_U8:  return HML_TK_U8;
        case TYPE_U16: return HML_TK_U16;
        case TYPE_U32: return HML_TK_U32;
        case TYPE_U64: return HML_TK_U64;
        case TYPE_F32: return HML_TK_F32;
        case TYPE_F64: return HML_TK_F64;
        case TYPE_BOOL: return HML_TK_BOOL;
        default: return HML_TK_OTHER;
    }
}

static const char *ctype(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8:  return "char";
        case HML_TK_I16: return "short";
        case HML_TK_I32: return "int";
        case HML_TK_I64: return "long";
        case HML_TK_U8:  return "uchar";
        case HML_TK_U16: return "ushort";
        case HML_TK_U32: return "uint";
        case HML_TK_U64: return "ulong";
        case HML_TK_F32: return "float";
        case HML_TK_F64: return "double";
        case HML_TK_BOOL: return "bool";
        default: return "void";
    }
}

static const char *tname(HmlTypeKind t) {
    return t == HML_TK_OTHER ? "?" : hml_tk_name(t);
}

static int tbits(HmlTypeKind t) { return hml_tk_sizeof(t) * 8; }

static int is_int(HmlTypeKind t) { return hml_tk_is_integer(t); }
static int is_signed(HmlTypeKind t) { return hml_tk_is_signed_integer(t); }
static int is_float(HmlTypeKind t) { return hml_tk_is_float(t); }
static int is_num(HmlTypeKind t) { return is_int(t) || is_float(t); }

// Inclusive range of an integer type, as C literals and as values
static const char *tmin_c(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8:  return "-128L";
        case HML_TK_I16: return "-32768L";
        case HML_TK_I32: return "-2147483648L";
        default: return "0L";
    }
}

static const char *tmax_c(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8:  return "127L";
        case HML_TK_I16: return "32767L";
        case HML_TK_I32: return "2147483647L";
        case HML_TK_U8:  return "255L";
        case HML_TK_U16: return "65535L";
        case HML_TK_U32: return "4294967295L";
        default: return "0L";
    }
}

// The bracketed range in the interpreter's "Value N out of range" messages
static const char *trange_text(HmlTypeKind t) {
    switch (t) {
        case HML_TK_I8:  return "[-128, 127]";
        case HML_TK_I16: return "[-32768, 32767]";
        case HML_TK_I32: return "[-2147483648, 2147483647]";
        case HML_TK_U8:  return "[0, 255]";
        case HML_TK_U16: return "[0, 65535]";
        case HML_TK_U32: return "[0, 4294967295]";
        case HML_TK_U64: return "[0, 18446744073709551615]";
        default: return "";
    }
}

// Result type of unary minus (unsigned types widen to the next signed type)
static HmlTypeKind neg_type(HmlTypeKind t) {
    switch (t) {
        case HML_TK_U8:  return HML_TK_I16;
        case HML_TK_U16: return HML_TK_I32;
        case HML_TK_U32:
        case HML_TK_U64: return HML_TK_I64;
        default: return t;
    }
}

// ========== GENERATOR STATE ==========

typedef struct {
    char *name;       // Hemlock name
    char *cname;      // C name
    HmlTypeKind type;
    int is_const;
    int depth;
} GVar;

typedef struct {
    char *label;      // Hemlock loop label (NULL if unlabeled)
    int id;
} GLoop;

typedef struct {
    KernelCheck *kc;
    Expr *kernel;
    const char *name;
    int failed;

    GVar *vars;
    int num_vars, vars_cap;
    int depth;
    int var_counter;

    GLoop *loops;
    int num_loops, loops_cap;
    int loop_counter;

    SBuf helpers;
    char **helper_names;
    int num_helpers, helpers_cap;

    GpuSite *sites;
    int num_sites, sites_cap;

    int uses_f64;
    char *f64_reason;
    int uses_groups;
    int uses_barrier; // kernel calls barrier(): different error flow (see PRELUDE)
    int line;         // line of the statement being generated
    int column;
} Gen;

static void gen_error(Gen *g, int line, const char *fmt, ...) {
    if (g->failed) return;
    g->failed = 1;
    if (!g->kc) return;
    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    kernel_error(g->kc, line > 0 ? line : g->line, line > 0 ? 0 : g->column, "%s", msg);
}

static void mark_f64(Gen *g, int line, const char *what) {
    if (g->uses_f64) return;
    g->uses_f64 = 1;
    char buf[160];
    snprintf(buf, sizeof(buf), "line %d: %s", line > 0 ? line : g->line, what);
    g->f64_reason = strdup(buf);
}

// Note a value of type t being computed; f64 values need device fp64
static void note_type(Gen *g, HmlTypeKind t, int line, const char *what) {
    if (t == HML_TK_F64) mark_f64(g, line, what);
}

// ========== SITES ==========

static unsigned new_site(Gen *g, int line, const char *detail, const char *buf, const char *fmt, ...) {
    if (g->num_sites >= g->sites_cap) {
        g->sites_cap = g->sites_cap ? g->sites_cap * 2 : 16;
        g->sites = realloc(g->sites, sizeof(GpuSite) * g->sites_cap);
    }
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    GpuSite *s = &g->sites[g->num_sites];
    s->msg = strdup(msg);
    s->detail = detail;
    s->buf = buf ? strdup(buf) : NULL;
    s->line = line > 0 ? line : g->line;
    if (g->num_sites >= (1 << HML_GPU_SITE_BITS) - 1) {
        gen_error(g, line, "kernel has too many checked operations (limit %d)", (1 << HML_GPU_SITE_BITS) - 1);
    }
    return (unsigned)g->num_sites++;
}

// The five sites of one conversion from S to T, in CONV_SITE_* order
static unsigned conv_sites(Gen *g, int line, HmlTypeKind from, HmlTypeKind to) {
    const char *t = tname(to);
    unsigned base = new_site(g, line, "none", NULL, "Cannot convert NaN to %s", t);
    new_site(g, line, "none", NULL, "Cannot convert Infinity to %s", t);
    new_site(g, line, "none", NULL, "Cannot convert -Infinity to %s", t);
    new_site(g, line, "none", NULL, "Float value out of range for integer conversion to %s", t);
    if (to == HML_TK_U64) {
        // a float source reports its float value, an integer source its int64 value
        const char *detail = from == HML_TK_F32 ? "f32" : from == HML_TK_F64 ? "f64" : "i64";
        new_site(g, line, detail, NULL, "Value %%v out of range for u64 %s", trange_text(to));
    } else {
        new_site(g, line, "i64", NULL, "Value %%v out of range for %s %s", t, trange_text(to));
    }
    return base;
}

// ========== HELPERS (emitted on demand) ==========

// Returns 1 the first time `name` is requested: the caller then emits it
static int want_helper(Gen *g, const char *name) {
    for (int i = 0; i < g->num_helpers; i++) {
        if (strcmp(g->helper_names[i], name) == 0) return 0;
    }
    if (g->num_helpers >= g->helpers_cap) {
        g->helpers_cap = g->helpers_cap ? g->helpers_cap * 2 : 16;
        g->helper_names = realloc(g->helper_names, sizeof(char*) * g->helpers_cap);
    }
    g->helper_names[g->num_helpers++] = strdup(name);
    return 1;
}

// Checked/wrapping + - * in type r: returns the helper name
static const char *helper_arith(Gen *g, BinaryOp op, HmlTypeKind r, char *name, size_t n) {
    const char *opname = op == OP_ADD ? "add" : op == OP_SUB ? "sub" : "mul";
    const char *opc = op == OP_ADD ? "+" : op == OP_SUB ? "-" : "*";
    snprintf(name, n, "hml_%s_%s", opname, tname(r));
    if (!want_helper(g, name)) return name;
    const char *c = ctype(r);
    SBuf *h = &g->helpers;
    sb_printf(h, "%s %s(HML_CTX *c, uint site, %s a, %s b) {\n", c, name, c, c);
    switch (r) {
        case HML_TK_I32:
            sb_printf(h, "    long r = (long)a %s (long)b;\n", opc);
            sb_printf(h, "    if (r < -2147483648L || r > 2147483647L) { hml_fail(c, site, 0); return 0; }\n");
            sb_printf(h, "    return (int)r;\n");
            break;
        case HML_TK_I64:
            if (op == OP_MUL) {
                sb_printf(h, "    long r = as_long(as_ulong(a) * as_ulong(b));\n");
                sb_printf(h, "    if (mul_hi(a, b) != (r >> 63)) { hml_fail(c, site, 0); return 0; }\n");
            } else if (op == OP_ADD) {
                sb_printf(h, "    long r = as_long(as_ulong(a) + as_ulong(b));\n");
                sb_printf(h, "    if (((a ^ r) & (b ^ r)) < 0) { hml_fail(c, site, 0); return 0; }\n");
            } else {
                sb_printf(h, "    long r = as_long(as_ulong(a) - as_ulong(b));\n");
                sb_printf(h, "    if (((a ^ b) & (a ^ r)) < 0) { hml_fail(c, site, 0); return 0; }\n");
            }
            sb_printf(h, "    return r;\n");
            break;
        case HML_TK_I8:
            sb_printf(h, "    return as_char((uchar)((int)a %s (int)b));\n", opc);
            break;
        case HML_TK_I16:
            sb_printf(h, "    return as_short((ushort)((int)a %s (int)b));\n", opc);
            break;
        case HML_TK_U8:
        case HML_TK_U16:
            sb_printf(h, "    return (%s)((uint)a %s (uint)b);\n", c, opc);
            break;
        default:  // u32, u64, f32, f64
            sb_printf(h, "    return a %s b;\n", opc);
            break;
    }
    sb_printf(h, "}\n");
    return name;
}

static const char *helper_neg(Gen *g, HmlTypeKind t, char *name, size_t n) {
    snprintf(name, n, "hml_neg_%s", tname(t));
    if (!want_helper(g, name)) return name;
    HmlTypeKind r = neg_type(t);
    SBuf *h = &g->helpers;
    sb_printf(h, "%s %s(HML_CTX *c, uint site, %s x) {\n", ctype(r), name, ctype(t));
    switch (t) {
        case HML_TK_I8:  sb_printf(h, "    return as_char((uchar)(-(int)x));\n"); break;
        case HML_TK_I16: sb_printf(h, "    return as_short((ushort)(-(int)x));\n"); break;
        case HML_TK_I32:
            sb_printf(h, "    if (x == (-2147483647 - 1)) { hml_fail(c, site, 0); return 0; }\n    return -x;\n");
            break;
        case HML_TK_I64:
            sb_printf(h, "    if (x == (-9223372036854775807L - 1)) { hml_fail(c, site, 0); return 0; }\n    return -x;\n");
            break;
        case HML_TK_U8:  sb_printf(h, "    return -(short)x;\n"); break;
        case HML_TK_U16: sb_printf(h, "    return -(int)x;\n"); break;
        case HML_TK_U32: sb_printf(h, "    return -(long)x;\n"); break;
        case HML_TK_U64:
            sb_printf(h, "    if (x > 9223372036854775807UL) { hml_fail(c, site, 0); return 0; }\n    return -(long)x;\n");
            break;
        default: sb_printf(h, "    return -x;\n"); break;
    }
    sb_printf(h, "}\n");
    return name;
}

// Integer `/` (always a double) and `%`, in type r
static const char *helper_divmod(Gen *g, int is_mod, HmlTypeKind r, char *name, size_t n) {
    snprintf(name, n, "hml_%s_%s", is_mod ? "mod" : "div", tname(r));
    if (!want_helper(g, name)) return name;
    const char *c = ctype(r);
    SBuf *h = &g->helpers;
    sb_printf(h, "%s %s(HML_CTX *c, uint site, %s a, %s b) {\n", is_mod ? c : "double", name, c, c);
    sb_printf(h, "    if (b == 0) { hml_fail(c, site, 0); return 0; }\n");
    if (is_mod) {
        if (is_signed(r)) sb_printf(h, "    if (b == -1) return 0;\n");
        sb_printf(h, "    return (%s)(a %% b);\n", c);
    } else {
        sb_printf(h, "    return (double)a / (double)b;\n");
    }
    sb_printf(h, "}\n");
    return name;
}

// Hemlock shifts in type r: negative count fails, counts >= width give 0
// (or -1 for >> of a negative signed value); the count is converted to r
static const char *helper_shift(Gen *g, int left, HmlTypeKind r, char *name, size_t n) {
    snprintf(name, n, "hml_%s_%s", left ? "shl" : "shr", tname(r));
    if (!want_helper(g, name)) return name;
    const char *c = ctype(r);
    int bw = tbits(r);
    SBuf *h = &g->helpers;
    sb_printf(h, "%s %s(HML_CTX *c, uint site, %s a, %s b) {\n", c, name, c, c);
    if (is_signed(r)) {
        sb_printf(h, "    long l = a, s = b;\n");
        sb_printf(h, "    if (s < 0) { hml_fail(c, site, 0); return 0; }\n");
        if (left) {
            sb_printf(h, "    if (s >= %d) return 0;\n", bw);
            switch (r) {
                case HML_TK_I8:  sb_printf(h, "    return as_char((uchar)((ulong)l << s));\n"); break;
                case HML_TK_I16: sb_printf(h, "    return as_short((ushort)((ulong)l << s));\n"); break;
                case HML_TK_I32: sb_printf(h, "    return as_int((uint)((ulong)l << s));\n"); break;
                default:         sb_printf(h, "    return as_long((ulong)l << s);\n"); break;
            }
        } else {
            sb_printf(h, "    if (s >= %d) return l < 0 ? -1 : 0;\n", bw);
            sb_printf(h, "    return (%s)(l >> s);\n", c);
        }
    } else {
        sb_printf(h, "    ulong l = a, s = b;\n");
        sb_printf(h, "    if ((long)s < 0) { hml_fail(c, site, 0); return 0; }\n");
        sb_printf(h, "    if (s >= %d) return 0;\n", bw);
        sb_printf(h, "    return (%s)(l %s s);\n", c, left ? "<<" : ">>");
    }
    sb_printf(h, "}\n");
    return name;
}

// The interpreter's convert_to_type(S -> T): used by explicit conversions
// and by every store into a typed local, parameter or buffer element
static const char *helper_conv(Gen *g, HmlTypeKind s, HmlTypeKind t, char *name, size_t n) {
    snprintf(name, n, "hml_cv_%s_%s", tname(s), tname(t));
    if (!want_helper(g, name)) return name;
    SBuf *h = &g->helpers;
    const char *ct = ctype(t);
    sb_printf(h, "%s %s(HML_CTX *c, uint site, %s x) {\n", ct, name, ctype(s));
    if (t == HML_TK_BOOL) {
        sb_printf(h, "    return x != 0;\n");
    } else if (is_float(s)) {
        if (is_float(t)) {
            sb_printf(h, "    return (%s)x;\n", ct);
        } else {
            sb_printf(h, "    if (isnan(x)) { hml_fail(c, site + %d, 0); return 0; }\n", CONV_SITE_NAN);
            sb_printf(h, "    if (isinf(x)) { hml_fail(c, site + (x < 0 ? %d : %d), 0); return 0; }\n",
                      CONV_SITE_NEG_INF, CONV_SITE_POS_INF);
            // |x| beyond the i64 range (bounds exact in both float widths)
            const char *sfx = s == HML_TK_F32 ? "f" : "";
            sb_printf(h, "    if (x >= 0x1.0p63%s || x < -0x1.0p63%s) { hml_fail(c, site + %d, 0); return 0; }\n",
                      sfx, sfx, CONV_SITE_FLOAT_RANGE);
            if (t == HML_TK_U64) {
                sb_printf(h, "    if (x < 0) { hml_fail(c, site + %d, (ulong)%s(x)); return 0; }\n",
                          CONV_SITE_VALUE_RANGE, s == HML_TK_F32 ? "as_uint" : "as_ulong");
                sb_printf(h, "    return (ulong)x;\n");
            } else {
                sb_printf(h, "    long v = (long)x;\n");
                if (t != HML_TK_I64) {
                    sb_printf(h, "    if (v < %s || v > %s) { hml_fail(c, site + %d, (ulong)v); return 0; }\n",
                              tmin_c(t), tmax_c(t), CONV_SITE_VALUE_RANGE);
                }
                sb_printf(h, "    return (%s)v;\n", ct);
            }
        }
    } else {
        // Integer or bool source: the interpreter widens to int64 first, so a
        // u64 above INT64_MAX reads as negative (kept for parity)
        if (s == HML_TK_BOOL) {
            sb_printf(h, "    long v = x ? 1 : 0;\n");
        } else if (s == HML_TK_U64) {
            sb_printf(h, "    long v = as_long(x);\n");
        } else {
            sb_printf(h, "    long v = (long)x;\n");
        }
        if (is_float(t)) {
            sb_printf(h, "    return (%s)v;\n", ct);
        } else if (t == HML_TK_I64) {
            sb_printf(h, "    return v;\n");
        } else if (t == HML_TK_U64) {
            if (s == HML_TK_U64) {
                sb_printf(h, "    return x;\n");
            } else {
                sb_printf(h, "    if (v < 0) { hml_fail(c, site + %d, (ulong)v); return 0; }\n", CONV_SITE_VALUE_RANGE);
                sb_printf(h, "    return (ulong)v;\n");
            }
        } else {
            sb_printf(h, "    if (v < %s || v > %s) { hml_fail(c, site + %d, (ulong)v); return 0; }\n",
                      tmin_c(t), tmax_c(t), CONV_SITE_VALUE_RANGE);
            sb_printf(h, "    return (%s)v;\n", ct);
        }
    }
    sb_printf(h, "}\n");
    return name;
}

// OpenCL 1.2 has no generic address space: global and local memory get
// separate load/store helpers
static const char *helper_load(Gen *g, int local, HmlTypeKind t, char *name, size_t n) {
    snprintf(name, n, "hml_ld%s_%s", local ? "l" : "", tname(t));
    if (!want_helper(g, name)) return name;
    const char *c = ctype(t);
    sb_printf(&g->helpers,
              "%s %s(HML_CTX *c, uint site, %s %s *b, ulong n, long i) {\n"
              "    if (i < 0 || (ulong)i >= n) { hml_fail(c, site, (ulong)i); return 0; }\n"
              "    return b[i];\n"
              "}\n", c, name, local ? "__local" : "__global", c);
    return name;
}

// Bounds check, then convert, then store (the emulator's store() order)
static const char *helper_store(Gen *g, int local, HmlTypeKind s, HmlTypeKind t, char *name, size_t n) {
    snprintf(name, n, "hml_st%s_%s_%s", local ? "l" : "", tname(s), tname(t));
    if (!want_helper(g, name)) return name;
    char conv[64];
    const char *cv = NULL;
    if (s != t) cv = helper_conv(g, s, t, conv, sizeof(conv));
    const char *c = ctype(t);
    SBuf *h = &g->helpers;
    sb_printf(h, "%s %s(HML_CTX *c, uint site_oob, uint site_conv, %s %s *b, ulong n, long i, %s v) {\n",
              c, name, local ? "__local" : "__global", c, ctype(s));
    sb_printf(h, "    if (i < 0 || (ulong)i >= n) { hml_fail(c, site_oob, (ulong)i); return 0; }\n");
    if (cv) {
        sb_printf(h, "    %s cv = %s(c, site_conv, v);\n", c, cv);
    } else {
        sb_printf(h, "    %s cv = v;\n", c);
    }
    sb_printf(h, "    if (!c->failed) b[i] = cv;\n    return cv;\n}\n");
    return name;
}

static const char *helper_minmax(Gen *g, int is_max, HmlTypeKind t, char *name, size_t n) {
    snprintf(name, n, "hml_%s_%s", is_max ? "max" : "min", tname(t));
    if (!want_helper(g, name)) return name;
    const char *c = ctype(t);
    sb_printf(&g->helpers, "%s %s(%s a, %s b) { return b %s a ? b : a; }\n",
              c, name, c, c, is_max ? ">" : "<");
    return name;
}

static const char *helper_abs(Gen *g, HmlTypeKind t, char *name, size_t n) {
    snprintf(name, n, "hml_abs_%s", tname(t));
    if (!want_helper(g, name)) return name;
    const char *c = ctype(t);
    if (is_signed(t)) {
        char neg[64];
        helper_neg(g, t, neg, sizeof(neg));
        sb_printf(&g->helpers, "%s %s(HML_CTX *c, uint site, %s x) { return x < 0 ? %s(c, site, x) : x; }\n",
                  c, name, c, neg);
    } else if (is_float(t)) {
        sb_printf(&g->helpers, "%s %s(HML_CTX *c, uint site, %s x) { return x < 0 ? -x : x; }\n", c, name, c);
    } else {
        sb_printf(&g->helpers, "%s %s(HML_CTX *c, uint site, %s x) { return x; }\n", c, name, c);
    }
    return name;
}

// ========== SCOPES ==========

static GVar *find_var(Gen *g, const char *name) {
    for (int i = g->num_vars - 1; i >= 0; i--) {
        if (strcmp(g->vars[i].name, name) == 0) return &g->vars[i];
    }
    return NULL;
}

static GVar *declare_var(Gen *g, const char *name, HmlTypeKind type, int is_const, const char *cname) {
    if (g->num_vars >= g->vars_cap) {
        g->vars_cap = g->vars_cap ? g->vars_cap * 2 : 16;
        g->vars = realloc(g->vars, sizeof(GVar) * g->vars_cap);
    }
    GVar *v = &g->vars[g->num_vars++];
    v->name = strdup(name);
    if (cname) {
        v->cname = strdup(cname);
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "v%d_%s", g->var_counter++, name);
        v->cname = strdup(buf);
    }
    v->type = type;
    v->is_const = is_const;
    v->depth = g->depth;
    return v;
}

static void push_scope(Gen *g) { g->depth++; }

static void pop_scope(Gen *g) {
    while (g->num_vars > 0 && g->vars[g->num_vars - 1].depth == g->depth) {
        g->num_vars--;
        free(g->vars[g->num_vars].name);
        free(g->vars[g->num_vars].cname);
    }
    g->depth--;
}

// ========== EXPRESSIONS ==========

static HmlTypeKind gen_expr(Gen *g, Expr *e, SBuf *out, int effects_ok);

static int line_of(Gen *g, Expr *e) { return e && e->line > 0 ? e->line : g->line; }

// C text for a value of type s converted (Hemlock conversion) to t
static void emit_conv(Gen *g, HmlTypeKind s, HmlTypeKind t, const char *val, int line, SBuf *out) {
    if (s == t) {
        sb_append(out, val);
        return;
    }
    char name[64];
    unsigned site = conv_sites(g, line, s, t);
    helper_conv(g, s, t, name, sizeof(name));
    sb_printf(out, "%s(&hml_c, %uu, %s)", name, site, val);
}

// A float literal, possibly negated: its value
static int float_literal(Expr *e, double *value) {
    if (e && e->type == EXPR_NUMBER && e->as.number.is_float) {
        *value = e->as.number.float_value;
        return 1;
    }
    if (e && e->type == EXPR_UNARY && e->as.unary.op == UNARY_NEGATE && float_literal(e->as.unary.operand, value)) {
        *value = -*value;
        return 1;
    }
    return 0;
}

// Emit a float literal converted to f32 at compile time (round to nearest,
// exactly what the run-time conversion does), so constants like
// `let x: f32 = -0.5;` need no device fp64. Returns 0 if `e` is not one.
static int fold_f32_literal(Expr *e, SBuf *out) {
    double d;
    if (!float_literal(e, &d)) return 0;
    float f = (float)d;
    if (isinf(f)) {
        sb_printf(out, "(%sINFINITY)", f < 0 ? "-" : "");
    } else {
        char probe[64];
        snprintf(probe, sizeof(probe), "%.9g", (double)f);
        sb_printf(out, "((float)%s%sf)", probe, strpbrk(probe, ".eEn") ? "" : ".0");
    }
    return 1;
}

// Generate `e` converted to t, folding float literals that go to f32
static HmlTypeKind gen_converted(Gen *g, Expr *e, HmlTypeKind t, SBuf *out) {
    if (t == HML_TK_F32 && fold_f32_literal(e, out)) {
        return HML_TK_F32;
    }
    SBuf tmp;
    sb_init(&tmp);
    HmlTypeKind s = gen_expr(g, e, &tmp, 0);
    if (s == HML_TK_OTHER) { free(tmp.data); return s; }
    if (s == HML_TK_BOOL && t != HML_TK_BOOL) {
        gen_error(g, line_of(g, e), "cannot convert bool to %s in kernels", tname(t));
        free(tmp.data);
        return HML_TK_OTHER;
    }
    emit_conv(g, s, t, tmp.data, line_of(g, e), out);
    note_type(g, t, line_of(g, e), "f64 value");
    free(tmp.data);
    return t;
}

static void emit_truth(HmlTypeKind t, const char *val, SBuf *out) {
    if (t == HML_TK_BOOL) {
        sb_printf(out, "(%s)", val);
    } else {
        sb_printf(out, "((%s) != 0)", val);
    }
}

// Generate operands a and b, check both numeric, and return them cast to
// their promoted type r
static HmlTypeKind gen_operands(Gen *g, Expr *e, SBuf *ca, SBuf *cb, HmlTypeKind *ta, HmlTypeKind *tb) {
    *ta = gen_expr(g, e->as.binary.left, ca, 0);
    *tb = gen_expr(g, e->as.binary.right, cb, 0);
    if (*ta == HML_TK_OTHER || *tb == HML_TK_OTHER) return HML_TK_OTHER;
    return hml_tk_promote(*ta, *tb);
}

static const char *binop_text(BinaryOp op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQUAL: return "==";
        case OP_NOT_EQUAL: return "!=";
        case OP_LESS: return "<";
        case OP_LESS_EQUAL: return "<=";
        case OP_GREATER: return ">";
        case OP_GREATER_EQUAL: return ">=";
        case OP_AND: return "&&";
        case OP_OR: return "||";
        case OP_BIT_AND: return "&";
        case OP_BIT_OR: return "|";
        case OP_BIT_XOR: return "^";
        case OP_BIT_LSHIFT: return "<<";
        case OP_BIT_RSHIFT: return ">>";
    }
    return "?";
}

static HmlTypeKind gen_binary(Gen *g, Expr *e, SBuf *out) {
    BinaryOp op = e->as.binary.op;
    int line = line_of(g, e);
    SBuf ca, cb;
    sb_init(&ca);
    sb_init(&cb);
    HmlTypeKind ta, tb, result = HML_TK_OTHER;
    HmlTypeKind r = gen_operands(g, e, &ca, &cb, &ta, &tb);
    char name[64];
    if (r == HML_TK_OTHER) goto done;

    if (op == OP_AND || op == OP_OR) {
        sb_append(out, "(");
        emit_truth(ta, ca.data, out);
        sb_printf(out, " %s ", binop_text(op));
        emit_truth(tb, cb.data, out);
        sb_append(out, ")");
        result = HML_TK_BOOL;
        goto done;
    }

    if (ta == HML_TK_BOOL || tb == HML_TK_BOOL) {
        if ((op == OP_EQUAL || op == OP_NOT_EQUAL) && ta == HML_TK_BOOL && tb == HML_TK_BOOL) {
            sb_printf(out, "(%s %s %s)", ca.data, binop_text(op), cb.data);
            result = HML_TK_BOOL;
        } else {
            gen_error(g, line, "operator '%s' needs numeric operands, got %s and %s",
                      binop_text(op), tname(ta), tname(tb));
        }
        goto done;
    }

    const char *cr = ctype(r);
    switch (op) {
        case OP_ADD:
        case OP_SUB:
        case OP_MUL: {
            const char *word = op == OP_ADD ? "addition" : op == OP_SUB ? "subtraction" : "multiplication";
            unsigned site = 0;
            if (r == HML_TK_I32 || r == HML_TK_I64) {
                site = new_site(g, line, "none", NULL, "Integer overflow: %s %s", tname(r), word);
            }
            helper_arith(g, op, r, name, sizeof(name));
            sb_printf(out, "%s(&hml_c, %uu, (%s)(%s), (%s)(%s))", name, site, cr, ca.data, cr, cb.data);
            note_type(g, r, line, "f64 arithmetic");
            result = r;
            break;
        }
        case OP_DIV:
            if (is_int(ta) && is_int(tb)) {
                unsigned site = new_site(g, line, "none", NULL, "Division by zero");
                helper_divmod(g, 0, r, name, sizeof(name));
                sb_printf(out, "%s(&hml_c, %uu, (%s)(%s), (%s)(%s))", name, site, cr, ca.data, cr, cb.data);
                mark_f64(g, line, "integer division returns f64 (convert operands with f32() for f32 division)");
                result = HML_TK_F64;
            } else if (r == HML_TK_F32) {
                sb_printf(out, "HML_DIV_F32((float)(%s), (float)(%s))", ca.data, cb.data);
                result = r;
            } else {
                sb_printf(out, "((double)(%s) / (double)(%s))", ca.data, cb.data);
                mark_f64(g, line, "f64 division");
                result = r;
            }
            break;
        case OP_MOD:
            if (is_float(r)) {
                sb_printf(out, "fmod((%s)(%s), (%s)(%s))", cr, ca.data, cr, cb.data);
                note_type(g, r, line, "f64 remainder");
            } else {
                unsigned site = new_site(g, line, "none", NULL, "Division by zero");
                helper_divmod(g, 1, r, name, sizeof(name));
                sb_printf(out, "%s(&hml_c, %uu, (%s)(%s), (%s)(%s))", name, site, cr, ca.data, cr, cb.data);
            }
            result = r;
            break;
        case OP_EQUAL:
        case OP_NOT_EQUAL:
        case OP_LESS:
        case OP_LESS_EQUAL:
        case OP_GREATER:
        case OP_GREATER_EQUAL:
            sb_printf(out, "((%s)(%s) %s (%s)(%s))", cr, ca.data, binop_text(op), cr, cb.data);
            note_type(g, r, line, "f64 comparison");
            result = HML_TK_BOOL;
            break;
        case OP_BIT_AND:
        case OP_BIT_OR:
        case OP_BIT_XOR:
            if (!is_int(r)) {
                gen_error(g, line, "operator '%s' needs integer operands, got %s and %s",
                          binop_text(op), tname(ta), tname(tb));
                break;
            }
            sb_printf(out, "((%s)((%s)(%s) %s (%s)(%s)))", cr, cr, ca.data, binop_text(op), cr, cb.data);
            result = r;
            break;
        case OP_BIT_LSHIFT:
        case OP_BIT_RSHIFT: {
            if (!is_int(r)) {
                gen_error(g, line, "operator '%s' needs integer operands, got %s and %s",
                          binop_text(op), tname(ta), tname(tb));
                break;
            }
            unsigned site = new_site(g, line, "none", NULL, "Shift amount must be non-negative");
            helper_shift(g, op == OP_BIT_LSHIFT, r, name, sizeof(name));
            sb_printf(out, "%s(&hml_c, %uu, (%s)(%s), (%s)(%s))", name, site, cr, ca.data, cr, cb.data);
            result = r;
            break;
        }
        default:
            gen_error(g, line, "operator '%s' is not supported in kernels", binop_text(op));
            break;
    }

done:
    free(ca.data);
    free(cb.data);
    return result;
}

static HmlTypeKind gen_unary(Gen *g, Expr *e, SBuf *out) {
    int line = line_of(g, e);
    SBuf co;
    sb_init(&co);
    HmlTypeKind t = gen_expr(g, e->as.unary.operand, &co, 0);
    HmlTypeKind result = HML_TK_OTHER;
    char name[64];
    if (t == HML_TK_OTHER) goto done;
    switch (e->as.unary.op) {
        case UNARY_NOT:
            sb_append(out, "(!");
            emit_truth(t, co.data, out);
            sb_append(out, ")");
            result = HML_TK_BOOL;
            break;
        case UNARY_NEGATE: {
            if (!is_num(t)) {
                gen_error(g, line, "cannot negate a %s", tname(t));
                break;
            }
            unsigned site = 0;
            if (t == HML_TK_I32 || t == HML_TK_I64) {
                site = new_site(g, line, "none", NULL, "Integer overflow: %s negation", tname(t));
            } else if (t == HML_TK_U64) {
                site = new_site(g, line, "none", NULL, "Cannot negate u64 value larger than INT64_MAX");
            }
            helper_neg(g, t, name, sizeof(name));
            sb_printf(out, "%s(&hml_c, %uu, %s)", name, site, co.data);
            result = neg_type(t);
            break;
        }
        case UNARY_BIT_NOT:
            if (!is_int(t)) {
                gen_error(g, line, "operator '~' needs an integer, got %s", tname(t));
                break;
            }
            sb_printf(out, "((%s)~(%s))", ctype(t), co.data);
            result = t;
            break;
    }
done:
    free(co.data);
    return result;
}

static HmlTypeKind gen_call(Gen *g, Expr *e, SBuf *out) {
    int line = line_of(g, e);
    // The validator guarantees a named intrinsic with the right arity; an
    // invalid kernel (still parsed by `hemlock check`) just stops here
    if (!e->as.call.func || e->as.call.func->type != EXPR_IDENT ||
        gpu_intrinsic_arity(e->as.call.func->as.ident.name) != e->as.call.num_args) {
        gen_error(g, line, "unsupported call in kernel");
        return HML_TK_OTHER;
    }
    const char *fname = e->as.call.func->as.ident.name;
    int n = e->as.call.num_args;
    if (strcmp(fname, "barrier") == 0) {
        gen_error(g, line, "barrier() must be a statement");
        return HML_TK_OTHER;
    }
    SBuf args[2];
    HmlTypeKind types[2] = { HML_TK_OTHER, HML_TK_OTHER };
    HmlTypeKind result = HML_TK_OTHER;
    char name[64];

    if (gpu_is_conversion_intrinsic(fname)) {
        TypeKind tk = TYPE_I32;
        if (strcmp(fname, "i8") == 0) tk = TYPE_I8;
        else if (strcmp(fname, "i16") == 0) tk = TYPE_I16;
        else if (strcmp(fname, "i32") == 0) tk = TYPE_I32;
        else if (strcmp(fname, "i64") == 0) tk = TYPE_I64;
        else if (strcmp(fname, "u8") == 0) tk = TYPE_U8;
        else if (strcmp(fname, "u16") == 0) tk = TYPE_U16;
        else if (strcmp(fname, "u32") == 0) tk = TYPE_U32;
        else if (strcmp(fname, "u64") == 0) tk = TYPE_U64;
        else if (strcmp(fname, "f32") == 0) tk = TYPE_F32;
        else if (strcmp(fname, "f64") == 0) tk = TYPE_F64;
        HmlTypeKind t = tk_of(tk);
        SBuf tmp;
        sb_init(&tmp);
        Expr *arg = e->as.call.args[0];
        HmlTypeKind s;
        if (t == HML_TK_F32 && fold_f32_literal(arg, &tmp)) {
            s = HML_TK_F32;
            sb_append(out, tmp.data);
        } else {
            s = gen_expr(g, arg, &tmp, 0);
            if (s != HML_TK_OTHER) {
                if (s == HML_TK_BOOL) {
                    // bool -> number: 0 or 1, never out of range
                    sb_printf(out, "((%s)((%s) ? 1 : 0))", ctype(t), tmp.data);
                } else {
                    emit_conv(g, s, t, tmp.data, line, out);
                }
            }
        }
        free(tmp.data);
        if (s == HML_TK_OTHER) return s;
        note_type(g, t, line, "f64() conversion");
        return t;
    }

    for (int i = 0; i < n && i < 2; i++) {
        sb_init(&args[i]);
        types[i] = gen_expr(g, e->as.call.args[i], &args[i], 0);
    }
    for (int i = 0; i < n && i < 2; i++) {
        if (types[i] == HML_TK_OTHER) goto done;
    }

    if (strcmp(fname, "global_id") == 0 || strcmp(fname, "local_id") == 0 ||
        strcmp(fname, "group_id") == 0 || strcmp(fname, "global_size") == 0 ||
        strcmp(fname, "local_size") == 0 || strcmp(fname, "num_groups") == 0) {
        if (!is_int(types[0])) {
            gen_error(g, line, "%s() needs an integer dimension, got %s", fname, tname(types[0]));
            goto done;
        }
        if (strcmp(fname, "global_id") != 0 && strcmp(fname, "global_size") != 0) {
            g->uses_groups = 1;
        }
        const char *cl = strcmp(fname, "global_id") == 0 ? "get_global_id"
                       : strcmp(fname, "local_id") == 0 ? "get_local_id"
                       : strcmp(fname, "group_id") == 0 ? "get_group_id"
                       : strcmp(fname, "global_size") == 0 ? "get_global_size"
                       : strcmp(fname, "local_size") == 0 ? "get_local_size"
                       : "get_num_groups";
        (void)cl;
        if (types[0] == HML_TK_U64) {
            sb_printf(out, "hml_%s(as_long(%s))", fname, args[0].data);
        } else {
            sb_printf(out, "hml_%s((long)(%s))", fname, args[0].data);
        }
        result = HML_TK_U32;
    } else if (strcmp(fname, "work_dim") == 0) {
        sb_append(out, "((uint)get_work_dim())");
        result = HML_TK_U32;
    } else if (strcmp(fname, "min") == 0 || strcmp(fname, "max") == 0) {
        if (!is_num(types[0]) || types[0] != types[1]) {
            gen_error(g, line, "%s() needs two numbers of the same type in kernels, got %s and %s",
                      fname, tname(types[0]), tname(types[1]));
            goto done;
        }
        helper_minmax(g, fname[1] == 'a', types[0], name, sizeof(name));
        sb_printf(out, "%s(%s, %s)", name, args[0].data, args[1].data);
        result = types[0];
    } else if (strcmp(fname, "abs") == 0) {
        if (!is_num(types[0])) {
            gen_error(g, line, "abs() needs a number, got %s", tname(types[0]));
            goto done;
        }
        unsigned site = 0;
        if (types[0] == HML_TK_I32 || types[0] == HML_TK_I64) {
            site = new_site(g, line, "none", NULL, "Integer overflow: %s negation", tname(types[0]));
        }
        helper_abs(g, types[0], name, sizeof(name));
        sb_printf(out, "%s(&hml_c, %uu, %s)", name, site, args[0].data);
        result = types[0];
    } else if (strcmp(fname, "pow") == 0) {
        if (!is_num(types[0]) || !is_num(types[1])) {
            gen_error(g, line, "pow() needs numbers, got %s and %s", tname(types[0]), tname(types[1]));
            goto done;
        }
        if (types[0] == HML_TK_F32 && types[1] != HML_TK_F64) {
            sb_printf(out, "HML_F32_POW(%s, %s)", args[0].data, args[1].data);
            result = HML_TK_F32;
        } else {
            sb_printf(out, "(%spow((double)(%s), (double)(%s)))",
                      types[0] == HML_TK_F32 ? "(float)" : "", args[0].data, args[1].data);
            result = types[0] == HML_TK_F32 ? HML_TK_F32 : HML_TK_F64;
            mark_f64(g, line, "pow() on f64 or integer arguments");
        }
    } else {
        // sqrt floor ceil exp log sin cos: f32 stays f32, anything else is f64
        if (!is_num(types[0])) {
            gen_error(g, line, "%s() needs a number, got %s", fname, tname(types[0]));
            goto done;
        }
        int exact_in_f32 = strcmp(fname, "floor") == 0 || strcmp(fname, "ceil") == 0;
        if (types[0] == HML_TK_F32) {
            if (exact_in_f32) {
                sb_printf(out, "%s(%s)", fname, args[0].data);
            } else {
                sb_printf(out, "HML_F32_1(%s, %s)", fname, args[0].data);
            }
            result = HML_TK_F32;
        } else {
            sb_printf(out, "%s((double)(%s))", fname, args[0].data);
            result = HML_TK_F64;
            mark_f64(g, line, "math on f64 or integer arguments returns f64");
        }
    }

done:
    for (int i = 0; i < n && i < 2; i++) free(args[i].data);
    return result;
}

// Assignment target `name`: a non-const scalar variable
static GVar *assign_target(Gen *g, const char *name, int line) {
    GVar *v = find_var(g, name);
    if (!v) {
        gen_error(g, line, "undefined variable '%s'", name);
        return NULL;
    }
    if (v->is_const) {
        gen_error(g, line, "cannot assign to const '%s'", name);
        return NULL;
    }
    return v;
}

static HmlTypeKind gen_buffer_access(Gen *g, Expr *e, Expr *object, Expr *index, Expr *value, SBuf *out) {
    int line = line_of(g, e);
    if (!object || object->type != EXPR_IDENT) {
        gen_error(g, line, "only gpu_buffer and gpu_local parameters can be indexed");
        return HML_TK_OTHER;
    }
    const char *buf = object->as.ident.name;
    Type *bt = gpu_buffer_param(g->kernel, buf);
    if (!bt) {
        gen_error(g, line, "'%s' is not a gpu_buffer or gpu_local", buf);
        return HML_TK_OTHER;
    }
    int local = gpu_is_local_type(bt);
    HmlTypeKind elem = tk_of(bt->type_args[0]->kind);
    SBuf ci, cv;
    sb_init(&ci);
    sb_init(&cv);
    HmlTypeKind result = HML_TK_OTHER;
    HmlTypeKind ti = gen_expr(g, index, &ci, 0);
    HmlTypeKind tv = HML_TK_OTHER;
    if (ti == HML_TK_OTHER) goto done;
    if (!is_int(ti)) {
        gen_error(g, line, "index into '%s' must be an integer, got %s", buf, tname(ti));
        goto done;
    }
    if (value) {
        if (elem == HML_TK_F32 && fold_f32_literal(value, &cv)) {
            tv = HML_TK_F32;
        } else {
            tv = gen_expr(g, value, &cv, 0);
        }
        if (tv == HML_TK_OTHER) goto done;
        if (tv == HML_TK_BOOL) {
            gen_error(g, line, "cannot store a bool into %s<%s> '%s'",
                      local ? HML_GPU_LOCAL_TYPE : HML_GPU_BUFFER_TYPE, tname(elem), buf);
            goto done;
        }
    }
    const char *idx_cast = ti == HML_TK_U64 ? "as_long" : "(long)";
    const char *detail = is_signed(ti) ? "i64" : "u64";
    unsigned oob = new_site(g, line, detail, buf, "%s[%%v] is out of bounds (length %%n)", buf);
    char name[64];
    if (value) {
        unsigned conv = tv != elem ? conv_sites(g, line, tv, elem) : 0;
        helper_store(g, local, tv, elem, name, sizeof(name));
        sb_printf(out, "%s(&hml_c, %uu, %uu, b_%s, n_%s, %s(%s), %s)",
                  name, oob, conv, buf, buf, idx_cast, ci.data, cv.data);
    } else {
        helper_load(g, local, elem, name, sizeof(name));
        sb_printf(out, "%s(&hml_c, %uu, b_%s, n_%s, %s(%s))", name, oob, buf, buf, idx_cast, ci.data);
    }
    note_type(g, elem, line, "gpu_buffer<f64> access");
    result = elem;
done:
    free(ci.data);
    free(cv.data);
    return result;
}

static HmlTypeKind gen_incdec(Gen *g, Expr *e, Expr *operand, int inc, int prefix, SBuf *out) {
    int line = line_of(g, e);
    if (!operand || operand->type != EXPR_IDENT) {
        gen_error(g, line, "++/-- only apply to scalar variables");
        return HML_TK_OTHER;
    }
    GVar *v = assign_target(g, operand->as.ident.name, line);
    if (!v) return HML_TK_OTHER;
    if (!is_num(v->type)) {
        gen_error(g, line, "++/-- need a numeric variable, '%s' is %s", v->name, tname(v->type));
        return HML_TK_OTHER;
    }
    // ++/-- always wrap in the variable's own type
    char name[64];
    snprintf(name, sizeof(name), "hml_%s%s_%s", prefix ? "pre" : "post", inc ? "inc" : "dec", tname(v->type));
    if (want_helper(g, name)) {
        const char *c = ctype(v->type);
        const char *op = inc ? "+" : "-";
        char expr[96];
        switch (v->type) {
            case HML_TK_I8:  snprintf(expr, sizeof(expr), "as_char((uchar)((uchar)*p %s 1))", op); break;
            case HML_TK_I16: snprintf(expr, sizeof(expr), "as_short((ushort)((ushort)*p %s 1))", op); break;
            case HML_TK_I32: snprintf(expr, sizeof(expr), "as_int((uint)*p %s 1u)", op); break;
            case HML_TK_I64: snprintf(expr, sizeof(expr), "as_long((ulong)*p %s 1UL)", op); break;
            case HML_TK_U8:
            case HML_TK_U16: snprintf(expr, sizeof(expr), "(%s)((uint)*p %s 1u)", c, op); break;
            default:         snprintf(expr, sizeof(expr), "*p %s 1", op); break;
        }
        if (prefix) {
            sb_printf(&g->helpers, "%s %s(%s *p) { *p = %s; return *p; }\n", c, name, c, expr);
        } else {
            sb_printf(&g->helpers, "%s %s(%s *p) { %s old = *p; *p = %s; return old; }\n", c, name, c, c, expr);
        }
    }
    sb_printf(out, "%s(&%s)", name, v->cname);
    note_type(g, v->type, line, "f64 variable");
    return v->type;
}

static HmlTypeKind gen_expr(Gen *g, Expr *e, SBuf *out, int effects_ok) {
    if (!e || g->failed) return HML_TK_OTHER;
    int line = line_of(g, e);
    switch (e->type) {
        case EXPR_NUMBER:
            if (e->as.number.is_float) {
                double d = e->as.number.float_value;
                char text[64];
                for (int prec = 1; prec <= DBL_DECIMAL_DIG; prec++) {
                    snprintf(text, sizeof(text), "%.*g", prec, d);
                    if (strtod(text, NULL) == d) break;
                }
                sb_printf(out, "((double)%s%s)", text, strpbrk(text, ".eE") ? "" : ".0");
                mark_f64(g, line, "f64 literal (write f32(x) for an f32 constant)");
                return HML_TK_F64;
            }
            if (e->as.number.is_u64) {
                sb_printf(out, "((ulong)%" PRIu64 "UL)", e->as.number.uint_value);
                return HML_TK_U64;
            }
            if (e->as.number.int_value >= INT32_MIN && e->as.number.int_value <= INT32_MAX) {
                sb_printf(out, "((int)%" PRId64 ")", e->as.number.int_value);
                return HML_TK_I32;
            }
            if (e->as.number.int_value == INT64_MIN) {
                // -9223372036854775808L is negation of an out-of-range literal in C
                sb_append(out, "((long)(-9223372036854775807L - 1L))");
                return HML_TK_I64;
            }
            sb_printf(out, "((long)%" PRId64 "L)", e->as.number.int_value);
            return HML_TK_I64;
        case EXPR_BOOL:
            sb_append(out, e->as.boolean ? "true" : "false");
            return HML_TK_BOOL;
        case EXPR_IDENT: {
            GVar *v = find_var(g, e->as.ident.name);
            if (!v) {
                gen_error(g, line, "undefined variable '%s'", e->as.ident.name);
                return HML_TK_OTHER;
            }
            sb_append(out, v->cname);
            note_type(g, v->type, line, "f64 variable");
            return v->type;
        }
        case EXPR_BINARY:
            return gen_binary(g, e, out);
        case EXPR_UNARY:
            return gen_unary(g, e, out);
        case EXPR_TERNARY: {
            SBuf cc, ct, cf;
            sb_init(&cc);
            sb_init(&ct);
            sb_init(&cf);
            HmlTypeKind tc = gen_expr(g, e->as.ternary.condition, &cc, 0);
            HmlTypeKind tt = gen_expr(g, e->as.ternary.true_expr, &ct, 0);
            HmlTypeKind tf = gen_expr(g, e->as.ternary.false_expr, &cf, 0);
            HmlTypeKind result = HML_TK_OTHER;
            if (tc != HML_TK_OTHER && tt != HML_TK_OTHER && tf != HML_TK_OTHER) {
                if (tt != tf) {
                    gen_error(g, line, "ternary branches must have the same type in kernels, got %s and %s",
                              tname(tt), tname(tf));
                } else {
                    sb_append(out, "(");
                    emit_truth(tc, cc.data, out);
                    sb_printf(out, " ? %s : %s)", ct.data, cf.data);
                    result = tt;
                }
            }
            free(cc.data);
            free(ct.data);
            free(cf.data);
            return result;
        }
        case EXPR_CALL:
            return gen_call(g, e, out);
        case EXPR_ASSIGN: {
            if (!effects_ok) {
                gen_error(g, line, "assignments must be statements in kernels, not part of an expression");
                return HML_TK_OTHER;
            }
            GVar *v = assign_target(g, e->as.assign.name, line);
            if (!v) return HML_TK_OTHER;
            sb_printf(out, "(%s = ", v->cname);
            HmlTypeKind t = gen_converted(g, e->as.assign.value, v->type, out);
            sb_append(out, ")");
            return t == HML_TK_OTHER ? t : v->type;
        }
        case EXPR_INDEX:
            return gen_buffer_access(g, e, e->as.index.object, e->as.index.index, NULL, out);
        case EXPR_INDEX_ASSIGN:
            if (!effects_ok) {
                gen_error(g, line, "buffer stores must be statements in kernels, not part of an expression");
                return HML_TK_OTHER;
            }
            return gen_buffer_access(g, e, e->as.index_assign.object, e->as.index_assign.index,
                                     e->as.index_assign.value, out);
        case EXPR_PREFIX_INC:
        case EXPR_PREFIX_DEC:
        case EXPR_POSTFIX_INC:
        case EXPR_POSTFIX_DEC: {
            if (!effects_ok) {
                gen_error(g, line, "++/-- must be statements in kernels, not part of an expression");
                return HML_TK_OTHER;
            }
            int inc = e->type == EXPR_PREFIX_INC || e->type == EXPR_POSTFIX_INC;
            int prefix = e->type == EXPR_PREFIX_INC || e->type == EXPR_PREFIX_DEC;
            Expr *operand = e->type == EXPR_PREFIX_INC ? e->as.prefix_inc.operand
                          : e->type == EXPR_PREFIX_DEC ? e->as.prefix_dec.operand
                          : e->type == EXPR_POSTFIX_INC ? e->as.postfix_inc.operand
                          : e->as.postfix_dec.operand;
            return gen_incdec(g, e, operand, inc, prefix, out);
        }
        default:
            gen_error(g, line, "expression is not supported in kernels");
            return HML_TK_OTHER;
    }
}

// ========== STATEMENTS ==========

static void indent(SBuf *out, int level) {
    for (int i = 0; i < level; i++) sb_append(out, "    ");
}

static void emit_check(SBuf *out, int level) {
    indent(out, level);
    sb_append(out, "HML_CHECK();\n");
}

static void gen_stmt(Gen *g, Stmt *s, SBuf *out, int level);

static int is_barrier_call(Expr *e) {
    return e && e->type == EXPR_CALL && e->as.call.func && e->as.call.func->type == EXPR_IDENT &&
           strcmp(e->as.call.func->as.ident.name, "barrier") == 0 && e->as.call.num_args == 0;
}

static int has_barrier(Stmt *s) {
    if (!s) return 0;
    switch (s->type) {
        case STMT_EXPR: return is_barrier_call(s->as.expr);
        case STMT_IF: return has_barrier(s->as.if_stmt.then_branch) || has_barrier(s->as.if_stmt.else_branch);
        case STMT_WHILE: return has_barrier(s->as.while_stmt.body);
        case STMT_LOOP: return has_barrier(s->as.loop_stmt.body);
        case STMT_FOR: return has_barrier(s->as.for_loop.body);
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++) {
                if (has_barrier(s->as.block.statements[i])) return 1;
            }
            return 0;
        case STMT_SWITCH:
            for (int i = 0; i < s->as.switch_stmt.num_cases; i++) {
                if (has_barrier(s->as.switch_stmt.case_bodies[i])) return 1;
            }
            return 0;
        default: return 0;
    }
}

// A failed work-item leaves loops early, except loops containing a barrier:
// the rest of its work-group still has to meet it there
static int loop_guard(Gen *g, Stmt *body) {
    return !(g->uses_barrier && has_barrier(body));
}

static GLoop *push_loop(Gen *g, const char *label) {
    if (g->num_loops >= g->loops_cap) {
        g->loops_cap = g->loops_cap ? g->loops_cap * 2 : 8;
        g->loops = realloc(g->loops, sizeof(GLoop) * g->loops_cap);
    }
    GLoop *l = &g->loops[g->num_loops++];
    l->label = label ? strdup(label) : NULL;
    l->id = g->loop_counter++;
    return l;
}

static void pop_loop(Gen *g) {
    g->num_loops--;
    free(g->loops[g->num_loops].label);
}

static GLoop *find_loop(Gen *g, const char *label) {
    for (int i = g->num_loops - 1; i >= 0; i--) {
        if (g->loops[i].label && strcmp(g->loops[i].label, label) == 0) return &g->loops[i];
    }
    return NULL;
}

// Loop body; a labeled loop gets a continue target at the end of its body
static void gen_loop_body(Gen *g, Stmt *body, GLoop *loop, SBuf *out, int level) {
    sb_append(out, "{\n");
    push_scope(g);
    if (body && body->type == STMT_BLOCK) {
        for (int i = 0; i < body->as.block.count && !g->failed; i++) {
            gen_stmt(g, body->as.block.statements[i], out, level + 1);
        }
    } else {
        gen_stmt(g, body, out, level + 1);
    }
    pop_scope(g);
    if (loop->label) {
        indent(out, level + 1);
        sb_printf(out, "L_cont_%d: ;\n", loop->id);
    }
    indent(out, level);
    sb_append(out, "}\n");
}

static void gen_loop_end(GLoop *loop, SBuf *out, int level) {
    if (loop->label) {
        indent(out, level);
        sb_printf(out, "L_brk_%d: ;\n", loop->id);
    }
}

// The literal integer value of a switch case (an integer literal, possibly negated)
static int case_literal(Expr *e, int64_t *value, int *is_u64) {
    *is_u64 = 0;
    if (e && e->type == EXPR_NUMBER && !e->as.number.is_float) {
        if (e->as.number.is_u64) {
            *is_u64 = 1;
            *value = (int64_t)e->as.number.uint_value;
        } else {
            *value = e->as.number.int_value;
        }
        return 1;
    }
    if (e && e->type == EXPR_UNARY && e->as.unary.op == UNARY_NEGATE &&
        e->as.unary.operand->type == EXPR_NUMBER && !e->as.unary.operand->as.number.is_float &&
        !e->as.unary.operand->as.number.is_u64) {
        *value = -e->as.unary.operand->as.number.int_value;
        return 1;
    }
    return 0;
}

static void gen_switch(Gen *g, Stmt *s, SBuf *out, int level) {
    int line = s->line;
    SBuf cs;
    sb_init(&cs);
    HmlTypeKind ts = gen_expr(g, s->as.switch_stmt.expr, &cs, 0);
    if (ts == HML_TK_OTHER) { free(cs.data); return; }
    if (!is_int(ts)) {
        gen_error(g, line, "switch in kernels needs an integer value, got %s", tname(ts));
        free(cs.data);
        return;
    }
    int id = g->loop_counter++;
    int n = s->as.switch_stmt.num_cases;
    int default_index = -1;
    // Hemlock compares switch cases as doubles; for |literal| < 2^53 that is
    // exact integer equality, computed here without needing device fp64
    indent(out, level);
    sb_printf(out, "{\n");
    indent(out, level + 1);
    sb_printf(out, "%s sw%d = %s;\n", ctype(ts), id, cs.data);
    emit_check(out, level + 1);
    indent(out, level + 1);
    sb_printf(out, "int sel%d = -1;\n", id);
    for (int i = 0; i < n; i++) {
        Expr *cv = s->as.switch_stmt.case_values[i];
        if (!cv) { default_index = i; continue; }
        int64_t v;
        int v_u64;
        if (!case_literal(cv, &v, &v_u64)) {
            gen_error(g, cv->line > 0 ? cv->line : line, "switch cases in kernels must be integer literals");
            free(cs.data);
            return;
        }
        if (!v_u64 && (v >= GPU_EXACT_DOUBLE_INT || v <= -GPU_EXACT_DOUBLE_INT)) {
            gen_error(g, cv->line > 0 ? cv->line : line, "switch case %" PRId64 " is too large for a kernel (limit 2^53)", v);
            free(cs.data);
            return;
        }
        if (v_u64 && (uint64_t)v >= (uint64_t)GPU_EXACT_DOUBLE_INT) {
            gen_error(g, cv->line > 0 ? cv->line : line, "switch case %" PRIu64 " is too large for a kernel (limit 2^53)", (uint64_t)v);
            free(cs.data);
            return;
        }
        indent(out, level + 1);
        if (is_signed(ts)) {
            sb_printf(out, "if (sel%d < 0 && (long)sw%d == %" PRId64 "L) sel%d = %d;\n", id, id, v, id, i);
        } else if (v < 0) {
            // an unsigned value never equals a negative case
        } else {
            sb_printf(out, "if (sel%d < 0 && (ulong)sw%d == %" PRIu64 "UL) sel%d = %d;\n", id, id, (uint64_t)v, id, i);
        }
    }
    if (default_index >= 0) {
        indent(out, level + 1);
        sb_printf(out, "if (sel%d < 0) sel%d = %d;\n", id, id, default_index);
    }
    indent(out, level + 1);
    sb_printf(out, "switch (sel%d) {\n", id);
    for (int i = 0; i < n && !g->failed; i++) {
        indent(out, level + 1);
        sb_printf(out, "case %d:\n", i);
        gen_stmt(g, s->as.switch_stmt.case_bodies[i], out, level + 2);
    }
    indent(out, level + 1);
    sb_append(out, "default: ;\n");
    indent(out, level + 1);
    sb_append(out, "}\n");
    indent(out, level);
    sb_append(out, "}\n");
    free(cs.data);
}

static void gen_local(Gen *g, Stmt *s, const char *name, Type *type, Expr *value, int is_const, SBuf *out, int level) {
    int line = s->line;
    if (!value) {
        gen_error(g, line, "local '%s' needs an initializer", name);
        return;
    }
    for (int i = g->num_vars - 1; i >= 0 && g->vars[i].depth == g->depth; i--) {
        if (strcmp(g->vars[i].name, name) == 0) {
            gen_error(g, line, "variable '%s' already defined in this scope", name);
            return;
        }
    }
    if (!type || tk_of(type->kind) == HML_TK_OTHER) {
        gen_error(g, line, "local '%s' needs a scalar type annotation", name);
        return;
    }
    HmlTypeKind t = tk_of(type->kind);
    SBuf cv;
    sb_init(&cv);
    // the initializer sees the enclosing scope, so declare after generating it
    HmlTypeKind tv = gen_converted(g, value, t, &cv);
    if (tv != HML_TK_OTHER) {
        GVar *v = declare_var(g, name, t, is_const, NULL);
        indent(out, level);
        sb_printf(out, "%s %s = %s;\n", ctype(t), v->cname, cv.data);
        emit_check(out, level);
    }
    free(cv.data);
}

static void gen_stmt(Gen *g, Stmt *s, SBuf *out, int level) {
    if (!s || g->failed) return;
    if (s->line > 0) {
        g->line = s->line;
        g->column = s->column;
    }
    switch (s->type) {
        case STMT_LET:
            gen_local(g, s, s->as.let.name, s->as.let.type_annotation, s->as.let.value, 0, out, level);
            return;
        case STMT_CONST:
            gen_local(g, s, s->as.const_stmt.name, s->as.const_stmt.type_annotation, s->as.const_stmt.value, 1,
                      out, level);
            return;
        case STMT_EXPR: {
            if (is_barrier_call(s->as.expr)) {
                indent(out, level);
                sb_append(out, "HML_BARRIER();\n");
                return;
            }
            SBuf ce;
            sb_init(&ce);
            if (gen_expr(g, s->as.expr, &ce, 1) != HML_TK_OTHER) {
                indent(out, level);
                sb_printf(out, "%s;\n", ce.data);
                emit_check(out, level);
            }
            free(ce.data);
            return;
        }
        case STMT_IF: {
            SBuf cc;
            sb_init(&cc);
            HmlTypeKind tc = gen_expr(g, s->as.if_stmt.condition, &cc, 0);
            if (tc != HML_TK_OTHER) {
                indent(out, level);
                sb_append(out, "if (");
                emit_truth(tc, cc.data, out);
                sb_append(out, ") {\n");
                push_scope(g);
                gen_stmt(g, s->as.if_stmt.then_branch, out, level + 1);
                pop_scope(g);
                indent(out, level);
                sb_append(out, "}");
                if (s->as.if_stmt.else_branch) {
                    sb_append(out, " else {\n");
                    push_scope(g);
                    gen_stmt(g, s->as.if_stmt.else_branch, out, level + 1);
                    pop_scope(g);
                    indent(out, level);
                    sb_append(out, "}");
                }
                sb_append(out, "\n");
                emit_check(out, level);
            }
            free(cc.data);
            return;
        }
        case STMT_WHILE: {
            SBuf cc;
            sb_init(&cc);
            HmlTypeKind tc = gen_expr(g, s->as.while_stmt.condition, &cc, 0);
            if (tc != HML_TK_OTHER) {
                GLoop *loop = push_loop(g, s->as.while_stmt.label);
                indent(out, level);
                sb_append(out, loop_guard(g, s->as.while_stmt.body) ? "while (!hml_c.failed && " : "while (");
                emit_truth(tc, cc.data, out);
                sb_append(out, ") ");
                gen_loop_body(g, s->as.while_stmt.body, loop, out, level);
                gen_loop_end(loop, out, level);
                pop_loop(g);
                emit_check(out, level);
            }
            free(cc.data);
            return;
        }
        case STMT_LOOP: {
            GLoop *loop = push_loop(g, s->as.loop_stmt.label);
            indent(out, level);
            sb_append(out, loop_guard(g, s->as.loop_stmt.body) ? "while (!hml_c.failed) " : "while (true) ");
            gen_loop_body(g, s->as.loop_stmt.body, loop, out, level);
            gen_loop_end(loop, out, level);
            pop_loop(g);
            emit_check(out, level);
            return;
        }
        case STMT_FOR: {
            indent(out, level);
            sb_append(out, "{\n");
            push_scope(g);
            gen_stmt(g, s->as.for_loop.initializer, out, level + 1);
            SBuf cc, ci;
            sb_init(&cc);
            sb_init(&ci);
            HmlTypeKind tc = HML_TK_BOOL;
            if (s->as.for_loop.condition) {
                tc = gen_expr(g, s->as.for_loop.condition, &cc, 0);
            } else {
                sb_append(&cc, "true");
            }
            HmlTypeKind ti = HML_TK_BOOL;
            if (s->as.for_loop.increment) {
                ti = gen_expr(g, s->as.for_loop.increment, &ci, 1);
            }
            if (tc != HML_TK_OTHER && ti != HML_TK_OTHER && !g->failed) {
                GLoop *loop = push_loop(g, s->as.for_loop.label);
                indent(out, level + 1);
                sb_append(out, loop_guard(g, s->as.for_loop.body) ? "for (; !hml_c.failed && " : "for (; ");
                emit_truth(tc, cc.data, out);
                sb_printf(out, "; %s) ", ci.data);
                gen_loop_body(g, s->as.for_loop.body, loop, out, level + 1);
                gen_loop_end(loop, out, level + 1);
                pop_loop(g);
            }
            free(cc.data);
            free(ci.data);
            pop_scope(g);
            indent(out, level);
            sb_append(out, "}\n");
            emit_check(out, level);
            return;
        }
        case STMT_BREAK:
        case STMT_CONTINUE: {
            int is_break = s->type == STMT_BREAK;
            const char *label = is_break ? s->as.break_stmt.label : s->as.continue_stmt.label;
            indent(out, level);
            if (!label) {
                sb_append(out, is_break ? "break;\n" : "continue;\n");
                return;
            }
            GLoop *loop = find_loop(g, label);
            if (!loop) {
                gen_error(g, s->line, "unknown loop label '%s'", label);
                return;
            }
            sb_printf(out, "goto L_%s_%d;\n", is_break ? "brk" : "cont", loop->id);
            return;
        }
        case STMT_BLOCK:
            indent(out, level);
            sb_append(out, "{\n");
            push_scope(g);
            for (int i = 0; i < s->as.block.count && !g->failed; i++) {
                gen_stmt(g, s->as.block.statements[i], out, level + 1);
            }
            pop_scope(g);
            indent(out, level);
            sb_append(out, "}\n");
            return;
        case STMT_RETURN:
            indent(out, level);
            sb_append(out, "HML_RETURN();\n");
            return;
        case STMT_SWITCH:
            gen_switch(g, s, out, level);
            emit_check(out, level);
            return;
        default:
            gen_error(g, s->line, "statement is not supported in kernels");
            return;
    }
}

// ========== PROGRAM ==========

static const char *PRELUDE =
    "// Generated by Hemlock from a gpu fn kernel. Do not edit.\n"
    "#pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable\n"
    "#ifdef cl_khr_fp64\n"
    "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
    "// f32 division, sqrt and transcendentals via f64 round exactly like the emulator\n"
    "#define HML_DIV_F32(a, b) ((float)((double)(a) / (double)(b)))\n"
    "#define HML_F32_1(fn, x) ((float)fn((double)(x)))\n"
    "#define HML_F32_POW(a, b) ((float)pow((double)(a), (double)(b)))\n"
    "#else\n"
    "#define HML_DIV_F32(a, b) ((a) / (b))\n"
    "#define HML_F32_1(fn, x) fn(x)\n"
    "#define HML_F32_POW(a, b) pow((a), (float)(b))\n"
    "#endif\n"
    "#pragma OPENCL FP_CONTRACT OFF\n"
    "\n"
    "// The error key orders work-items as the emulator runs them: by global\n"
    "// linear id, or for barrier kernels by work-group, then local id\n"
    "typedef struct {\n"
    "    int failed, frozen, barrier_keys;\n"
    "    uint site;\n"
    "    ulong detail, lin, group_lin, local_lin;\n"
    "} hml_ctx;\n"
    "\n"
    "// Record a failure; within one statement the lowest site (earliest in\n"
    "// Hemlock evaluation order) wins. Once a statement has failed the error is\n"
    "// frozen (a failed work-item may keep running until its next barrier).\n"
    "void hml_fail(HML_CTX *c, uint site, ulong detail) {\n"
    "    if (c->frozen) return;\n"
    "    if (!c->failed || site < c->site) { c->site = site; c->detail = detail; }\n"
    "    c->failed = 1;\n"
    "}\n"
    "\n"
    "// Publish this work-item's error if it has the lowest key so far\n"
    "void hml_report(HML_CTX *c, __global ulong *err) {\n"
    "    ulong order = c->barrier_keys ? (c->group_lin << HML_KEY_LOCAL_BITS) | c->local_lin : c->lin;\n"
    "    ulong key = (order << HML_SITE_BITS) | c->site;\n"
    "    volatile __global ulong *k = (volatile __global ulong *)&err[0];\n"
    "    ulong old = *k;\n"
    "    while (key < old) {\n"
    "        ulong prev = atom_cmpxchg(k, old, key);\n"
    "        if (prev == old) {\n"
    "            ulong slot = atom_inc((volatile __global ulong *)&err[1]);\n"
    "            if (slot < HML_ERR_SLOTS) { err[2 + 2 * slot] = key; err[3 + 2 * slot] = c->detail; }\n"
    "            break;\n"
    "        }\n"
    "        old = prev;\n"
    "    }\n"
    "}\n"
    "\n"
    "// Work-item intrinsics. A dimension outside [0, work_dim) reads as id 0\n"
    "// and size 1, as in the emulator; the fallback is applied here rather\n"
    "// than trusted to the driver (PoCL returns size 0 for such dimensions).\n"
    "int hml_dim_ok(long d) { return d >= 0 && d < (long)get_work_dim(); }\n"
    "uint hml_global_id(long d) { return hml_dim_ok(d) ? (uint)get_global_id((uint)d) : 0u; }\n"
    "uint hml_local_id(long d) { return hml_dim_ok(d) ? (uint)get_local_id((uint)d) : 0u; }\n"
    "uint hml_group_id(long d) { return hml_dim_ok(d) ? (uint)get_group_id((uint)d) : 0u; }\n"
    "uint hml_global_size(long d) { return hml_dim_ok(d) ? (uint)get_global_size((uint)d) : 1u; }\n"
    "uint hml_local_size(long d) { return hml_dim_ok(d) ? (uint)get_local_size((uint)d) : 1u; }\n"
    "uint hml_num_groups(long d) { return hml_dim_ok(d) ? (uint)get_num_groups((uint)d) : 1u; }\n"
    "\n"
    "#define HML_RETURN() { if (hml_c.failed) hml_report(&hml_c, hml_err); return; }\n"
    "\n";

// Statement-end check without barriers: a failed work-item stops at once
static const char *CHECK_PLAIN =
    "#define HML_CHECK() if (hml_c.failed) { hml_report(&hml_c, hml_err); return; }\n"
    "\n";

// With barriers a failed work-item must not return early (its work-group
// would wait for it at the next barrier). It keeps running with stores
// suppressed and its error frozen; at each barrier the group checks a shared
// flag and, if any member failed, every member returns together. The second
// barrier keeps a fast work-item from setting the flag for the next round
// before a slow one has read it for this round.
// Every exit goes through one label at the end of the kernel, where the
// error is reported: a report-and-return right after a barrier is not
// reliably compiled by every OpenCL implementation (PoCL drops it).
static const char *CHECK_BARRIER =
    "#define HML_CHECK() if (hml_c.failed) { hml_c.frozen = 1; }\n"
    "#undef HML_RETURN\n"
    "#define HML_RETURN() goto hml_exit\n"
    "#define HML_BARRIER() { \\\n"
    "    if (hml_c.failed) { hml_c.frozen = 1; hml_group_failed = 1; } \\\n"
    "    barrier(CLK_LOCAL_MEM_FENCE | CLK_GLOBAL_MEM_FENCE); \\\n"
    "    int hml_gf = hml_group_failed; \\\n"
    "    barrier(CLK_LOCAL_MEM_FENCE); \\\n"
    "    if (hml_gf) goto hml_exit; \\\n"
    "}\n"
    "\n";

int gpu_codegen(KernelCheck *kc, const char *name, Expr *kernel, GpuDeviceCode *out) {
    memset(out, 0, sizeof(*out));
    Gen g;
    memset(&g, 0, sizeof(g));
    g.kc = kc;
    g.kernel = kernel;
    g.name = name;
    g.line = kernel->line;
    sb_init(&g.helpers);

    SBuf sig, body;
    sb_init(&sig);
    sb_init(&body);

    char entry[160];
    snprintf(entry, sizeof(entry), "hml_k_%s", name);
    sb_printf(&sig, "__kernel void %s(", entry);

    // Parameters: scalars by value (bool travels as uchar), each buffer as
    // a pointer plus its element count
    int nk = kernel->as.function.num_params;
    for (int i = 0; i < nk && !g.failed; i++) {
        const char *pname = kernel->as.function.param_names[i];
        Type *pt = kernel->as.function.param_types ? kernel->as.function.param_types[i] : NULL;
        if (!gpu_is_scalar_type(pt) && !gpu_is_buffer_type(pt)) {
            gen_error(&g, kernel->line, "parameter '%s' has no kernel type", pname);
            break;
        }
        int is_const = kernel->as.function.param_is_const ? kernel->as.function.param_is_const[i] : 0;
        if (gpu_is_buffer_type(pt)) {
            HmlTypeKind elem = tk_of(pt->type_args[0]->kind);
            sb_printf(&sig, "%s %s *b_%s, ulong n_%s, ", gpu_is_local_type(pt) ? "__local" : "__global",
                      ctype(elem), pname, pname);
            note_type(&g, elem, kernel->line, "gpu_buffer<f64> parameter");
        } else {
            HmlTypeKind t = tk_of(pt->kind);
            char cname[128];
            snprintf(cname, sizeof(cname), "p_%s", pname);
            if (t == HML_TK_BOOL) {
                sb_printf(&sig, "uchar a_%s, ", pname);
                sb_printf(&body, "    bool %s = a_%s != 0;\n", cname, pname);
            } else {
                sb_printf(&sig, "%s %s, ", ctype(t), cname);
            }
            declare_var(&g, pname, t, is_const, cname);
            note_type(&g, t, kernel->line, "f64 parameter");
        }
    }
    sb_append(&sig, "__global ulong *hml_err)");

    g.uses_barrier = has_barrier(kernel->as.function.body);
    if (g.uses_barrier) g.uses_groups = 1;
    sb_append(&body,
              "    HML_CTX hml_c;\n"
              "    hml_c.failed = 0;\n"
              "    hml_c.frozen = 0;\n"
              "    hml_c.site = 0;\n"
              "    hml_c.detail = 0;\n"
              "    hml_c.lin = (ulong)hml_global_id(0) + (ulong)hml_global_size(0) *\n"
              "                ((ulong)hml_global_id(1) + (ulong)hml_global_size(1) * (ulong)hml_global_id(2));\n"
              "    hml_c.group_lin = (ulong)hml_group_id(0) + (ulong)hml_num_groups(0) *\n"
              "                  ((ulong)hml_group_id(1) + (ulong)hml_num_groups(1) * (ulong)hml_group_id(2));\n"
              "    hml_c.local_lin = (ulong)hml_local_id(0) + (ulong)hml_local_size(0) *\n"
              "                  ((ulong)hml_local_id(1) + (ulong)hml_local_size(1) * (ulong)hml_local_id(2));\n");
    if (g.uses_barrier) {
        sb_append(&body,
                  "    hml_c.barrier_keys = 1;\n"
                  "    __local int hml_group_failed;\n"
                  "    if (hml_c.local_lin == 0) hml_group_failed = 0;\n"
                  "    barrier(CLK_LOCAL_MEM_FENCE);\n");
    } else {
        sb_append(&body, "    hml_c.barrier_keys = 0;\n");
    }

    Stmt *kbody = kernel->as.function.body;
    push_scope(&g);
    if (kbody && kbody->type == STMT_BLOCK) {
        for (int i = 0; i < kbody->as.block.count && !g.failed; i++) {
            gen_stmt(&g, kbody->as.block.statements[i], &body, 1);
        }
    } else {
        gen_stmt(&g, kbody, &body, 1);
    }
    pop_scope(&g);
    pop_scope(&g);  // parameters
    if (g.uses_barrier) {
        sb_append(&body, "hml_exit:\n    if (hml_c.failed) hml_report(&hml_c, hml_err);\n");
    } else {
        sb_append(&body, "    HML_RETURN();\n");
    }

    int ok = !g.failed;
    if (ok) {
        SBuf prog;
        sb_init(&prog);
        sb_printf(&prog, "#define HML_SITE_BITS %d\n#define HML_ERR_SLOTS %d\n", HML_GPU_SITE_BITS, HML_GPU_ERR_SLOTS);
        sb_printf(&prog, "#define HML_KEY_LOCAL_BITS %d\n", HML_GPU_KEY_LOCAL_BITS);
        // In barrier kernels the work-item context is volatile: PoCL loses
        // private state kept in registers across a barrier under a
        // non-constant condition (work-items saw each other's values)
        sb_printf(&prog, "#define HML_CTX %shml_ctx\n", g.uses_barrier ? "volatile " : "");
        sb_append(&prog, PRELUDE);
        sb_append(&prog, g.uses_barrier ? CHECK_BARRIER : CHECK_PLAIN);
        sb_append(&prog, g.helpers.data);
        sb_printf(&prog, "\n%s {\n%s}\n", sig.data, body.data);
        out->source = sb_take(&prog);
        out->entry = strdup(entry);
        out->sites = g.sites;
        out->num_sites = g.num_sites;
        out->uses_f64 = g.uses_f64;
        out->f64_reason = g.f64_reason;
        out->uses_groups = g.uses_groups;
        out->uses_barrier = g.uses_barrier;
        g.sites = NULL;
        g.num_sites = 0;
        g.f64_reason = NULL;
    }

    for (int i = 0; i < g.num_sites; i++) {
        free(g.sites[i].msg);
        free(g.sites[i].buf);
    }
    free(g.sites);
    free(g.f64_reason);
    for (int i = 0; i < g.num_helpers; i++) free(g.helper_names[i]);
    free(g.helper_names);
    free(g.helpers.data);
    while (g.num_vars > 0) {
        g.num_vars--;
        free(g.vars[g.num_vars].name);
        free(g.vars[g.num_vars].cname);
    }
    free(g.vars);
    while (g.num_loops > 0) pop_loop(&g);
    free(g.loops);
    free(sig.data);
    free(body.data);
    return ok;
}

void gpu_device_code_free(GpuDeviceCode *dc) {
    free(dc->source);
    free(dc->entry);
    for (int i = 0; i < dc->num_sites; i++) {
        free(dc->sites[i].msg);
        free(dc->sites[i].buf);
    }
    free(dc->sites);
    free(dc->f64_reason);
    memset(dc, 0, sizeof(*dc));
}
