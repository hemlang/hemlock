# Bundled POSIX regex engine (Windows builds only)

TRE-based `regcomp`/`regexec`/`regerror`/`regfree`, vendored from
musl libc 1.2.5 (`src/regex/` + `include/regex.h`). MinGW has no POSIX
`<regex.h>`, so on Windows this directory is added to the include path
and compiled in via `src/shared/regex_compat.c`; the regex builtins then
use the exact same POSIX code path as on Linux/macOS. POSIX builds never
see these files.

- TRE is Copyright (c) 2001-2009 Ville Laurikari, 2-clause BSD — see the
  unmodified license headers in each source file.
- musl is MIT licensed (https://musl.libc.org).

Local patches, each marked with a `Patched for Hemlock` comment:

- `regex.h`: musl's `<features.h>` / `<bits/alltypes.h>` type plumbing
  replaced with `<stddef.h>` + `typedef ptrdiff_t regoff_t;`.
- `tre.h`: musl's internal `hidden` macro (ELF visibility) defined away.
- `regerror.c`: musl's `LCTRANS_CUR` locale translation removed
  (messages stay English).

Matching is byte-oriented in the C locale, the same behavior the
builtins have on POSIX systems.
