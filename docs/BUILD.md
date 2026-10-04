# Building

## Environment

* A PS2DEV toolchain with PS2SDK (EE + IOP compilers, `bin2c`,
  `iopfixup`). Tested with the toolchain at `/usr/local/ps2dev/ps2dev`
  (EE GCC 15.2.0) under Ubuntu on WSL2. Any equivalent PS2DEV
  environment works; Docker is optional (the `ps2dev/ps2dev` image).
* `make`, `patch`, `git`, `python3`, and a native `cc` for host tests.

```sh
. tools/ps2env.sh        # sets PS2DEV/PS2SDK/PATH (override PS2DEV first if needed)
make references          # clone pinned upstream sources into reference/
make all                 # installer ELF, OPL-Launcher ELF, host tests
```

`reference/REVISIONS.txt` records the exact upstream commits. They match
the revisions pinned in the implementation plan:

| Project | Commit |
|---|---|
| udpfsd | 58d7c8f11ac196d4a5ca7b65b78743fbeedfdd80 |
| neutrino | 7be8de2798c99af433c91993e021746a54d6800d |
| OPL-Launcher | 6da1af2e175c09da4cbb6da50ccf472132f8f114 |
| HDLGameInstaller | ec37c81f8edb6fc45e2be51c86a322780641275a |
| ps2-usbhdl | b681bc64a409753922d7d145ac05fa2f9039ce7f |
| hdl-dump | 32c296c69cf9c263fcbe035004aa28c345b3b279 |

On Windows, `tools/wsl-run.ps1 <command>` runs a command in the WSL
project directory with the environment sourced.

## Targets

| Target | Output |
|---|---|
| `make installer` | `dist/desr-udpfs-installer.elf` (bootstrap/homebrew ELF) |
| `make opl-launcher` | `build/opl-launcher/OPL-Launcher.elf` (unsigned, pinned upstream) |
| `make kelfs` | `dist/installer-EXECUTE.KELF`, `dist/opl-launcher-EXECUTE.KELF` |
| `make test` | host unit tests (`test/host`) |
| `make dist` | `dist/` with ELF, docs, server examples, `PAYLOAD/` if signed |

## Signing (`make kelfs`)

KELF signing needs `kelftool` and a `PS2KEYS.dat` dumped from a console
you own. Neither is shipped or committed. The target fails with a clear
error when either is missing; it never writes an unsigned ELF under a
`.KELF` name (`tools/kelf-sign.sh` rejects ELF output).

```sh
PS2KEYS=/path/to/PS2KEYS.dat make kelfs
```

The default invocation is the one OPL-Launcher documents,
`kelftool encrypt mbr <in> <out>`. For kelftool forks whose CLI is
`kelftool encrypt <in> <out>` (e.g. xfwcfw/kelftool, which writes a PSX
header), set `KELF_MODE=none`.

`make kelfs` also copies the signed OPL-Launcher to
`vendor/opl-launcher/EXECUTE.KELF`; run `make installer` again to embed
it in the ELF (then game installs do not depend on the server's
`PAYLOAD/` folder). Without embedding, the installer loads it from
`PP.UDPFS-INSTALLER:/payload/OPL-LAUNCHER.KELF` (written by the
self-install) or `udpfs:/PAYLOAD/opl-launcher-EXECUTE.KELF`.

## Build adjustments to upstream code

* **Neutrino** (`patches/neutrino/0001-retonly-after-export-table.patch`):
  current PS2SDK links `exports.o` first, which puts `_retonly` at
  `.text` offset 0 and makes `iopfixup` reject smap/ministack/udpfs
  (the error message itself prescribes this fix). The patch only moves
  the `_retonly` definition below the export table (forward-declared
  above it). Applied to a scratch copy in `build/neutrino`; `reference/`
  is untouched.
* **OPL-Launcher**: source unmodified. `tools/bin2s` replaces the
  `bin2s` tool current PS2SDK no longer ships, and `EE_CFLAGS` is set on
  the make command line to `-D_EE -G0 -O2 -Wno-stringop-truncation`
  because the current EE toolchain rejects upstream's `-G8192` with
  abicalls. `-G` only affects small-data placement.

* **ps2hdd-hdl.irx** (`tools/patch-ps2hdd-hdl.py`): one-word binary
  patch so the driver can remove `__.` hidden game partitions (the
  stock module returns -EACCES for any `__*` name). The script checks
  the input hash and the surrounding instructions and refuses anything
  else. A source rebuild was rejected: apa-hdl only builds against a
  2022 libapa and would replace the hardware-proven binary wholesale.

## Host tests

`make test` builds `src/` modules that have no PS2 dependencies with
`-fsanitize=address,undefined` and runs `test/host/build/tests`.
`test/fixtures/ppaa_hdldump.bin` is the PPAA/system.cnf header produced
by the pinned `hdl_dump modify_header` on a blank image;
`tools/make-ppaa-fixture.sh` regenerates it (needs `pfsshell`).
