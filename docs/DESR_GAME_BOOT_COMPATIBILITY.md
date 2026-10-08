# DESR generation detection and game boot compatibility

Status: researched policy, 2026-10-07. No automatic selection is implemented
by this document. PSX1 PFS resource stability and the selected wrapper still
need testing on the user's DESR-7000 and DESR-5700.

## Detect hardware, not installed XMB version

PS2Ident obtains an i.Link ID with `sceCdRI()` and extracts its model code:

```c
/* Only after sceCdRI returned success and its status has no error. */
uint32_t model_id = (uint32_t)ilink_id[1]
                  | ((uint32_t)ilink_id[2] << 8)
                  | ((uint32_t)ilink_id[3] << 16);
```

The PS2Ident MODEL_ID database has these entries:

| Model ID | Model | Generation |
| --- | --- | --- |
| 0x20D381 | DESR-7000 | PSX1 |
| 0x20D382 | DESR-5000 | PSX1 |
| 0x20D383 | DESR-7100 | PSX1 |
| 0x20D384 | DESR-5100 | PSX1 |
| 0x20D385 | DESR-5100/S | PSX1 |
| 0x20D386 | DESR-7500 | PSX2 |
| 0x20D387 | DESR-5500 | PSX2 |
| 0x20D388 | DESR-7700 | PSX2 |
| 0x20D389 | DESR-5700 | PSX2 |

Use exact table entries; unmatched codes remain UNKNOWN. Do not classify
by numerical DESR model order: 7000 is older than 5700.

An alternate source is the MECHACON model name. `sceCdRM()` exposes it
through CDVD RPC; PS2Ident's `sceCdAltRM()` instead issues read-model
S-command 0x17 twice (offsets 0 and 8), collecting 16 bytes. Validate both
call results/statuses, terminate the string, and match known DESR names.
If identifiers disagree, report UNKNOWN rather than guessing.

These calls need a working CDVD RPC environment. The current installer
does not explicitly load CDVDMAN/CDVDFSV, so adding a bare sceCdRI/sceCdRM
call to its boot path is not sufficient. Verify compatible service/module
availability and provide bounded initialization or an optional diagnostic
probe. Do not introduce another unbounded startup RPC bind. Cache the
identity result in EE state; recheck service validity after an IOP reset.
Failed/absent hardware identification must leave installation usable and
offer a persisted PSX1/PSX2 override, clearly reported in Diagnostics.

Do not rely on `sceCdReadModelID()` as a universally available shortcut:
PS2SDK documents it as requiring newer CDVDMAN in DNAS IOPRP.
Do not classify by XMB 1.31 versus 2.11 or the current DVRP version;
DVRPwned explicitly supports XMB 2.11 on PSX1 and changes version values.

## Boot format and wrapper are separate choices

hdl-dump's README explicitly states that PSX1 does not support
`BOOT2 = PATINFO`. It documents `BOOT2 = pfs:/EXECUTE.KELF` as the PFS
alternative. The current `build_channel()` in src/xmb_game_channel.c
unconditionally builds PATINFO-based PS2 game entries.

| Profile | PS2 game layout | Launcher candidate |
| --- | --- | --- |
| PSX1 | Hidden HDL game plus visible PFS boot channel; BOOT2 = pfs:/EXECUTE.KELF | OPL-Launcher wrapped using the released XMB Manager v1.2 tool/template pair |
| PSX2 | Existing visible HDL game with PATINFO boot header | Existing kelftool payload, until a universal wrapper is hardware-verified |
| UNKNOWN | Do not automatically publish a PATINFO game channel | Require an explicit generation/profile choice |

A PFS boot channel can use default resources; downloaded cover art is not
required for this boot route. However, this user's XMB freezes with two
image-bearing PFS channels. Reusing the current Add XMB cover flow without
testing/correcting the resources could reproduce that freeze. Keep a
PSX1 game hidden/channel-pending until its boot channel is complete;
do not call it launch-compatible based only on byte read-back.

The installer channel and POPStarter channels already use PFS boot and
are separate from the PS2 PATINFO defect. Do not replace POPStarter's
distributed KELF with OPL-Launcher.

## Pin the released wrapper AND its template

XMB Manager v1.2 release notes say "Changed KRYPTO for the ELF to KELF
wrapper" to fix first-DESR games/homebrew not booting. SCEDoormat_NoME
uses a `KRYPTO.KHN` template; changing only the executable is insufficient.
The repository's v1.2-tagged tool assets differ from the released ZIP.

Verified files inside `PSX.XMB.Manager.v1.2.x64.zip`:

| File | Bytes | SHA-256 |
| --- | --- | --- |
| Tools/KRYPTO.KHN | 20971782 | f597f8178b9fa739ac0daf0286e90c262863b8091e463e9386845755e05ba3f7 |
| Tools/SCEDoormat_NoME.exe | 16896 | b4736263c071cea6b7e763102b839681a6f6c23479a04485097e1cf4c7d4c595 |

Build both launcher candidates from the same pinned OPL-Launcher ELF.
SCEDoormat's interface accepts an explicit template argument:

```text
SCEDoormat_NoME.exe OPL-Launcher.elf opl-launcher-psx1.KELF KRYPTO.KHN
```

Use absolute arguments in build tooling; check the template/tool hashes,
ELF capacity, output integrity, and the actual generated payload before
publishing it. The mirrored tool returns 1 on its success path, so do not
assume conventional exit-code-zero success. The current kelf-sign.sh
DNASLOAD header assertion does not apply to this alternate wrapper.

## Enforce selection at the existing payload boundary

`payload_opl_launcher()` is the common selection point for PS2 install,
repair/rebuild, and Add XMB cover. Extend it to resolve a console profile
and select the matching embedded launcher. Pass that profile through
channel creation and verification so the boot layout matches the wrapper.

Today a single server launcher is accepted when it matches the server's
own advertised SHA-256/size. That proves transfer integrity, not PSX1
compatibility. For PSX1, do not let that generic server payload override
the selected compatible embedded payload. Either disable the override
for PSX1 initially, or add separate profile-tagged server assets with
trusted expected hashes and explicit wrapper/template provenance.

Store profile and selected payload hash in installation/repair metadata.
Make channel health checks profile-aware: a correct PATINFO header is
still incompatible on PSX1. Repair should change launcher/header/channel
layout while retaining and validating the existing HDL game data.
Record the selected generation, detection source, boot mode, payload
source/hash, and reused-module versions in Diagnostics.

## Validation required before enabling automatic PSX1 channels

Host checks: all model IDs above, unknown/failed/conflicting identity,
manual override, generic-server bypass prevention, consistent install/
repair/cover selection, PSX1 rejection of PATINFO, and no HDL data changes
during repair. Test failed wrapper output and template hash mismatch.

Hardware: confirm raw model ID/model-name on DESR-7000 and DESR-5700;
compare the same OPL ELF in both wrappers with the same PFS resources;
then test one and two PSX1 PFS channels through cold XMB startup and game
launch. A universal v1.2-template wrapper is a possible simplification
only after it passes on both generations. It does not make PATINFO work
on PSX1.

## Sources

- PS2Ident identity extraction: https://github.com/ps2homebrew/PS2Ident/blob/master/ident.c
- PS2Ident model-name probe: https://github.com/ps2homebrew/PS2Ident/blob/master/libcdvd_add.c
- PS2Ident model-code table: https://github.com/ps2homebrew/PS2Ident/blob/master/PS2Ident.db
- Generation and firmware caveat: https://github.com/pcm720/dvrpwned
- PATINFO restriction: reference/hdl-dump/README.md:164
- Wrapper compatibility release: https://github.com/SvenGDK/PSX-XMB-Manager/releases/tag/v1.2
- Released ZIP: https://github.com/SvenGDK/PSX-XMB-Manager/releases/download/v1.2/PSX.XMB.Manager.v1.2.x64.zip
- Wrapper source/usage: https://github.com/ps2dev-mirror/SCEDoormat_NoME
# Implemented console profiles

Select **Network / Console Settings > DESR generation** before installing
PS2 games. The selection is saved as `console=psx1` or `console=psx2` in
`config/network.ini`. Old settings files remain `unknown`; unknown consoles
cannot publish PS2 game channels or start automatic installation.

| Profile | Game data | XMB channel | Launcher |
| --- | --- | --- | --- |
| PSX1 (5000/5100/7000/7100) | Hidden `__.` HDL | Separate 128 MiB `PP.` PFS, `BOOT2 = pfs:/EXECUTE.KELF` | Embedded PSX1 template wrapper |
| PSX2 (5500/5700/7500/7700) | Hidden `__.` HDL | Separate 128 MiB `PP.` PFS, `BOOT2 = pfs:/EXECUTE.KELF` | Existing signed launcher/server selection |

PSX1 repair preserves an existing PFS channel or migrates visible HDL data
to its hidden name before creating PFS. A failed migration leaves verified
data hidden for retry; it never restores an incompatible PATINFO entry.
Scans compare a PSX1 channel's launcher with the embedded payload and offer
repair when they differ. Title editing supports PFS channels.

`tools/build-psx1-kelf.py` downloads and verifies the released v1.2
SCEDoormat executable and KRYPTO template by size and SHA-256, wraps the
same stripped OPL ELF as the PSX2 build, and verifies the exact embedded
ELF and container size/header. It runs on Windows or WSL with Windows
executable interoperability. The PSX1 launcher is embedded in development,
app and bootstrap variants. Generic server launcher overrides are disabled
for PSX1. This wrapper does not require PS2KEYS; signing the installer app
still does.

Generation selection is currently manual. `console_from_model_id` contains
the PS2Ident hardware-ID mapping for future bounded CDVD detection; startup
does not make an unbounded CDVD RPC call or infer hardware from firmware.

Both generations automatically create PFS cover/launch partitions. Server
artwork and game information are written during installation and repair;
missing covers use the bundled default resources. This layout change does not
establish the cause of the reported two-channel XMB startup freeze. Test
one channel, boot and launch it, then add a second and repeat on hardware.
The installer IOP-loading hang is a separate issue; controller-driver reuse
is a diagnostic fix candidate, not a hardware-verified resolution.
