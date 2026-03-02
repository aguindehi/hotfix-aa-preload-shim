aa-preload-shim (AppArmor /proc attr redirect)
=================================================

Build
-----

  make

This produces:
  libaa_redirect.so

Quick test
----------

```
  LD_PRELOAD=$PWD/libaa_redirect.so cat /proc/self/attr/current
```

If your system uses LSM stacking and AppArmor expects the stable interface under:

```
  /proc/self/attr/apparmor/current
```

then this shim will transparently redirect exact opens of:

```
  /proc/self/attr/current
```

Systemd deployment (Arch httpd)
-------------------------------

```
  sudo install -D -m 0755 libaa_redirect.so /opt/shims/libaa_redirect.so
  sudo systemctl edit httpd
```

Add:

```
  [Service]
  Environment=LD_PRELOAD=/opt/shims/libaa_redirect.so
```

Then:

```
  sudo systemctl daemon-reload
  sudo systemctl restart httpd
```

Notes / tradeoffs
-----------------

- Works only for dynamically linked processes (LD_PRELOAD).
- Not used for setuid/setgid binaries (dynamic loader ignores LD_PRELOAD).
- Redirect is "surgical": only exact string match /proc/self/attr/current.
