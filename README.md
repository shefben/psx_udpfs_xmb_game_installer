# PSX DESR UDPFS XMB Game Installer (v4.0)

An installer for the PSX DESR that runs on the console as its own XMB
channel. It installs PS2 and PS1 games to the internal HDD from a PC over
the network ([udpfsd](https://github.com/pcm720/udpfsd)) or from a USB
drive. Every installed game appears in the XMB and starts through
[OPL-Launcher](https://github.com/ps2homebrew/OPL-Launcher) (PS2) or
POPStarter (PS1). PS2 games use PFS-BatchKit-Manager's layout, which
loads reliably on a DESR: one partition per game, booted from its own
header.

```
PC .iso/.zso/.cso/.chd/.iso.001/.vcd/.cue -> udpfsd (LZ4 frames) -> UDPFS/UDPRDMA -> udpfs:/...
USB .iso/.zso/.vcd -> mass0:/...      PS2 disc in the DESR -> cdrom0:
  -> GameSource (read-ahead; LZ4/ZSO unpacked on the EE) -> CRC-32 -> hddpump.irx -> hdl0:
     (hddpump reads earlier parts back during the copy)
  -> hidden __.<ID>..<TITLE> -> check of the rest -> boot header -> renamed PP.<ID>..<TITLE>
  -> game extras: OPL CFG/CHT/VMC/ART, POPStarter SLOT0/1.VMC + CHEATS.TXT
```

Status: tested on the PC side (host tests, signed build, server smoke
tests). Testing on a DESR is in progress; see the
[hardware checklist](docs/HARDWARE_TEST_CHECKLIST.md). Back up the HDD
before the first run.

**Download:** `PSX-UDPFS-Installer_V4.0.zip` (from `make package`)
contains the installer ELF, the ready-to-run server folder,
`README.txt`, `CHANGELOG.txt` and `SERVER-MANUAL.txt`.

## Features

### Installing
- **From the PC (udpfsd):** browse the server's folders, or use
  **Install All** to pick several games (Square toggles).
- **Auto-install:** with `auto_install = yes` the console installs every
  new game that fits, with no button presses.
- **From USB:** `.iso` and `.zso` files on a FAT32 or exFAT drive; no PC
  needed.
- **From the DESR's own disc drive:** *Install Game from Disc* copies a
  PS2 CD or DVD (DVD-9 included) to the HDD, with the same checks,
  journal and XMB channel as a network install.
- **Any image format:** the server also offers `.cso`, `.chd` and split
  `.iso.001`/`.002` sets as plain images, and PS1 `BIN/CUE` as `.VCD`
  (cue2pops' format): no converter tools needed.
- **PS1 games:** `.VCD` files through POPStarter. Each PS1 game gets its
  own XMB channel. POPStarter (rev13 Beta) is included in
  `PC\udpfsd\POPS`; you supply Sony's `POPS.ELF` and `IOPRP252.IMG`.
  **Multi-disc games** (`... (Disc 1).VCD`, `(Disc 2)` ...) install as one
  game: `IMAGE0..3.VCD` plus `DISCS.TXT`, discs changed in-game.
- **OPL:** installed automatically if the DESR has none. An existing OPL
  is never replaced.
- **Game extras:** with every game, and later from *Saves, Cheats & Game
  Extras*, the server's (or USB drive's) folders are installed where the
  game's software reads them:
  - `CFG\<ID>.cfg`: OPL per-game settings (kept if OPL already has some,
    unless you choose to replace them).
  - `CHT\<ID>.cht`: OPL cheats, switched on for that game;
    `CHT\<ID>.txt` becomes a PS1 game's POPStarter `CHEATS.TXT`.
  - `VMC\<ID>_0.bin` / `_1.bin`: OPL memory cards (PCSX2 `.ps2` cards
    converted), assigned to the game's slots; PS1 cards (`.VMC`, `.mcr`,
    `.mcd`, `.gme`, `.vmp`) and single saves (`.mcs`) become the game's
    POPStarter `SLOT0/1.VMC`; `.psu` saves go onto a real memory card.
  - `ART\<ID>_COV.jpg` etc.: OPL art, converted to the PNG names OPL
    loads (COV, COV2, ICO, LAB, LGO, BG, SCR, SCR2).

  Existing memory cards are never replaced without a firm confirmation.
- **Titles, covers, game info:**
  - Titles come from your OPL CFG files, a game list or the file name.
  - Covers come from your ART folder or are downloaded automatically.
  - Release date, developer, publisher and genre come from
    PFS-BatchKit-Manager's `PS2DB.xml` (`gamedb`).
- **Sort and search:** in every game list, L2 changes the order and R2
  searches by name.
- **128 GiB safe limit:** going past 128 GiB of games and data, or
  placing a partition beyond the 128 GiB mark of the disk, needs a firm
  warning to be confirmed (hold R1 + X). Past that mark only a DESR with
  LBA48-aware custom DVRP firmware (dvrpwned) and a PS2 area enlarged
  with psxrepart is safe; the installer's ATA driver (PS2SDK 2026-10-06)
  then uses 48-bit LBA, and Diagnostics shows whether that firmware was
  detected. The space used by games and data is shown in the game lists
  and Diagnostics.

### Safe copies
- **Verified copies:** every game is read back from the HDD and its
  CRC-32 compared with the data received. Only then is the XMB channel
  created, so a failed copy never leaves a visible channel. Most of the
  read-back now happens *during* the copy (the HDD is idle while the
  network delivers), so only the last part is read afterwards. START
  skips that last check if you're in a hurry; *Verify game data* does it
  later.
- **Pause and resume:** START pauses a copy. After a pause, a network
  error or a power cut, *Resume copy* continues where the copy stopped:
  - A checkpoint is saved every 64 MiB.
  - Before continuing, the newest parts are read back and checked.
  - Only data after the last good part is copied again.
- **Journal:** every step is recorded in a journal on the installer
  partition, so an interrupted install is always detected and can be
  resumed, repaired or deleted.

### Speed
- **Fast copy:** our own IOP module (`iop/hddpump`) writes to the HDD
  while the next block is downloaded, and reads finished parts back for
  the check at the same time.
- **Read-ahead:** the next 512 KiB are already being fetched while the
  current block is checked and written, with 128 KiB per network request
  (was 64 KiB).
- **Compressed transfers:** the patched udpfsd compresses every image
  with LZ4 for the network and the console unpacks it, so padding and
  other compressible data cost almost nothing to send. ZSO games still
  travel compressed too.
- **Checksums:** slice-by-8 CRC-32, overlapped with the HDD writes and
  the read-back.
- **Speed display:** the copy screen shows network, CRC and HDD speeds
  separately.

### Managing games
- **Installed Games:** shows every game's state. For each game:
  - Rename the XMB title
  - Repair XMB channel
  - Verify game data
  - Back up to USB (`.iso` / `.VCD`, OPL-style names)
  - Resume copy
  - Details: why a game is in its state
  - Delete
- **Remove Games:** delete several games at once (Square toggles, Start
  selects all, hold R1 + X to confirm).
- **Repair XMB Channels:** lists only the games that need attention.
- **HDD Health Check:** SMART status and attributes (when the DVRP passes
  them through), space used, the largest game that still fits, and
  *Check all installed games* (reads every game back and compares it with
  the CRC-32 recorded when it was copied).

### Apps
- **Homebrew as XMB channels:** *Apps* installs any `.ELF` from the
  server's `APPS` folder or a USB drive's `APPS` folder (with the files
  in its folder if wanted) as its own channel `PP.APPS-NNNNN..TITLE`.
  The channel's `EXECUTE.KELF` is a small signed launcher
  (`launcher/main.c`), the same for every app: it mounts the channel and
  starts the ELF named in `APP.CFG`. *Installed Apps* deletes them.

### Console app
- **Starts instantly:** the menu appears at once and the server is
  searched for in the background. Everything except installing from the
  PC works without the server.
- **DHCP:** the console gets its IP from the router; a fixed IP is used
  only as the fallback. Network Settings switches between automatic and
  fixed.
- **Power off when done:** an option for Install All, and
  `power_off_after_install` for auto-install. A 15-second countdown lets
  any button cancel.
- **The installer as an XMB channel:** `PP.UDPF-00001..INSTALLER`
  appears in the XMB, so no USB stick is needed after the first run.
- **Diagnostics:** PASS / FAIL for every module, the HDD, the network,
  DHCP, fast copy and OPL.

## How it works

- **One partition per PS2 game:** a game is copied into the hidden HDL
  partition `__.SLUS-20312..GRAN_TURISMO_4`. After the full read-back
  matches, its header gets `system.cnf` (`BOOT2 = PATINFO`), `icon.sys`,
  an icon and OPL-Launcher as boot KELF, and the partition is renamed
  `PP.SLUS-20312..GRAN_TURISMO_4`; that rename is what puts it in the
  XMB. This is exactly how PFS-BatchKit-Manager installs games (checked
  against a dump of its games on a DESR). Separate PFS channels per game
  froze the DESR XMB once two existed. A PS1 game's XMB header is
  written last, after its `IMAGE0.VCD` has been verified.
- **Journal:** every step is journaled under
  `PP.UDPF-00001..INSTALLER:/state/`, together with the copy checkpoints
  (`install-<name>.seg`). The journal, not the HDL format, records
  whether data is verified.
- **Release builds:** `desr-udpfs-installer-bootstrap.elf` (first run;
  embeds the signed app and OPL-Launcher KELFs) and the signed XMB app
  `installer-EXECUTE.KELF`. The HDD driver, the patched Neutrino network
  modules (with DHCP) and `hddpump.irx` are all built from source.

## Docs

[Quick start](docs/QUICKSTART.md) ·
[Server manual](docs/SERVER_MANUAL.md) ·
[Changelog](CHANGELOG.md) ·
[Install & use](docs/INSTALL.md) ·
[Build](docs/BUILD.md) ·
[Provenance](docs/PROVENANCE.md) ·
[Hardware checklist](docs/HARDWARE_TEST_CHECKLIST.md) ·
[Limitations](KNOWN_LIMITATIONS.md)

## Layout

```
src/        console code; pure modules are host-tested
iop/        our own IOP module (hddpump: overlapped HDD writes)
test/host/  native unit tests        test/fixtures/  hdl_dump PPAA fixture
tools/      fetch, sign, package, fixture and asset scripts
patches/    patches applied to upstream build copies (udpfsd, Neutrino, driver)
assets/     embedded jacket PNGs
docs/       guides, manual, checklist; docs/package/ = release zip texts
```

## Credits

| Component | Author / project |
|---|---|
| UDPFS / udpfsd, Neutrino network modules (smap, ministack, udpfs_ioman) | Maximus32 |
| Open PS2 Loader, OPL-Launcher | ps2homebrew and contributors |
| APA/HDL driver (`ps2hdd-hdl.irx`), `hdlfs.irx` | HDLGameInstaller (sp193) |
| POPStarter (PS1 games; rev13 Beta, included) | krHACKen |
| PS2SDK (incl. USB / BDM drivers) | ps2dev |
| Cover art downloads | xlenore/ps2-covers |
| Partition naming, CFG/ART layout, `GameListPS2.txt`, game database (`PS2DB.xml`) | PFS-BatchKit-Manager (GDX-X) |

Open PS2 Loader is distributed under the AFL-3.0 licence (shipped as
`udpfsd/OPL-LICENSE.txt`). POPStarter is krHACKen's freeware, shipped
unchanged; Sony's POPS files are not part of this project. Pinned upstream revisions:
[reference/REVISIONS.txt](reference/REVISIONS.txt) and
[provenance](docs/PROVENANCE.md).
