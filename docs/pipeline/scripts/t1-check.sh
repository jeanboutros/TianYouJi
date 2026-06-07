#!/usr/bin/env bash
# T1 Mechanical Compliance Check for ESP32 nRF24L01+ Project
# ============================================================
# Validates mechanical (automatable) quality gates.
# Exit 0 = all checks PASS, non-zero = FAIL with violation report.
#
# Checks:
#   1. Build passes (idf.py build, zero warnings)
#   2. Doxygen on public symbols
#   3. No decision references (D-N, F-N) in source code
#   4. No changelog-style comments in source code
#   5. No raw uint8_t where typed vocabulary exists (best-effort)
#   6. No magic numbers in @code examples
#   7. Constants in correct module
#   8. Reserved bits handled in to_byte()/from_byte()
#
# SELF-REFLECTION CLAUSE
# When T1 catches a violation:
# 1. Why was this violation not caught during implementation?
# 2. What procedural safeguard would prevent recurrence?
# 3. Update the pipeline doc or relevant skill with the lesson.
#
# To add a new T1 check:
# 1. Add a check_N function below
# 2. Add it to the main execution list
# 3. Update the check count in the header
# 4. Document the check in docs/pipeline/pipeline.md T1 table

set -uo pipefail

PROJECT_ROOT="/home/huyang/projects/esp32"
PASS=0
FAIL=1
VIOLATIONS=""
CHECKS_PASSED=0
CHECKS_FAILED=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ─── Utility ──────────────────────────────────────────────────────────

add_violation() {
    local check_num="$1"
    local message="$2"
    VIOLATIONS="${VIOLATIONS}\n${RED}[Check ${check_num}]${NC} ${message}"
    return 0
}

report_check() {
    local check_num="$1"
    local description="$2"
    local result="$3"  # "PASS" or "FAIL"
    if [ "$result" = "PASS" ]; then
        echo -e "  ${GREEN}PASS${NC}  Check ${check_num}: ${description}"
        CHECKS_PASSED=$((CHECKS_PASSED + 1))
    else
        echo -e "  ${RED}FAIL${NC}  Check ${check_num}: ${description}"
        CHECKS_FAILED=$((CHECKS_FAILED + 1))
    fi
}

# ─── Check 1: Build passes ────────────────────────────────────────────

check_1_build() {
    echo "  Running: idf.py build ..."
    local build_output
    local build_exit=0

    # Run build and capture output; allow non-zero exit
    build_output=$(cd "$PROJECT_ROOT" && bash -c 'source ~/.espressif/tools/activate_idf_v6.0.1.sh 2>/dev/null && idf.py build 2>&1') || build_exit=$?

    if [ "$build_exit" -ne 0 ]; then
        add_violation 1 "Build failed with exit code ${build_exit}."
        # Show last 10 lines of build output for context
        echo "$build_output" | tail -10 | while IFS= read -r line; do
            add_violation 1 "  ${line}"
        done
        report_check 1 "Build passes" "FAIL"
        return 1
    fi

    # Check for warnings (the build should have -Werror, but double-check)
    local warning_count
    warning_count=$(echo "$build_output" | grep -ci "warning:" || true)
    if [ "$warning_count" -gt 0 ]; then
        add_violation 1 "Build succeeded but ${warning_count} warning(s) found."
        # Show first 5 warnings
        echo "$build_output" | grep -i "warning:" | head -5 | while IFS= read -r line; do
            add_violation 1 "  ${line}"
        done
        report_check 1 "Build passes (no warnings)" "FAIL"
        return 1
    fi

    report_check 1 "Build passes (zero warnings)" "PASS"
    return 0
}

# ─── Check 2: Doxygen on public symbols ─────────────────────────────────

# Heuristic: for each .h file, find lines that look like public DECLARATIONS
# (function declaration, struct/class/enum declaration, typedef)
# NOT inside private:/protected: sections, and NOT inside function bodies.
# Then check that a Doxygen comment (/** or ///) appears in the preceding lines.
# Forward declarations and function-body lines are skipped.

check_2_doxygen() {
    local header_dirs=(
        "$PROJECT_ROOT/components/nrf24l01plus/include"
        "$PROJECT_ROOT/components/nrf24_espidf/include"
        "$PROJECT_ROOT/main"
    )
    local violations_found=0
    local tmp_violations=""

    for dir in "${header_dirs[@]}"; do
        if [ ! -d "$dir" ]; then
            continue
        fi

        while IFS= read -r -d '' hfile; do
            local line_num=0
            local in_private=0
            local brace_depth=0

            # Read file into an array of lines for backwards scanning
            mapfile -t lines < "$hfile"

            local total_lines=${#lines[@]}

            for ((line_num=0; line_num < total_lines; line_num++)); do
                local line="${lines[$line_num]}"

                # Track brace depth: skip lines inside function/method bodies
                # Count { and } (simplified: doesn't handle strings, but good enough for headers)
                local opens closes
                opens=$(echo "$line" | tr -cd '{' | wc -c)
                closes=$(echo "$line" | tr -cd '}' | wc -c)

                # Track private/protected sections
                if [[ "$line" =~ ^[[:space:]]*private: ]]; then
                    in_private=1
                    continue
                fi
                if [[ "$line" =~ ^[[:space:]]*public: ]]; then
                    in_private=0
                    continue
                fi
                if [[ "$line" =~ ^[[:space:]]*protected: ]]; then
                    in_private=1
                    continue
                fi

                # Skip lines inside private/protected sections
                if [ "$in_private" -eq 1 ]; then
                    # Still track braces to keep depth accurate
                    brace_depth=$((brace_depth + opens - closes))
                    continue
                fi

                # Update brace depth AFTER processing private/public markers
                # But if we're inside a function body (depth > 0 at start of struct/class),
                # we need to skip

                # Normalize: strip leading/trailing whitespace
                local trimmed
                trimmed=$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')

                # Skip blank lines, comments, preprocessor directives
                if [ -z "$trimmed" ]; then continue; fi
                if [[ "$trimmed" =~ ^[\/\*] ]]; then continue; fi
                if [[ "$trimmed" =~ ^# ]]; then continue; fi
                if [[ "$trimmed" =~ ^\} ]]; then continue; fi
                if [[ "$trimmed" =~ ^namespace ]]; then continue; fi
                if [[ "$trimmed" =~ ^using ]]; then continue; fi
                if [[ "$trimmed" =~ ^friend[[:space:]] ]]; then continue; fi
                if [[ "$trimmed" =~ ^typedef ]]; then continue; fi
                if [[ "$trimmed" =~ ^static_assert ]]; then continue; fi

                # Skip lines inside function/method bodies
                # Heuristic: if we're at depth > 0 and the line has a {, it's a body start
                # Lines that are clearly body code: if, for, while, return, case, switch, etc.
                if [[ "$trimmed" =~ ^(if|for|while|switch|case|return|break|continue|else|do)[[:space:]\(] ]]; then
                    continue
                fi

                # Skip assignment lines (bool x = true; uint8_t y = 0;)
                if [[ "$trimmed" =~ ^(bool|uint8_t|uint16_t|uint32_t|int|size_t|auto|const)[[:space:]]+[a-z_]+[[:space:]]*= ]]; then
                    if ! [[ "$trimmed" =~ \( ]]; then
                        continue
                    fi
                fi

                # Skip member field declarations (uint8_t field_name; or bool field_name;)
                # but NOT if they are a struct/class declaration
                if [[ "$trimmed" =~ ^(bool|uint8_t|uint16_t|uint32_t|int|size_t|const)[[:space:]]+(IrqMask|CrcMode|CrcEncoding|PowerMode|PrimaryMode|DataRate|TxPower|ContWave|PllLock|AddressWidth|AutoRetransmitDelay|AutoRetransmitCount|RxPipeNo|BleAdvPduType|DiagPhase|DiagStatus|DiagVerbosity)[a-z_]+\s*= ]] && ! [[ "$trimmed" =~ \( ]]; then
                    continue
                fi

                # Detect forward declarations: "class Foo;" or "struct Foo;"
                if [[ "$trimmed" =~ ^(class|struct)[[:space:]] ]] && \
                   [[ "$trimmed" == *";" ]] && \
                   [[ "$trimmed" != *"{"* ]] && [[ "$trimmed" != *"::"* ]]; then
                    continue
                fi

                # Detect declarations that need Doxygen
                local needs_doxygen=0

                # struct/class/enum declaration (starts a type definition)
                if [[ "$trimmed" =~ ^(struct|class|enum)[[:space:]] ]]; then
                    local is_forward_decl=0
                    # Forward declarations: "class Foo;" or "struct Foo;" — already handled above
                    # But if we got here, check if it has { (body) or : (inheritance)
                    if [[ "$trimmed" == *"{"* ]]; then
                        needs_doxygen=1
                    elif [[ "$trimmed" == *":"* ]]; then
                        needs_doxygen=1
                    elif [[ "$trimmed" == *";" ]]; then
                        # Check if this is NOT a simple forward declaration
                        if ! [[ "$trimmed" =~ ^(class|struct)[[:space:]] ]]; then
                            needs_doxygen=1
                        fi
                    fi
                fi

                # enum class (always needs Doxygen regardless of what follows)
                if [[ "$trimmed" =~ ^enum[[:space:]]+class[[:space:]] ]]; then
                    needs_doxygen=1
                fi

                # Function/method declaration: has parentheses, ends with ; or {
                # But NOT inside a function body (check that it's a declaration line)
                if [[ "$trimmed" == *"("* ]] && ( [[ "$trimmed" == *";"* ]] || [[ "$trimmed" == *"{"* ]] ); then
                    # Skip if this is a return statement containing parentheses
                    if [[ "$trimmed" =~ ^return ]]; then continue; fi
                    # Skip lines that are clearly inside a function body
                    # (have = or >> or << operators, or are static_cast lines)
                    if [[ "$trimmed" == *"static_cast"* ]] || [[ "$trimmed" == *"dynamic_cast"* ]] || [[ "$trimmed" == *"reinterpret_cast"* ]]; then continue; fi
                    # Skip lines that are clearly bit manipulation inside a function body
                    if [[ "$trimmed" == *">>"* ]] || [[ "$trimmed" == *"<<"* ]]; then
                        # But keep function declarations that happen to contain << or >>
                        if [[ "$trimmed" != *"("* ]]; then continue; fi
                    fi
                    needs_doxygen=1
                fi

                # Inline function definitions at namespace scope
                if [[ "$trimmed" =~ ^inline[[:space:]]+(constexpr|const|void|bool|uint8_t|int)[[:space:]] ]] && [[ "$trimmed" == *"{"* ]]; then
                    needs_doxygen=1
                fi

                # Skip type/member declarations that don't need standalone Doxygen
                if [[ "$trimmed" =~ ^static[[:space:]]+constexpr ]]; then
                    continue
                fi

                # Simple member variable declarations (like "bool rx_dr = false;")
                if [[ "$trimmed" =~ ^(bool|uint8_t|uint16_t|uint32_t|int|size_t|const)[[:space:]]+[a-z_]+[[:space:]]*= ]] && ! [[ "$trimmed" == *"("* ]]; then
                    continue
                fi

                # Array member declarations (like "bool pipe[6] = ...;")
                if [[ "$trimmed" =~ ^(bool|uint8_t|uint16_t|uint32_t|int)[[:space:]]+[a-z_]+\[ ]]; then
                    continue
                fi

                # Switch/case lines inside function bodies
                if [[ "$trimmed" =~ ^(case|default): ]]; then continue; fi

                # Const variable in function body
                if [[ "$trimmed" =~ ^const[[:space:]]+(uint8_t|int|char|bool)[[:space:]]+[a-z_]+[[:space:]]*= ]]; then
                    if ! [[ "$trimmed" == *"(" ]]; then continue; fi
                fi

                if [ "$needs_doxygen" -eq 0 ]; then
                    continue
                fi

                # Check preceding 5 lines for Doxygen comment
                local found_doxygen=0
                for ((back=1; back <= 5; back++)); do
                    local prev_idx=$((line_num - back))
                    if [ "$prev_idx" -lt 0 ]; then break; fi
                    local prev_line="${lines[$prev_idx]}"

                    # Doxygen comment patterns: /**, ///, /*!<, or a line ending with */
                    if [[ "$prev_line" == *"/**"* ]] || \
                       [[ "$prev_line" =~ ^[[:space:]]*\/\/\/ ]] || \
                       [[ "$prev_line" == *"/*!<"* ]] || \
                       [[ "$prev_line" == *"*/"* ]]; then
                        found_doxygen=1
                        break
                    fi

                    # Stop searching at a non-comment, non-blank line
                    local prev_trimmed
                    prev_trimmed=$(echo "$prev_line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
                    if [ -z "$prev_trimmed" ]; then
                        # blank line — keep looking
                        continue
                    fi
                    # Stop at a non-comment line that doesn't look like it's inside a comment block
                    if [[ "$prev_trimmed" != *"*"* ]] && [[ "$prev_trimmed" != *"/**"* ]] && [[ "$prev_trimmed" != *"///"* ]]; then
                        break
                    fi
                done

                if [ "$found_doxygen" -eq 0 ]; then
                    local display_line=$((line_num + 1))
                    local rel_path="${hfile#$PROJECT_ROOT/}"
                    # Show a short snippet of the line (max 80 chars)
                    local snippet
                    snippet=$(echo "$trimmed" | head -c 80)
                    tmp_violations="${tmp_violations}\n    ${rel_path}:${display_line}: ${snippet}"
                    violations_found=$((violations_found + 1))
                fi
            done
        done < <(find "$dir" -name '*.h' -print0 2>/dev/null)
    done

    if [ "$violations_found" -gt 0 ]; then
        add_violation 2 "Missing Doxygen on ${violations_found} public symbol(s):${tmp_violations}"
        report_check 2 "Doxygen on public symbols" "FAIL"
        return 1
    fi

    report_check 2 "Doxygen on public symbols" "PASS"
    return 0
}

# ─── Check 3: No decision references in source code ────────────────────

check_3_no_decision_refs() {
    local violations_found=0
    local tmp_violations=""

    # Search .cpp and .h files, excluding docs/ and this script
    while IFS= read -r -d '' srcfile; do
        local rel_path="${srcfile#$PROJECT_ROOT/}"

        # Skip documentation files
        if [[ "$rel_path" =~ ^docs/ ]]; then continue; fi
        # Skip this script
        if [[ "$rel_path" =~ t1-check\.sh ]]; then continue; fi
        # Skip build directory
        if [[ "$rel_path" =~ ^build/ ]]; then continue; fi

        # Check for D-N pattern (e.g., D-1, D-2, D-10)
        local d_matches
        d_matches=$(grep -nP '\bD-\d+\b' "$srcfile" 2>/dev/null || true)
        if [ -n "$d_matches" ]; then
            while IFS= read -r match; do
                tmp_violations="${tmp_violations}\n    ${rel_path}: ${match}"
                violations_found=$((violations_found + 1))
            done <<< "$d_matches"
        fi

        # Check for F-N pattern (e.g., F-1, F-2, F-10)
        local f_matches
        f_matches=$(grep -nP '\bF-\d+\b' "$srcfile" 2>/dev/null || true)
        if [ -n "$f_matches" ]; then
            while IFS= read -r match; do
                tmp_violations="${tmp_violations}\n    ${rel_path}: ${match}"
                violations_found=$((violations_found + 1))
            done <<< "$f_matches"
        fi

        # Check for "(decision" (case-insensitive)
        local dec_matches
        dec_matches=$(grep -niP '\(decision' "$srcfile" 2>/dev/null || true)
        if [ -n "$dec_matches" ]; then
            while IFS= read -r match; do
                tmp_violations="${tmp_violations}\n    ${rel_path}: ${match}"
                violations_found=$((violations_found + 1))
            done <<< "$dec_matches"
        fi

    done < <(find "$PROJECT_ROOT/components" "$PROJECT_ROOT/main" \
             \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -print0 2>/dev/null)

    if [ "$violations_found" -gt 0 ]; then
        add_violation 3 "Found ${violations_found} decision reference(s) in source code:${tmp_violations}"
        report_check 3 "No decision references in source" "FAIL"
        return 1
    fi

    report_check 3 "No decision references in source" "PASS"
    return 0
}

# ─── Check 4: No changelog-style comments in source code ───────────────

check_4_no_changelog_comments() {
    local violations_found=0
    local tmp_violations=""

    local patterns=(
        "replaces the"
        "was previously"
        "formerly"
        "refactored from"
    )

    while IFS= read -r -d '' srcfile; do
        local rel_path="${srcfile#$PROJECT_ROOT/}"

        # Skip documentation files
        if [[ "$rel_path" =~ ^docs/ ]]; then continue; fi
        # Skip this script
        if [[ "$rel_path" =~ t1-check\.sh ]]; then continue; fi
        # Skip build directory
        if [[ "$rel_path" =~ ^build/ ]]; then continue; fi

        for pattern in "${patterns[@]}"; do
            local matches
            # Case-insensitive search for the pattern
            matches=$(grep -ni "${pattern}" "$srcfile" 2>/dev/null || true)
            if [ -n "$matches" ]; then
                while IFS= read -r match; do
                    tmp_violations="${tmp_violations}\n    ${rel_path}: ${match}"
                    violations_found=$((violations_found + 1))
                done <<< "$matches"
            fi
        done

    done < <(find "$PROJECT_ROOT/components" "$PROJECT_ROOT/main" \
             \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -print0 2>/dev/null)

    if [ "$violations_found" -gt 0 ]; then
        add_violation 4 "Found ${violations_found} changelog-style comment(s) in source code:${tmp_violations}"
        report_check 4 "No changelog-style comments" "FAIL"
        return 1
    fi

    report_check 4 "No changelog-style comments" "PASS"
    return 0
}

# ─── Check 5: No raw uint8_t where typed vocabulary exists ─────────────

# Best-effort heuristic:
# Scan public headers in components/nrf24l01plus/include/ for public methods
# with uint8_t parameters. If the method is in a public section and
# there's no typed template overload on the same class, flag it.
# Whitelist: to_byte(), from_byte(), constructor, size/length params, buf params,
#            SPI HAL interface (spi_xfer).

check_5_no_raw_uint8() {
    local violations_found=0
    local tmp_violations=""

    local header_dir="$PROJECT_ROOT/components/nrf24l01plus/include"

    if [ ! -d "$header_dir" ]; then
        report_check 5 "No raw uint8_t where typed vocabulary exists" "PASS"
        return 0
    fi

    while IFS= read -r -d '' hfile; do
        local rel_path="${hfile#$PROJECT_ROOT/}"
        local in_private=0
        local line_num=0

        mapfile -t lines < "$hfile"
        local total_lines=${#lines[@]}

        for ((line_num=0; line_num < total_lines; line_num++)); do
            local line="${lines[$line_num]}"

            # Track private/protected sections
            if [[ "$line" =~ ^[[:space:]]*private: ]]; then
                in_private=1
                continue
            fi
            if [[ "$line" =~ ^[[:space:]]*public: ]]; then
                in_private=0
                continue
            fi
            if [[ "$line" =~ ^[[:space:]]*protected: ]]; then
                in_private=1
                continue
            fi

            # Only check public section
            if [ "$in_private" -eq 1 ]; then
                continue
            fi

            # Skip comment lines
            if [[ "$line" =~ ^[[:space:]]*(///|\*|/\*) ]]; then
                continue
            fi
            if [[ "$line" =~ ^[[:space:]]*// ]]; then
                continue
            fi

            # Look for public method declarations with uint8_t parameters
            if [[ "$line" == *"uint8_t"*"("*")"* ]]; then
                # Whitelist: to_byte() and from_byte() are conversion functions
                if [[ "$line" == *"to_byte"* ]] || [[ "$line" == *"from_byte"* ]]; then
                    continue
                fi

                # Whitelist: template methods (they have typed overloads via StructType)
                if [[ "$line" =~ template ]]; then
                    continue
                fi

                # Whitelist: constructor with Hal& (dependency injection)
                if [[ "$line" == *"Driver("*"Hal"* ]]; then
                    continue
                fi

                # Whitelist: methods documented as "internal" (Prefers typed overload)
                local has_typed_note=0
                for ((back=1; back <= 10; back++)); do
                    local prev_idx=$((line_num - back))
                    if [ "$prev_idx" -lt 0 ]; then break; fi
                    local prev="${lines[$prev_idx]}"
                    if [[ "$prev" == *"typed overload"* ]] || [[ "$prev" == *"Prefer"*"typed"* ]] || \
                       [[ "$prev" == *"raw uint8_t"* ]] || [[ "$prev" == *"internal"* ]]; then
                        has_typed_note=1
                        break
                    fi
                done
                if [ "$has_typed_note" -eq 1 ]; then
                    continue
                fi

                # Whitelist: HAL interface (spi_xfer is byte-level SPI, inherently uint8_t)
                if [[ "$line" == *"spi_xfer"* ]]; then
                    continue
                fi

                # Whitelist: type conversion functions (to_reg, from_byte, swapbits)
                # These intentionally use uint8_t for byte-level manipulation
                local func_name
                func_name=$(echo "$line" | grep -oP '[a-z_]+(?=\s*\()' | head -1 || true)
                if [[ "$func_name" =~ ^(to_reg|from_byte|swapbits|channel_to_rf_ch|dewhiten|pdu_type_name|format_address|adv_address)$ ]]; then
                    continue
                fi

                # Whitelist: uint8_t in struct member declarations (fields like uint8_t rf_ch;)
                # These are within struct bodies and are data fields, not method parameters
                if [[ "$line" =~ ^[[:space:]]*(bool|uint8_t|uint16_t|uint32_t|uint64_t|int|char|const)[[:space:]]+[a-z_]+\s*(=|;|\[) ]]; then
                    continue
                fi

                # Whitelist: size/buffer/channel/len/ms/us parameters
                # These are inherently byte-level quantities, not register addresses
                local param_name
                # Try to extract the parameter name after uint8_t
                param_name=$(echo "$line" | grep -oP 'uint8_t\s+\K[a-z_]+' | head -1 || true)
                if [[ "$param_name" =~ ^(len|size|buf|ms|us|channel|phase|count|idx|index|poll|duration|width|v|byte|addr|pipe)$ ]]; then
                    continue
                fi

                # Whitelist: EspIdfHal init (platform-specific, pins are inherently numeric)
                if [[ "$line" == *"init"* ]] || [[ "$line" == *"EspIdfPins"* ]]; then
                    continue
                fi

                # Whitelist: format/snprintf (buf and len are data parameters)
                if [[ "$line" == *"format"* ]] || [[ "$line" == *"snprintf"* ]]; then
                    continue
                fi

                # Whitelist: template method declarations (they have typed overloads)
                if [[ "$line" =~ template ]]; then
                    continue
                fi

                # Whitelist: override keyword (virtual method implementations)
                if [[ "$line" =~ override ]]; then
                    continue
                fi

                local display_line=$((line_num + 1))
                local trimmed_line
                trimmed_line=$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | head -c 100)
                tmp_violations="${tmp_violations}\n    ${rel_path}:${display_line}: uint8_t param in public section: ${trimmed_line}"
                violations_found=$((violations_found + 1))
            fi
        done
    done < <(find "$header_dir" -name '*.h' -print0 2>/dev/null)

    if [ "$violations_found" -gt 0 ]; then
        add_violation 5 "Found ${violations_found} public uint8_t param(s) that may need typed overloads:${tmp_violations}"
        report_check 5 "No raw uint8_t where typed vocabulary exists" "FAIL"
        return 1
    fi

    report_check 5 "No raw uint8_t where typed vocabulary exists" "PASS"
    return 0
}

# ─── Check 6: No magic numbers in @code examples ────────────────────────

# Scan library headers for @code blocks. Within each block, find hex
# literals > 0x01 and decimal literals > 1 that don't correspond to
# a named constant. Whitelist: 0x00, 0xFF, 0, 1, true, false

check_6_no_magic_numbers() {
    local violations_found=0
    local tmp_violations=""

    local header_dir="$PROJECT_ROOT/components/nrf24l01plus/include"

    if [ ! -d "$header_dir" ]; then
        report_check 6 "No magic numbers in @code examples" "PASS"
        return 0
    fi

    while IFS= read -r -d '' hfile; do
        local rel_path="${hfile#$PROJECT_ROOT/}"
        local in_code_block=0

        mapfile -t lines < "$hfile"
        local total_lines=${#lines[@]}

        for ((line_num=0; line_num < total_lines; line_num++)); do
            local line="${lines[$line_num]}"

            # Track @code blocks
            if [[ "$line" == *@code* ]]; then
                in_code_block=1
                continue
            fi
            if [[ "$line" == *@endcode* ]]; then
                in_code_block=0
                continue
            fi

            if [ "$in_code_block" -eq 0 ]; then
                continue
            fi

            local display_line=$((line_num + 1))

            # Inside @code block: flag cases where a hex literal is used as
            # a register write value without using a typed struct or constant
            # Specifically: write_reg(0xNN, ...) calls with raw hex addresses
            if [[ "$line" == *"write_reg"*"0x"* ]]; then
                # If it's using a named constant (like nrf24::reg::CONFIG), that's OK
                if [[ "$line" == *"nrf24"*"::"* ]] || [[ "$line" == *"::ADDRESS"* ]]; then
                    continue
                fi
                # If it's using a struct variable (like cfg.to_byte()), that's OK
                if [[ "$line" == *".to_byte("* ]] || [[ "$line" == *"to_byte()"* ]]; then
                    continue
                fi

                tmp_violations="${tmp_violations}\n    ${rel_path}:${display_line}: hex literal in write_reg call in @code (use named constant)"
                violations_found=$((violations_found + 1))
            fi

            # Flag read_reg(0xNN, ...) calls with raw hex
            if [[ "$line" == *"read_reg"*"0x"* ]]; then
                if [[ "$line" == *"nrf24"*"::"* ]] || [[ "$line" == *"::ADDRESS"* ]]; then
                    continue
                fi
                if [[ "$line" == *".to_byte("* ]] || [[ "$line" == *"to_byte()"* ]]; then
                    continue
                fi

                tmp_violations="${tmp_violations}\n    ${rel_path}:${display_line}: hex literal in read_reg call in @code (use named constant)"
                violations_found=$((violations_found + 1))
            fi
        done
    done < <(find "$header_dir" -name '*.h' -print0 2>/dev/null)

    if [ "$violations_found" -gt 0 ]; then
        add_violation 6 "Found ${violations_found} magic number(s) in @code examples:${tmp_violations}"
        report_check 6 "No magic numbers in @code examples" "FAIL"
        return 1
    fi

    report_check 6 "No magic numbers in @code examples" "PASS"
    return 0
}

# ─── Check 7: Constants in correct module ───────────────────────────────

check_7_constants_in_module() {
    local violations_found=0
    local tmp_violations=""

    # Check 7a: NRF24_MAX_PAYLOAD redefined outside of driver.h
    local max_payload_redefs
    max_payload_redefs=$(grep -rnP 'static\s+constexpr.*MAX_PAYLOAD|#define\s+MAX_PAYLOAD' \
        "$PROJECT_ROOT/components/nrf24l01plus/src/" \
        "$PROJECT_ROOT/components/nrf24_espidf/" \
        "$PROJECT_ROOT/main/" \
        2>/dev/null || true)

    if [ -n "$max_payload_redefs" ]; then
        while IFS= read -r match; do
            tmp_violations="${tmp_violations}\n    ${match} (MAX_PAYLOAD should only be defined in driver.h)"
            violations_found=$((violations_found + 1))
        done <<< "$max_payload_redefs"
    fi

    # Check 7b: BLE_DIAG should not exist (deprecated)
    local ble_diag_refs
    ble_diag_refs=$(grep -rn 'BLE_DIAG' \
        "$PROJECT_ROOT/components/" \
        "$PROJECT_ROOT/main/" \
        2>/dev/null | grep -v 't1-check' || true)

    if [ -n "$ble_diag_refs" ]; then
        while IFS= read -r match; do
            tmp_violations="${tmp_violations}\n    Deprecated BLE_DIAG found: ${match}"
            violations_found=$((violations_found + 1))
        done <<< "$ble_diag_refs"
    fi

    # Check 7c: static constexpr in main/*.cpp that defines a chip-level constant
    # Heuristic: any constant related to the nRF24 chip
    local chip_constant_keywords=(
        'NRF24'
        'RF_CH'
        'RF_SETUP'
        'CONFIG'
        'EN_AA'
        'EN_CRC'
        'SETUP_RETR'
        'FIFO_STATUS'
        'RX_ADDR'
        'TX_ADDR'
        'MAX_PAYLOAD'
        'BLE_ADV'
        'ACCESS_ADDR'
    )

    local main_cpp_files
    main_cpp_files=$(find "$PROJECT_ROOT/main" -name '*.cpp' 2>/dev/null || true)
    if [ -n "$main_cpp_files" ]; then
        while IFS= read -r cppfile; do
            local rel_path="${cppfile#$PROJECT_ROOT/}"
            for keyword in "${chip_constant_keywords[@]}"; do
                local matches
                # Use grep with fixed string matching for the keyword
                matches=$(grep -n "static constexpr.*${keyword}" "$cppfile" 2>/dev/null || true)
                if [ -n "$matches" ]; then
                    while IFS= read -r match; do
                        tmp_violations="${tmp_violations}\n    ${rel_path}: ${match} (chip constant should be in library)"
                        violations_found=$((violations_found + 1))
                    done <<< "$matches"
                fi
            done
        done <<< "$main_cpp_files"
    fi

    if [ "$violations_found" -gt 0 ]; then
        add_violation 7 "Found ${violations_found} constant placement violation(s):${tmp_violations}"
        report_check 7 "Constants in correct module" "FAIL"
        return 1
    fi

    report_check 7 "Constants in correct module" "PASS"
    return 0
}

# ─── Check 8: Reserved bits handled in to_byte()/from_byte() ───────────

# For each register struct header, verify that if reserved bits are
# mentioned in the bit layout comment, the implementation handles them.

check_8_reserved_bits() {
    local violations_found=0
    local tmp_violations=""

    local hdr_dir="$PROJECT_ROOT/components/nrf24l01plus/include/nrf24l01plus/registers"

    if [ ! -d "$hdr_dir" ]; then
        report_check 8 "Reserved bits handled in to_byte()/from_byte()" "PASS"
        return 0
    fi

    while IFS= read -r -d '' hfile; do
        local rel_path="${hfile#$PROJECT_ROOT/}"
        local filename
        filename=$(basename "$hfile" .h)

        # Skip the addresses header (no register struct)
        if [ "$filename" = "addresses" ] || [ "$filename" = "static_asserts" ]; then
            continue
        fi

        local file_content
        file_content=$(cat "$hfile")

        # Check if this file has a struct with to_byte() and from_byte()
        if [[ "$file_content" != *"to_byte"* ]] || [[ "$file_content" != *"from_byte"* ]]; then
            continue
        fi

        # Check if the register comment mentions reserved bits
        local has_reserved_mention=0
        if [[ "$file_content" == *"rsvd"* ]] || [[ "$file_content" == *"reserved"* ]] || [[ "$file_content" == *"Reserved"* ]]; then
            has_reserved_mention=1
        fi

        # If no reserved bits mentioned, the register may be fully used — skip
        if [ "$has_reserved_mention" -eq 0 ]; then
            continue
        fi

        # Now verify reserved bits are handled. We look for several patterns:
        #
        # Pattern A: Explicit mask (& 0xNN) that zeroes reserved bits
        # Pattern B: to_byte() uses named fields only (no raw bit positions for reserved bits)
        # Pattern C: from_byte() extracts only named fields (masks out reserved bits)
        # Pattern D: A 'reserved' or padding field exists
        # Pattern E: The struct uses to_reg() helper functions (which place bits correctly)
        # Pattern F: Comment explicitly states "reserved bits are implicitly 0"

        local handles_reserved=0

        # Extract to_byte() implementation (up to 25 lines after)
        local to_byte_section
        to_byte_section=$(echo "$file_content" | grep -A 25 'to_byte()' | head -30)

        # Pattern A: Explicit mask
        if echo "$to_byte_section" | grep -qP '0x[0-9A-Fa-f]+' | head -5; then
            # Look for & 0xNN patterns that mask out reserved bits
            if echo "$to_byte_section" | grep -q '& 0x'; then
                handles_reserved=1
            fi
        fi

        # Pattern B: to_byte() constructs value from named fields only (uses to_reg or shift operations)
        if echo "$to_byte_section" | grep -q 'to_reg('; then
            handles_reserved=1
        fi

        # Pattern C: from_byte() uses bit masks (& 0xNN) to extract fields
        local from_byte_section
        from_byte_section=$(echo "$file_content" | grep -A 25 'from_byte' | head -30)
        if echo "$from_byte_section" | grep -q '& 0x'; then
            handles_reserved=1
        fi

        # Pattern D: RfCh style — to_byte() masks with & 0x7F
        if echo "$to_byte_section" | grep -q '0x7F'; then
            handles_reserved=1
        fi

        # Pattern E: Uses shift operations (static_cast << N) which place bits correctly
        if echo "$to_byte_section" | grep -q 'static_cast'; then
            handles_reserved=1
        fi

        # Pattern F: for-loop style (EnAa), bits are constructed per-field
        if echo "$to_byte_section" | grep -q 'for '; then
            handles_reserved=1
        fi

        # Pattern G: Boolean fields shifted individually — reserved bits default to 0
        if echo "$to_byte_section" | grep -q 'bool'; then
            handles_reserved=1
        fi

        # Pattern H: to_byte() ORs to_reg() results — each to_reg() places bits
        # at the correct position, so reserved bits are implicitly 0
        if echo "$to_byte_section" | grep -q 'to_reg'; then
            handles_reserved=1
        fi

        # Pattern I: Simple return of single field with mask or shift
        # (e.g., "return static_cast<uint8_t>(received_power);" or "return channel & 0x7F;")
        if echo "$to_byte_section" | grep -q 'return.*static_cast'; then
            handles_reserved=1
        fi
        if echo "$to_byte_section" | grep -q 'return.*&.*0x'; then
            handles_reserved=1
        fi

        if [ "$handles_reserved" -eq 0 ]; then
            tmp_violations="${tmp_violations}\n    ${rel_path}: reserved bits mentioned but no handling found in to_byte()/from_byte()"
            violations_found=$((violations_found + 1))
        fi

    done < <(find "$hdr_dir" -name '*.h' -print0 2>/dev/null)

    if [ "$violations_found" -gt 0 ]; then
        add_violation 8 "Found ${violations_found} reserved bit handling issue(s):${tmp_violations}"
        report_check 8 "Reserved bits handled in to_byte()/from_byte()" "FAIL"
        return 1
    fi

    report_check 8 "Reserved bits handled in to_byte()/from_byte()" "PASS"
    return 0
}

# ─── Main ──────────────────────────────────────────────────────────────

echo "========================================"
echo "T1 Mechanical Compliance Check"
echo "========================================"
echo ""

RESULT=0

# Check 1: Build passes
check_1_build || RESULT=1

# Check 2: Doxygen on public symbols
check_2_doxygen || RESULT=1

# Check 3: No decision references in source code
check_3_no_decision_refs || RESULT=1

# Check 4: No changelog-style comments in source code
check_4_no_changelog_comments || RESULT=1

# Check 5: No raw uint8_t where typed vocabulary exists
check_5_no_raw_uint8 || RESULT=1

# Check 6: No magic numbers in @code examples
check_6_no_magic_numbers || RESULT=1

# Check 7: Constants in correct module
check_7_constants_in_module || RESULT=1

# Check 8: Reserved bits handled in to_byte()/from_byte()
check_8_reserved_bits || RESULT=1

echo ""
echo "========================================"
echo "  ${CHECKS_PASSED} passed, ${CHECKS_FAILED} failed"
echo "========================================"

if [ -z "$VIOLATIONS" ]; then
    echo -e "${GREEN}ALL T1 CHECKS PASSED${NC}"
    exit 0
else
    echo -e "${RED}T1 CHECKS FAILED${NC}"
    echo ""
    echo "Violations:"
    echo -e "$VIOLATIONS"
    echo ""
    echo "Fix these violations and re-run T1."
    exit 1
fi