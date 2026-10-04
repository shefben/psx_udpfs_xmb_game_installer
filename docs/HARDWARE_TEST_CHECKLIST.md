# Real-hardware test checklist

Status legend: `PASS`, `FAIL`, `NOT RUN`. Nothing below has been run on a
DESR yet.

| # | Test | Expected | Status |
|---|---|---|---|
| 1 | Installer launched from bootstrap ELF | boots, Diagnostics shows HDD ok and 0 IOP failures | NOT RUN |
| 2 | Installer self-installs | PP.UDPFS-INSTALLER verified; appears in XMB | NOT RUN |
| 3 | Installer launched from XMB | boots, reaches NETWORK_READY and the UDPFS browser | NOT RUN |
| 4 | Plain ISO under 4 GiB | installs, channel appears, launches | NOT RUN |
| 5 | ZSO source under 4 GiB | server decompresses, installs, channel appears, launches | NOT RUN |
| 6 | ISO above 4 GiB | sub-partitions created; verify passes | NOT RUN |
| 7 | ZSO with logical size above 4 GiB | logical-size install works | NOT RUN |
| 8 | Invalid .iso file | "Not a valid PS2 ISO", no HDD change | NOT RUN |
| 9 | Network loss before install | ERR_NETWORK / source error, no HDD change | NOT RUN |
| 10 | Network loss during stream | ERR_SOURCE_READ, no PP. channel | NOT RUN |
| 11 | Power cycle after partial hidden install | Repair shows INCOMPLETE copy | NOT RUN |
| 12 | OPL missing | channel creation blocked, clear error | NOT RUN |
| 13 | Existing valid hidden game, no PP. | Create XMB channel; data untouched | NOT RUN |
| 14 | Existing PP., no hidden game | ORPHANED channel, removable | NOT RUN |
| 15 | Delete complete game | PP. removed first, then __. | NOT RUN |
| 16 | Reboot after successful install | channel still visible | NOT RUN |
| 17 | Select game from XMB | that channel's OPL-Launcher runs | NOT RUN |
| 18 | OPL-Launcher hand-off | the intended HDL game boots | NOT RUN |
| 19 | ZSO data check | installed sector 16 / last sector equal decompressed ISO (installer verifies this automatically) | NOT RUN |
| 20 | DVD9 game | layer break recorded, game reads past layer 0 | NOT RUN |

Before the first wet run, back up the APA table (e.g. `hdl_dump toc`
or pfsshell) so the disk can be restored if anything goes wrong.

## Benchmarks (plan section 31)

| Buffer | Source read MiB/s | End-to-end MiB/s | Status |
|---|---|---|---|
| 256 KiB | - | - | NOT RUN |
| 512 KiB | - | - | NOT RUN |
| 1 MiB (shipped) | - | - | NOT RUN |
| 2 MiB | - | - | NOT RUN |

Change `STREAM_BUF_SIZE` in `src/hdl_install.h` to benchmark.
