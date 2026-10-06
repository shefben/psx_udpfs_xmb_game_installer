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
* **128 GiB limit:** games and data (all non-system partitions) never
  pass 128 GiB in total, and a partition the driver placed beyond the
  128 GiB mark is removed again; a bigger drive or game area is not
  used past that point.
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
  the HDD (CRC-32), adding roughly the HDD read time of the game.
* **Formats.** Plain `.iso` and udpfsd's virtual `.zso.iso` only. CSO/CHD
  virtual images are hidden; split `.iso.001` sets are not offered.
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
* **Transfer tuning** has not been done; the stream buffer is 1 MiB.
* **OPL from the server** is installed only where OPL-Launcher looks for
  it: the default `+OPL` is created (128 MiB PFS) when missing; if
  `__common/OPL/conf_hdd.cfg` names a partition that does not exist,
  nothing is created. An existing `OPNPS2LD.ELF` is never replaced or
  updated. If formatting a newly created `+OPL` fails, the empty
  partition stays (the installer may not delete `+OPL`); remove it with
  another tool. OPL itself creates its folders (CFG, ART, ...) on first
  start.

* **PS1 games (POPStarter), first version.** Only .VCD images (convert
  BIN/CUE with cue2pops); multi-disc games (DISCS.TXT) are not set up;
  PS1 games are not part of Install All / auto-install. POPStarter and
  Sony's POPS.ELF / IOPRP252.IMG are not included and must be supplied.
  Whether the DESR XMB boots krHACKen's POPSTARTER.KELF as distributed
  is untested; `POPSTARTER_ELF=/path/POPSTARTER.ELF make dist` signs one
  the same way as the other KELFs (checklist D35).
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
