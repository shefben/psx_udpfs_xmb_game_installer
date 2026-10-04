# Using the DESR UDPFS XMB Game Installer

Plain-text, controller-driven installer. Every screen lists its buttons
on the bottom line.

## What you need

* PSX DESR with its internal HDD (APA formatted, as the DESR ships it).
* A way to run one unsigned ELF once (any homebrew entry point you
  already use, e.g. wLaunchELF).
* OPL installed on the HDD: `OPNPS2LD.ELF` in `+OPL`, or wherever
  `hdd0:__common/OPL/conf_hdd.cfg` points (`hdd_partition=...`). This is
  what OPL-Launcher boots; the installer checks for it.
* A PC/NAS running `udpfsd` on the same LAN (see `udpfsd-example/README.txt`).
* The two signed payloads in the server's `PAYLOAD/` folder:
  `installer-EXECUTE.KELF` and `opl-launcher-EXECUTE.KELF`
  (built with `make kelfs`; see `docs/BUILD.md`).

## 1. First bootstrap

1. Start `udpfsd` on the PC (read-only).
2. Copy `desr-udpfs-installer.elf` to wherever your homebrew launcher
   can start it, and run it.
3. The installer loads its modules and looks for the server (about 8 s).
   The status line shows `NETWORK_READY <ip>` when it found udpfsd.
   If it shows `NETWORK_ERROR`, open **Network Settings**, set the
   console's static IP for your LAN, then **Save and restart network**.
4. A notice says `PP.UDPFS-INSTALLER does not exist yet` — expected.

## 2. Install the installer into the XMB

1. Choose **Install/Repair Installer XMB App**, confirm with X.
2. The installer creates `PP.UDPFS-INSTALLER` (128 MiB), writes
   `EXECUTE.KELF` (from `udpfs:/PAYLOAD/installer-EXECUTE.KELF`),
   `res/info.sys`, both jacket images, the PFS-boot `system.cnf`
   header, `config/network.ini` and a copy of the OPL-Launcher KELF in
   `payload/`, then verifies all of it.
3. Return to the XMB or reboot. **UDPFS Game Installer** now appears as
   a channel; start it from there from now on. The bootstrap ELF is not
   deleted.

Run the same menu entry again at any time to repair the channel; your
network setting and install journals are kept.

## 3. Install a game

1. **Install Games from UDPFS**. Folders are shown as `[DIR]`; games as
   `[ISO]` or `[ZSO]` with their size. X opens, O goes up.
2. Select a game. The installer reads its volume descriptor and
   `SYSTEM.CNF`; an image that is not a PS2 disc is rejected with
   `Not a valid PS2 ISO` and nothing is written.
3. The install screen shows title, startup ID, source path, and the
   exact hidden (`__.`) and visible (`PP.`) partition names. Press
   Square to edit the display title (the startup ID cannot change).
4. Press X. Stages: creating HDL, copying game, validating, creating
   XMB channel, finished. Progress shows bytes, %, MiB/s, elapsed and
   ETA. Hold SELECT and press O to abort a copy.
5. `Finished` is only shown after every check passed. Return to the XMB:
   the game is its own channel. Selecting it runs that channel's own
   copy of OPL-Launcher, which boots the game through OPL.

If OPL is not found you are asked whether to copy the game data anyway.
The XMB channel is then **not** created; once OPL is installed use
**Repair XMB Channels > Create XMB channel** (no recopy).

## 4. Installed games, repair and delete

**Installed Games** lists every game pair with its state:

| State | Meaning | Offered actions |
|---|---|---|
| installed | data verified, channel verified | Repair XMB channel, Reinstall, Delete |
| no XMB channel | data verified, no `PP.` | Create XMB channel, Reinstall, Delete |
| INCOMPLETE copy | copy did not finish/verify | Delete incomplete game, Reinstall |
| channel BROKEN | data fine, `PP.` invalid | Rebuild XMB channel, Delete |
| ORPHANED channel | `PP.` without game data | Remove broken channel |
| game data INVALID | `PP.` exists, data untrusted | Remove channel (first) |

**Repair XMB Channels** shows the same list filtered to games that need
attention, after listing unfinished install journals.

Deleting always shows both exact partition names and needs **R1 held +
X**. The visible channel is removed first and checked gone, then the
hidden game data, then the journal. Nothing is ever deleted by prefix.

"Reinstall" deletes the pair; from the install screen the copy then
starts again, from the Installed Games list pick the image in the
browser afterwards.

## After a power cut

Nothing incomplete is ever visible in the XMB: the channel is only
created after the copied data verified. On the next start the installer
reports unfinished installs; open **Repair XMB Channels** and delete or
reinstall them.
