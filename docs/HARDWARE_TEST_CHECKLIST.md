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
| P12 | Signed release in canonical `KELF_MODE=mbr` | needs a kelftool fork with `encrypt mbr` | NOT RUN |
| P13 | udpfsd mounts (`-install-dir` / `udpfsd.cfg` folders): listing, always read-only, no path escape, mounts-only mode | Go tests in `patches/udpfsd/0002` (`make test-udpfsd`) | PASS |
| P14 | Batch selection/summary logic | `test_batch.c` | PASS |
| P15 | udpfsd.cfg parsing/precedence; game prep: ISO/ZSO probe, titles (CFG > game list > file name), art lookup + 74x108 PNG, cover download (local test server), manifest format/atomic write, scan cache | Go tests in `patches/udpfsd/0002` (`make test-udpfsd`) | PENDING |
| P16 | PS2 manifest parser, server-launcher check (hash/size/KELF), OPL cfg decision, auto-install selection, journal fields | `test_manifest.c`, `test_server_assets.c`, `test_batch.c`, `test_state.c` | PENDING |

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
| D1 | Install/Repair Installer XMB App from the bootstrap ELF | `PP.UDPFS-INSTALLER` verified; game install enabled | NOT RUN |
| D2 | Cold reboot, installer channel visible in XMB, launches, reaches NETWORK_READY and the browser | | NOT RUN |
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
| D19 | Jackets prepared by the server (ART `_COV` and a downloaded cover) appear in the XMB | | NOT RUN |
| D20 | OPL cfg copied to `<OPL partition>/CFG/<ID>.cfg`; its compatibility modes take effect when the channel boots; behaviour of a `$VMC_0=` entry whose VMC does not exist yet; an existing cfg is kept | | NOT RUN |
| D21 | Channels use the server OPL-Launcher (Diagnostics: "OPL-Launcher KELF from server", journal `launcher_source=server`); a mismatching server copy falls back to the embedded one | | NOT RUN |
| D22 | Fresh console (no `PP.UDPFS-INSTALLER`), `auto_install = yes`, one ISO + one ZSO on the server: start the bootstrap ELF, no further input | installer partition created, both games complete the acceptance chain, console returns to the XMB, both games boot | NOT RUN |
| D23 | `LoadExecPS2("rom0:OSDSYS")` after auto-install lands in the DESR XMB | | NOT RUN |
| D17 | Remove one of two games, reboot | other game and installer still work | NOT RUN |

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
