# Hardware test checklist

Three verification levels. A feature is only described as supported on
a DESR once its row in the corresponding table is `PASS`.

* **PC VERIFIED** - proven on the PC (host tests, fixtures, build).
* **DESR NON-DESTRUCTIVE VERIFIED** - proven on a DESR without creating
  or deleting game partitions.
* **DESR DESTRUCTIVE/BOOT VERIFIED** - proven on a DESR with real game
  installs, deletes and XMB boots.

Status values: `PASS`, `FAIL`, `NOT RUN`.

Before the first destructive test: back up the APA table (`hdl_dump toc`
or pfsshell) so the disk can be restored.

## Level 1 - PC VERIFIED

| # | Item | Evidence | Status |
|---|---|---|---|
| P1 | Partition names byte-identical to hdl-dump `hdl_pname()` | `test_partname_hdldump.c` | PASS |
| P2 | PPAA/system.cnf header byte-identical to `hdl_dump modify_header` | `test_xmb.c` + fixture | PASS |
| P3 | info.sys CRLF / system.cnf LF byte fixtures | `test_xmb.c` | PASS |
| P4 | ISO9660 / SYSTEM.CNF / DVD9 probe, 64-bit sizes > 4 GiB | `test_source_iso.c` | PASS |
| P5 | APA planner for drive maximum 128M..4G, no empty sub, <= 64 subs | `test_plan.c` | PASS |
| P6 | Journal model, CRC trust rule, pair states/actions | `test_state.c`, `test_game_pair.c` | PASS |
| P7 | Delete whitelist (installer) and driver remove policy | `test_policy.c` | PASS |
| P8 | HDD driver: unmodified source rebuild == vendored IRX; shipped hash pinned | `make driver`, `test_driver.py` | PASS |
| P9 | KELF signing transactional, explicit keys, mode validation | `test_kelf_sign.sh` | PASS |
| P10 | Release build graph order / embedding / no spurious re-sign | `make test-graph` (fake kelftool) | PASS |
| P11 | Signed release built with real kelftool + PS2KEYS | `PS2KEYS=... KELF_MODE=none make dist` with xfwcfw kelftool 6b9b471; both KELFs pass `kelftool decrypt` (all signatures) and decrypt to the exact input ELF | PASS (mode none) |
| P12 | Signed release in `KELF_MODE=dnasload` (default) | ps2homebrew/kelftool 05bbeb8; both KELFs: header bytes 0x00-0x0F and 0x16-0x1F identical to PFS-BatchKit-Manager's OPL-Launcher `boot.kelf` (DNASLOAD, PS2, app type 11, flags 0x22C, MG 0xFF); `kelftool decrypt` returns the input ELF + 16 zero bytes of padding | PASS |
| P13 | udpfsd mounts (`-install-dir` / `udpfsd.cfg` folders): listing, always read-only, no path escape, mounts-only mode | Go tests in `patches/udpfsd/0002` (`make test-udpfsd`) | PASS |
| P14 | Batch selection/summary logic | `test_batch.c` | PASS |
| P15 | udpfsd.cfg parsing/precedence; game prep: ISO/ZSO probe, titles (CFG > game list > file name), art lookup + 76x108 / 256x256 256-colour PNG, cover download (local test server), manifest format/atomic write, scan cache | Go tests in `patches/udpfsd/0002` (`make test-udpfsd`) | PASS |
| P16 | PS2 manifest parser, server-launcher check (hash/size/KELF), OPL cfg decision, auto-install selection, journal fields | `test_manifest.c`, `test_server_assets.c`, `test_batch.c`, `test_state.c` | PASS |
| P17 | OPL runtime: pinned archive/ELF SHA-256 (`tools/fetch-opl.sh`), server serves only an ELF with `opl=` hash, console install decision (create `+OPL` only by default, add ELF to existing partition, never for a missing configured partition), hash check | `make dist`, Go `TestServesOPLRuntime`, `test_server_assets.c`, `test_manifest.c` | PASS |

## Level 2 - DESR NON-DESTRUCTIVE VERIFIED

Run in this order. Nothing here creates or deletes a game partition.

| # | Test | Expected | Status |
|---|---|---|---|
| N1 | Bootstrap ELF starts from a homebrew launcher | main menu, first-run notice | NOT RUN |
| N2 | Diagnostics > Pre-hardware checks | every IRX `PASS`; HDD driver hash = manifest; HDD present/formatted; sizes; IP; SMAP/ministack; UDPFS connected; `udpfs:/` listing; OPL resolved; `OPNPS2LD.ELF` exists; embedded KELF hashes = manifest | NOT RUN |
| N3 | UDPFS browser lists folders, ISO and ZSO entries, sizes > 4 GiB correct | | NOT RUN |
| N4 | Select an invalid `.iso` | "Not a valid PS2 ISO", no HDD change | NOT RUN |
| N5 | Select a valid ISO and a ZSO, back out at the install screen | ID/title/partitions/allocation shown, no HDD change | NOT RUN |
| N6 | Unplug network, select a game | source error, no HDD change | NOT RUN |
| N7 | Diagnostics > HDD self-test (`PP.UDPFS-TEST`) | all 10 steps PASS; partition count unchanged | NOT RUN |
| N9 | Install All Games list with `-install-dir` (ISO, ZSO, CD/ DVD/ subfolders, an invalid file, a game already installed) | correct statuses, only `new` selected; back out writes nothing | NOT RUN |
| N10 | Install All with `udpfsd.cfg` game folders (DVD/CD/GAMES): list appears without probing, real titles from CFG/game list, same game as .iso + .zso shown once as `duplicate` | correct statuses; back out writes nothing | NOT RUN |
| N8 | Installed Games on a disk with existing hdl-dump games | visible hdl-dump games (PP., type HDL) not listed; `-hide` games listed as UNKNOWN/UNVERIFIED | NOT RUN |

## Level 3 - DESR DESTRUCTIVE/BOOT VERIFIED

| # | Test | Expected | Status |
|---|---|---|---|
| D1 | Install Installer as XMB Channel from the bootstrap ELF | `PP.UDPF-00001..INSTALLER` verified; game install enabled | PASS (v2.0, mode none) |
| D2 | Cold reboot, installer channel visible in XMB, launches, reaches NETWORK_READY and the browser | | FAIL (v2.0, mode none: black screen after selecting the channel); retest with mode dnasload as D43 |
| D43 | Bootstrap ELF (dnasload build) > Install Installer as XMB Channel (rewrites `EXECUTE.KELF` in place), cold reboot, start the channel from the XMB | installer menu appears | NOT RUN |
| D44 | With D43 done and udpfsd's folder replaced (new `opl-launcher-EXECUTE.KELF`): install 2 games, cold reboot | XMB finishes loading (v2.0 mode none: froze ~10 s into the loading screen with 2+ games, booted with 0 or 1); both games start | NOT RUN |
| D46 | Upgrade from a v2.0 build without OSD icons: games listed by Repair XMB Channels; Rebuild XMB channel on each, reinstall the installer channel from the bootstrap ELF, cold reboot | XMB finishes loading with the installer + 2 games; the HDD OSD-style header (icon.sys/icon) is present on every `PP.` and `__.` partition (Diagnostics/Details: channel `valid`); games still start | NOT RUN |
| D47 | BatchKit layout: with this build install 2 PS2 games (and keep the installer channel), cold reboot. Then Installed Games > a game > Hide from the XMB, reboot, Create XMB channel, reboot | partitions are `PP.<ID>..<TITLE>` type 0x1337 (Diagnostics > Dump XMB channels: header has PATINFO system.cnf, icon.sys, icon at 0x800 and 0x40000, KELF at 0x110000); XMB loads with the installer + 2 games; each game starts OPL from its own header; hide/show work without copying; Rebuild XMB channel converts a game from an earlier v2.0 build (its PFS channel gone, `__.` renamed to `PP.`) | NOT RUN |
| D45 | Only if D44 still freezes: from the bootstrap ELF (wLaunchELF, no XMB), Installed Games > second game > Remove XMB channel (keep game data); cold reboot. Then Create XMB channel on it again | boots: the freeze comes from the second channel (content or count), not from the game data partitions; freezes: it comes from the `__.` data partitions. Create XMB channel restores the game with no copy | NOT RUN |
| D3 | Diagnostics from the XMB app | installer KELF from `pfs0:/EXECUTE.KELF`, hash = manifest | NOT RUN |
| D4 | Plain ISO < 4 GiB: full acceptance chain (below) | | NOT RUN |
| D5 | ZSO < 4 GiB: full acceptance chain (below), independently | | NOT RUN |
| D6 | ISO > 4 GiB (sub-partitions) | plan matches drive maximum; CRC equal | NOT RUN |
| D7 | ZSO with logical size > 4 GiB | CRC over decompressed stream equal | NOT RUN |
| D8 | DVD9 game | layer break recorded; game reads past layer 0 | NOT RUN |
| D9 | Network loss during stream | ERR_SOURCE_READ; no PP.; state UNKNOWN/UNVERIFIED | NOT RUN |
| D10 | Power cut during copy, restart | unfinished journal reported; UNKNOWN/UNVERIFIED; delete works | NOT RUN |
| D11 | OPL missing: data-only install | verified, channel pending; channel later via Repair | NOT RUN |
| D12 | Existing verified game without PP. | Create XMB channel, data untouched | NOT RUN |
| D13 | PP. without hidden game | ORPHANED, removable | NOT RUN |
| D14 | Delete a complete game | journal deleting=1, PP. removed first, `__.` removed (patched driver), journal removed | NOT RUN |
| D15 | Delete refused for `__common` etc. (cannot be selected in the UI) | | NOT RUN |
| D16 | Reboot after successful installs | channels remain | NOT RUN |
| D18 | Install All: 2+ games (one ISO, one ZSO) in one batch | each completes the acceptance chain; summary correct; abort mid-batch leaves aborted game UNVERIFIED and the rest skipped | NOT RUN |
| D19 | Jackets prepared by the server (ART `_COV` and a downloaded cover) appear in the XMB; `udpfsd-cache/served/jkt/` has `<ID>.png` 76x108 and `<ID>_L.png` 256x256; the channel has `res/jkt_cp.png`, `res/man.xml` + `res/image/0..2.png` (XMB Manual entry opens a blank page); a game not in `gamedb` shows the install date as release date | | NOT RUN |
| D20 | OPL cfg copied to `<OPL partition>/CFG/<ID>.cfg`; its compatibility modes take effect when the channel boots; behaviour of a `$VMC_0=` entry whose VMC does not exist yet; an existing cfg is kept | | NOT RUN |
| D21 | Channels use the server OPL-Launcher (Diagnostics: "OPL-Launcher KELF from server", journal `launcher_source=server`); a mismatching server copy falls back to the embedded one | | NOT RUN |
| D22 | Fresh console (no `PP.UDPF-00001..INSTALLER`), `auto_install = yes`, one ISO + one ZSO on the server: start the bootstrap ELF, no further input | installer partition created, both games complete the acceptance chain, console returns to the XMB, both games boot | NOT RUN |
| D23 | `LoadExecPS2("rom0:OSDSYS")` after auto-install lands in the DESR XMB | | NOT RUN |
| D24 | DESR without OPL: auto-install creates `+OPL` with the server's OPNPS2LD.ELF, then installs games; channels boot through it | `+OPL` 128 MiB PFS, ELF hash = manifest, games reach title screen | NOT RUN |
| D25 | OPL already present: server OPL is never written; a configured partition that is missing stops with a reason | | NOT RUN |
| D26 | Text readable on the DESR's TV output: bold letters, nothing cut off at the screen edges, colours as in `docs/ui-preview-*.png` | | NOT RUN |
| D27 | Install speed: Diagnostics shows `PASS fileXio transfer buffer 64 KiB`; copy screen line `network / CRC / HDD write` MiB/s and validate line `HDD read / CRC` (16 KiB buffer: 3.5 MiB/s total, 64 KiB: 4.0) | note all four numbers | NOT RUN |
| D17 | Remove one of two games, reboot | other game and installer still work | NOT RUN |
| D30 | Press START during "validating" of one game; then Installed Games > that game > Verify game data | install finishes with `Installed CRC-32: SKIPPED`, channel created and boots, game listed `installed, NOT VERIFIED`; Verify game data reports CRC equal and the label becomes `installed`; SELECT+O during Verify game data changes nothing | NOT RUN |
| D31 | L2 / R2 in Install Games, Install All, Installed Games, Remove Games | order cycles name A-Z / Z-A / size; search keeps folders, filters names; X/Square act on the shown entry | NOT RUN |
| D32 | Installed Games > game > Rename, then return to the XMB; then Repair XMB channel | new title in the XMB; partition name and game unchanged; title kept after Repair | NOT RUN |
| D33 | Unplug the network cable at ~40 % of a > 1 GiB copy; reconnect; Installed Games > game > Resume copy (also: Install All lists it as 
esume copy) | resumes at the last 256 MiB checkpoint, full read-back CRC equal, channel boots | NOT RUN |
| D34 | Install Games from USB: one .iso and one .zso on an exFAT stick (one > 4 GiB) | both install and verify; ZSO decompressed on the PS2 gives the same CRC as the server install | NOT RUN |
| D35 | PS1: .VCD + POPSTARTER.KELF + POPS.ELF + IOPRP252.IMG in udpfsd's POPS folder; install one PS1 game | PP.SLUS-xxxxx..TITLE channel appears and boots through POPStarter; __common/POPS has POPS.ELF, IOPRP252.IMG and the VMC folder; Installed Games lists it as PS1 game (POPStarter); Delete removes it. If the DESR refuses the KELF: rebuild with POPSTARTER_ELF=... (signed like the other KELFs) | NOT RUN |
| D36 | A .zso game from the server: copy screen shows a higher total MiB/s than before (only compressed bytes cross the network); CRC/read-back equal | ZSO decompressed on the PS2; a ZSO the PS2 cannot read falls back to the server | NOT RUN |
| D37 | gamedb = PS2DB.xml: install a game; XMB info screen | release date, developer, publisher, genre shown; XMB still boots normally | NOT RUN |
| D38 | Install All with "power off when done" on; and auto-install with power_off_after_install = yes | 15 s countdown (any button cancels), then the DESR switches off; next boot: games installed and complete | NOT RUN |
| D39 | Installed Games > game > Back up to USB (exFAT stick; a PS2 game > 4 GiB and a PS1 game); also a FAT32 stick with a > 4 GiB game | file in DVD/ (or CD/, POPS/) named <ID>.<title>.iso/.VCD, CRC equals the install CRC, read back equal; OPL runs the copy from USB; FAT32 > 4 GiB fails with the exFAT message and leaves no partial file | NOT RUN |
| D40 | DHCP: start with the router on (status shows the leased IP + DHCP, server found); with ip_mode=static (Network Settings > IP address: fixed); with the DESR on a switch without DHCP | lease shown in the status line and Diagnostics; static mode as before; no DHCP: after ~12 s the fallback IP is used and the status says so | NOT RUN |
| D41 | Fast copy (hddpump): Diagnostics shows `hddpump loaded, in use`; install a > 2 GiB ISO and a ZSO; compare total MiB/s with Network Settings > Copy engine: basic | higher total speed with fast; CRC/read-back equal in both modes; a pulled network cable mid-copy still fails cleanly and Resume works | NOT RUN |
| D42 | Pause: START at ~30 % of a copy; then Resume copy. Also: switch the DESR off mid-copy (~50 %), restart, Resume copy | pause message shows MiB on the HDD; resume shows 'checking the data already copied', continues near the stop point (power cut: at most one 64 MiB part again); final read-back CRC equal; game boots | NOT RUN |
| D29 | Start-up without udpfsd running, then start udpfsd and use Network Settings > Restart; separately: open Installed Games / Details during the first 8 s while the server is being searched | menu appears at once with `looking for udpfsd...`, then `NETWORK_ERROR (udpfsd not found)` without a key press; HDD menus work during the search (HDD and network share DEV9); with the server up and `auto_install = yes` the countdown starts only if no menu entry was chosen | NOT RUN |
| D28 | Remove Games: select 2 of 3 games (Square), hold R1 + X | summary `2 removed, 0 failed`; each game's `PP.` partition (and any `__.`) gone (Installed Games no longer lists them, free space grows); third game and installer still boot; O / no R1 removes nothing | NOT RUN |

### Acceptance chain for D4 / D5 (each must be observed)

UDPFS source parsed -> hidden HDL partition created -> complete logical
ISO copied -> complete installed data read back -> CRC-32 matches source
stream (shown on the Finished screen) -> HDL metadata validates -> PP.
resource partition created -> per-game `EXECUTE.KELF` present ->
`res/info.sys` present -> both jacket images present -> PPAA/system.cnf
validates -> cold reboot -> native DESR XMB shows the game -> selecting
the channel runs OPL-Launcher -> OPL-Launcher resolves the matching
`__.` partition -> OPL starts the intended game -> game reaches its
normal title screen.

Only then mark D4 (ISO path) or D5 (ZSO path) `PASS`.

## Benchmarks (plan section 31) - after D4/D5 pass

| Buffer | Source read MiB/s | Read-back MiB/s | End-to-end MiB/s | Status |
|---|---|---|---|---|
| 256 KiB | - | - | - | NOT RUN |
| 512 KiB | - | - | - | NOT RUN |
| 1 MiB (shipped) | - | - | - | NOT RUN |
| 2 MiB | - | - | - | NOT RUN |

`STREAM_BUF_SIZE` in `src/hdl_install.h`.
