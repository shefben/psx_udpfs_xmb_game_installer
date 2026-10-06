# PSX DESR UDPFS XMB Game Installer (v2.0)

An installer for the PSX DESR that runs on the console as its own XMB
channel. It installs PS2 and PS1 games to the internal HDD from a PC over
the network ([udpfsd](https://github.com/pcm720/udpfsd)) or from a USB
drive. Every installed game appears in the XMB and starts through
[OPL-Launcher](https://github.com/ps2homebrew/OPL-Launcher) (PS2) or
POPStarter (PS1). PS2 games use PFS-BatchKit-Manager's layout, which
loads reliably on a DESR: one partition per game, booted from its own
header.

```
PC .iso/.zso/.vcd -> udpfsd -> UDPFS/UDPRDMA (DHCP or fixed IP) -> udpfs:/...
USB .iso/.zso/.vcd -> mass0:/...
  -> GameSource (ZSO unpacked on the EE) -> CRC-32 -> hddpump.irx -> hdl0:
  -> hidden __.<ID>..<TITLE> -> full read-back -> boot header -> renamed PP.<ID>..<TITLE>
```

Status: tested on the PC side (host tests, signed build, server smoke
tests). Testing on a DESR is in progress; see the
[hardware checklist](docs/HARDWARE_TEST_CHECKLIST.md). Back up the HDD
before the first run.

**Download:** `PSX-UDPFS-Installer_V2.0.zip` (from `make package`)
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
- **PS1 games:** `.VCD` files through POPStarter. Each PS1 game gets its
  own XMB channel. You supply `POPSTARTER.KELF`, `POPS.ELF` and
  `IOPRP252.IMG`.
- **OPL:** installed automatically if the DESR has none. An existing OPL
  is never replaced. Per-game OPL settings (`CFG\<ID>.cfg`) are copied
  over.
- **Titles, covers, game info:**
  - Titles come from your OPL CFG files, a game list or the file name.
  - Covers come from your ART folder or are downloaded automatically.
  - Release date, developer, publisher and genre come from
    PFS-BatchKit-Manager's `PS2DB.xml` (`gamedb`).
- **Sort and search:** in every game list, L2 changes the order and R2
  searches by name.

### Safe copies
- **Verified copies:** every game is read back from the HDD and its
  CRC-32 compared with the data received. Only then is the XMB channel
  created, so a failed copy never leaves a visible channel. START skips
  the read-back if you're in a hurry; *Verify game data* does it later.
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
  while the next block is downloaded.
- **ZSO games travel compressed:** the console unpacks them itself, so
  less data crosses the network.
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
| POPStarter (PS1 games; not included) | krHACKen |
| PS2SDK (incl. USB / BDM drivers) | ps2dev |
| Cover art downloads | xlenore/ps2-covers |
| Partition naming, CFG/ART layout, `GameListPS2.txt`, game database (`PS2DB.xml`) | PFS-BatchKit-Manager (GDX-X) |

Open PS2 Loader is distributed under the AFL-3.0 licence (shipped as
`udpfsd/OPL-LICENSE.txt`). POPStarter and Sony's POPS files are not
part of this project. Pinned upstream revisions:
[reference/REVISIONS.txt](reference/REVISIONS.txt) and
[provenance](docs/PROVENANCE.md).
