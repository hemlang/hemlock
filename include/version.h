/*
 * Hemlock Version Header
 *
 * Centralized version information for all Hemlock components.
 * All components (interpreter, compiler, LSP) share this version.
 */

#ifndef HEMLOCK_VERSION_H
#define HEMLOCK_VERSION_H

#define HEMLOCK_VERSION_MAJOR 1
#define HEMLOCK_VERSION_MINOR 6
#define HEMLOCK_VERSION_PATCH 5

#define HEMLOCK_VERSION "1.6.5"
#define HEMLOCK_VERSION_STRING "Hemlock v" HEMLOCK_VERSION

/*
 * Version history:
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
