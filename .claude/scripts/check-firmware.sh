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

# P0: StartTask* implementations must live in freertos.c.
# main.c may contain ARMCC-only stubs inside #ifndef __GNUC__ guard (GCC migration).
# Check: any StartTask body in main.c that is NOT inside the GCC guard = FAIL.
if grep -n "void StartTask" "$FW/Core/Src/main.c" 2>/dev/null | grep -qv ';$'; then
    # Bodies exist — verify they are guarded by #ifndef __GNUC__
    # Extract the region between #ifndef __GNUC__ and #endif, then check StartTask inside it
    HAS_GUARD=$(sed -n '/#ifndef __GNUC__/,/#endif.*__GNUC__/p' "$FW/Core/Src/main.c" 2>/dev/null | grep -c "void StartTask" || true)
    TOTAL_BODIES=$(grep -c "void StartTask" "$FW/Core/Src/main.c" 2>/dev/null || true)
    if [ "$HAS_GUARD" -gt 0 ] && [ "$HAS_GUARD" -ge "$TOTAL_BODIES" ]; then
        echo "  OK:   StartTask* in main.c are inside #ifndef __GNUC__ guard (ARMCC-only stubs)"
    else
        echo "  FAIL: StartTask* function bodies in main.c without GCC guard!"
        errors=$((errors + 1))
    fi
else
    echo "  OK:   StartTask* not duplicated (only forward declarations)"
fi

# RTC format consistency
check "RTC uses FORMAT_BIN (not BCD)" \
    "RTC_FORMAT_BCD" "$FW/Core/Src/main.c" "absent"

check "RTC uses STOREOPERATION_SET" \
    "RTC_STOREOPERATION_SET" "$FW/Core/Src/main.c" "found"

# SPI speed: APB2=84MHz, SPI1=84/8=10.5MHz (within ST7735 max 15MHz)
check "SPI1 prescaler is 8 (10.5MHz, within ST7735 spec)" \
    "BAUDRATEPRESCALER_8" "$FW/Core/Src/main.c" "found"

# Dead code
check "state.scale dead code removed" \
    "state\.scale" "$FW/Middleware/pointer/pointer_engine.c" "absent"

# Draw text correctness (check: draws per-char, calls st7735_draw_char for each)
check "st7735 uses framebuffer (g_fb) + flush" \
    "g_fb.*LCD_WIDTH" "$FW/Drivers/BSP/lcd/st7735.c" "found"

# clock_mode must selectively render per-section (not whole screen every second)
if grep -q "time_changed\|date_changed\|ampm_changed\|bar_changed" "$FW/App/modes/clock_mode.c" 2>/dev/null; then
    echo "  OK:   clock_mode uses selective per-section render (small flush, no tear)"
else
    echo "  FAIL: clock_mode missing per-section render (full screen every second!)"
    errors=$((errors + 1))
fi

# Motor feedback loop removed
check "BG task no longer sends motor targets" \
    "QueueMotorTargetsHandle.*pointer_get_current_angle" "$FW/Core/Src/freertos.c" "absent"

# Render dedup in place
check "temp_mode has render cache (last_rendered)" \
    "last_rendered" "$FW/App/modes/temp_mode.c" "found"

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

# Framebuffer: st7735_init must flush after memset (TFT GRAM must sync)
if grep -A3 "memset.*g_fb" "$FW/Drivers/BSP/lcd/st7735.c" 2>/dev/null | grep -q "st7735_flush"; then
    echo "  OK:   st7735_init calls flush after memset"
else
    echo "  FAIL: st7735_init missing flush after memset (TFT shows garbage)"
    errors=$((errors + 1))
fi

# SPI1 must NOT use prescaler 4 (21MHz exceeds ST7735 max 15MHz)
check "SPI1 prescaler 4 removed (was 21MHz, too fast)" \
    "BAUDRATEPRESCALER_4" "$FW/Core/Src/main.c" "absent"

echo ""
if [ $errors -eq 0 ]; then
    echo "All checks passed."
    exit 0
else
    echo "$errors check(s) failed! Fix before continuing."
    exit 1
fi
