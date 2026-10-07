# Known limitations

Status vocabulary: see `docs/HARDWARE_TEST_CHECKLIST.md`. Nothing is
yet DESR-verified; everything below "works" only at the PC VERIFIED
level unless stated otherwise.

* **DESR results (v2.0 test builds, fixed in v3.0):** installs and deletes work from
  the bootstrap ELF. With two or more of our PFS channels the XMB froze
  while loading; two PFS-BatchKit-Manager games load fine. PS2 games are
  now installed in BatchKit's layout (one visible HDL partition with a
  PATINFO boot header, no PFS channel); D47 confirms it on hardware. The
  installer channel is still a PFS channel: whether one PFS channel next
  to several such games loads is part of D47.
* **128 GiB safe limit:** passing 128 GiB of games and data (all
  non-system partitions), or a partition the driver placed beyond the
  128 GiB mark (LBA 2^28), needs the warning confirmed (R1 + X, once per
  session); declined, nothing is created (a misplaced partition is
  removed again). The installer itself uses 48-bit LBA on a DESR with
  dvrpwned firmware, but whether OPL (its in-game driver), the XMB (1.31
  / 2.11), OPL-Launcher builds of other tools, or other PC/PS2 tools
  read or write data past the mark correctly is not tested: software
  that uses 28-bit commands there can corrupt the start of the HDD.
  Detection of the firmware (hddpump: ATA IDENTIFY words 121-124) is not
  yet tested on hardware.
* **Apps (homebrew channels), first version.** Up to 2 GiB and 256
  files per app (folders 4 levels deep). The app starts with argv[0] =
  `pfs0:<ELF>` and after an IOP reset, so apps that read files from
  their own folder may not find them. No argument editing on the console
  (`arg =` lines in `APP.CFG` work). The launcher and app channels are
  not yet tested on a DESR (checklist D52-D53).
* **Signing is external.** `kelftool` (ps2homebrew/kelftool) and
  `PS2KEYS.dat` are build prerequisites, never shipped.
* **KELF sizes.** The app KELF embeds the OPL-Launcher KELF (~0.3 MB,
  debug-stripped); the app KELF is about 1 MB.
* **DHCP is a simple client** (added to ministack): one lease at start-up,
  no renewal (an installer session is far shorter than a lease), no
  gateway (udpfsd must be on the same network). Without an answer within
  ~12 s the fixed fallback IP is used. Changing IP settings restarts the IOP.
* **One udpfsd server.** The first server that answers discovery is
  used. Discovery runs once, when udpfs_ioman loads (5 s); restart the
  network if udpfsd starts later.
* **Verification cost.** Every install reads the whole game back from
  the HDD (CRC-32). With Fast copy most of it is read during the copy;
  it only reads data at least 256 MiB behind the last 64 MiB checkpoint
  (far beyond any drive cache, so it comes from the disk), and the last
  320 MiB or so are read afterwards (all of a CD-sized game). The basic
  copy engine and *Verify game data* read everything afterwards.
* **Formats.** `.iso`, `.zso`, and from the patched udpfsd also `.cso`,
  `.chd` (only if the server was built with CHD support) and split
  `.iso.001`/`.002` sets, all decompressed by the server. USB: `.iso` and
  `.zso`.
* **Compressed transfers (LZ4 frames)** need the 4.0 udpfsd (manifest
  `wire=lz4f`); with an older server the console reads plain bytes. The
  server compresses on the fly with one CPU core per transfer.
* **Trust is per installer partition.** Data is trusted only through a
  completed, CRC-verified journal in `PP.UDPF-00001..INSTALLER:/state/` that
  also matches the live partition's start sector, size and HDL-header
  CRC. If that partition is recreated, for games installed by other
  tools, or for a same-named partition recreated by another tool, the
  data shows as UNKNOWN/UNVERIFIED (delete or reinstall; no channel is
  ever created on it).
* **Games installed by other tools** (hdl-dump / PFS-BatchKit-Manager:
  visible `PP.` HDL partitions) are listed, as not verified by this
  installer: they can be hidden, deleted or backed up; a same-named one
  blocks a new install (partition already exists).
* **XMB info for PS2 games:** the XMB shows the title from the game's
  boot header, but no cover (that needs a PFS partition: *Add XMB cover*,
  experimental). Covers, release date, developer and genre
  (`info.sys`, `gamedb`) only apply to PFS channels (PS1 games, installer).
* **Game IDs and titles.** A game without a `XXXX_NNN.NN` BOOT2 entry is
  rejected. Titles that sanitize to the same partition name are a
  conflict (shown as an existing installation), never silently renamed.
* **APA limits.** Partitions are planned from the drive's reported
  maximum partition size; a game needing more than 64 sub-partitions is
  rejected before anything is written.
* **Patched HDD driver.** `ps2hdd-hdl.irx` is a reproducible source build
  with one change (remove `__.` partitions of type HDL). See
  `tools/driver/README.md`.
* **Batch install** lists every game in the server's manifest (all
  `udpfsd.cfg` game folders), up to 256 images; there is no per-game
  title edit in a batch (the server's title is used). It needs the
  patched udpfsd from `dist/udpfsd/`; with a stock udpfsd only
  `udpfs:/INSTALL` would be checked, and a stock udpfsd has none.
* **Server-prepared data** (titles, jackets, OPL configs, launcher)
  is used only with the patched udpfsd. Titles come from your CFG
  folder and game list (no title database is bundled). Downloaded
  covers are third-party images (xlenore/ps2-covers). A failed cover
  download disables downloads until the server restarts.
* **OPL per-game config** is copied only when OPL has none for the game;
  a `$VMC_*` entry naming a virtual memory card that does not exist yet
  is copied unchanged (OPL behaviour untested, checklist D20).
* **Auto-install** exits through `rom0:OSDSYS`; that this lands in the
  DESR XMB is checklist D23. It waits at most three minutes for the server
  to finish its scan; on a very large first scan it falls back to the
  menu (start the installer again later).
* **Lists.** The browser shows up to 256 entries per folder; the manage
  screens handle up to 128 game pairs.
* **Disc type** comes from the server folder (`CD/`, `DVD/`), else UDF
  presence or size.
* **Jacket art** is made by the patched udpfsd only (140x200 and 74x108,
  256 colours, the sizes PSX-XMB-Manager uses). A cover of another
  size, or from an older udpfsd, is not used: the built-in jacket is.
* **No controller:** the installer only backs out of menus; it never
  starts an install or accepts a prompt without a pad.
* **Transfer tuning** has not been measured on hardware: 128 KiB per
  UDPFS request (patches/neutrino/0003; udpfsd always accepted it), two
  512 KiB read-ahead buffers, 4 hddpump slots of 128 KiB. If the IOP
  cannot allocate the 128 KiB fileXio buffer, 64 KiB and then 16 KiB are
  used (Diagnostics shows which).
* **OPL from the server** is installed only where OPL-Launcher looks for
  it: the default `+OPL` is created (128 MiB PFS) when missing; if
  `__common/OPL/conf_hdd.cfg` names a partition that does not exist,
  nothing is created. An existing `OPNPS2LD.ELF` is never replaced or
  updated. If formatting a newly created `+OPL` fails, the empty
  partition stays (the installer may not delete `+OPL`); remove it with
  another tool. OPL itself creates its folders (CFG, ART, ...) on first
  start.

* **PS1 games (POPStarter).** .VCD images, or BIN/CUE served as .VCD by
  the 4.0 udpfsd (one BINARY file per cue, as cue2pops requires; its
  optional game fixes / trainer / NTSC patch are not applied).
  Multi-disc games: up to 4 discs, recognised by "(Disc N)" / "CD N" in
  the file names, all in one partition (up to 4 GiB) with DISCS.TXT both
  next to the VCDs and in `__common/POPS/<game>/` (the two places the
  known tools use; which one POPStarter reads for HDD games is checklist
  D59). PS1 games are not part of Install All / auto-install. POPStarter
  rev13 Beta is included as distributed (another one, signed like the
  other KELFs: `POPSTARTER_ELF=/path/POPSTARTER.ELF make dist`); Sony's POPS.ELF /
  IOPRP252.IMG are not included and must be supplied (checklist D35).
* **Install from disc.** PS2 CDs and DVDs only. For a DVD the drive is
  asked whether it has two layers and where layer 1 starts; layer 1's
  PVD must sit right where layer 0's volume ends (as OPL expects). If
  the drive does not answer or anything does not fit, the disc is
  refused rather than copied as one layer. PS1 discs are refused
  (copy them on a PC). Burned discs need a drive that reads them
  (modchip / MechaPwn). Read errors are retried 16 times by the drive,
  then the copy stops (Resume copy continues it with the disc in).
* **Game extras.** Copied after the game is complete, best effort: a
  failure is reported but never undoes the install. OPL reads art only
  as PNG and only with "Cover art" enabled in its display settings;
  VMCs must be raw images (PCSX2 .ps2 cards are converted) and, on the
  HDD, not split into more than 10 PFS fragments (OPL's limit; an almost
  full +OPL partition may cause that). POPStarter's folder for a PP. game
  (`__common/POPS/<partition name without PP.>/`) is what PFS-BatchKit-
  Manager uses; untested on a DESR (D63). `.psu` saves go to a real
  memory card (not into a VMC); `.max`/`.cbs` saves are not read.
* **HDD Health Check.** SMART goes through the DVRP, which may not pass
  it on; then the status is "unknown". *Check all installed games* can
  only check games this installer copied (it compares with the CRC-32 in
  their journal); PS1 games and other tools' games are skipped.
* **USB installs** read FAT32/exFAT drives on the first USB device
  (`mass0:`). Covers and OPL settings still come from udpfsd when it is
  running; without it the default cover is used.
* **Resume copy / pause.** Checkpoints every 64 MiB and where a copy stops
  (pause, network error, abort), each with the HDD cache flushed. A resume
  reads back up to the 4 newest checkpoint parts and continues after the
  newest one that is still correct (else from the start of the same
  partition); the whole game is read back afterwards (START cannot skip
  that check).
* **Fast copy (iop/hddpump)** is new and untested on hardware. It writes
  through 4 IOP buffers of 128 KiB; if it cannot allocate two, or the
  module does not load, the previous copy loop is used. Network Settings >
  Copy engine: basic turns it off.
