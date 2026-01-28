/*
 * Hemlock Version Header
 *
 * Centralized version information for all Hemlock components.
 * All components (interpreter, compiler, LSP) share this version.
 */

#ifndef HEMLOCK_VERSION_H
#define HEMLOCK_VERSION_H

#define HEMLOCK_VERSION_MAJOR 1
#define HEMLOCK_VERSION_MINOR 8
#define HEMLOCK_VERSION_PATCH 8

#define HEMLOCK_VERSION "1.8.8"
#define HEMLOCK_VERSION_STRING "Hemlock v" HEMLOCK_VERSION

/*
 * Version history:
 *   1.8.8 - Version bump
 *   1.8.7 - Fix multi-argument print/eprint in compiler codegen
 *   1.8.6 - Fix segfault in hml_string_append_inplace for SSO strings
 *   1.8.5 - 5 new array methods (every, some, indexOf, sort, fill), major performance optimizations, memory leak fixes
 *   1.8.4 - Graceful handling for reserved keywords (def, func, var, class), fix flaky CI tests
 *   1.8.3 - Code polish: consolidate magic numbers, standardize error messages
 *   1.8.2 - Memory leak prevention: exception-safe eval, task/channel cleanup, optimizer fixes
 *   1.8.1 - Fix use-after-free bug in function return value handling
 *   1.8.0 - Pattern matching, arena allocator, memory leak fixes
 *   1.7.5 - Fix formatter else-if indentation bug
 *   1.7.4 - Formatter improvements: function parameter, binary expr, import, and method chain line breaking
 *   1.7.3 - Fix formatter comment and blank line preservation
 *   1.7.2 - Maintenance release
 *   1.7.1 - Single-line if/while/for statements (braceless syntax)
 *   1.7.0 - Type aliases, function types, const params, method signatures, loop labels, named args, null coalescing
 *   1.6.7 - Octal literals, block comments, hex/unicode escapes, numeric separators
 *   1.6.6 - Float literals without leading zero, fix strength reduction bug
 *   1.6.5 - Fix for-in loop syntax without 'let' keyword
 *   1.6.4 - Hotfix release
 *   1.6.3 - Fix runtime method dispatch for file, channel, socket types
 *   1.6.2 - Patch release
 *   1.6.1 - Patch release
 *   1.6.0 - Compile-time type checking, LSP integration, bitwise operators
 *   1.5.0 - Full type system, async/await, atomics, 39 stdlib modules
 *   1.1.0 - Unified versioning across all components
 *   1.0.x - Initial release series (interpreter only)
 *   0.1.x - Pre-release compiler development
 */

#endif /* HEMLOCK_VERSION_H */
