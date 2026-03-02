# aa-preload-shim (AppArmor /proc attr redirect)

Temporary runtime shim to work around an upstream `libapparmor` path bug affecting
`mod_apparmor` hat transitions on AppArmor 4.x systems.

## Why this project exists

On AppArmor 4.x, the kernel hat-transition interface moved to:

`/proc/self/attr/apparmor/current`

However, in `libapparmor` 4.1.6, `aa_change_hatv()` still writes to the old path:

`/proc/self/attr/current`

For `mod_apparmor`, that mismatch causes `EPERM` during per-request hat switching
(for example, entering a vhost hat). The website can still return HTTP 200, but
the expected AppArmor hat isolation is not active.

See `BUGS.md` for the full incident details, traces, environment, and upstream
status.

## What this shim does

This project builds a shared library (`libaa_redirect.so`) that is loaded via
`LD_PRELOAD`.

At runtime, it intercepts exact opens of:

`/proc/self/attr/current`

and redirects them to:

`/proc/self/attr/apparmor/current`

This allows affected userspace code paths (including `aa_change_hatv()` callers)
to reach the correct AppArmor 4.x kernel interface without patching every caller
immediately.

## Scope and intent

- Intended as a hotfix/bridge while waiting for upstream `libapparmor` fix and
  package rollout.
- Keeps policy and module wiring intact so hat transitions can work again in
  affected environments.
- Designed to be surgical: redirect only applies to the exact legacy path.

## Build

```bash
make
```

Output:

- `libaa_redirect.so`

## Quick local check

```bash
LD_PRELOAD=$PWD/libaa_redirect.so cat /proc/self/attr/current
```

If the shim is active, opens targeting `/proc/self/attr/current` are transparently
redirected to `/proc/self/attr/apparmor/current`.

## Deployment example (systemd, Arch `httpd`)

Install the shim:

```bash
sudo install -D -m 0755 libaa_redirect.so /opt/shims/libaa_redirect.so
```

Create a service override:

```bash
sudo systemctl edit httpd
```

Add:

```ini
[Service]
Environment=LD_PRELOAD=/opt/shims/libaa_redirect.so
```

Apply and restart:

```bash
sudo systemctl daemon-reload
sudo systemctl restart httpd
```

## How this helps with current AppArmor issues

In the bug scenario documented in `BUGS.md`, `mod_apparmor` request-time hat entry
fails because `aa_change_hatv()` targets the stale proc path. This shim redirects
that access to the correct AppArmor 4.x path, which mitigates the `EPERM` caused
by interface mismatch and restores the intended chance for hat transitions to
succeed.

In short:

- Root cause: stale proc interface path in `aa_change_hatv()`.
- Effect: per-request hat transitions fail, isolation is effectively bypassed.
- Mitigation: runtime redirect from old path to new path via `LD_PRELOAD`.

## Verification

After enabling the shim for `httpd`, use the checks below.

1) Confirm the service is running with `LD_PRELOAD`:

```bash
systemctl show httpd -p Environment
```

Expected: output includes `LD_PRELOAD=/opt/shims/libaa_redirect.so`.

2) Confirm Apache workers loaded the shim:

```bash
pid=$(pgrep -n httpd)
grep libaa_redirect.so /proc/$pid/maps
```

Expected: at least one mapping line for `libaa_redirect.so`.

3) Send a few requests through Apache:

```bash
curl -fsS http://127.0.0.1/ >/dev/null
curl -fsS -H 'Host: datacore.ch' http://127.0.0.1/ >/dev/null
```

4) Check logs for the previous error pattern:

```bash
journalctl -u httpd -n 200 --no-pager
```

Expected: no repeated lines like:

- `aa_change_hatv call failed ... (errno: 1, EPERM)`
- `Failed to change_hat to 'HANDLING_UNTRUSTED_INPUT' ... (errno: 1, EPERM)`

5) Optional policy sanity check:

```bash
aa-status
```

Expected: relevant Apache profile and hats are loaded.

If you still see the same `EPERM` spam, verify the unit override is applied to the
actual running service and that workers were fully restarted after `daemon-reload`.

## Limitations and tradeoffs

- Works only for dynamically linked processes that honor `LD_PRELOAD`.
- Not applied to setuid/setgid binaries (loader security restrictions).
- Process-local mitigation; not a replacement for upstream fix.
- Should be removed once fixed `libapparmor` is deployed.

## Compatibility

Validated in the incident environment documented in `BUGS.md`:

- OS: Arch Linux
- Kernel: Linux 6.18.13 with AppArmor 4.1.6-2
- AppArmor package: `apparmor 4.1.6-2`
- `libapparmor`: 1.24.3 (from `apparmor 4.1.6-2`)
- Apache package: `apache 2.4.66-1`
- Apache module: `/usr/lib/httpd/modules/mod_apparmor.so`
- Module ownership: `mod_apparmor.so` provided by `apparmor 4.1.6-2`

Known good behavior:

| Scenario | Expected |
|---|---|
| Dynamic process with `LD_PRELOAD` set | Redirect active |
| `mod_apparmor` on AppArmor 4.x hitting old proc path | Mitigated by redirect |

Known unsupported / no effect:

| Scenario | Why |
|---|---|
| Static binaries | No dynamic loader interposition |
| setuid/setgid binaries | `LD_PRELOAD` ignored for security |
| Processes without `LD_PRELOAD` | Shim never loaded |

## Security and risk notes

- This is a narrow runtime compatibility shim, not a policy bypass tool.
- It rewrites only one exact path string and leaves all other file opens untouched.
- Keep deployment scope minimal (only affected service units).
- Treat as temporary operational mitigation until upstream package fix lands.

## Troubleshooting

If verification still shows `EPERM` errors, check:

- `systemctl cat httpd` includes the override with the correct `LD_PRELOAD` path.
- `systemctl show httpd -p Environment` shows the same path at runtime.
- Worker processes were restarted after `daemon-reload`.
- File exists and is readable: `/opt/shims/libaa_redirect.so`.
- Target process is dynamically linked and not setuid/setgid.

Useful commands:

```bash
systemctl cat httpd
systemctl show httpd -p Environment
pid=$(pgrep -n httpd)
grep libaa_redirect.so /proc/$pid/maps
journalctl -u httpd -n 200 --no-pager
apachectl -M | grep apparmor
pacman -Qo /usr/lib/httpd/modules/mod_apparmor.so
```

## Impact of upstream fix

When upstream `libapparmor` is fixed (so `aa_change_hatv()` uses
`/proc/self/attr/apparmor/current` directly), this shim is no longer needed.

Important operational detail:

- The upstream fix does not break this shim.
- If left enabled, behavior remains functionally equivalent for this one path.
- Recommended action is still to remove `LD_PRELOAD` after upgrade to reduce
  moving parts and return to a standard runtime.

## Exit criteria (remove hotfix)

You can retire this hotfix when all of the following are true:

1. Installed `libapparmor` package includes the `aa_change_hatv()` path fix.
2. `LD_PRELOAD` override is removed from affected services.
3. Services are restarted.
4. Verification checks pass and prior `EPERM` hat-switch errors stay absent.
5. `BUGS.md` status is updated for your environment.

## Rollback

Remove the `LD_PRELOAD` environment override from the service and restart the
service.

## Upstream status

Expected permanent fix: update `libapparmor` `aa_change_hatv()` to use
`/proc/self/attr/apparmor/current`, consistent with `aa_change_hat()`.

Track details and context in `BUGS.md`.

## License

This project is licensed under the Apache License 2.0. See `LICENSE`.
