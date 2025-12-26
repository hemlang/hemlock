/*
 * Hemlock Signal Compatibility Layer
 *
 * Provides cross-platform signal handling:
 * - POSIX signals on Unix/Linux/macOS
 * - Console control handlers on Windows
 *
 * NOTE: Windows has very limited signal support. Only SIGINT (Ctrl+C),
 * SIGTERM, and SIGABRT are supported. SIGUSR1, SIGUSR2, SIGALRM, etc.
 * are NOT available on Windows.
 */

#ifndef HML_COMPAT_SIGNALS_H
#define HML_COMPAT_SIGNALS_H

#include "platform.h"

#ifdef HML_WINDOWS

/* ========== Windows Implementation ========== */

#include <windows.h>
#include <signal.h>

/*
 * Windows only supports a few signals:
 * - SIGINT  (2)  - Ctrl+C
 * - SIGILL  (4)  - Illegal instruction
 * - SIGFPE  (8)  - Floating point exception
 * - SIGSEGV (11) - Segmentation fault
 * - SIGTERM (15) - Termination request
 * - SIGABRT (22) - Abort
 *
 * We map Hemlock signal numbers to these where possible.
 */

/* Signal numbers (matching POSIX where applicable) */
#ifndef SIGHUP
#define SIGHUP    1   /* Not supported on Windows */
#endif
#ifndef SIGINT
#define SIGINT    2
#endif
#ifndef SIGQUIT
#define SIGQUIT   3   /* Not supported on Windows */
#endif
#ifndef SIGKILL
#define SIGKILL   9   /* Not supported on Windows */
#endif
#ifndef SIGUSR1
#define SIGUSR1   10  /* Not supported on Windows */
#endif
#ifndef SIGUSR2
#define SIGUSR2   12  /* Not supported on Windows */
#endif
#ifndef SIGPIPE
#define SIGPIPE   13  /* Not supported on Windows */
#endif
#ifndef SIGALRM
#define SIGALRM   14  /* Not supported on Windows */
#endif
#ifndef SIGTERM
#define SIGTERM   15
#endif
#ifndef SIGCHLD
#define SIGCHLD   17  /* Not supported on Windows */
#endif
#ifndef SIGCONT
#define SIGCONT   18  /* Not supported on Windows */
#endif
#ifndef SIGSTOP
#define SIGSTOP   19  /* Not supported on Windows */
#endif

/* Signal handler type */
typedef void (*hml_sighandler_t)(int);

/* Special signal handlers */
#define HML_SIG_DFL ((hml_sighandler_t)0)
#define HML_SIG_IGN ((hml_sighandler_t)1)
#define HML_SIG_ERR ((hml_sighandler_t)-1)

/* Storage for custom signal handlers */
static hml_sighandler_t hml_sigint_handler = NULL;
static hml_sighandler_t hml_sigterm_handler = NULL;

/* Windows console control handler */
static BOOL WINAPI hml_console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
            if (hml_sigint_handler && hml_sigint_handler != HML_SIG_DFL && hml_sigint_handler != HML_SIG_IGN) {
                hml_sigint_handler(SIGINT);
                return TRUE;
            }
            if (hml_sigint_handler == HML_SIG_IGN) {
                return TRUE;  /* Ignore */
            }
            return FALSE;  /* Use default handler */

        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (hml_sigterm_handler && hml_sigterm_handler != HML_SIG_DFL && hml_sigterm_handler != HML_SIG_IGN) {
                hml_sigterm_handler(SIGTERM);
                return TRUE;
            }
            return FALSE;

        case CTRL_BREAK_EVENT:
            /* Similar to Ctrl+C */
            if (hml_sigint_handler && hml_sigint_handler != HML_SIG_DFL && hml_sigint_handler != HML_SIG_IGN) {
                hml_sigint_handler(SIGINT);
                return TRUE;
            }
            return FALSE;

        default:
            return FALSE;
    }
}

/* Initialize signal handling (call once at startup) */
HML_INLINE void hml_signal_init(void) {
    static int initialized = 0;
    if (!initialized) {
        SetConsoleCtrlHandler(hml_console_ctrl_handler, TRUE);
        initialized = 1;
    }
}

/* Set signal handler */
HML_INLINE hml_sighandler_t hml_signal(int sig, hml_sighandler_t handler) {
    hml_signal_init();

    hml_sighandler_t old_handler = HML_SIG_DFL;

    switch (sig) {
        case SIGINT:
            old_handler = hml_sigint_handler ? hml_sigint_handler : HML_SIG_DFL;
            hml_sigint_handler = handler;
            break;

        case SIGTERM:
            old_handler = hml_sigterm_handler ? hml_sigterm_handler : HML_SIG_DFL;
            hml_sigterm_handler = handler;
            break;

        case SIGABRT:
        case SIGFPE:
        case SIGILL:
        case SIGSEGV:
            /* Use standard C signal() for these */
            return signal(sig, handler);

        default:
            /* Unsupported signal - return error */
            return HML_SIG_ERR;
    }

    return old_handler;
}

/* Raise a signal */
HML_INLINE int hml_raise(int sig) {
    switch (sig) {
        case SIGINT:
            if (hml_sigint_handler && hml_sigint_handler != HML_SIG_DFL && hml_sigint_handler != HML_SIG_IGN) {
                hml_sigint_handler(SIGINT);
                return 0;
            }
            break;

        case SIGTERM:
            if (hml_sigterm_handler && hml_sigterm_handler != HML_SIG_DFL && hml_sigterm_handler != HML_SIG_IGN) {
                hml_sigterm_handler(SIGTERM);
                return 0;
            }
            break;

        case SIGABRT:
        case SIGFPE:
        case SIGILL:
        case SIGSEGV:
            return raise(sig);

        default:
            /* Unsupported signal */
            return -1;
    }

    return raise(sig);
}

/* sigaction is not supported on Windows - use hml_signal instead */
struct hml_sigaction {
    hml_sighandler_t sa_handler;
    int sa_flags;
};

#define SA_RESTART 0  /* Not meaningful on Windows */

HML_INLINE int hml_sigaction(int sig, const struct hml_sigaction *act, struct hml_sigaction *oldact) {
    if (oldact) {
        oldact->sa_handler = hml_signal(sig, act->sa_handler);
        oldact->sa_flags = 0;
    } else {
        hml_signal(sig, act->sa_handler);
    }
    return 0;
}

/* Signal set operations (no-op on Windows) */
typedef unsigned long hml_sigset_t;

HML_INLINE int hml_sigemptyset(hml_sigset_t *set) {
    *set = 0;
    return 0;
}

HML_INLINE int hml_sigfillset(hml_sigset_t *set) {
    *set = ~0UL;
    return 0;
}

HML_INLINE int hml_sigaddset(hml_sigset_t *set, int sig) {
    *set |= (1UL << sig);
    return 0;
}

HML_INLINE int hml_sigdelset(hml_sigset_t *set, int sig) {
    *set &= ~(1UL << sig);
    return 0;
}

HML_INLINE int hml_sigismember(const hml_sigset_t *set, int sig) {
    return (*set & (1UL << sig)) != 0;
}

/* Check if a signal is supported on this platform */
HML_INLINE int hml_signal_supported(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
        case SIGABRT:
        case SIGFPE:
        case SIGILL:
        case SIGSEGV:
            return 1;
        default:
            return 0;
    }
}

#else /* POSIX Implementation */

/* ========== POSIX Implementation ========== */

#include <signal.h>

/* Signal handler type */
typedef void (*hml_sighandler_t)(int);

/* Special signal handlers */
#define HML_SIG_DFL SIG_DFL
#define HML_SIG_IGN SIG_IGN
#define HML_SIG_ERR SIG_ERR

/* Initialize signal handling (no-op on POSIX) */
HML_INLINE void hml_signal_init(void) {
    /* Nothing to do */
}

/* Set signal handler */
HML_INLINE hml_sighandler_t hml_signal(int sig, hml_sighandler_t handler) {
    return signal(sig, handler);
}

/* Raise a signal */
HML_INLINE int hml_raise(int sig) {
    return raise(sig);
}

/* sigaction wrapper - use hml_handler to avoid sa_handler macro conflict */
struct hml_sigaction {
    void (*hml_handler)(int);
    sigset_t hml_mask;
    int hml_flags;
};

HML_INLINE int hml_sigaction(int sig, const struct hml_sigaction *act, struct hml_sigaction *oldact) {
    struct sigaction posix_act, posix_oldact;

    if (act) {
        posix_act.sa_handler = act->hml_handler;
        posix_act.sa_mask = act->hml_mask;
        posix_act.sa_flags = act->hml_flags;
    }

    int result = sigaction(sig, act ? &posix_act : NULL, oldact ? &posix_oldact : NULL);

    if (oldact && result == 0) {
        oldact->hml_handler = posix_oldact.sa_handler;
        oldact->hml_mask = posix_oldact.sa_mask;
        oldact->hml_flags = posix_oldact.sa_flags;
    }

    return result;
}

/* Signal set operations - use sig_ prefix, threading.h has hml_ versions */
typedef sigset_t hml_sigset_t;

HML_INLINE int hml_sig_emptyset(hml_sigset_t *set) {
    return sigemptyset(set);
}

HML_INLINE int hml_sig_fillset(hml_sigset_t *set) {
    return sigfillset(set);
}

HML_INLINE int hml_sigaddset(hml_sigset_t *set, int sig) {
    return sigaddset(set, sig);
}

HML_INLINE int hml_sigdelset(hml_sigset_t *set, int sig) {
    return sigdelset(set, sig);
}

HML_INLINE int hml_sigismember(const hml_sigset_t *set, int sig) {
    return sigismember(set, sig);
}

/* NSIG may not be defined on all systems */
#ifndef NSIG
#ifdef _NSIG
#define NSIG _NSIG
#else
#define NSIG 64
#endif
#endif

/* Check if a signal is supported (all signals are supported on POSIX) */
HML_INLINE int hml_signal_supported(int sig) {
    return sig > 0 && sig < NSIG;
}

#endif /* HML_WINDOWS */

#endif /* HML_COMPAT_SIGNALS_H */
