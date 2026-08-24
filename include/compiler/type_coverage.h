/*
 * Hemlock Compiler - Specialization Coverage Report
 *
 * Reports which numeric variable sites the unboxing optimization
 * specializes to native C types and which stay boxed as HmlValue, with a
 * stable reason code (HC21xx) and a hint for every boxed site.
 *
 * The analysis runs the same functions codegen uses to decide unboxing
 * (type_check_analyze_block_for_unboxing and friends), so the
 * unboxed/boxed decision matches what `hemlockc` generates at the default
 * optimization level. Reason classification for boxed sites replays the
 * analyzer's checks in the same order.
 *
 * Linked into both backends: `hemlockc --coverage` and
 * `hemlock check --coverage` serve the same report.
 */

#ifndef HEMLOCK_TYPE_COVERAGE_H
#define HEMLOCK_TYPE_COVERAGE_H

#include "type_check.h"

// What kind of variable site was analyzed
typedef enum {
    COVERAGE_SITE_TYPED_VAR,     // let with a primitive type annotation
    COVERAGE_SITE_INFERRED_VAR,  // untyped let with a numeric initializer
    COVERAGE_SITE_LOOP_COUNTER,  // for-loop counter
    COVERAGE_SITE_ACCUMULATOR,   // while-loop accumulator
    COVERAGE_SITE_PARAMETER,     // function parameter
    COVERAGE_SITE_TOP_LEVEL,     // top-level (module scope) variable
} CoverageSiteKind;

// Why a site stayed boxed. Codes are stable API: HC2100 + enum value.
typedef enum {
    COVERAGE_OK = 0,             // unboxed (no reason code)
    COVERAGE_OTHER,              // HC2101: not specialized by the current analysis
    COVERAGE_UNTYPED,            // HC2102: no static numeric type could be inferred
    COVERAGE_TOP_LEVEL,          // HC2103: top-level variables stay boxed
    COVERAGE_PARAMETER,          // HC2104: parameters are always boxed
    COVERAGE_ESCAPES,            // HC2105: escapes (captured/stored/returned)
    COVERAGE_DYNAMIC_INIT,       // HC2106: initializer is not statically analyzable
    COVERAGE_MIXED_ASSIGN,       // HC2107: later assignment of a non-numeric value
    COVERAGE_COMPLEX_LOOP,       // HC2108: loop shape too complex to specialize
    COVERAGE_RUNE,               // HC2109: runes keep their type tag
    COVERAGE_UNSUPPORTED_TYPE,   // HC2110: inferred type has no unboxed form
} CoverageReason;

typedef struct CoverageSite {
    char *name;                  // variable name (owned)
    int line;                    // 1-based
    int column;                  // 1-based, 0 = unknown
    CoverageSiteKind kind;
    int unboxed;                 // 1 = native C type, 0 = boxed HmlValue
    CheckedTypeKind native_type; // valid when unboxed
    CoverageReason reason;       // COVERAGE_OK when unboxed
    char *message;               // owned; detail for boxed sites (may be NULL)
    struct CoverageSite *next;
} CoverageSite;

typedef struct CoverageReport {
    CoverageSite *sites;         // ordered by source line
    int num_sites;
    int num_unboxed;
} CoverageReport;

// Analyze a checked program. `ctx` must have been through
// type_check_program already (the analysis consults its type environment
// and reuses its unboxable-variable marks; the marks are clobbered).
CoverageReport *type_coverage_analyze(TypeCheckContext *ctx,
                                      Stmt **stmts, int stmt_count);

void coverage_report_free(CoverageReport *report);

// Renderers return a malloc'd string (caller frees).
// Text: human-readable summary + one line per site.
char *coverage_report_render_text(const CoverageReport *report,
                                  const char *filename);
// JSON: one object {"version", "file", "summary", "sites"}. Every line is
// prefixed with `indent` spaces so it can nest inside a larger document.
char *coverage_report_render_json(const CoverageReport *report,
                                  const char *filename, int indent);

// Stable string forms used by both renderers.
const char *coverage_reason_code(CoverageReason reason);   // "HC2104"
const char *coverage_reason_text(CoverageReason reason);   // default message
const char *coverage_reason_hint(CoverageReason reason);   // may be NULL
const char *coverage_site_kind_name(CoverageSiteKind kind); // "loop_counter"

#endif // HEMLOCK_TYPE_COVERAGE_H
