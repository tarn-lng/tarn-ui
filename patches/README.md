# Experimental dependency patches

`fontconfig-2.18.3-lsan-reachability.patch` addresses conservative-scanner
reachability of heap `FcPattern` element arrays. Heap patterns retain real
pointers; constant cached patterns retain relative offsets. It does not free or
ignore allocations and adds no sanitizer suppression or trusted allocation.

This is a local diagnostic candidate, not an installed dependency or a production
build requirement. It has not been submitted or accepted upstream. See
[memory status](../docs/memory-status.md) for measured results and limitations.

The tested source is the Fontconfig 2.18.3 tag archive:
`https://codeload.github.com/fontconfig/fontconfig/tar.gz/refs/tags/2.18.3`.
SHA-256: `9ae01e1d53acdef56010c5451cd34aa41d325b2faccd8606448d8fa01b2496b3`.

From a fresh extracted copy, apply with `patch -p1` and configure a separate
Meson build. No installation or administrator access is required:

```bash
patch -d /absolute/fontconfig-2.18.3 -p1 < patches/fontconfig-2.18.3-lsan-reachability.patch
meson setup /absolute/fontconfig-build /absolute/fontconfig-2.18.3 \
  --buildtype=debug --sysconfdir=/etc -Ddoc=disabled -Dnls=disabled \
  -Dtests=enabled -Dtests-external-fonts=disabled -Dtools=enabled -Dcache-build=disabled
meson compile -C /absolute/fontconfig-build -j 2
meson test -C /absolute/fontconfig-build --print-errorlogs -j 2
bash scripts/test-fontconfig-reachability.sh /absolute/fontconfig-build
LD_LIBRARY_PATH=/absolute/fontconfig-build EVIDENCE_DIR="$PWD/.build/evidence/fontconfig-candidate" \
  bash scripts/investigate-gtk-leaks.sh
```

The last command still exits 1 on the measured host because Mesa/EGL findings
remain with the default renderer. Normal project builds continue to use the
system libraries. Cache interoperability was checked in both directions with
one isolated TrueType font; broader platform/distribution validation is pending.
