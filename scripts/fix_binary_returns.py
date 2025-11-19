#!/usr/bin/env python3
import re

# Read the file
with open('src/interpreter/runtime/expressions.c', 'r') as f:
    lines = f.readlines()

# We need to replace ALL `return val_X(...)` in EXPR_BINARY case (lines 175-605 approximately)
# with `binary_result = val_X(...); goto binary_cleanup;`

in_target_range = False
result_lines = []

for i, line in enumerate(lines, 1):
    # Start replacing after line 175 (after left/right evaluation)
    if i == 175:
        in_target_range = True
    # Stop at line 605 (approximate end of EXPR_BINARY)
    if i >= 605:
        in_target_range = False

    if in_target_range and 'return val_' in line:
        # Replace `return val_X(...);` or `case XXX: return val_X(...);`
        match = re.match(r'(\s*)(?:case \w+:\s*)?return\s+(val_\w+\([^)]+\));', line)
        if match:
            indent = match.group(1)
            # If it's a case statement on same line, preserve that
            if 'case ' in line:
                case_match = re.match(r'(\s*)(case \w+):\s*return', line)
                if case_match:
                    result_lines.append(f'{case_match.group(1)}{case_match.group(2)}:\n')
                    result_lines.append(f'{indent}    binary_result = {match.group(2)};\n')
                    result_lines.append(f'{indent}    goto binary_cleanup;\n')
                else:
                    result_lines.append(line)
            else:
                result_lines.append(f'{indent}binary_result = {match.group(2)};\n')
                result_lines.append(f'{indent}goto binary_cleanup;\n')
        else:
            result_lines.append(line)
    else:
        result_lines.append(line)

# Write back
with open('src/interpreter/runtime/expressions.c', 'w') as f:
    f.writelines(result_lines)

print("Replaced return statements with goto binary_cleanup")
