# Wine PE donor experiment

These five DLLs are unmodified Debian libwine 10.0~repack-6 x86-64 Windows
artifacts. SHA256.json pins the bytes copied from
/usr/lib/x86_64-linux-gnu/wine/x86_64-windows. They are diagnostic inputs until
strict import closure and initialization succeed on Xbox; they are not shipped
as working donor routes merely because the mapper accepts their sections.

License: LGPL-2.1-or-later (LICENSE and COPYING.LIB). Corresponding Debian source:
https://deb.debian.org/debian/pool/main/w/wine/wine_10.0~repack.orig.tar.xz
https://deb.debian.org/debian/pool/main/w/wine/wine_10.0~repack-6.debian.tar.xz
Wine 11 source in /workspace/research/wine is a behavior reference, not the
corresponding source for these Wine 10 binaries.

imports.h includes both direct and delay imports parsed from these exact PEs.
The strict probe reports every unresolved dependency before mapping and never
uses MLP failure placeholders. The runtime gate initializes each donor through
the existing W^X/unwind mapper and checks a representative behavior. Game entry
points remain uncalled. avrt is checked only for its documented invalid-task
failure; Wine scheduling success placeholders are not claimed as a backend.

Acceptance on Xbox Series S, launcher 0.5.51.0:
`python3 xbox-test/app/donors/check.py xbox-test/app/donors/evidence/console-launch-probe.log`
requires all five executed behavior checks, exactly 22 donor IAT routes and a
remaining historical STUB/MISSING surface <=40. Final result: 26 overall, 7 for
hid/setupapi/winmm. The count reduction includes inherited native tail work;
donor execution is verified independently. See LANE_NOTES.md for full limits.
