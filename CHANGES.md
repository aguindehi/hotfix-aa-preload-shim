
# CHANGES.md

#### 2026-03-02 · `a3a3c38` · Document AppArmor hotfix rationale and operations

**architecture:** Expanded project documentation to explain why this shim exists: on AppArmor 4.x, `aa_change_hatv()` in `libapparmor` 4.1.6 still targets `/proc/self/attr/current`, which causes `EPERM` and prevents per-request hat isolation in `mod_apparmor` even when sites still return HTTP 200. Chose a surgical `LD_PRELOAD` path redirect as a temporary bridge while waiting for upstream packaging; trade-off accepted is dynamic-linker-only scope and extra service-level preload management.
**feature:** Added practical operator guidance in `README.md` for deployment, verification, troubleshooting, compatibility, rollback, and hotfix exit criteria, including tested package versions (`apache 2.4.66-1`, `apparmor 4.1.6-2`) and upstream-fix impact.
**admin:** Added `LICENSE` under Apache License 2.0 and included explicit license reference in `README.md`.
**admin:** Added `BUGS.md` to capture the incident record, reproduction evidence, and upstream-fix context that motivated this hotfix.
