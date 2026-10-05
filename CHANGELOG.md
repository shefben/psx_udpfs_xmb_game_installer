# Changelog

## 2.0 (from 1.0)

Only partly tested on a DESR: back up anything important on the hard
disk first. `docs/HARDWARE_TEST_CHECKLIST.md` (D28-D44) lists what to
check.

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

- **Channels did not start from the XMB.** The installer and game KELFs
  were signed with a PSX `xosdmain` header (the DESR's own XMB type),
  and the installer channel stayed on a black screen. KELFs now carry
  the header of the OPL-Launcher and POPStarter KELFs that run from PSX
  XMB channels, and OPL-Launcher is signed without its debug data
  (1.5 MB to 0.3 MB). The same build also froze the XMB while loading
  once two or more games were installed; that is being retested with
  the new KELFs. Start the new bootstrap ELF once and choose
  *Install Installer as XMB Channel* to replace the installer's KELF,
  and use the new `udpfsd` folder.
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
