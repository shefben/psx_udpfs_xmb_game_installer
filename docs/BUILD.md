# Building

## Environment

* A PS2DEV toolchain with PS2SDK (EE + IOP compilers, `bin2c`,
  `iopfixup`). Used here: `/usr/local/ps2dev/ps2dev` (EE GCC 15.2.0)
  under Ubuntu on WSL2. Docker is optional for normal builds.
* `make`, `patch`, `git`, `python3`, a native `cc` (host tests).
* For signed releases: `kelftool` on PATH (or `KELFTOOL=...`) and your
  own `PS2KEYS.dat`. Neither is shipped, searched for or committed.
* Only for `make driver`: Docker (pulls `ps2dev/ps2dev:v1.0`).

```sh
. tools/ps2env.sh        # PS2DEV/PS2SDK/PATH (override PS2DEV first if needed)
make references          # clone pinned upstream sources into reference/
make test                # host tests
make dev                 # unsigned development ELF: build/dev/
PS2KEYS=/absolute/path/to/PS2KEYS.dat make dist
```

On Windows, `tools/wsl-run.ps1 <command>` runs a command in the WSL
project directory with the environment sourced.

`reference/REVISIONS.txt` (committed) pins the upstream commits;
`make references` checks out exactly those, and every build verifies
the checkouts (`tools/fetch-references.sh --check`) and stops on drift.
`hdlfs.irx` and the HDD driver are additionally hash-checked against
`tools/driver/driver.env`. The pins match the implementation plan:

| Project | Commit |
|---|---|
| udpfsd | 58d7c8f11ac196d4a5ca7b65b78743fbeedfdd80 |
| neutrino | 7be8de2798c99af433c91993e021746a54d6800d |
| OPL-Launcher | 6da1af2e175c09da4cbb6da50ccf472132f8f114 |
| HDLGameInstaller | ec37c81f8edb6fc45e2be51c86a322780641275a |
| ps2-usbhdl | b681bc64a409753922d7d145ac05fa2f9039ce7f |
| hdl-dump | 32c296c69cf9c263fcbe035004aa28c345b3b279 |

## Release build graph (`make dist`)

Strictly in this order; each step depends on the previous file, so the
order cannot vary:

1. `build/opl-launcher/OPL-Launcher.elf` - pinned upstream, unmodified.
2. `build/kelf/opl-launcher-EXECUTE.KELF` - signed + verified.
3. `build/app/desr-udpfs-installer-app.elf` - the XMB application;
   embeds (2); does **not** contain its own KELF.
4. `build/kelf/installer-EXECUTE.KELF` - (3) signed + verified.
5. `build/bootstrap/desr-udpfs-installer-bootstrap.elf` - first-run
   executable; embeds (2) and (4).
6. `dist/` - both ELFs, both KELFs, `SHA256SUMS`, `BUILD-MANIFEST.txt`,
   docs and server examples.

Release ELFs are debug-stripped before signing; they are only replaced
when their bytes change, so an unchanged tree re-signs nothing.
`make test-graph` runs the whole graph with a fake kelftool in a private
directory and checks the order, what each ELF embeds, `dist/`, and the
no-re-sign property.

Variants share all sources (`-DVARIANT_APP/BOOTSTRAP/DEV`):

| Variant | OPL-Launcher KELF | Installer KELF | Shipped |
|---|---|---|---|
| bootstrap | embedded | embedded (the app KELF) | yes |
| app | embedded | its own `pfs0:/EXECUTE.KELF` (repair) | yes (as KELF) |
| dev | `udpfs:/PAYLOAD/` fallback only | `udpfs:/PAYLOAD/` fallback only | never |

A release variant that does not embed the OPL-Launcher KELF fails to
compile (`#error`), so a normal install never depends on the server.

## udpfsd server (`make udpfsd`)

`tools/build-udpfsd.sh` copies the pinned `reference/udpfsd`, applies
`patches/udpfsd/*.patch` in order (`0001`: `-install-dir`; `0002`:
`udpfsd.cfg`, read-only mounts, game preparation and the manifest the
installer reads), runs `go vet` and the Go tests of every package
except `chd` (needs CGO) and builds `build/udpfsd/udpfsd-windows-amd64.exe`
and `udpfsd-linux-amd64` (CGO off, so ISO/CSO/ZSO like upstream's
release binaries). It uses a local Go >= 1.25 or the digest-pinned
`golang:1.25` Docker image. `make dist` puts both binaries, the signed
`opl-launcher-EXECUTE.KELF` and the `udpfsd.cfg` template in
`dist/udpfsd/`; `make test-udpfsd` only tests.

To change the server patches, work in a scratch tree:

```sh
tools/udpfsd-patch.sh init 2     # reference + patches numbered < 0002
# edit build/udpfsd-dev/...
tools/udpfsd-patch.sh fmt        # gofmt the touched packages
tools/udpfsd-patch.sh test       # gofmt check, go vet, go test
tools/udpfsd-patch.sh save 0002-game-prep.patch
```

## Signing (`tools/kelf-sign.sh`)

```sh
PS2KEYS=/absolute/path/to/PS2KEYS.dat make kelfs     # or make dist
```

* `PS2KEYS` is required and must be absolute; it is used through a
  symlink in a private temporary `HOME`, never copied.
* `KELF_MODE=dnasload` (default):
  `kelftool encrypt dnasload <in> <out> --apptype=0B`, with
  [ps2homebrew/kelftool](https://github.com/ps2homebrew/kelftool)
  (05bbeb8; `make`, needs `libssl-dev`; pass it as `KELFTOOL=...`).
  This is the header of the KELFs known to start from PSX XMB channels:
  PFS-BatchKit-Manager's OPL-Launcher `boot.kelf` and `POPSTARTER.KELF`
  (UserDefined `01 00 00 04 00 06 00 4a 00 0e 01 00 00 00 00 02`,
  SystemType 0 = PS2, ApplicationType 11, Flags 0x22C, MG zones 0xFF).
  `kelf-sign.sh` checks those bytes after signing. A `PS2KEYS.dat` of
  bare `KEY=VALUE` lines (no `[default]` section) is selected with
  `--keys=` automatically.
* OPL-Launcher's README says `encrypt mbr`, but it means the old
  FMCB-compatible fork, whose `mbr` header is today's `dnasload` with
  application type 11. Today's `mbr` (`KELF_MODE=mbr`) writes a
  different header and is not used.
* `KELF_MODE=none`: `kelftool encrypt <in> <out>` with
  [xfwcfw/kelftool](https://github.com/xfwcfw/kelftool) (6b9b471), which
  always writes a PSX header (SystemType 1 = PSX, ApplicationType 1 =
  `xosdmain`, MG zone 1). v2.0's first signed build used it; on a DESR
  the installer channel then stayed on a black screen. Kept only for
  comparison. Any other mode fails.
  The mode is a build input (`build/.kelf-mode`): switching it re-signs
  both KELFs and rebuilds the bootstrap; `BUILD-MANIFEST.txt` records
  the mode the shipped KELFs were actually signed with.
* OPL-Launcher is signed without its debug sections
  (`OPL-Launcher-stripped.elf`, ~0.3 MB instead of 1.5 MB), like the app.
* Transactional: the old output is deleted first, the KELF is written to
  `<out>.tmp`, verified, then renamed. Verification: non-empty, >= 1 KiB,
  not an ELF, the header (dnasload), and `kelftool decrypt` (signature
  check) returns the input ELF plus at most 16 zero bytes of padding. On
  failure nothing is left behind and the exit code is non-zero.
  `test/host/test_kelf_sign.sh` covers every failure path.

## HDD driver (`make driver`)

`vendor/irx/ps2hdd-hdl.irx` is a reproducible legacy source build with
one documented source change; see `tools/driver/README.md`. Every
installer build checks its SHA-256 (`make driver-check`).

## Other adjustments to upstream code

* **Neutrino** (`patches/neutrino/0001-retonly-after-export-table.patch`):
  current PS2SDK links `exports.o` first, which puts `_retonly` at
  `.text` offset 0 and makes `iopfixup` reject smap/ministack/udpfs. The
  patch only moves the `_retonly` definition below the export table.
  Applied to a scratch copy in `build/neutrino`.
* **OPL-Launcher**: source unmodified. `tools/bin2s` replaces the
  `bin2s` tool current PS2SDK no longer ships, and `EE_CFLAGS` is set on
  the make command line to `-D_EE -G0 -O2 -Wno-stringop-truncation`
  because the current EE toolchain rejects `-G8192` with abicalls.

## IOP module order (as implemented)

The plan's section 7 order is kept, split in two halves so the static
IP can be read from the HDD before the network starts
(`src/iop_boot.c`):

1. `iop_boot_base`: IOP reset/sync, RPC, `sbv_patch_enable_lmb`,
   `sbv_patch_disable_prefix_check`, iomanX, fileXio (+ `fileXioInit`),
   poweroff, ps2dev9, ps2atad, ps2hdd-hdl (`-o 4 -n 128`), ps2fs
   (`-m 4 -o 10 -n 40`), hdlfs, then sio2man, padman.
2. Mount `PP.UDPF-00001..INSTALLER` at `pfs0:` and read `config/network.ini`.
3. `iop_boot_network`: smap, ministack `ip=<validated ip>`, udpfs_ioman
   (server discovery happens in its device init).

Invariants: one DEV9 stack (ps2dev9 once, before anything that uses
it); the HDD is available before settings are read; the network starts
only with a validated IP; every HDD write is disabled when any HDD
module failed (`hdd_ok`); the UDPFS browser only runs in
`NETWORK_READY`. Changing the IP reboots the IOP and runs both halves.

## Tests

`make test`:

* `test/host` C tests (ASan/UBSan): partition naming (byte-parity with
  hdl-dump `hdl_pname()`), ISO probe, APA planner for every drive
  maximum, CRC-32/SHA-256 vectors, journal and pair-state rules, delete
  policy (installer + driver header), PPAA writer vs a real `hdl_dump`
  fixture, info.sys (CRLF) and system.cnf (LF) byte fixtures.
* `test/host/test_driver.py`, `test/host/test_kelf_sign.sh`,
  `tools/verify-assets.py`.

`make test-graph` (needs the PS2 toolchain): release graph with a fake
kelftool.
