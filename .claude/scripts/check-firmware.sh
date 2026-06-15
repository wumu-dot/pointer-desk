#!/usr/bin/env bash
# OV-Watch firmware consistency check
# Runs after every Write/Edit to catch regression bugs early
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FW="$ROOT/firmware"
errors=0

check() {
    local desc="$1"
    local pattern="$2"
    local files="$3"
    local expected="$4"  # "found" means pattern SHOULD exist; "absent" means it should NOT
    local result

    result=$(grep -rn "$pattern" $files 2>/dev/null || true)

    if [ "$expected" = "absent" ] && [ -n "$result" ]; then
        echo "  FAIL: $desc (should be absent but found):"
        echo "$result" | head -3
        errors=$((errors + 1))
    elif [ "$expected" = "found" ] && [ -z "$result" ]; then
        echo "  FAIL: $desc (should exist but missing)"
        errors=$((errors + 1))
    else
        echo "  OK:   $desc"
    fi
}

echo "=== OV-Watch Post-Edit Check ==="
echo ""

# P0: No duplicate function BODIES (forward decls on L137-140 are fine)
# Check: all "void StartTask" lines in main.c end with ";" (declarations), not "{" (definitions)
if grep -n "void StartTask" "$FW/Core/Src/main.c" 2>/dev/null | grep -qv ';$'; then
    echo "  FAIL: StartTask* function bodies should NOT be in main.c"
    errors=$((errors + 1))
else
    echo "  OK:   StartTask* not duplicated (only forward declarations)"
fi

# RTC format consistency
check "RTC uses FORMAT_BIN (not BCD)" \
    "RTC_FORMAT_BCD" "$FW/Core/Src/main.c" "absent"

check "RTC uses STOREOPERATION_SET" \
    "RTC_STOREOPERATION_SET" "$FW/Core/Src/main.c" "found"

# SPI speed
check "SPI1 prescaler is 4 (10.5MHz, not 16/2.6MHz)" \
    "BAUDRATEPRESCALER_4" "$FW/Core/Src/main.c" "found"

# Dead code
check "state.scale dead code removed" \
    "state\.scale" "$FW/Middleware/pointer/pointer_engine.c" "absent"

# Draw text correctness (check: draws per-char, calls st7735_draw_char for each)
check "draw_text uses per-char st7735_draw_char" \
    "st7735_draw_char.*\*str" "$FW/Drivers/BSP/lcd/st7735.c" "found"

# Motor feedback loop removed
check "BG task no longer sends motor targets" \
    "QueueMotorTargetsHandle.*pointer_get_current_angle" "$FW/Core/Src/freertos.c" "absent"

# Render dedup in place
check "temp_mode has render cache" \
    "render_cache" "$FW/App/modes/temp_mode.c" "found"

check "timer_mode has render cache" \
    "render_cache" "$FW/App/modes/timer_mode.c" "found"

check "settings_mode has needs_render" \
    "needs_render" "$FW/App/modes/settings_mode.c" "found"

check "clock_mode has need_full_refresh" \
    "need_full_refresh" "$FW/App/modes/clock_mode.c" "found"

# Pointer log dedup
check "pointer_set_target has LOG dedup" \
    "angle_changed.*mode_changed" "$FW/Middleware/pointer/pointer_engine.c" "found"

# Font reading direction
check "font read uses column-major [col]>>row" \
    "font_5x7\[idx\]\[base_col\].*base_row" "$FW/Drivers/BSP/lcd/st7735.c" "found"

# Flash init must be called before fs_mgr_init
check "w25q64_init called in fs_mgr.c (deferred to BG task)" \
    "w25q64_init()" "$FW/Middleware/fs/fs_mgr.c" "found"

check "fs_mgr_init NOT called in main.c (deferred to BG task)" \
    "fs_mgr_init()" "$FW/Core/Src/main.c" "absent"

echo ""
if [ $errors -eq 0 ]; then
    echo "All checks passed."
    exit 0
else
    echo "$errors check(s) failed! Fix before continuing."
    exit 1
fi
