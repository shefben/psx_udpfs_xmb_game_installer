# Provenance and licenses

## Embedded IOP modules

| Module | Source | Revision | License |
|---|---|---|---|
| iomanX.irx, fileXio.irx, poweroff.irx, ps2dev9.irx, ps2atad.irx, ps2fs.irx, sio2man.irx, padman.irx | PS2SDK installed with the toolchain (`$PS2SDK/iop/irx`) | toolchain build | AFL 2.0 (PS2SDK) |
| ps2hdd-hdl.irx (`vendor/irx/`, embedded as `ps2hdd_hdl_irx`) | source build of HDLGameInstaller `apa-hdl` (cdc6636 = ec37c81) + PS2SDK 1de4bd8 libapa in `ps2dev/ps2dev:v1.0`, with `patches/apa-hdl/0001` | cdc6636 | GPL-2.0 (HDLGameInstaller) / AFL-2.0 (libapa) |
| hdlfs.irx | HDLGameInstaller `irx/hdlfs.irx` | ec37c81 | GPL-2.0 |
| smap.irx, ministack.irx, udpfs_ioman.irx | Neutrino `iop/smap`, `iop/ministack`, `iop/udpfs` (UDPFS_IOMAN=1), built here with `patches/neutrino/0001` | 7be8de2 | Neutrino license (see reference/neutrino/LICENSE) |

The unmodified source build reproduces HDLGameInstaller's vendored
`irx/ps2hdd-hdl.irx` byte-for-byte (sha256 58b217e9...); the shipped
driver is the same build with one source change (`__.` HDL partitions
become removable). Details, hashes and disassembly:
`tools/driver/README.md`, `docs/driver/`. `hdlfs.irx` is HDLGameInstaller's
binary, unchanged.

Because GPL-2.0 modules are embedded, the installer ELF as distributed
is GPL-2.0.

## Upstream code copied or adapted

| This project | Upstream | Revision | What |
|---|---|---|---|
| src/hdl_install.c | ps2-usbhdl src/hdl.c | b681bc6 | partition create, HIOCADDSUB with FIO_O_WRONLY, hdlfs format, hdl0: streaming loop |
| src/hdl_plan.c | ps2-usbhdl src/iso.c | b681bc6 | APA size buckets and sub-partition spill (rounding changed to round up) |
| src/iso9660.c | ps2-usbhdl src/iso.c | b681bc6 | PVD parse, root-directory walk, SYSTEM.CNF BOOT2 extraction; moved behind GameSource |
| src/iso9660.c | Open-PS2-Loader src/supportbase.c | 3e3f34e | DVD9 layer-1 detection (PVD at maxLBA, layer1 = maxLBA-16) |
| src/hdl_header.c | HDLGameInstaller hdlfs/hdlfs.h | ec37c81 | header parse; installer marker in the unused `reserved` field |
| src/iop_boot.c | ps2-usbhdl src/iop.c | b681bc6 | embedded module loading, ps2hdd-hdl `-o 4 -n 128` args |
| src/ui.c | ps2-usbhdl src/ui.c | b681bc6 | libpad init and edge-detected polling |
| src/hdl_header.h | HDLGameInstaller hdlfs/hdlfs.h | ec37c81 | HDLFS_FormatArgs and hdl_game_info layout |
| src/partname.c | hdl-dump hdl.c `hdl_pname()` | 32c296c | partition naming rules (host test compares against the original) |
| src/apa_osd_header.c | hdl-dump hdl.c `hdd_inject_header()` | 32c296c | PPAA magic + system.cnf descriptor/data |
| src/opl_dependency.c | OPL-Launcher src/main.c | 6da1af2 | conf_hdd.cfg / +OPL resolution |
| src/hdd_partitions.c | PS2SDK ee/rpc/hdd/src/libhdd.c | d317f8f | PFS create/format args, HDD status/space semantics |
| src/xmb_text.c | OPL-Launcher README | 6da1af2 | system.cnf and info.sys templates |

OPL-Launcher itself is built unmodified from 6da1af2 and signed; every
game channel gets its own copy of that KELF.
