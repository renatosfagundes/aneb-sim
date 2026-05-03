# com0com — signed driver bundle

This folder ships the **signed** redistribution of com0com (the open-
source null-modem virtual serial port driver), produced by Pete
Batard and originally hosted at:

  https://pete.akeo.ie/2011/07/com0com-signed-drivers.html

The driver itself is com0com 3.0.0.0 by Vyacheslav Frolov; the
.cat catalogue file in `x64/` carries Pete's counter-signature so
Windows 10/11 will load it without test-signing or driver-signature-
enforcement workarounds.

License: GPLv2 (same as upstream com0com).

The aneb-sim project ships this bundle so the `setup_com.bat /
setup_com.ps1` script can install working virtual COM port pairs out
of the box on a student machine — no separate download needed, no
driver-signing surgery.

## Layout

```
com0com_signed/
├── x64/             64-bit driver bundle (modern Windows)
│   ├── com0com.cat
│   ├── com0com.inf
│   ├── com0com.sys
│   ├── setup.dll
│   └── setupc.exe
├── i386/            32-bit driver bundle (legacy Windows)
└── ReadMe.txt       upstream com0com README
```

`setup_com.ps1` invokes `x64\setupc.exe` directly to install the
driver and create COM-port pairs — no other prerequisites.
