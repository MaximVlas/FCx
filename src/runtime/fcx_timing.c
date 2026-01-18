// FCx Timing and Benchmarking Runtime
// Provides high-precision timing functions for FCx programs

#include "fcx_runtime.h"
#include <time.h>

// ============================================================================
// High-Precision Timing
// ============================================================================

// Get current time in nanoseconds (monotonic clock)
int64_t fcx_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Get current time in microseconds
int64_t fcx_time_us(void) {
    return fcx_time_ns() / 1000;
}

// Get current time in milliseconds
int64_t fcx_time_ms(void) {
    return fcx_time_ns() / 1000000;
}

// Get CPU cycles (using rdtscp for serialization)
uint64_t fcx_cycles(void) {
    return fcx_rdtscp();
}

// ============================================================================
// Timer State Management
// ============================================================================

// Global timer slots (up to 16 concurrent timers)
#define MAX_TIMERS 16

typedef struct {
    int64_t start_ns;
    uint64_t start_cycles;
    bool active;
} FcxTimer;

static FcxTimer g_timers[MAX_TIMERS] = {0};

// Start a timer, returns timer ID (0-15), or -1 on error
int64_t fcx_timer_start(void) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!g_timers[i].active) {
            g_timers[i].start_ns = fcx_time_ns();
            g_timers[i].start_cycles = fcx_cycles();
            g_timers[i].active = true;
            return i;
        }
    }
    return -1;  // No free timer slots
}

// Stop timer and return elapsed nanoseconds
int64_t fcx_timer_stop_ns(int64_t timer_id) {
    if (timer_id < 0 || timer_id >= MAX_TIMERS || !g_timers[timer_id].active) {
        return -1;
    }
    int64_t elapsed = fcx_time_ns() - g_timers[timer_id].start_ns;
    g_timers[timer_id].active = false;
    return elapsed;
}

// Stop timer and return elapsed microseconds
int64_t fcx_timer_stop_us(int64_t timer_id) {
    int64_t ns = fcx_timer_stop_ns(timer_id);
    return ns >= 0 ? ns / 1000 : -1;
}

// Stop timer and return elapsed milliseconds
int64_t fcx_timer_stop_ms(int64_t timer_id) {
    int64_t ns = fcx_timer_stop_ns(timer_id);
    return ns >= 0 ? ns / 1000000 : -1;
}

// Stop timer and return elapsed CPU cycles
int64_t fcx_timer_stop_cycles(int64_t timer_id) {
    if (timer_id < 0 || timer_id >= MAX_TIMERS || !g_timers[timer_id].active) {
        return -1;
    }
    uint64_t elapsed = fcx_cycles() - g_timers[timer_id].start_cycles;
    g_timers[timer_id].active = false;
    return (int64_t)elapsed;
}

// Read timer without stopping (peek)
int64_t fcx_timer_elapsed_ns(int64_t timer_id) {
    if (timer_id < 0 || timer_id >= MAX_TIMERS || !g_timers[timer_id].active) {
        return -1;
    }
    return fcx_time_ns() - g_timers[timer_id].start_ns;
}

// Reset timer (restart from now)
void fcx_timer_reset(int64_t timer_id) {
    if (timer_id >= 0 && timer_id < MAX_TIMERS && g_timers[timer_id].active) {
        g_timers[timer_id].start_ns = fcx_time_ns();
        g_timers[timer_id].start_cycles = fcx_cycles();
    }
}

// ============================================================================
// Simple Inline Timing (no state)
// ============================================================================

// These are for simple timing without managing timer IDs
static __thread int64_t g_simple_start_ns = 0;
static __thread uint64_t g_simple_start_cycles = 0;

void fcx_tick(void) {
    g_simple_start_ns = fcx_time_ns();
    g_simple_start_cycles = fcx_cycles();
}

int64_t fcx_tock_ns(void) {
    return fcx_time_ns() - g_simple_start_ns;
}

int64_t fcx_tock_us(void) {
    return fcx_tock_ns() / 1000;
}

int64_t fcx_tock_ms(void) {
    return fcx_tock_ns() / 1000000;
}

int64_t fcx_tock_cycles(void) {
    return (int64_t)(fcx_cycles() - g_simple_start_cycles);
}

// ============================================================================
// Benchmark Helpers
// ============================================================================

// Print timing result with label
void fcx_print_timing(const char* label, int64_t ns) {
    fcx_print_str(label);
    fcx_print_str(": ");
    
    if (ns >= 1000000000) {
        // Print as seconds
        fcx_print_int(ns / 1000000000);
        fcx_print_str(".");
        int64_t frac = (ns % 1000000000) / 1000000;
        if (frac < 100) fcx_print_str("0");
        if (frac < 10) fcx_print_str("0");
        fcx_print_int(frac);
        fcx_print_str(" s");
    } else if (ns >= 1000000) {
        // Print as milliseconds
        fcx_print_int(ns / 1000000);
        fcx_print_str(".");
        int64_t frac = (ns % 1000000) / 1000;
        if (frac < 100) fcx_print_str("0");
        if (frac < 10) fcx_print_str("0");
        fcx_print_int(frac);
        fcx_print_str(" ms");
    } else if (ns >= 1000) {
        // Print as microseconds
        fcx_print_int(ns / 1000);
        fcx_print_str(".");
        int64_t frac = ns % 1000;
        if (frac < 100) fcx_print_str("0");
        if (frac < 10) fcx_print_str("0");
        fcx_print_int(frac);
        fcx_print_str(" us");
    } else {
        // Print as nanoseconds
        fcx_print_int(ns);
        fcx_print_str(" ns");
    }
    
    fcx_print_newline();
}

// ============================================================================
// FCx Runtime Exports (underscore-prefixed for linker)
// ============================================================================

// Time getters
int64_t _fcx_time_ns(void) { return fcx_time_ns(); }
int64_t _fcx_time_us(void) { return fcx_time_us(); }
int64_t _fcx_time_ms(void) { return fcx_time_ms(); }
int64_t _fcx_cycles(void) { return (int64_t)fcx_cycles(); }

// Timer management
int64_t _fcx_timer_start(void) { return fcx_timer_start(); }
int64_t _fcx_timer_stop_ns(int64_t id) { return fcx_timer_stop_ns(id); }
int64_t _fcx_timer_stop_us(int64_t id) { return fcx_timer_stop_us(id); }
int64_t _fcx_timer_stop_ms(int64_t id) { return fcx_timer_stop_ms(id); }
int64_t _fcx_timer_stop_cycles(int64_t id) { return fcx_timer_stop_cycles(id); }
int64_t _fcx_timer_elapsed_ns(int64_t id) { return fcx_timer_elapsed_ns(id); }
void _fcx_timer_reset(int64_t id) { fcx_timer_reset(id); }

// Simple tick/tock
void _fcx_tick(void) { fcx_tick(); }
int64_t _fcx_tock_ns(void) { return fcx_tock_ns(); }
int64_t _fcx_tock_us(void) { return fcx_tock_us(); }
int64_t _fcx_tock_ms(void) { return fcx_tock_ms(); }
int64_t _fcx_tock_cycles(void) { return fcx_tock_cycles(); }

// Print timing
void _fcx_print_timing(const char* label, int64_t ns) { fcx_print_timing(label, ns); }
