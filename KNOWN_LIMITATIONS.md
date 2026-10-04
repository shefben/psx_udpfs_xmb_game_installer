# Known limitations

Status vocabulary: see `docs/HARDWARE_TEST_CHECKLIST.md`. Nothing is
yet DESR-verified; everything below "works" only at the PC VERIFIED
level unless stated otherwise.

* **No DESR results yet.** XMB behaviour of the generated channels, the
  jacket PNGs, CRLF `info.sys`, OPL-Launcher hand-off, the patched HDD
  driver and the KELF format accepted by the DESR are all unobserved.
* **Signing is external.** `kelftool` and `PS2KEYS.dat` are build
  prerequisites, never shipped. Whether the DESR accepts the canonical
  `kelftool encrypt mbr` KELF from `pfs:/EXECUTE.KELF` is the first thing
  D1/D2 prove; `KELF_MODE=none` is an untested fallback.
* **KELF sizes.** The app KELF embeds the OPL-Launcher KELF, so it is
  roughly app + 1.6 MB. No size limit of the DESR loader is known to be
  exceeded, but none has been tested.
* **Static IP only.** Neutrino's ministack has no DHCP. Changing the IP
  restarts the IOP.
* **One udpfsd server.** The first server that answers discovery is
  used. Discovery runs once, when udpfs_ioman loads (5 s); restart the
  network if udpfsd starts later.
* **No resume.** An interrupted copy restarts from the beginning.
* **Verification cost.** Every install reads the whole game back from
  the HDD (CRC-32), adding roughly the HDD read time of the game.
* **Formats.** Plain `.iso` and udpfsd's virtual `.zso.iso` only. CSO/CHD
  virtual images are hidden; split `.iso.001` sets are not offered.
* **Trust is per installer partition.** Data is trusted only through a
  completed, CRC-verified journal in `PP.UDPFS-INSTALLER:/state/` that
  also matches the live partition's start sector, size and HDL-header
  CRC. If that partition is recreated, for games installed by other
  tools, or for a same-named partition recreated by another tool, the
  data shows as UNKNOWN/UNVERIFIED (delete or reinstall; no channel is
  ever created on it).
* **hdl-dump visible installs** (`PP.` partitions of type HDL) are not
  managed: they are neither listed nor removable by this installer, and
  a same-named one blocks a new install (partition already exists).
* **Game IDs and titles.** A game without a `XXXX_NNN.NN` BOOT2 entry is
  rejected. Titles that sanitize to the same partition name are a
  conflict (shown as an existing installation), never silently renamed.
* **APA limits.** Partitions are planned from the drive's reported
  maximum partition size; a game needing more than 64 sub-partitions is
  rejected before anything is written.
* **Patched HDD driver.** `ps2hdd-hdl.irx` is a reproducible source build
  with one change (remove `__.` partitions of type HDL). See
  `tools/driver/README.md`.
* **Lists.** The browser shows up to 256 entries per folder; the manage
  screens handle up to 128 game pairs.
* **Disc type** comes from the server folder (`CD/`, `DVD/`), else UDF
  presence or size.
* **Jacket art** is copied as-is (no resizing); an invalid PNG falls back
  to the built-in jacket.
* **No controller:** the installer only backs out of menus; it never
  starts an install or accepts a prompt without a pad.
* **Transfer tuning** has not been done; the stream buffer is 1 MiB.
