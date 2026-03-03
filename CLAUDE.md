# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A temporary `LD_PRELOAD` shim for an upstream `libapparmor` 4.1.6 bug: `aa_change_hatv()` writes to the legacy path `/proc/self/attr/current` instead of the AppArmor 4.x path `/proc/self/attr/apparmor/current`, causing `EPERM` during per-request hat switching in `mod_apparmor`.

The shim intercepts `open`/`openat` (and their `64`/FORTIFY variants) and rewrites the path. It handles both the `self` literal form and the numeric-PID form (`/proc/<PID>/attr/current`) that `libapparmor` may construct via `getpid()`.

## Build

```bash
make              # produces libaa_redirect.so
make clean
```

Compiler flags: `-O2 -fPIC -Wall -Wextra -shared -ldl`. Override with `CC`, `CFLAGS`, `LDFLAGS`.

## Quick verification

```bash
LD_PRELOAD=$PWD/libaa_redirect.so cat /proc/self/attr/current
```

## Architecture

Single-file C library (`aa_redirect.c` → `libaa_redirect.so`):

- `rewrite_path()` — pure path rewriter, handles both `/proc/self/attr/current` and `/proc/<PID>/attr/current`. Uses a thread-local buffer (`_rewrite_buf[48]`) for the dynamic PID form.
- `in_hook` — thread-local recursion guard to prevent re-entrant interposition.
- Exported hooks: `open`, `open64`, `openat`, `openat64`, `__open_2`, `__open64_2`, `__openat_2` — all delegate to the real symbol via `dlsym(RTLD_NEXT, ...)`.

## Exit criteria

Remove this shim once `libapparmor` is updated to fix `aa_change_hatv()`. The shim is safe to leave loaded after the upstream fix (behavior stays equivalent for this one path), but removing it is preferred. See `README.md § Exit criteria` for the full checklist.

Incident context and strace evidence are in `BUGS.md`.
