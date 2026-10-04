# Using the DESR UDPFS XMB Game Installer

Plain-text, controller-driven installer. Every screen lists its buttons
on the bottom line. Destructive actions need **R1 held + X**.

## What you need

* PSX DESR with its internal HDD (APA formatted, as the DESR ships it).
* A way to run one unsigned ELF once (any homebrew launcher).
* OPL on the HDD: `OPNPS2LD.ELF` in `+OPL`, or wherever
  `hdd0:__common/OPL/conf_hdd.cfg` points (`hdd_partition=...`).
* A PC/NAS running `udpfsd` on the LAN with **only your games and
  optional art** (`udpfsd-example/README.txt`). No payload files are
  needed on the server: the signed OPL-Launcher and installer KELFs are
  inside the release ELFs.
* The release files from `dist/`: `desr-udpfs-installer-bootstrap.elf`.

## 0. Before the first wet run

Back up the HDD partition table (e.g. `hdl_dump toc` from a PC, or
pfsshell). Then:

1. Start the bootstrap ELF.
2. **Diagnostics > Pre-hardware checks**. All lines must be `PASS`
   (the installer-partition lines fail until step 2 below). Note the
   HDD driver and KELF hashes; they must match `BUILD-MANIFEST.txt`.
3. **Diagnostics > HDD self-test** creates, formats, mounts, writes,
   reads, unmounts and deletes a 128 MiB `PP.UDPFS-TEST` partition, then
   re-reads the partition table. It must end with `self-test passed`.
   If an interrupted run left `PP.UDPFS-TEST` behind, use
   **Diagnostics > Remove leftover PP.UDPFS-TEST**.

Do not install a game until both pass.

## 1. First run

1. Start `udpfsd` (read-only).
2. Run `desr-udpfs-installer-bootstrap.elf`. It loads its modules and
   looks for the server (about 8 s); the status line shows
   `NETWORK_READY <ip>` when udpfsd was found. Otherwise open
   **Network Settings**, set the console's static IP for your LAN and
   choose **Save and restart network**.
3. A notice explains that `PP.UDPFS-INSTALLER` does not exist yet and
   that game installation stays disabled until it does (it holds the
   install journals and the network setting).

## 2. Install the installer into the XMB

1. **Install/Repair Installer XMB App**, confirm with X.
2. `PP.UDPFS-INSTALLER` (128 MiB) is created and receives the signed
   installer `EXECUTE.KELF` (embedded in the bootstrap ELF),
   `res/info.sys`, both jacket images, the PFS-boot `system.cnf` header,
   `config/network.ini` and `state/`. Everything is verified.
3. Game installation is now enabled. After the XMB refreshes (or a
   reboot) **UDPFS Game Installer** is a channel; use it from now on.
   The bootstrap ELF is not deleted. Running the same entry from the XMB
   app repairs the channel from its own `EXECUTE.KELF`.

## 3. Install a game

1. **Install Games from UDPFS**. Folders are `[DIR]`; games `[ISO]` or
   `[ZSO]` with their size. X opens, O goes up.
2. Select a game. The installer reads its volume descriptor and
   `SYSTEM.CNF`; anything that is not a PS2 disc is rejected with
   `Not a valid PS2 ISO` and nothing is written.
3. The install screen shows title, startup ID, source, hidden (`__.`)
   and visible (`PP.`) partition names, allocation, validation result,
   region and install state. Square edits the display title.
4. X starts. Stages: creating HDL, copying game, validating (full
   read-back), creating XMB channel, finished. Hold SELECT and press O
   to abort a copy.
5. `Finished` appears only at `TX_COMPLETE`: all bytes copied, the whole
   installed game read back from the HDD with a CRC-32 equal to the CRC
   of the stream received from udpfsd, HDL metadata valid, channel files
   and header verified. The screen shows both CRCs.

If OPL is missing you may copy the game data anyway; it is fully
verified, the channel is **not** created, and the game is shown as
"verified, channel pending" until **Repair XMB Channels > Create XMB
channel** succeeds.

## 3b. Install several games at once

1. Put the games in one folder on the PC and start the server with
   `-install-dir`, e.g.
   `udpfsd-windows-amd64.exe -fsroot D:\PS2 -install-dir D:\PS2\ToInstall -ro`.
   The folder appears on the console as `udpfs:/INSTALL` (read-only).
2. **Install All Games from udpfs:/INSTALL**. Every `.iso` / `.zso` in
   the folder (and in subfolders one level down, such as `CD/`, `DVD/`)
   is checked. The list shows type, file, startup ID, size and status:
   `new` (selected), `already on HDD`, `duplicate`, `not a PS2 image`,
   `too big for APA` (not selectable).
3. Square toggles a game; the status line shows the space needed against
   the free space. X starts.
4. Games are installed one after another with exactly the single-game
   procedure (copy, full read-back CRC check, then the XMB channel), using
   the default title of each game. Hold SELECT + O to abort the current
   game; you are then asked whether to stop the rest.
5. A summary lists each game as installed, data only (channel pending,
   when OPL is missing), FAILED (with error and stage) or skipped. Failed
   copies show as UNKNOWN/UNVERIFIED in Repair XMB Channels.

## 4. Installed games, repair and delete

| State | Meaning | Offered actions |
|---|---|---|
| installed | data verified (journal CRC), channel verified | Repair XMB channel, Reinstall, Delete |
| verified, channel pending | data verified, no `PP.` | Create XMB channel, Reinstall, Delete |
| UNKNOWN/UNVERIFIED data | `__.` exists but no completed, CRC-verified journal (interrupted copy, lost journal, other tool) | Delete, Reinstall |
| channel BROKEN | data verified, `PP.` invalid | Rebuild XMB channel, Delete |
| ORPHANED channel | `PP.` without game data | Remove broken channel |
| channel on UNVERIFIED data | `PP.` exists, data not verified | Remove channel (first) |

A channel is never created for unverified data. Deleting shows both
exact partition names; the journal is marked `deleting=1` first, then
the visible channel is removed and checked gone, then the hidden data,
then the journal.

## After a power cut

Nothing incomplete is ever visible in the XMB. On the next start the
installer reports unfinished journals; **Repair XMB Channels** lists
the affected games with their recovery actions.
