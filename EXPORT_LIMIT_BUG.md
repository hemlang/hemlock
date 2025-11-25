# Export Limit Bug in Hemlock Module System

## Summary
Hemlock has a bug where modules with more than 8 exports fail to load properly. The exported constants/functions become `null` when imported, causing runtime errors.

## Reproduction
```hemlock
// stdlib/test_module.hml
let ESC: rune = 27;

export let E1 = ESC + "[30m";
export let E2 = ESC + "[31m";
export let E3 = ESC + "[32m";
export let E4 = ESC + "[33m";
export let E5 = ESC + "[34m";
export let E6 = ESC + "[35m";
export let E7 = ESC + "[36m";
export let E8 = ESC + "[37m";
export let E9 = ESC + "[90m";  // 9th export - causes failure
export let E10 = ESC + "[91m";  // 10th export

```

```bash
# This works (8 exports)
./hemlock -c 'import { E1, E8 } from "@stdlib/test_module_8"; print(typeof(E1));'  # Outputs: string

# This fails (10 exports)
./hemlock -c 'import { E1, E10 } from "@stdlib/test_module_10"; print(typeof(E1));'  # Outputs: null
```

## Investigation

### Initial Hypothesis - Fixed Array Size
- Found that `module->export_names` is allocated with fixed size of 32 pointers (src/module.c:251)
- No bounds checking when adding exports
- Added `export_capacity` field to Module struct
- Implemented `ensure_export_capacity()` helper with dynamic resizing
- **Result: Did not fix the issue!** Limit remains at 8-9 exports.

### Current Status
- The bug persists even with dynamic array resizing
- Debug output shows that export processing code is NOT being executed
- Module caching appears to work correctly
- The issue manifests as exported values being `null` at runtime, not as a hard failure

### Files Modified (Attempted Fix)
- `include/module.h`: Added `export_capacity` field to Module struct
- `src/module.c`:
  - Added `ensure_export_capacity()` helper function
  - Added capacity tracking and dynamic resizing
  - Calls added before each `module->export_names[...]` assignment

### Debug Findings
- Added debug output to track export processing
- Debug output NEVER triggers, even for fresh unique module names
- This suggests exports are handled through a different code path than expected
- `load_module()` may not be the active code path during imports

## Workaround
For now, limit stdlib modules to 8 exports maximum, or use namespace imports:

```hemlock
// Instead of named imports:
import { RED, GREEN, BLUE, ... } from "@stdlib/terminal";  // Fails with >8

// Use namespace import:
import * as term from "@stdlib/terminal";  // Works!
print(term.RED + "text" + term.RESET);
```

## Next Steps for Investigation
1. Find the actual code path for module import execution (not `load_module`?)
2. Check if parser creates different AST structure for `export let`
3. Investigate if there's a limit in Environment or Value storage
4. Check for array/hash table size limits in interpreter
5. Add debug output to ALL possible module/export code paths

## Related Code Locations
- `src/module.c`: Module loading and export handling
- `include/module.h`: Module struct definition
- `src/parser/statements.c`: Export statement parsing
- `src/interpreter/environment.c`: Variable storage

## Impact
- Terminal module requires 85 exports (colors, styles, functions)
- Currently broken due to this bug
- Many stdlib modules will hit this limit
- Critical blocker for stdlib expansion

## Priority
**HIGH** - Blocks stdlib development and makes Hemlock unusable for modules with comprehensive APIs.
