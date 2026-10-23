/*
 * Stubs for the runtime's error entry points.
 *
 * The real functions longjmp to the nearest Hemlock `catch` (or exit), so in
 * a harness they are modelled as: "raising here must be allowed by the spec"
 * followed by cutting the path (the operation never returns a value).
 * Harnesses set hml_cbmc_error_allowed before calling into the runtime.
 */
#include <stdarg.h>

int hml_cbmc_error_allowed;

__attribute__((noreturn)) static void hml_cbmc_raise(void) {
    __CPROVER_assert(hml_cbmc_error_allowed, "runtime error raised only when the spec allows it");
    __CPROVER_assume(0);
    __builtin_unreachable();
}

__attribute__((noreturn)) void hml_runtime_error(const char *format, ...) { (void)format; hml_cbmc_raise(); }
__attribute__((noreturn)) void hml_runtime_error_loc(const char *format, ...) { (void)format; hml_cbmc_raise(); }
__attribute__((noreturn)) void hml_runtime_error_line(const char *format, ...) { (void)format; hml_cbmc_raise(); }
