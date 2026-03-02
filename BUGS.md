# Known Bugs and Workarounds

## BUG-001: libapparmor 4.1.6 — `aa_change_hatv` uses stale kernel interface path

**Status:** Open (upstream bug)
**Discovered:** 2026-02-28
**Affects:** `hp-www.i7.datacore.ch`
**Symptoms:** Apache error log floods with:
```
[apparmor:warn] aa_change_hatv call failed for 'datacore.ch' (errno: 1, EPERM)
[apparmor:error] Failed to change_hat to 'HANDLING_UNTRUSTED_INPUT' (errno: 1, EPERM)
```
The site itself works (HTTP 200), but no AppArmor hat isolation is active.

### Background

AppArmor 4.x moved the kernel interface for hat transitions from:
- **Old path:** `/proc/self/attr/current`
- **New path:** `/proc/self/attr/apparmor/current`

libapparmor was updated to use the new path in `aa_change_hat()`, but **`aa_change_hatv()` was not updated** and still uses the old path. The old path returns `EPERM` for hat-entry writes under AppArmor 4.x (it still works for unconfined → hat transitions from root, but not from within a confined process).

### How mod_apparmor uses these functions

mod_apparmor (Apache module) has three hat-switching call sites:

| Function | Call site | API used | Result |
|----------|-----------|----------|--------|
| `aa_child_init` | Worker process startup → enter `HANDLING_UNTRUSTED_INPUT` | `aa_change_hat()` | **Succeeds** (new path) |
| `aa_enter_hat` | Per-request → enter vhost hat (e.g. `datacore.ch`) | `aa_change_hatv()` | **FAILS** (old path, EPERM) |
| `aa_exit_hat` | Post-request → revert to `HANDLING_UNTRUSTED_INPUT` | `aa_change_hat()` | Fails (wrong state after aa_enter_hat failure) |

Because `aa_child_init` fails to enter `HANDLING_UNTRUSTED_INPUT` (the module sets `inside_default_hat=0` on failure), mod_apparmor also skips the `aa_enter_hat` call entirely for some code paths, meaning **no vhost hat is ever entered**.

### Confirmed via strace

strace of Apache worker process (pid 899707) showed:

```
# aa_enter_hat → aa_change_hatv → writes to OLD path (fd 115 = /proc/self/attr/current)
write(115, "changehat c5c555c805c33186^amir."..., 77) = -1 EPERM

# aa_exit_hat → aa_change_hat → writes to NEW path (fd 116 = /proc/self/attr/apparmor/current)
write(116, "changehat c5c555c805c33186^\0", 28) = 28   ← success (NULL revert)
write(116, "changehat c5c555c805c33186^HANDL"..., 51) = -1 EPERM  ← fails because state is wrong
```

Python test of `aa_change_hat()` directly (correct new path):
```
openat(AT_FDCWD, "/proc/906385/attr/apparmor/current", O_WRONLY) = 3
write(3, "changehat 0000000000003039^HANDL"..., 51) = 51   ← success
```

### Environment

- **OS:** Arch Linux
- **Kernel:** 6.x with AppArmor 4.x
- **libapparmor:** 4.1.6
- **Apache:** `/usr/bin/httpd` (profile: `httpd-bin`)
- **AppArmor feature:** `features/domain/change_hat: yes`, `features/domain/version: 1.2`
- **mod_apparmor:** loaded via `LoadModule apparmor_module modules/mod_apparmor.so`

### Current workaround

None applied yet. The Apache profile hats (`apache2.d/datacore.ch`, `apache2.d/a.guindehi.ch`) remain in place but provide no isolation until the bug is fixed.

To suppress the error log spam until a fixed libapparmor is released, disable mod_apparmor:
- Set `apache_mod_apparmor_enable: false` in `host_vars/hp-www.i7.datacore.ch.yml`
- Remove `APPARMOR` from `apache_defines` for that host

Re-enable once `aa_change_hatv` is fixed upstream and the package is updated.

### Upstream fix

The fix in libapparmor is to make `aa_change_hatv()` open `/proc/self/attr/apparmor/current` instead of `/proc/self/attr/current`, consistent with how `aa_change_hat()` was already fixed.

Candidate upstream location: `https://gitlab.com/apparmor/apparmor`

### Notes

- Running the profile in **complain mode** does not fix the EPERM — this is a kernel interface path mismatch, not a policy denial.
- `aa-status` correctly shows all hats loaded (policy side is fine).
- The `apparmor="DENIED" operation="ptrace"` messages seen during debugging are unrelated — they come from the profile blocking strace, not from hat switching.
- The Apache profile (`usr.bin.httpd` / `usr.sbin.httpd`) was modified during debugging to add `^HANDLING_UNTRUSTED_INPUT` hats and allow `/proc/@{pid}/attr/apparmor/current rw`. These changes are correct and should be kept for when the upstream bug is fixed.
