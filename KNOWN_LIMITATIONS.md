# Known limitations

* **Not yet tested on a real DESR.** Everything that can be checked on a
  PC is covered by host tests; the hardware matrix in
  `docs/HARDWARE_TEST_CHECKLIST.md` is still open. In particular the
  XMB behaviour of the generated channels, the jacket PNGs and the
  OPL-Launcher hand-off have not been observed on hardware yet.
* **KELF signing is external.** `kelftool` and `PS2KEYS.dat` are build
  prerequisites that are not shipped. Which kelftool header mode the
  DESR XMB accepts for `pfs:/EXECUTE.KELF` must be confirmed on
  hardware (`KELF_MODE`, see docs/BUILD.md).
* **Static IP only.** Neutrino's ministack has no DHCP. Changing the IP
  restarts the IOP.
* **One udpfsd server.** The first server that answers discovery is
  used; there is no server picker.
* **Discovery only at start.** udpfs_ioman discovers the server once,
  when the module loads (5 s). If udpfsd starts later, use
  Network Settings > Restart network.
* **No resume.** An interrupted copy restarts from the beginning; the
  journal records progress but no resume algorithm is shipped.
* **Version-1 formats.** Plain `.iso` and udpfsd's virtual `.zso.iso`
  only. CSO/CHD virtual images are hidden. Split `.iso.001` sets are not
  offered over UDPFS.
* **Game IDs and titles.** A game without a `XXXX_NNN.NN` BOOT2 entry is
  rejected. Two titles that sanitize to the same partition name are a
  conflict (shown as an existing installation), never silently renamed.
  Install journals are keyed by game ID; a second install of the same
  ID under a different title shares the journal file name, and the
  installer ignores a journal whose partition names do not match.
* **Partition table size.** The browser lists up to 256 entries per
  folder; the manage screens handle up to 128 game pairs.
* **Disc type** comes from the server folder (`CD/`, `DVD/`), else UDF
  presence or size. A CD image larger than 870 MiB outside `CD/` would
  be treated as a DVD.
* **Repair of foreign games.** Hidden games installed by other tools
  (no journal) are trusted if their HDL header is valid; their data is
  not re-verified against a source.
* **Jacket art** is copied as-is (no resizing). An invalid PNG falls
  back to the built-in jacket.
* **Transfer tuning** (buffer size benchmarks) has not been done; the
  stream buffer is a fixed 1 MiB.
