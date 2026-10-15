// Bundled POSIX regex engine for Windows builds (musl/TRE, see
// src/shared/regex_win32/README.md). MinGW has no <regex.h>; the
// Windows CFLAGS put src/shared/regex_win32 on the include path so the
// regex builtins compile their normal POSIX code path against this
// engine. Unity-built here so the platform-independent Makefile
// wildcards pick it up; the TRE sources share no static symbol names.
// On POSIX builds this file compiles to nothing.
#ifdef _WIN32

// Vendored code compiles with TRE's own style (intentional switch
// fallthroughs, int/size_t comparisons on values known small); keep the
// musl diff minimal and quiet the warnings for this TU only.
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

#include "regex_win32/tre-mem.c"
#include "regex_win32/regcomp.c"
#include "regex_win32/regexec.c"
#include "regex_win32/regerror.c"

#endif // _WIN32
