# DESR-5000/7000 launch and XMB freeze investigation

Date: 2026-10-07. Status: source and local asset inspection; hardware causes unconfirmed.

## Reported behavior

The user reports that the installer works on DESR-5700. On DESR-5000/7000,
USB launch works but HDD channel launch stops at `Loading IOP modules...`.
After two games are installed and channels are given icons, XMB freezes on
startup until the image-bearing game partitions are deleted. The precise
release and icon-adding operation have not yet been identified.

## HDD launch

The message comes from `src/app_state.c:63`. It covers IOP initialization,
HDD partition enumeration, installer mounting, and settings loading; it
does not identify the failing module by itself.

`src/iop_boot.c:92` always resets and synchronizes the IOP, applies SBV
patches, then loads embedded modules. Reset/sync loops have no timeout.
Storage loading includes iomanX, fileXio, poweroff, ps2dev9, ps2atad,
ps2hdd-hdl, ps2fs, and hdlfs. Module RPC calls and fileXio initialization
can also block. No module is read from USB/HDD during this sequence
(`Makefile.ee` embeds the IRX files).

Existing uncommitted changes add `Step:` progress before each operation.
They were present before this investigation and were left unchanged.
The exact last step is needed to distinguish reset/sync failure from
DEV9/ATA initialization, APA scanning, and PFS mounting.

The reference OPL launcher (`reference/OPL-Launcher/src/main.c:110`)
resets the IOP for a homebrew launch with a partition argument, but does
not reset it for its normal XMB launch. This is a meaningful comparison,
not proof that omitting reset is safe for the installer. Its USB path
must still cope with preloaded homebrew modules.

The SDK ATA source documents DVRP-specific ATA interface stalls and
LBA limitations (`reference/ps2sdk/iop/dev9/atad/src/ps2atad.c:286`).
That supports checking the driver step and firmware, but does not
establish a DESR-5000/7000-specific driver defect.

Local binaries are different: root `desr-udpfs-installer-bootstrap.elf`
has SHA-256 `5358c908fd8597ed440a63ac4f56a631a374b5b86b774fd34421f53bd907e5d0`;
`dist/desr-udpfs-installer-bootstrap.elf` has SHA-256
`8ce7dfe4f8c28bb95e20448838bb5f2502a33740e43ad055e854c0b72eff842d`.
Both contain `Step: %s`, but only the dist copy contains `IOP sync`.
The dist app ELF also contains that trace. These checks do not establish
which build or signed KELF is installed on the user's console.

## XMB freeze after icons/covers

`KNOWN_LIMITATIONS.md:7` already records an earlier freeze with multiple
PFS channels. Plain PS2 game installation now uses a visible HDL partition
with a PATINFO boot header. However, `game_add_cover` in
`src/xmb_game_channel.c:914` hides that HDL partition and creates a visible
128 MiB PFS resource channel with EXECUTE.KELF and `res/` files.
Thus the cover operation reintroduces the layout implicated by that note.
The note does not establish that all PFS channels are incompatible.

Confirmed format mismatch: all eight bundled PNGs under `assets/` are
8-bit RGB (PNG colour type 2), non-interlaced. They decode successfully
with Pillow. Documentation describes 256-colour jackets, but these are
not palette-indexed PNGs. The server encoder in
`patches/udpfsd/0002-game-prep.patch:1536` also builds an RGBA image before
PNG encoding, rather than explicitly generating a palette image.
Whether the earlier XMB rejects these images or suffers resource pressure
from them requires a hardware comparison with known-working resources.

`src/hdl_header.c:66` checks only signature, IHDR size, and dimensions;
`png_is_size` additionally checks expected width/height. Truncated images,
unsupported colour/depth combinations, interlacing, or bad compressed
data can pass. Channel verification compares written bytes to supplied
bytes; it does not prove that XMB can parse or render them.

Other resource candidates include `res/info.sys`, manual XML, copyright
images, and PPAA descriptors. For example, the manual renderer emits two
PAGE elements with the same PIC0000 ID (`src/xmb_text.c:218`). No XMB
parser behavior was measured, so this is a comparison target, not a
confirmed explanation.

## Next hardware evidence

1. Record installer release, console model, XMB firmware, DVRP firmware,
   icon-adding action, and the last `Step:` on HDD launch. Compare a USB
   bootstrap and installed signed channel produced by the same build.
2. From the working USB launch, use Diagnostics > Dump XMB channels to
   USB (read-only with respect to HDD). Preserve a failing resource
   channel and a known-working channel made by BatchKit/XMB Manager.
   The dump includes partition locations, PPAA area, PFS superblock,
   and channel files under `mass0:/xmb-dump/`.
3. On a backed-up test drive, compare plain HDL entries with one then two
   PFS cover channels. Change one resource at a time, beginning with
   known-working opaque indexed jackets of the same dimensions; then
   compare info.sys, manual/copyright files, and PPAA metadata.
4. If the trace stops at reset/sync, investigate XMB-to-installer IOP
   handoff. If it stops at ps2dev9/ps2atad, investigate controller state
   and the exact embedded driver. If it stops at ps2hdd/PFS, investigate
   partition enumeration, locations, and mount behavior.

## Verified XMB Manager wrapper precedent

The upstream v1.2 release explicitly says "Changed KRYPTO for the ELF to
KELF wrapper" and "Games or homebrew do not boot from XMB on first DESR
models": https://github.com/SvenGDK/PSX-XMB-Manager/releases/tag/v1.2 .
The v1.2 MainWindow.xaml.vb invokes `Tools/SCEDoormat_NoME.exe` to wrap
both homebrew ELFs (line 432) and OPL-Launcher (line 467). This verifies
the screenshot's precedent but does not identify the exact failing
initialization stage or establish that our kelftool has the same defect.

This installer uses `tools/kelf-sign.sh`: kelftool dnasload, apptype 0B.
The existing dist installer KELF starts with these 32 bytes:
`010000040006004a000e010000000002206914008000000b2c020000ff000000`.
The bundled EXECUTE.KELF in XMB Manager's v1.2 repository starts with:
`010000010003004a000102190000005690cc0100800001072c020000ff000000`.
These are different headers, not proof of incompatible payload behavior.
The bundled file need not be identical to the output of its wrapper.

An informative hardware comparison would wrap the exact same installer
ELF with each tool and replace only EXECUTE.KELF in the same test channel.
Keep the IOP/module changes and resource files identical between trials.
Reaching the installer UI establishes that EE execution began, so a
complete failure to authenticate or launch is not the observed symptom;
wrapper-related launch environment differences remain a hypothesis.

## Resident controller diagnostic change

Following the user's PADMAN conflict hypothesis, the boot sequence now
checks the post-reset IOP module list with PS2SDK's `smod_get_mod_by_name`.
If PADMAN is resident, it leaves both PADMAN and its SIO2 dependency alone.
Otherwise it reuses resident SIO2MAN or loads SIO2MAN before PADMAN; a
failed SIO2MAN load prevents the dependent PADMAN load. Diagnostics shows
the version of reused modules. Reset and HDD module order are unchanged.
`test/host/test_iop_boot.py` exercises the real boot code with a simulated
IOP list for resident PADMAN, resident SIO2MAN, fresh IOP, and failed SIO2MAN.
This change tests the hypothesis; hardware resolution remains unconfirmed.
If the reset removes all resident controller modules, both still load.

No hardware fix is claimed. Do not delete hidden HDL game data merely to
remove a resource channel; use an established recovery tool and preserve
the game partition. A hidden game may need to be made visible again.
