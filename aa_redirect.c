#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * LD_PRELOAD shim: redirect old AppArmor kernel interface path to new one.
 *
 * AppArmor 4.x moved hat-transition writes from:
 *   /proc/self/attr/current          (or /proc/<PID>/attr/current)
 * to:
 *   /proc/self/attr/apparmor/current (or /proc/<PID>/attr/apparmor/current)
 *
 * libapparmor fixed aa_change_hat() but not aa_change_hatv(). Depending on
 * the build, aa_change_hatv() may use the literal "self" string or getpid()
 * to construct a numeric-PID path. Both forms are handled here.
 *
 * Designed to be tiny and "surgical".
 */

/* Thread-local buffer for dynamically constructed /proc/<PID>/... paths. */
static __thread char _rewrite_buf[48];

/* Thread-local recursion guard. */
static __thread int in_hook = 0;

static inline const char *rewrite_path(const char *path) {
    if (!path) return path;

    /* Case 1: literal self path */
    if (strcmp(path, "/proc/self/attr/current") == 0)
        return "/proc/self/attr/apparmor/current";

    /* Case 2: numeric PID path — /proc/<digits>/attr/current
     * libapparmor uses getpid() to construct the path, producing e.g.
     * /proc/38696/attr/current instead of /proc/self/attr/current.
     */
    if (strncmp(path, "/proc/", 6) == 0) {
        const char *p = path + 6;
        const char *pid_start = p;
        while (*p >= '0' && *p <= '9') p++;
        if (p > pid_start && strcmp(p, "/attr/current") == 0) {
            int n = snprintf(_rewrite_buf, sizeof(_rewrite_buf),
                             "/proc/%.*s/attr/apparmor/current",
                             (int)(p - pid_start), pid_start);
            if (n > 0 && n < (int)sizeof(_rewrite_buf))
                return _rewrite_buf;
        }
    }

    return path;
}

/* --- real function typedefs --- */
typedef int (*open_fn)(const char *pathname, int flags, ...);
typedef int (*openat_fn)(int dirfd, const char *pathname, int flags, ...);

/* Resolve symbol once, lazily. */
static inline void *resolve_next(const char *sym) {
    return dlsym(RTLD_NEXT, sym);
}

/* Common helper for open/open64 and friends (varargs). */
static inline int call_open_like(const char *sym, const char *pathname, int flags, va_list ap) {
    open_fn real = (open_fn)resolve_next(sym);
    const char *p = rewrite_path(pathname);

    if (flags & O_CREAT) {
        mode_t mode = (mode_t)va_arg(ap, int);
        return real(p, flags, mode);
    }
    return real(p, flags);
}

/* Common helper for openat/openat64 and friends (varargs). */
static inline int call_openat_like(const char *sym, int dirfd, const char *pathname, int flags, va_list ap) {
    openat_fn real = (openat_fn)resolve_next(sym);
    const char *p = rewrite_path(pathname);

    if (flags & O_CREAT) {
        mode_t mode = (mode_t)va_arg(ap, int);
        return real(dirfd, p, flags, mode);
    }
    return real(dirfd, p, flags);
}

/* --- exported interposed symbols --- */

int open(const char *pathname, int flags, ...) {
    if (in_hook) {
        va_list ap;
        va_start(ap, flags);
        int r = call_open_like("open", pathname, flags, ap);
        va_end(ap);
        return r;
    }

    in_hook = 1;
    va_list ap;
    va_start(ap, flags);
    int r = call_open_like("open", pathname, flags, ap);
    va_end(ap);
    in_hook = 0;
    return r;
}

int open64(const char *pathname, int flags, ...) {
    if (in_hook) {
        va_list ap;
        va_start(ap, flags);
        int r = call_open_like("open64", pathname, flags, ap);
        va_end(ap);
        return r;
    }

    in_hook = 1;
    va_list ap;
    va_start(ap, flags);
    int r = call_open_like("open64", pathname, flags, ap);
    va_end(ap);
    in_hook = 0;
    return r;
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    if (in_hook) {
        va_list ap;
        va_start(ap, flags);
        int r = call_openat_like("openat", dirfd, pathname, flags, ap);
        va_end(ap);
        return r;
    }

    in_hook = 1;
    va_list ap;
    va_start(ap, flags);
    int r = call_openat_like("openat", dirfd, pathname, flags, ap);
    va_end(ap);
    in_hook = 0;
    return r;
}

int openat64(int dirfd, const char *pathname, int flags, ...) {
    if (in_hook) {
        va_list ap;
        va_start(ap, flags);
        int r = call_openat_like("openat64", dirfd, pathname, flags, ap);
        va_end(ap);
        return r;
    }

    in_hook = 1;
    va_list ap;
    va_start(ap, flags);
    int r = call_openat_like("openat64", dirfd, pathname, flags, ap);
    va_end(ap);
    in_hook = 0;
    return r;
}

/*
 * glibc FORTIFY entrypoints sometimes get used depending on build flags.
 * They are NOT variadic; they do not accept mode. We can safely rewrite path
 * and tail-call to the real symbol.
 */
typedef int (*open_2_fn)(const char *pathname, int flags);
typedef int (*openat_2_fn)(int dirfd, const char *pathname, int flags);

int __open_2(const char *pathname, int flags) {
    open_2_fn real = (open_2_fn)resolve_next("__open_2");
    const char *p = rewrite_path(pathname);
    return real(p, flags);
}

int __open64_2(const char *pathname, int flags) {
    open_2_fn real = (open_2_fn)resolve_next("__open64_2");
    const char *p = rewrite_path(pathname);
    return real(p, flags);
}

int __openat_2(int dirfd, const char *pathname, int flags) {
    openat_2_fn real = (openat_2_fn)resolve_next("__openat_2");
    const char *p = rewrite_path(pathname);
    return real(dirfd, p, flags);
}
