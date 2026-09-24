/*
 * Hemlock Runtime Library - Frame Loop
 *
 * main_loop(callback, fps) / main_loop_stop()
 *
 * The portable way to drive per-frame work (games, animation, simulation).
 *
 * Why this exists rather than `while (true) { render(); sleep(1.0/60.0); }`:
 * in the browser a Hemlock program owns no event loop, so sleep() has to be
 * implemented with Emscripten's Asyncify, which rewrites the whole module to
 * be able to unwind and rewind the WASM stack at any suspend point. That
 * instrumentation is paid on every call in the program, not just the sleeping
 * one, and it is the difference between a frame budget that fits in 16 ms and
 * one that does not. main_loop() hands the callback to the host's own frame
 * scheduler (requestAnimationFrame via emscripten_set_main_loop_arg) so no
 * stack unwinding is needed and the program can be linked with
 * `hemlockc --target wasm --no-asyncify`.
 *
 * Natively there is no host scheduler, so main_loop() is an ordinary paced
 * loop: call, sleep out the rest of the frame, repeat until main_loop_stop().
 */

#include "builtins_internal.h"

// The running callback. Retained for as long as the loop owns it so the
// closure cannot be collected out from under the frame scheduler.
static HmlValue g_main_loop_callback;
static int g_main_loop_active = 0;
static volatile int g_main_loop_stopping = 0;

#ifndef __EMSCRIPTEN__
// Monotonic milliseconds, used only for frame pacing (never for wall time).
// Only the native loop needs it; under Emscripten the host schedules frames.
static double hml_mainloop_now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#else
    return (double)hml_to_i64(hml_time_ms());
#endif
}
#endif

static void hml_mainloop_release_callback(void) {
    if (!g_main_loop_active) return;
    g_main_loop_active = 0;
    g_main_loop_stopping = 0;
    hml_release(&g_main_loop_callback);
    g_main_loop_callback = hml_val_null();
}

#ifdef __EMSCRIPTEN__
static void hml_mainloop_tick(void *unused) {
    (void)unused;
    if (!g_main_loop_active) return;
    hml_call_function(g_main_loop_callback, NULL, 0);
}
#endif

void hml_main_loop(HmlValue callback, HmlValue fps) {
    if (callback.type != HML_VAL_FUNCTION && callback.type != HML_VAL_BUILTIN_FN) {
        hml_runtime_error("main_loop() first argument must be a function");
    }
    if (g_main_loop_active) {
        hml_runtime_error("main_loop() is already running (call main_loop_stop() first)");
    }

    int64_t target_fps = hml_to_i64(fps);
    if (target_fps < 0) {
        hml_runtime_error("main_loop() fps must be >= 0 (0 = let the host pick the rate)");
    }

    g_main_loop_callback = callback;
    hml_retain(&g_main_loop_callback);
    g_main_loop_active = 1;
    g_main_loop_stopping = 0;

#ifdef __EMSCRIPTEN__
    // fps 0 tells Emscripten to use requestAnimationFrame, which is what a
    // browser game wants: the display's refresh rate, throttled in background
    // tabs. The final argument unwinds the C stack instead of returning, so
    // the browser gets its event loop back between frames -- nothing after
    // this call runs. See docs/advanced/wasm.md.
    emscripten_set_main_loop_arg(hml_mainloop_tick, NULL, (int)target_fps, 1);
#else
    double frame_ms = target_fps > 0 ? 1000.0 / (double)target_fps : 0.0;
    while (!g_main_loop_stopping) {
        double frame_start = hml_mainloop_now_ms();

        hml_call_function(g_main_loop_callback, NULL, 0);

        if (g_main_loop_stopping) break;

        if (frame_ms > 0.0) {
            double elapsed = hml_mainloop_now_ms() - frame_start;
            double remaining = frame_ms - elapsed;
            if (remaining > 0.0) {
                hml_sleep(hml_val_f64(remaining / 1000.0));
            }
        }
    }
    hml_mainloop_release_callback();
#endif
}

void hml_main_loop_stop(void) {
    if (!g_main_loop_active) return;

    g_main_loop_stopping = 1;
#ifdef __EMSCRIPTEN__
    // The native loop tears itself down when it notices the flag; under
    // Emscripten there is no loop body to return to, so drop it here.
    emscripten_cancel_main_loop();
    hml_mainloop_release_callback();
#endif
}

// ========== BUILTIN WRAPPERS ==========

HmlValue hml_builtin_main_loop(HmlClosureEnv *env, HmlValue callback, HmlValue fps) {
    (void)env;
    hml_main_loop(callback, fps);
    return hml_val_null();
}

HmlValue hml_builtin_main_loop_stop(HmlClosureEnv *env) {
    (void)env;
    hml_main_loop_stop();
    return hml_val_null();
}
