# Changelog

## 3.0 (from 2.0)

Tested on a DESR: games install, show in the XMB and start; the XMB
loads with the installer and several games. Back up anything important
on the hard disk first. `docs/HARDWARE_TEST_CHECKLIST.md` (D43-D49)
lists what is still to check.

### Fixed

- **XMB froze while loading with two or more channels.** With the
  installer and a game, or two games, the DESR XMB stopped at its loading
  screen. PS2 games are now installed the way PFS-BatchKit-Manager does
  it, which a dump of a working DESR confirmed: each game is one
  partition, `PP.<ID>..<TITLE>`, booted from its own header
  (`BOOT2 = PATINFO`, `icon.sys`, an icon and OPL-Launcher). It is copied
  and verified as the hidden `__.<ID>..<TITLE>` first and renamed to
  `PP.` only after the full read-back matches. No separate 128 MiB
  channel partition per game any more.
- **Installer channel showed a black screen.** The KELFs were signed
  with a PSX `xosdmain` header. They now carry the header of the
  OPL-Launcher and POPStarter KELFs that run from PSX XMB channels
  (`KELF_MODE=dnasload`), and OPL-Launcher is signed without its debug
  data (1.5 MB to 0.3 MB).
- **PS1 installs said POPSTARTER.KELF was missing** although it was in
  the server's `POPS` folder. The installer looked for it with a status
  query that udpfsd only answers for files already open; it now opens the
  file to check. The same applies to `POPS.ELF` and `IOPRP252.IMG`.
  `.VCD` games can also be kept in the `CD` folder; the POPStarter files
  are then taken from `POPS`.
- **Delete/repair of games from other tools.** Installed Games and
  Remove Games now also list games PFS-BatchKit-Manager or hdl-dump
  installed (visible `PP.` HDL partitions): they can be hidden, deleted
  or backed up.

### New

- **XMB covers (experimental).** The XMB shows a picture and game ID
  only from a PFS partition's `res/`. *Add XMB cover* gives a game
  PFS-BatchKit-Manager's / PSX-XMB-Manager's resource partition: a PFS
  `PP.` partition with covers, title, game ID and OPL-Launcher, the game
  kept as `__.`. *Repair XMB Channels* offers it for all games at once;
  *Remove cover* turns a game back into one partition.
- **Space used and a 128 GiB limit.** The game lists, the install
  screen and Diagnostics show the space used by games, by games and data
  out of 128 GiB, and the HDD's free space. Games and data together can
  never pass 128 GiB: every partition is checked before it is created,
  and one the driver placed beyond the 128 GiB mark of the disk is
  removed again.
- **Hide from the XMB.** Takes a game out of the XMB without deleting it
  (renamed to `__.`); *Create XMB channel* shows it again.
- **Delete Installer XMB Channel** in the main menu.
- **Diagnostics > Dump XMB channels to USB.** Copies every `PP.`
  partition's header, files and PFS superblock to `mass0:/xmb-dump/`
  (read-only on the HDD), to compare channels with other tools'.
- **POPStarter included.** `PC\udpfsd\POPS\POPSTARTER.KELF` is
  POPStarter rev13 Beta (krHACKen; the last public release), unchanged:
  the same KELF PFS-BatchKit-Manager and PSX-XMB-Manager use on the PSX. Sony's `POPS.ELF` and `IOPRP252.IMG` are still not
  included: add your own next to it.
- **Every partition header is complete:** `system.cnf`, `icon.sys` and an
  icon, as hdl_dump / PFS-BatchKit-Manager write them.

### Changed

- `info.sys` like the other tools: `title_id = SLUS-20312` (no region
  suffix), `area` from the game ID, no empty field (install date as
  release date when the game database has none, "Unknown" developer,
  publisher and genre), no line break after the last line. A default
  manual page (`res/man.xml`, blank pages) and a blank `jkt_cp.png` are
  added to PFS channels.
- Covers at PSX-XMB-Manager's sizes: `jkt_001.png` 140x200 and
  `jkt_002.png` 74x108, 256 colours. udpfsd makes both; covers it cached
  at other sizes are made again.
- Games show as `Game.zso` instead of udpfsd's `Game.zso.iso`.
- Install All and auto-install no longer reserve 128 MiB per game.

### HDD driver

- `patches/apa-hdl/0002`: the driver may rename hidden `__.` HDL games
  (same rule as for removing them; system partitions stay protected).
  Rebuilt reproducibly; new pinned hash.

### Server (udpfsd)

- Covers in two sizes (`jkt/<ID>.png` 74x108, `jkt/<ID>_L.png` 140x200),
  256 colours. Restart the server once after updating.

### Upgrading from 2.0

1. Replace both the `PC\udpfsd` folder and the installer.
2. Start the new bootstrap ELF from wLaunchELF and choose *Install
   Installer as XMB Channel*.
3. Games installed by 2.0: *Repair XMB Channels*, then *Rebuild XMB
   channel* on each (converted without copying them again).
4. Restart the DESR.

## 2.0 (from 1.0)

Not yet tested on a DESR: back up anything important on the hard disk
first. `docs/HARDWARE_TEST_CHECKLIST.md` (D28-D42) lists what to check.

### New

- **PS1 games (POPStarter).** Install PS1 games in `.VCD` format from the
  server's new `POPS` folder or from USB. Each game gets its own XMB
  channel and starts through POPStarter. You supply `POPSTARTER.KELF`,
  `POPS.ELF` and `IOPRP252.IMG`; none of them are included.
- **Install from USB.** New menu entry *Install Games from USB* reads
  `.iso` and `.zso` from a FAT32 or exFAT drive, with no PC needed.
  Games over 4 GiB need exFAT.
- **Pause and resume.** Press START while a game copies to pause it.
  Continue later with *Installed Games > game > Resume copy*, or
  *Install All*. A copy cut short by a power cut or a network error
  resumes the same way.
  - A checkpoint is saved every 64 MiB and at the point where the copy
    stopped.
  - Before continuing, the last copied part is read back and checked.
    Only data after the last good part is copied again.
- **Remove Games.** New menu entry. Pick several installed games
  (Square toggles, Start selects all) and delete them in one go. Each
  game's partitions are checked as gone afterwards.
- **Rename.** Change the title shown in the XMB without reinstalling.
- **Back up to USB.** Copy an installed game to a USB drive, with
  OPL-style names in `DVD\`, `CD\` or `POPS\`. The copy is checked
  against the install checksum and read back.
- **XMB game info.** With `gamedb = PS2DB.xml` (PFS-BatchKit-Manager's
  game database), new channels show release date, developer,
  publisher and genre.
- **Power off when done.** Install All has a "power off when done"
  toggle. For auto-install, set `power_off_after_install = yes` in
  `udpfsd.cfg`. Both give a 15-second countdown that any button
  cancels.
- **DHCP.** The console gets its IP address from the router. The fixed
  IP is now only the fallback when no router answers, and Network
  Settings switches between automatic and fixed.
- **Sort and search.** In every game list, L2 changes the order (name
  A-Z, Z-A, size) and R2 searches by name.
- **Skip verification.** Press START during the read-back to skip it.
  The game is marked "NOT VERIFIED", and *Verify game data* checks it
  later.
- **Details screen.** Each installed game has a Details screen that
  explains its state, e.g. why it shows UNKNOWN/UNVERIFIED.

### Faster

- **Fast copy.** A new I/O-processor module writes to the HDD while the
  next block is downloaded. Network Settings > Copy engine switches back
  to the old method if needed.
- **ZSO.** ZSO games are sent compressed and unpacked on the console,
  so less data crosses the network. If the console can't unpack a
  block, it switches to the server's unpacking for the rest of the game.
- **Checksums.** The checksum is about 4 times faster. Checksumming
  now runs alongside the HDD writes and the read-back.

### Changed

- **Instant menu.** The menu appears at once. The server is searched for
  in the background, so Installed Games, Remove Games, Repair and
  Diagnostics work without a server.
- **Auto-install.** It starts when the server answers, and only if no
  menu entry has been chosen yet.

### Fixed

- **Broken USB backups.** Backing up a game whose copy never finished
  is refused, so a broken `.iso` can no longer pass its own check.
- **Game info memory bug.** Loading the game info no longer writes
  past the end of its buffer.

### Server (udpfsd)

- New `udpfsd.cfg` keys:
  - `pops` (PS1 folder, served as `/POPS`)
  - `gamedb` (XMB game info)
  - `power_off_after_install`
- The `udpfsd.cfg` in the release now has these keys.

### Upgrading from 1.0

- **Server and installer together.** Replace both, from the new zip.
- **Network settings.** Your saved fixed IP becomes the DHCP fallback.
  To keep the fixed address, set Network Settings > IP address to
  fixed, then Save.
- **Existing games.** They stay as they are. Copies that were
  interrupted under 1.0 resume from their last checkpoint, without the
  read-back check.
