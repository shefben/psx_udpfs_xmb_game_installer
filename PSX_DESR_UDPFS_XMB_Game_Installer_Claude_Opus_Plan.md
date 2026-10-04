# PSX DESR UDPFS XMB Game Installer
## Claude Opus Implementation Plan

**Goal:** Build a native PlayStation 2 / PSX DESR installer application that appears as its own application/channel in the PSX DESR XMB, browses PS2 games served by `udpfsd`, installs either plain ISO or ZSO-backed virtual ISO sources directly to the internal DESR HDD in HDL format, and creates a separate native XMB channel for every installed game. Every game channel must contain its own copy of the signed OPL-Launcher KELF so selecting the channel in the XMB launches that specific HDL game through OPL.

This document is an implementation specification. Do not substitute a different architecture, rename the partition scheme, omit the per-game OPL-Launcher copy, replace UDPFS with SMB/NBD/TCP, or write compressed ZSO bytes directly into HDL partitions.

---

# 1. Non-negotiable project requirements

The finished project shall satisfy all of the following:

1. The **installer itself must be installable as a persistent application/channel in the PSX DESR XMB**.
2. The installer must use `pcm720/udpfsd` as the network game source.
3. The installer must use the PS2-side UDPFS implementation from Neutrino, specifically `udpfs_ioman.irx`, so remote files are exposed through the normal `iomanX`/`fileXio` file API as `udpfs:`.
4. Supported source formats for version 1 are:
   - normal `.iso`
   - `.zso` served by `udpfsd` through its virtual decompressed `.iso` view.
5. ZSO decompression must happen on the server through `udpfsd`. The DESR must receive the logical uncompressed ISO stream.
6. The installer must **never write raw ZSO compressed bytes into an HDL partition**.
7. Games must be installed to the DESR internal HDD in standard HDL/HDLoader form using the proven `ps2hdd-hdl.irx` + `hdlfs.irx` path.
8. Every completed game installation must appear as its **own native PSX DESR XMB channel**.
9. Every game XMB channel must contain its **own physical copy** of `EXECUTE.KELF`, where `EXECUTE.KELF` is a signed build of `OPL-Launcher`.
10. The same signed OPL-Launcher payload may be reused byte-for-byte for all game channels. It does not need to be recompiled per game. The per-game context is derived from the resource partition name.
11. A game must use a paired partition layout:
    - hidden HDL game-data partition: `__.<suffix>`
    - visible PFS resource/channel partition: `PP.<suffix>`
12. The two partition names must differ only in the first two characters. Everything after `PP` / `__` must be identical.
13. The visible game channel must not be created until the HDL game data has been completely written and validated.
14. Removing a game must remove the visible `PP.` resource partition first, then the hidden `__.` HDL partition.
15. A failed or interrupted game copy must never leave a visible XMB channel pointing to incomplete data.
16. The installer must detect whether the OPL runtime required by OPL-Launcher is actually installed before it commits an XMB-ready game installation.
17. Version 1 must favor correctness and recoverability over cleverness. Do not add unrelated features until the end-to-end hardware path works.

---

# 2. Source repositories and exact reference points

Audit these repositories before editing anything. Use the source code as authority when README text disagrees with code.

At the time this plan was written, the following revisions were inspected:

| Project | Repository | Inspected revision |
|---|---|---|
| UDPFS server | `https://github.com/pcm720/udpfsd` | `58d7c8f11ac196d4a5ca7b65b78743fbeedfdd80` tree |
| Neutrino | `https://github.com/ps2max32/neutrino` | source around `7be8de2798c99af433c91993e021746a54d6800d` |
| OPL-Launcher | `https://github.com/ps2homebrew/OPL-Launcher` | `6da1af2e175c09da4cbb6da50ccf472132f8f114` |
| HDLGameInstaller | `https://github.com/ps2homebrew/HDLGameInstaller` | `ec37c81f8edb6fc45e2be51c86a322780641275a` |
| ps2-usbhdl | `https://github.com/binkynz/ps2-usbhdl` | `b681bc64a409753922d7d145ac05fa2f9039ce7f` |
| hdl-dump | `https://github.com/ps2homebrew/hdl-dump` | `32c296c69cf9c263fcbe035004aa28c345b3b279` |
| PS2SDK | `https://github.com/ps2dev/ps2sdk` | current source compatible with the project toolchain |

If any pinned source has moved substantially, re-audit only the affected interface before implementation. Do not silently assume a changed struct layout is still binary-compatible.

Important source files to study before writing replacement code:

<paths>
ps2-usbhdl/src/hdl.c
ps2-usbhdl/src/iso.c
ps2-usbhdl/src/iop.c
ps2-usbhdl/src/main.c

HDLGameInstaller/hdlfs/hdlfs.h
HDLGameInstaller/hdlfs/main.c
HDLGameInstaller/apa-hdl/src/hdd_fio.c

neutrino/iop/udpfs/src/udpfs_ioman.c
neutrino/iop/udpfs/src/udpfs_core.c
neutrino/iop/udpfs/src/udprdma.c
neutrino/iop/ministack/src/main.c
neutrino/ee/loader/config/bsd-udpfs-hdd.toml

OPL-Launcher/src/main.c
OPL-Launcher/README.md

hdl-dump/hdl.c
hdl-dump/hdl_dump.c
hdl-dump/README.md

ps2sdk/ee/rpc/hdd/src/libhdd.c
</paths>

---

# 3. Chosen architecture

Use `ps2-usbhdl` as the primary application skeleton because it already has a working console-side pipeline for:

- ISO9660 parsing.
- `SYSTEM.CNF` parsing.
- startup-ID extraction.
- APA partition sizing.
- HDL partition creation.
- `HDLFS_FormatArgs`.
- sequential ISO-to-HDL streaming.
- internal HDD management.
- controller-driven UI.

Replace its USB source layer with an abstract source layer whose first network implementation is UDPFS.

Reuse or vendor the proven IRX modules:

<modules>
iomanX.irx
fileXio.irx
poweroff.irx
ps2dev9.irx
ps2atad.irx
ps2hdd-hdl.irx
ps2fs.irx
hdlfs.irx
smap.irx
ministack.irx
udpfs_ioman.irx
sio2man.irx
padman.irx
</modules>

Do not use NBD for game installation.

Do not make the PC modify the DESR partition table remotely.

The PC/server serves files only. APA/PFS/HDL creation and all HDD writes happen locally on the DESR.

The data path is:

<flow>
PC/NAS ISO or ZSO
    -> udpfsd
    -> UDPFS / UDPRDMA
    -> udpfs_ioman.irx
    -> udpfs:/logical-game.iso
    -> installer source abstraction
    -> 1 MiB aligned EE buffer
    -> hdl0:
    -> internal DESR HDD
</flow>

---

# 4. Critical ZSO behavior

`udpfsd` already supports transparent ZSO decompression.

When compression support is enabled, a server-side file such as:

<example>
Gran Turismo 4.zso
</example>

is advertised to the client with a virtual ISO name similar to:

<example>
Gran Turismo 4.zso.iso
</example>

and the reported size is the uncompressed disc-image size.

When the client opens that virtual `.iso` path, `udpfsd` serves decompressed ISO bytes.

Therefore:

- The DESR installer must open the exact path returned by the UDPFS directory listing.
- It must not strip `.iso` from a virtual ZSO path.
- It must not intentionally open the physical `.zso` filename.
- It must run the same ISO9660 parser against plain ISO and ZSO-backed virtual ISO sources.
- A ZSO source is only an input-storage optimization. The installed HDL game data is the uncompressed logical disc image.

For version 1, the file browser shall display:

- plain `.iso`
- virtual `.zso.iso`

Do not deliberately expose CSO or CHD in the UI for version 1 even though `udpfsd` may support them.

Every selected source must still pass ISO9660 validation before the installer enables installation.

---

# 5. Project layout

Refactor the project into explicit subsystems. Use this layout unless an existing equivalent already exists:

<layout>
src/
    main.c
    app_state.c
    app_state.h

    iop_boot.c
    iop_boot.h

    network.c
    network.h

    source.c
    source.h
    source_udpfs.c
    source_udpfs.h

    iso9660.c
    iso9660.h

    hdl_install.c
    hdl_install.h
    hdd_partitions.c
    hdd_partitions.h

    pfs_channel.c
    pfs_channel.h
    apa_osd_header.c
    apa_osd_header.h

    opl_dependency.c
    opl_dependency.h
    opl_launcher_payload.c
    opl_launcher_payload.h

    xmb_installer_app.c
    xmb_installer_app.h
    xmb_game_channel.c
    xmb_game_channel.h

    transaction.c
    transaction.h

    settings.c
    settings.h

    ui.c
    ui.h

assets/
    installer/
        info.sys.template
        jkt_001.png
        jkt_002.png
    game/
        default_jkt_001.png
        default_jkt_002.png

vendor/
    irx/
    opl-launcher/
        EXECUTE.KELF

tools/
    package-opl-launcher.sh
    package-installer-kelf.sh
    verify-assets.py

test/
    host/
    iso/
</layout>

Do not put unrelated HDD, network, ISO parsing, and XMB-header logic back into one `main.c`.

---

# 6. Source abstraction

Create a small logical-file interface so the installer is not tied directly to POSIX `open/read/lseek`.

Use a structure equivalent to:

<code>
typedef struct GameSource GameSource;

typedef struct {
    int (*open)(GameSource *src, const char *path);
    int (*close)(GameSource *src);
    int (*read)(GameSource *src, void *buf, uint32_t size);
    int64_t (*seek)(GameSource *src, int64_t offset, int whence);
    int64_t (*size)(GameSource *src);
} GameSourceOps;
</code>

Version 1 only needs `source_udpfs.c`, but the ISO parser must use `GameSource` rather than calling `open()` directly.

Rules:

- All offsets and total sizes in the source abstraction are 64-bit.
- Do not let PS2SDK's signed 32-bit `off_t` silently corrupt files above 2 GiB.
- If the underlying UDPFS I/O path uses `lseek64`, use it.
- Read failures must preserve a distinct error code from EOF.
- A short read before expected EOF is an installation error.
- The source must be seekable because metadata parsing and future resume support require seeking.

---

# 7. IOP boot sequence

Create one authoritative `boot_iop_modules()` function.

Required order:

1. `SifInitRpc(0)`
2. Reset and synchronize the IOP.
3. Reinitialize RPC / IOP heap / loadfile.
4. Apply:
   - `sbv_patch_enable_lmb()`
   - `sbv_patch_disable_prefix_check()` if required by the selected embedded modules.
5. Load `iomanX.irx`.
6. Load `fileXio.irx`.
7. Initialize `fileXio`.
8. Load `poweroff.irx`.
9. Load `ps2dev9.irx`.
10. Load `ps2atad.irx`.
11. Load `ps2hdd-hdl.irx`.
12. Load `ps2fs.irx`.
13. Load `hdlfs.irx`.
14. Load `smap.irx`.
15. Load `ministack.irx` with exactly one `ip=<local-ip>` argument.
16. Load `udpfs_ioman.irx`.
17. Load controller modules if not already available.

Use the `ps2hdd-hdl.irx` argument form already proven by `ps2-usbhdl` / HDLGameInstaller:

<example>
-o
4
-n
128
</example>

encoded as the null-separated module argument blob expected by the IRX loader.

Do not load both the stock PS2 HDD driver and `ps2hdd-hdl.irx`.

Do not load a second DEV9 implementation after the HDD stack is already active.

After each module load:

- check both the loader return value and module result.
- log failures with the module name.
- abort before any destructive HDD operation if a required module failed.

---

# 8. Network configuration

Neutrino's current `ministack` parses a static `ip=` argument. Do not pretend DHCP exists in this module unless it is explicitly implemented and tested during this project.

Version 1 network policy:

- Use a configurable static DESR IP.
- Default only for first-run bootstrap: `192.168.1.10`.
- Store persistent settings inside the installer's own PFS application partition.
- Use a simple text file:
  - `pfs0:/config/network.ini`
- Required field:
  - `local_ip=<IPv4 address>`
- Validate all four octets before passing the value to `ministack`.
- Invalid configuration falls back to the compiled default and shows a warning.
- Changing the local IP requires a controlled IOP/network restart before reconnecting.

UDPFS uses discovery/data handling provided by the Neutrino client and `udpfsd`.

Version 1 shall assume exactly one intended `udpfsd` server is visible on the LAN. Do not invent a server-selection protocol unless the current UDPFS client already exposes enough peer information to do it safely.

The UI must have explicit network states:

<states>
NETWORK_DOWN
NETWORK_STARTING
NETWORK_DISCOVERING
NETWORK_READY
NETWORK_ERROR
</states>

Do not allow the game browser to run unless the state is `NETWORK_READY`.

---

# 9. UDPFS browser

Browse through the registered `udpfs:` device.

Required operations:

- `dopen`
- `dread`
- `dclose`
- `getstat`
- file open/read/seek/close

Start at:

<path>
udpfs:/
</path>

Support recursive directory navigation.

Display only:

- directories
- valid candidate game files ending in `.iso`, including virtual `.zso.iso`

Do not decide that a file is a valid PS2 game merely because its extension matches.

When the user highlights a candidate, asynchronously or on explicit selection:

1. open source.
2. obtain logical size.
3. read ISO9660 PVD at logical sector 16.
4. verify `CD001`.
5. locate `SYSTEM.CNF`.
6. extract `BOOT2`.
7. normalize startup ID.
8. read volume ID.
9. determine 2 KiB sector count.
10. build the install plan.
11. close source if no install begins.

Show an error such as `Not a valid PS2 ISO` rather than attempting installation when validation fails.

---

# 10. ISO metadata parser

Reuse the proven `ps2-usbhdl/src/iso.c` logic but move it behind `GameSource`.

The parser must extract:

- volume identifier.
- logical disc size.
- number of 2048-byte sectors.
- startup path/ID from `SYSTEM.CNF`.
- disc type.
- optional DVD9 layer-break metadata if available.

The parser must not infer game identity solely from the filename.

Normalize startup IDs into two forms:

<example>
BOOT form:      SLUS_203.12
Partition form: SLUS-20312
</example>

Preserve the BOOT form in `HDLFS_FormatArgs.StartupPath`.

Use the partition form for XMB/APA naming.

Disc-type constants must match `HDLFS_FormatArgs`:

- CD: `0x12`
- DVD: `0x14`

Do not change the `HDLFS_FormatArgs` layout. It must remain binary-compatible with HDLGameInstaller's `hdlfs.irx`.

Use compile-time assertions for structure size and critical offsets if the compiler permits them.

---

# 11. Canonical partition naming

Use one function for both game partitions so their suffixes can never diverge.

Input:

- canonical startup ID.
- display game title.

Output:

<example>
visible: PP.SLUS-20312..GAME_TITLE
hidden:  __.SLUS-20312..GAME_TITLE
</example>

Match the `hdl-dump` naming rules closely:

- APA partition maximum length: 32 characters.
- first three characters are prefix.
- positions representing game ID are normalized.
- game title is uppercase.
- characters outside `[A-Z0-9]` in the title suffix become `_`.
- truncate the title only after preserving the mandatory ID portion.
- visible and hidden names must have exactly the same bytes from index 2 onward except the prefix difference.

Implement:

<code>
int build_game_partition_pair(
    const char *startup_id,
    const char *display_title,
    char visible[33],
    char hidden[33]);
</code>

Add host-side tests for:

- SLUS.
- SCUS.
- SLES.
- SCES.
- SLPS.
- SLPM.
- punctuation.
- spaces.
- long titles.
- non-ASCII bytes.
- duplicate sanitized titles.

If two titles generate the same partition pair, treat it as an existing-title conflict and present repair/reinstall options. Do not silently create a different ad-hoc name.

---

# 12. HDL game-data partition

The actual game data must live in the hidden `__.` partition.

Base the implementation on `ps2-usbhdl/src/hdl.c`.

Required sequence:

1. Confirm the target hidden partition does not already exist, or enter explicit repair/reinstall handling.
2. Calculate required APA main and sub-partition sizes.
3. Create the HDL partition with `ps2hdd-hdl.irx`.
4. Add required sub-partitions through `HIOCADDSUB`.
5. Build `HDLFS_FormatArgs`.
6. Call:
   - `fileXioFormat("hdl0:", "hdd0:<hidden-name>", ...)`
7. Mount:
   - `fileXioMount("hdl0:", "hdd0:<hidden-name>", FIO_MT_RDWR)`
8. Open `hdl0:` for sequential writes.
9. Stream the logical uncompressed ISO from UDPFS.
10. Flush/close.
11. Unmount.
12. Reopen/read metadata and verify the HDL header.
13. Verify expected logical byte count.
14. Mark the data stage complete.

Use `FIO_O_*` flags, not host/newlib flags where the IOP driver expects PS2 I/O flag values. Preserve the existing `ps2-usbhdl` fix for `FIO_O_WRONLY`.

Use the existing APA size buckets proven by the PS2 HDD driver:

<values>
128M
256M
512M
1G
2G
4G
</values>

Do not assume arbitrary partition sizes are accepted by the driver.

---

# 13. Streaming implementation

Use a fixed aligned streaming buffer initially:

<value>
1 MiB, 64-byte aligned
</value>

Do not optimize chunk size before correctness tests pass.

Required stream loop:

<flow>
source_read()
    -> verify positive byte count
    -> fileXioWrite(hdl_fd)
    -> require exact write count
    -> update 64-bit total
    -> update progress at a throttled rate
</flow>

Requirements:

- `written` is 64-bit.
- expected total is 64-bit.
- a source short read before the expected end is a hard error.
- a short HDD write is a hard error.
- UI refresh must not happen on every packet/read.
- progress updates should occur at most a few times per second.
- calculate MiB/s using 64-bit counters.
- final success requires `written == expected_total`.

On a stream failure:

1. close source.
2. close `hdl0:`.
3. unmount `hdl0:`.
4. leave the transaction journal indicating failure.
5. do not create the visible `PP.` channel.
6. offer `Retry from start` and `Delete incomplete install`.

Version 1 does not need resumable partial writes. Design the journal so resume can be added later, but do not ship an unverified resume algorithm.

---

# 14. OPL runtime dependency check

Per-game OPL-Launcher does not contain OPL itself. It locates an installed `OPNPS2LD.ELF`.

Before allowing an installation to reach `XMB_READY`, reproduce OPL-Launcher's runtime resolution logic.

Check:

1. Mount `hdd0:__common`.
2. Read:
   - `OPL/conf_hdd.cfg`
3. Resolve the configured OPL partition if present.
4. If the config is absent or invalid, use OPL-Launcher's fallback convention:
   - `+OPL`
5. Mount the resolved partition.
6. verify the expected `OPNPS2LD.ELF` exists.
7. unmount cleanly.

If OPL cannot be resolved:

- show `OPL runtime not found`.
- do not create a game XMB channel that would be known-broken.
- the already-copied hidden HDL game may remain installed and be repairable later.
- provide `Create XMB channel later` from Manage/Repair mode once OPL is present.

Do not bundle or silently replace the user's OPL installation in version 1.

---

# 15. Per-game OPL-Launcher payload

Build OPL-Launcher from the pinned upstream source.

Create a signed KELF on the host, following the OPL-Launcher documented KELF packaging method.

The build pipeline must produce:

<artifact>
vendor/opl-launcher/EXECUTE.KELF
</artifact>

Treat this as an immutable binary asset embedded into the installer build or packaged alongside it.

Every game channel must contain a separate copy:

<path>
pfs:/EXECUTE.KELF
</path>

Do not use a shortcut where all channels point to one global launcher file.

Do not modify OPL-Launcher to hard-code a game ID.

OPL-Launcher already determines the source partition from how it was launched. For a `PP.` resource partition that is not itself an HDL game, it changes the first two characters to `__` and searches the matching hidden HDL partition.

Therefore the paired partition naming is part of the boot ABI and must not drift.

---

# 16. Per-game visible XMB resource partition

After the hidden HDL game passes validation and OPL dependency checks pass, create the visible PFS partition.

Use:

<example>
PP.SLUS-20312..GAME_TITLE
</example>

Allocate the smallest supported PFS partition size that is safe for:

- PFS metadata.
- `EXECUTE.KELF`.
- `res/info.sys`.
- `res/jkt_001.png`.
- `res/jkt_002.png`.
- future small metadata.

Use **128 MiB** for version 1. Do not dynamically shrink below the HDD driver's supported 128 MiB minimum.

Creation sequence:

1. `fileXioOpen("hdd0:<visible>,,,128M,PFS", FIO_O_RDWR | FIO_O_CREAT, ...)`
2. close partition handle.
3. format with:
   - `fileXioFormat("pfs:", "hdd0:<visible>", ...)`
4. mount as `pfs0:`.
5. create `pfs0:/res`.
6. copy per-game `EXECUTE.KELF`.
7. generate `pfs0:/res/info.sys`.
8. create/copy `pfs0:/res/jkt_001.png`.
9. create/copy `pfs0:/res/jkt_002.png`.
10. flush and unmount.
11. patch the partition's OSD/XMB header with `system.cnf`.
12. read back and verify all required metadata.

If any step fails, remove the visible `PP.` partition. Leave the already-valid hidden game partition intact so the operation can be repaired without recopying the ISO.

---

# 17. Mandatory `system.cnf` for every game channel

Every visible game partition uses:

<config>
BOOT2 = pfs:/EXECUTE.KELF
VER = 1.00
VMODE = NTSC
HDDUNITPOWER = NICHDD
</config>

For the PSX DESR target, use `NTSC`.

Do not use `BOOT2 = PATINFO` for this project.

The first-generation DESR path is one of the reasons the PFS KELF method is being used.

The `system.cnf` that tells the XMB how to launch the channel belongs in the partition's OSD/PAT header area, not merely as an ordinary PFS file.

---

# 18. OSD/XMB partition-header writer

Port the minimum required, well-understood portion of `hdl-dump`'s `hdd_inject_header()` behavior into an on-console helper.

Call this subsystem `apa_osd_header`.

Do not copy the entire PC `hdl-dump` program.

For the DESR channel use case, implement the PPAA header and `system.cnf` entry first.

Reference layout from `hdl-dump`:

- PPAA magic begins at partition-relative offset `0x1000`.
- `system.cnf` descriptor:
  - offset field at `0x1010`
  - length field at `0x1014`
- system.cnf data:
  - partition-relative offset `0x1200`
- descriptor offset value:
  - `0x0200`, relative to `0x1000`

Perform read-modify-write rather than blindly zeroing unrelated bytes.

Implementation requirements:

1. Open the target PFS partition through `hdd0:<partition>` in raw partition mode.
2. Read the region containing the PPAA header.
3. Preserve all unrelated existing bytes.
4. write the PPAA magic.
5. write little-endian system.cnf offset.
6. write little-endian system.cnf length.
7. write the exact generated `system.cnf` bytes at `0x1200`.
8. flush/close.
9. reopen and verify byte-for-byte.
10. never write outside the reserved header area as part of this operation.

Do not inject `boot.kelf` into PATINFO for game channels. The actual launcher KELF lives in PFS as `pfs:/EXECUTE.KELF`.

`icon.sys` and `list.ico` are not required for the DESR XMB path. Keep them out of the version-1 critical path. They may be added later for HDD OSD compatibility.

Before finalizing this writer, compare its bytes against a known-good PP partition produced by `hdl_dump modify_header` using the same `system.cnf`.

Create a host-side binary comparison test for the relevant PPAA/header region.

---

# 19. `res/info.sys` generation

Every game `PP.` partition must contain:

<path>
pfs0:/res/info.sys
</path>

Generate it deterministically.

Template:

<config>
title = <GAME TITLE>
title_id = <NORMALIZED GAME ID> (<REGION>)
title_sub_id = 0
release_date =
developer_id =
publisher_id =
note =
content_web =
image_topviewflag = 0
image_type = 0
image_count = 1
image_viewsec = 600
copyright_viewflag = 0
copyright_imgcount = 0
genre =
parental_lock = 1
effective_date = 0
expire_date = 0
area = J
violence_flag = 0
content_type = 255
content_subtype = 0
</config>

Region label mapping:

- `SLUS`, `SCUS` -> `NTSC-U`
- `SLES`, `SCES` -> `PAL`
- `SLPS`, `SLPM`, `SCPS`, `SCAJ`, `SCKA` -> `NTSC-J`
- unknown prefix -> `UNKNOWN`

Do not block installation only because the region mapping is unknown.

Use the real game title gathered from ISO metadata as the default `title`.

Allow the user to edit the display title before committing the channel, but do not allow editing the startup ID.

Escape or remove line breaks and control characters from generated values.

---

# 20. Jacket images

Every visible game channel must contain:

<paths>
pfs0:/res/jkt_001.png
pfs0:/res/jkt_002.png
</paths>

Version-1 deterministic behavior:

1. Look for optional artwork on UDPFS using normalized startup ID:
   - `udpfs:/ART/<BOOT_ID>.png`
   - example: `udpfs:/ART/SLUS_203.12.png`
2. If not found, look next to the ISO:
   - `<logical-source-path>.png`
3. If neither exists, use the embedded default jacket image.
4. Copy the same selected PNG to both `jkt_001.png` and `jkt_002.png`.

Do not implement PNG resizing on the DESR in version 1.

Provide one known-good embedded PNG asset that has already been verified to render in the DESR XMB.

If a custom PNG fails basic validation, fall back to the embedded image rather than creating a broken channel.

---

# 21. Installer application as an XMB app

The installer itself must have a persistent XMB application/channel.

Use a dedicated visible PFS partition:

<partition>
PP.UDPFS-INSTALLER
</partition>

Do not pair the installer app with a hidden HDL partition.

The installer app partition contains:

<layout>
/
├── EXECUTE.KELF
├── config/
│   └── network.ini
└── res/
    ├── info.sys
    ├── jkt_001.png
    └── jkt_002.png
</layout>

The installer XMB app must use the same PFS boot method:

<config>
BOOT2 = pfs:/EXECUTE.KELF
VER = 1.00
VMODE = NTSC
HDDUNITPOWER = NICHDD
</config>

The installer `EXECUTE.KELF` is a signed KELF of the installer ELF, not OPL-Launcher.

The installer application must expose a menu action:

<menu>
Install/Repair Installer XMB App
</menu>

First bootstrap flow:

1. User launches the unsigned installer ELF through an existing homebrew entry point.
2. User selects `Install/Repair Installer XMB App`.
3. Installer creates/repairs `PP.UDPFS-INSTALLER`.
4. Installer writes its signed `EXECUTE.KELF`.
5. Installer writes its XMB resources.
6. Installer patches the PPAA/system.cnf header.
7. Installer verifies the partition.
8. User can thereafter start the installer directly from the DESR XMB.

The host build must therefore produce both:

- development/homebrew ELF.
- signed installer `EXECUTE.KELF` payload suitable for PFS launch.

Do not attempt to generate/sign KELF cryptography on the DESR.

---

# 22. Host-side KELF packaging

Add deterministic build targets for the two signed payloads.

Required outputs:

<artifacts>
dist/desr-udpfs-installer.elf
dist/installer-EXECUTE.KELF
dist/opl-launcher-EXECUTE.KELF
</artifacts>

Packaging jobs:

### OPL-Launcher

1. build upstream/pinned OPL-Launcher.
2. run the KELF packaging/signing utility using the same supported form documented by OPL-Launcher.
3. verify non-zero output.
4. embed/copy it as the per-game launcher payload.

### Installer

1. build installer ELF.
2. package/sign it into the PFS-launchable KELF.
3. verify output.
4. embed it into the bootstrap ELF if self-install is supported, or stage it beside the bootstrap ELF with an explicit packaging manifest.

If the selected KELF tool cannot be legally or practically redistributed, make it a build prerequisite and fail the packaging target with a clear error. Do not silently ship an unsigned ELF under a `.KELF` filename.

---

# 23. OPL-Launcher contract verification

Before relying on OPL-Launcher, add a compatibility test based on its actual source behavior.

Verify that when launched from:

<example>
PP.SLUS-20312..GAME_TITLE
</example>

it can derive/find:

<example>
__.SLUS-20312..GAME_TITLE
</example>

and read the hidden partition's HDL metadata.

Verify that it passes the expected arguments to OPL:

1. game startup path.
2. game start sector.
3. OPL partition name.
4. `"mini"`.

Do not rewrite this mechanism unless a real DESR test proves the upstream launcher cannot function.

The game channel's per-game KELF copy should remain a stock or minimally patched OPL-Launcher build.

Any patch to OPL-Launcher must be kept in a separate patch file and documented.

---

# 24. Transaction model

Implement explicit per-game transaction states.

Use:

<states>
TX_NONE
TX_PLANNED
TX_HDL_CREATED
TX_STREAMING
TX_HDL_COMPLETE
TX_HDL_VERIFIED
TX_CHANNEL_CREATED
TX_CHANNEL_VERIFIED
TX_COMPLETE
TX_FAILED
</states>

Persist a small journal under the installer's application partition:

<path>
pfs0:/state/install-<normalized-game-id>.ini
</path>

Fields:

<config>
source_path=
source_size=
startup_id=
visible_partition=
hidden_partition=
bytes_expected=
bytes_written=
state=
last_error=
</config>

Ordering rules:

- `PP.` channel creation is forbidden before `TX_HDL_VERIFIED`.
- `TX_COMPLETE` is only written after the visible channel passes verification.
- on startup, scan journals and detect unfinished transactions.
- never automatically delete an incomplete hidden partition without user confirmation.
- never advertise an incomplete install as a valid game channel.

---

# 25. Install operation: exact order

For one selected game, execute exactly this high-level order:

1. Validate network is ready.
2. Validate source is still accessible.
3. Open source.
4. Parse ISO metadata.
5. Build canonical partition pair.
6. Check for existing visible and hidden partitions.
7. Check HDD free space.
8. Check OPL runtime dependency.
9. Create transaction journal as `TX_PLANNED`.
10. Create hidden HDL partition.
11. Set `TX_HDL_CREATED`.
12. Format HDL metadata.
13. Set `TX_STREAMING`.
14. Stream uncompressed logical ISO.
15. Close/unmount HDL target.
16. Set `TX_HDL_COMPLETE`.
17. Read back HDL metadata and verify.
18. Set `TX_HDL_VERIFIED`.
19. Create visible `PP.` PFS channel.
20. Mount `PP.` partition.
21. copy per-game `EXECUTE.KELF`.
22. create `res`.
23. generate `res/info.sys`.
24. install `jkt_001.png`.
25. install `jkt_002.png`.
26. unmount PFS.
27. inject/patch PPAA + `system.cnf`.
28. set `TX_CHANNEL_CREATED`.
29. verify visible partition resources and header.
30. set `TX_CHANNEL_VERIFIED`.
31. set `TX_COMPLETE`.
32. close source.
33. show success.

Do not reorder channel creation ahead of game verification.

---

# 26. Existing-partition behavior

Handle the four possible pair states explicitly.

### Neither partition exists

Normal new installation.

### Hidden `__.` exists, visible `PP.` does not

Treat as one of:

- valid game without XMB channel.
- incomplete previous installation.

Inspect HDL metadata.

If metadata and size are valid:

- offer `Create/Repair XMB Channel`.
- do not recopy the game.

If invalid:

- offer `Delete Incomplete Game`.
- offer `Reinstall`.

### Visible `PP.` exists, hidden `__.` does not

Treat as a broken XMB channel.

Offer:

- `Remove Broken Channel`.

Do not launch it.

### Both exist

Validate both.

If both valid:

- show as installed.
- offer `Repair XMB Channel`, `Reinstall Game`, or `Delete`.

If `PP.` is invalid but hidden game is valid:

- rebuild only the `PP.` partition.

If hidden game is invalid:

- remove the `PP.` channel before offering data-partition repair/reinstall.

---

# 27. Delete behavior

Deletion is destructive and must be deliberately confirmed.

For a complete game pair:

1. verify selected game.
2. display both exact partition names.
3. require explicit confirmation.
4. remove visible `PP.` resource partition first.
5. verify it is gone.
6. remove hidden `__.` HDL partition.
7. verify it is gone.
8. remove transaction journal.
9. refresh installed-game list.

If the `PP.` delete succeeds but hidden delete fails, the game remains hidden from XMB, which is preferable to a visible broken channel.

Do not delete unrelated partitions by prefix wildcard.

---

# 28. XMB repair mode

Provide a dedicated `Repair XMB Channels` mode.

For every hidden game partition:

1. parse HDL metadata.
2. compute expected visible partner.
3. check visible partner.
4. validate OPL dependency.
5. if missing or invalid, offer repair.
6. repair should create only the resource partition and must not rewrite game data.

For every visible `PP.` game partition:

1. compute expected hidden partner.
2. if no valid hidden partner exists, flag `orphaned channel`.
3. offer removal.

This mode is essential because an interrupted installation can legitimately leave a valid hidden game without a visible channel.

---

# 29. UI requirements

Version 1 does not require a flashy GUI. It does require a safe, understandable interface.

Top-level menu:

<menu>
Install Games from UDPFS
Installed Games
Repair XMB Channels
Network Settings
Install/Repair Installer XMB App
Diagnostics
Exit
</menu>

Game browser row should show:

- title/filename.
- source type: `ISO` or `ZSO`.
- logical uncompressed size.
- parsed game ID when available.
- install state if already installed.

Installation screen must show:

- title.
- startup ID.
- source path.
- hidden destination partition.
- visible destination partition.
- logical bytes transferred.
- logical total.
- percentage.
- MiB/s.
- elapsed time.
- ETA.
- current stage:
  - creating HDL.
  - copying game.
  - validating.
  - creating XMB channel.
  - finished.

Never display a success message until `TX_COMPLETE`.

---

# 30. Source-type presentation

Determine UI source type without changing data semantics.

If the returned path ends in:

<suffix>
.zso.iso
</suffix>

display `ZSO`.

Otherwise if it ends in:

<suffix>
.iso
</suffix>

display `ISO`.

In both cases the installer reads an ISO byte stream.

Do not use different HDL-writing code paths for ISO and ZSO.

---

# 31. Memory and performance rules

Correctness first.

Initial transfer configuration:

- one 1 MiB aligned EE buffer.
- sequential reads.
- sequential writes.
- no entire-disc buffering.
- no PS2-side decompression.
- no per-packet UI updates.

After correctness is established, benchmark:

- 256 KiB.
- 512 KiB.
- 1 MiB.
- 2 MiB.

Select the fastest stable size on real DESR hardware.

Do not consume memory needed by the UI or IOP RPC layers merely to gain negligible throughput.

Record separately:

- source read throughput.
- destination write throughput if measurable.
- effective end-to-end throughput.

---

# 32. Error handling

All destructive functions return structured error codes.

At minimum distinguish:

<errors>
ERR_NETWORK
ERR_UDPFS_DISCOVERY
ERR_SOURCE_OPEN
ERR_SOURCE_READ
ERR_SOURCE_INVALID_ISO
ERR_SOURCE_SYSTEM_CNF
ERR_HDD_MISSING
ERR_HDD_NOT_FORMATTED
ERR_NO_SPACE
ERR_PARTITION_EXISTS
ERR_HDL_CREATE
ERR_HDL_FORMAT
ERR_HDL_MOUNT
ERR_HDL_WRITE
ERR_HDL_VERIFY
ERR_PFS_CREATE
ERR_PFS_FORMAT
ERR_PFS_MOUNT
ERR_XMB_RESOURCE_WRITE
ERR_XMB_HEADER_WRITE
ERR_XMB_VERIFY
ERR_OPL_NOT_FOUND
ERR_KELF_MISSING
ERR_USER_ABORT
</errors>

Every error screen must report:

- stage.
- high-level error.
- underlying numeric return code where useful.
- whether the hidden game partition exists.
- whether the visible channel exists.
- safe recovery action.

Do not collapse everything to `install failed`.

---

# 33. Verification rules

### HDL verification

After copying:

- reopen hidden partition.
- read HDL metadata.
- confirm startup ID.
- confirm game title is non-empty.
- confirm disc type.
- confirm expected data size/allocation information is plausible.
- confirm partition type is HDL.

### PFS/XMB verification

After channel creation:

- mount `PP.` partition.
- confirm `/EXECUTE.KELF`.
- confirm `/res/info.sys`.
- confirm `/res/jkt_001.png`.
- confirm `/res/jkt_002.png`.
- unmount.
- raw-read PPAA/header region.
- confirm PPAA magic.
- confirm system.cnf descriptor.
- confirm system.cnf content.
- confirm `BOOT2 = pfs:/EXECUTE.KELF`.

### Pair verification

Confirm:

<invariant>
visible + 2 == hidden + 2
</invariant>

in terms of suffix bytes after the prefix.

No install is complete without all three verification layers.

---

# 34. Installer XMB self-install verification

After creating `PP.UDPFS-INSTALLER`:

1. verify PFS mount.
2. verify `EXECUTE.KELF`.
3. verify installer `res/info.sys`.
4. verify installer jacket files.
5. verify PPAA/system.cnf.
6. display a message instructing the user that the installer should now appear in the XMB after the XMB refresh/reboot required by the DESR environment.

Do not delete the bootstrap ELF automatically.

---

# 35. Build-system requirements

The repository must support a clean build from a documented environment.

Add targets equivalent to:

<commands>
make all
make installer
make opl-launcher
make kelfs
make test
make dist
</commands>

`make dist` must produce a directory containing everything needed for first bootstrap and server setup, except redistributability-restricted third-party tools.

Suggested output:

<layout>
dist/
    desr-udpfs-installer.elf
    installer-EXECUTE.KELF
    opl-launcher-EXECUTE.KELF
    udpfsd-example/
        README.txt
        server-example.bat
        server-example.sh
</layout>

Do not make Docker mandatory if the project is expected to build inside an existing PS2DEV/PS2SDK environment. A container may remain optional.

---

# 36. `udpfsd` server documentation

Ship a short server guide.

Windows example:

<command>
udpfsd.exe -fsroot D:\PS2
</command>

Linux example:

<command>
udpfsd -fsroot /srv/ps2
</command>

Recommend read-only mode for the game server:

<command>
udpfsd -fsroot /srv/ps2 -ro
</command>

Document:

- UDP discovery/data port default `62966`.
- firewall must allow the UDP traffic.
- bind to the intended LAN interface if the server has multiple NICs.
- ZSO transparent decompression must remain enabled.
- `.zso` files will appear to the client through a virtual `.iso` path.
- the server machine performs ZSO decompression.

Suggested server directory:

<layout>
PS2/
    DVD/
        Game A.iso
        Game B.zso
    CD/
        Game C.iso
    ART/
        SLUS_203.12.png
        SCUS_971.99.png
</layout>

---

# 37. Host-side tests

Create native tests for logic that does not require PS2 hardware.

Mandatory tests:

1. partition-name normalization.
2. visible/hidden pair equivalence.
3. 32-character APA limit.
4. startup-ID parsing.
5. ISO PVD parsing.
6. `SYSTEM.CNF` parsing.
7. region mapping.
8. `info.sys` generation.
9. `system.cnf` generation.
10. PPAA header generation.
11. PPAA byte layout against a known-good fixture.
12. transaction-state transitions.
13. duplicate/conflict detection.
14. ZSO virtual filename classification.
15. 64-bit size arithmetic above 2 GiB and 4 GiB.

Keep binary fixtures small and checked into `test/fixtures`.

---

# 38. Real-hardware test matrix

Do not call the project complete without real DESR tests.

Run at least these cases:

| Test | Expected result |
|---|---|
| Installer launched from bootstrap ELF | boots and sees HDD |
| Installer self-installs | installer appears in XMB |
| Installer launched from XMB | boots successfully |
| Plain ISO under 4 GiB | installs, channel appears, launches |
| ZSO source under 4 GiB | server decompresses, installs, channel appears, launches |
| ISO above 4 GiB | sub-partitions work |
| ZSO whose logical size is above 4 GiB | logical-size install works |
| Invalid `.iso` file | rejected before HDD modification |
| Network loss before install | no HDD modification |
| Network loss during stream | no visible XMB channel |
| Power cycle after partial hidden install | repair mode detects it |
| OPL missing | channel creation blocked with clear error |
| Existing hidden valid game/no PP | repair creates only channel |
| Existing PP/no hidden game | flagged orphan |
| Delete complete game | PP removed first, then hidden game |
| Reboot after successful install | game channel remains visible |
| Select game from XMB | per-game OPL-Launcher runs |
| OPL-Launcher handoff | intended HDL game boots |

For ZSO testing, verify the HDD-installed logical data corresponds to the decompressed ISO content, not the ZSO container bytes.

---

# 39. Development phase order

Follow this order. Do not begin later phases before the prior phase acceptance gate passes.

## Phase 1: Import and baseline

Tasks:

- fork/import `ps2-usbhdl` as the application baseline.
- build it unmodified.
- preserve its existing HDL-install code.
- vendor or build the exact required IRX modules.
- record licenses and provenance.
- create a baseline tag/commit.

Gate:

- project builds.
- baseline ELF starts on target hardware.
- HDD detection works.

## Phase 2: Refactor source abstraction

Tasks:

- extract ISO file operations behind `GameSource`.
- keep a temporary local/USB backend if useful for regression tests.
- migrate ISO parser to 64-bit source API.
- add host tests.

Gate:

- existing ISO parsing tests still pass.
- HDL planning produces identical results for known fixtures.

## Phase 3: Integrate Neutrino network stack

Tasks:

- add SMAP.
- add ministack.
- add static IP configuration.
- add `udpfs_ioman.irx`.
- mount/register `udpfs:`.
- add diagnostic directory listing.

Gate:

- DESR can enumerate a `udpfsd` directory repeatedly without crash or leaked handles.

## Phase 4: UDPFS game browser

Tasks:

- recursive browser.
- ISO/ZSO virtual-source filtering.
- metadata probe.
- source-type display.
- error handling.

Gate:

- plain ISO and ZSO-backed virtual ISO both parse to correct game ID and logical size.

## Phase 5: UDPFS-to-HDL install

Tasks:

- feed UDPFS `GameSource` into existing HDL stream writer.
- hidden `__.` partition naming.
- transaction journal.
- end-to-end copy.
- HDL verification.

Gate:

- plain ISO installed from UDPFS boots through ordinary OPL.
- ZSO-backed source installed from UDPFS boots through ordinary OPL.
- no XMB game channel yet.

## Phase 6: OPL dependency resolver

Tasks:

- reproduce OPL-Launcher lookup behavior.
- verify `OPNPS2LD.ELF`.
- expose diagnostics.

Gate:

- correct OPL path is found on target hardware.
- missing OPL is detected cleanly.

## Phase 7: PFS channel creator

Tasks:

- create 128 MiB `PP.` PFS partition.
- mount/write/unmount.
- embed/copy OPL-Launcher KELF.
- generate `res/info.sys`.
- install jacket assets.

Gate:

- all required files survive unmount/remount.

## Phase 8: PPAA/system.cnf header support

Tasks:

- implement minimal OSD/XMB header writer.
- generate PFS boot `system.cnf`.
- compare generated bytes with known-good `hdl-dump modify_header` fixture.

Gate:

- binary header fixture matches expected layout.
- test PP partition appears as a valid DESR XMB channel.

## Phase 9: Per-game XMB channel

Tasks:

- combine hidden game + visible channel.
- enforce transaction ordering.
- test per-game OPL-Launcher handoff.

Gate:

- selecting the game directly from DESR XMB launches the intended game through OPL.

## Phase 10: Installer self-install

Tasks:

- package installer KELF.
- create `PP.UDPFS-INSTALLER`.
- add installer resources.
- add self-install/repair menu.

Gate:

- cold reboot.
- launch installer directly from DESR XMB.
- installer reaches UDPFS browser.

## Phase 11: Repair/manage/delete

Tasks:

- pair scanner.
- orphan detection.
- channel rebuild.
- delete order.
- transaction recovery.

Gate:

- every partial-state case in the hardware matrix has a safe recovery path.

## Phase 12: Performance tuning

Tasks:

- measure buffer sizes.
- reduce unnecessary RPC/UI work.
- bind server to correct host interface.
- profile ZSO server decompression.

Gate:

- select fastest stable configuration without weakening integrity checks.

## Phase 13: Final regression and documentation

Tasks:

- clean build.
- full test matrix.
- server guide.
- first-install guide.
- XMB install guide.
- troubleshooting.
- source/license attribution.

Gate:

- another developer can build and install from documentation without hidden manual steps.

---

# 40. Things Claude must not do

Do not:

- replace UDPFS with NBD.
- expose the DESR HDD to the PC for normal game installation.
- send APA metadata writes over a remote block-device protocol.
- implement a new UDP file protocol.
- decompress ZSO on the DESR.
- write ZSO container bytes to HDL.
- make the game partition visible instead of using the paired `PP.` / `__.` layout.
- point all game channels at one global `EXECUTE.KELF`.
- omit the per-game OPL-Launcher copy.
- use `BOOT2 = PATINFO` for the DESR game channel design.
- create the visible game channel before the hidden game is verified.
- assume an OPL runtime exists without checking.
- silently rename conflicting partitions.
- delete partitions based only on a textual prefix.
- use 32-bit counters for disc size or transfer totals.
- change the `HDLFS_FormatArgs` binary layout.
- invent undocumented APA offsets instead of using verified source/fixtures.
- optimize before the plain ISO and ZSO hardware paths both work.
- declare success based solely on PCSX2; the final XMB/channel behavior must be tested on a real DESR.

---

# 41. Required deliverables from Claude

At the end of implementation, provide all of the following:

1. Complete source code.
2. Clean build instructions.
3. `dist/desr-udpfs-installer.elf`.
4. signed installer `EXECUTE.KELF`.
5. signed OPL-Launcher `EXECUTE.KELF`.
6. vendored IRX provenance list.
7. license notices.
8. server-start examples.
9. first-bootstrap instructions.
10. installer-XMB self-install instructions.
11. game-install instructions.
12. repair/delete instructions.
13. test results for every host test.
14. real-hardware test checklist with pass/fail status.
15. benchmark results for transfer chunk sizes.
16. exact list of files changed or added.
17. exact list of upstream code copied or adapted and its source revision.
18. a short `KNOWN_LIMITATIONS.md`.

Do not leave undocumented manual steps required to make a game channel boot.

---

# 42. Definition of done

The project is complete only when this exact user flow works:

1. Start `udpfsd` on a PC containing:
   - `Game1.iso`
   - `Game2.zso`
2. Launch the bootstrap installer ELF on the DESR.
3. Install the installer into the DESR XMB.
4. Reboot/refresh XMB.
5. Launch the installer from its XMB application/channel.
6. Browse UDPFS.
7. Select the plain ISO.
8. Installer creates hidden HDL game data, validates it, then creates the visible XMB channel with its own OPL-Launcher `EXECUTE.KELF`.
9. Return to XMB.
10. The game appears as its own channel.
11. Select it.
12. OPL-Launcher resolves the matching hidden HDL partition and launches that specific game through OPL.
13. Repeat with the ZSO source.
14. The ZSO is decompressed by `udpfsd` during transfer; the DESR does not perform ZSO decompression.
15. The second game also appears as its own native XMB channel and boots.
16. Remove one game through the installer.
17. Its visible channel disappears and its hidden HDL partition is removed without affecting the other game.
18. Reboot.
19. Installer app and remaining game channel still work.

Anything less than this is an intermediate milestone, not completion.
