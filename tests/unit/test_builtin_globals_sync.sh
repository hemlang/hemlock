#!/bin/bash

# include/builtin_globals.h is the single source of truth for the names the
# runtime installs in the global environment: the interpreter expands its
# lists to register them, and the frontend name resolver expands them to
# decide whether a bare identifier is a builtin.
#
# A name inside one of the header's macro lists cannot drift: both consumers
# expand the same macro, so adding X("foo", builtin_foo) registers it AND
# makes the resolver accept it. What can drift is the hand-written code around
# those lists, and that is what this test guards:
#
#   * a global registered by hand in register_builtins() but not listed in the
#     header - the resolver would call it undefined and reject working code;
#   * a macro the header defines that registration.c stops expanding - the
#     resolver would accept names the runtime no longer provides.
#
# This matters because the project already had the other kind of list:
# include/builtins_registry.h was a second, hand-maintained copy that had
# drifted badly - it called `sin`, `open` and `getenv` builtins (they are
# stdlib exports) and omitted ~110 names that are, `write` and every
# ptr_read_* among them. It has been deleted; this test keeps its replacement
# honest.

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

if [ -f "../../Makefile" ]; then
    PROJECT_ROOT="$(cd ../.. && pwd)"
elif [ -f "Makefile" ]; then
    PROJECT_ROOT="$(pwd)"
else
    echo -e "${RED}✗ Cannot find Makefile${NC}"
    exit 1
fi

HEADER="$PROJECT_ROOT/include/builtin_globals.h"
REGISTRATION="$PROJECT_ROOT/src/backends/interpreter/builtins/registration.c"

for f in "$HEADER" "$REGISTRATION"; do
    if [ ! -f "$f" ]; then
        echo -e "${RED}✗ missing $f${NC}"
        exit 1
    fi
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

FAILED=0

check_list() {
    local label="$1" expected="$2" actual="$3"
    if diff -u "$expected" "$actual" > "$WORK/diff"; then
        echo -e "${GREEN}✓${NC} $label"
        return 0
    fi
    echo -e "${RED}✗${NC} $label"
    echo "  -- only in registration.c / + only in builtin_globals.h"
    sed -n '4,$p' "$WORK/diff" | sed 's/^/    /'
    FAILED=1
    return 1
}

echo "======================================"
echo " Builtin Globals Sync"
echo "======================================"
echo ""

# Every macro the header defines must be expanded by registration.c, or its
# names reach the resolver without ever being registered.
for macro in HML_BUILTIN_FNS HML_BUILTIN_TYPE_CONSTS HML_BUILTIN_I32_CONSTS \
             HML_BUILTIN_F64_CONSTS; do
    if ! grep -q "$macro(" "$REGISTRATION"; then
        echo -e "${RED}✗${NC} $macro is defined but registration.c never expands it"
        FAILED=1
    fi
done

# HML_BUILTIN_DYNAMIC_GLOBALS names globals built from startup data, which
# registration.c installs by hand rather than through the macro.
grep -oE 'X\("[A-Za-z_0-9]+"\)' "$HEADER" \
    | grep -oE '"[A-Za-z_0-9]+"' | tr -d '"' | sort -u > "$WORK/header_bare"
while read -r name; do
    if ! grep -q "\"$name\"" "$REGISTRATION"; then
        # A compiler-only builtin is deliberately absent from registration.c.
        if ! awk '/HML_COMPILER_ONLY_BUILTINS/,0' "$HEADER" | grep -q "\"$name\""; then
            echo -e "${RED}✗${NC} '$name' is in builtin_globals.h but registration.c never registers it"
            FAILED=1
        fi
    fi
done < "$WORK/header_bare"

# Nothing may be registered by hand that the header does not list.
grep -oE 'env_set\(env, "[A-Za-z_0-9]+"' "$REGISTRATION" \
    | grep -oE '"[A-Za-z_0-9]+"' | tr -d '"' | sort -u > "$WORK/registered_bare"
while read -r name; do
    if ! grep -q "\"$name\"" "$HEADER"; then
        echo -e "${RED}✗${NC} registration.c registers '$name' but builtin_globals.h does not list it"
        FAILED=1
    fi
done < "$WORK/registered_bare"

if [ "$FAILED" -eq 0 ]; then
    echo -e "${GREEN}✓${NC} every name registered by hand is listed in the header"
    echo -e "${GREEN}✓${NC} every header macro is expanded by registration.c"
fi

echo ""
if [ "$FAILED" -eq 0 ]; then
    echo -e "${GREEN}Builtin globals are in sync! 🎉${NC}"
    exit 0
fi
echo -e "${RED}builtin_globals.h and registration.c have drifted.${NC}"
exit 1
