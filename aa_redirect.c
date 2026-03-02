#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

/*
 * LD_PRELOAD shim: redirect exact path
 *   /proc/self/attr/current -> /proc/self/attr/apparmor/current
 *
 * Designed to be tiny and "surgical".
 */

static const char *FROM = "/proc/self/attr/current";
static const char *TO   = "/proc/self/attr/apparmor/current";

/* Thread-local recursion guard. */
static __thread int in_hook = 0;

static inline const char *rewrite_path(const char *path) {
    if (path && strcmp(path, FROM) == 0) return TO;
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
