# Using the DESR UDPFS XMB Game Installer

Plain-text, controller-driven installer. Every screen lists its buttons
on the bottom line. Destructive actions need **R1 held + X**.

## What you need

* PSX DESR with its internal HDD (APA formatted, as the DESR ships it).
* A way to run one unsigned ELF once (any homebrew launcher).
* OPL on the HDD: `OPNPS2LD.ELF` in `+OPL`, or wherever
  `hdd0:__common/OPL/conf_hdd.cfg` points (`hdd_partition=...`).
* A PC/NAS running the `udpfsd` from `dist/udpfsd/` on the LAN, with
  `udpfsd.cfg` pointing at your game folders and, optionally, your OPL
  `CFG`/`ART` folders and a game list (`udpfsd-example/README.txt`).
  The shipped `opl-launcher-EXECUTE.KELF` sits next to the server; the
  release ELFs also contain their own copies, so installs never depend
  on the server for it.
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

1. Set the game folders in `udpfsd.cfg` next to the server (`dvd`, `cd`,
   `games`, `install`; see `udpfsd-example/README.txt`) and start it.
   The server reads every game once and publishes its list, titles,
   jackets and OPL configs.
2. **Install All Games from the server**. The list appears at once (no
   per-game probing over the network) and shows type, file, startup ID,
   size and status: `new` (selected), `already on HDD`, `duplicate`
   (e.g. the same game as `.iso` and `.zso`), `not a PS2 image`,
   `too big for APA` (not selectable). With a server that publishes no
   list (an older udpfsd), the installer checks `udpfs:/INSTALL` itself.
3. Square toggles a game; the status line shows the space needed against
   the free space. X starts.
4. Games are installed one after another with exactly the single-game
   procedure (copy, full read-back CRC check, then the XMB channel), using
   the server's title for each game. Hold SELECT + O to abort the current
   game; you are then asked whether to stop the rest.
5. A summary lists each game as installed, data only (channel pending,
   when OPL is missing), FAILED (with error and stage) or skipped. Failed
   copies show as UNKNOWN/UNVERIFIED in Repair XMB Channels.

## 3c. Fully automatic installs

With `auto_install = yes` in `udpfsd.cfg`, starting the installer (the
bootstrap ELF from wLaunchELF, or its XMB channel) needs no further input:

1. A 10 second countdown starts as soon as the server is found (if the
   server is still reading games, the installer waits up to a minute).
   O or Triangle cancels into the normal menu.
2. First run: `PP.UDPFS-INSTALLER` is created and verified (the installer
   then also appears in the XMB).
3. If OPL is not on the HDD nothing is installed; the reason is shown.
4. Every game that is not on the HDD yet and fits the free space is
   installed as in 3b. Each new channel gets the server's OPL-Launcher
   (if its SHA-256 matches the server's list; otherwise the built-in
   copy), title, jacket, and - if OPL has none yet - the game's OPL
   config (`CFG/<ID>.cfg` on the OPL partition).
5. The summary is shown for 15 seconds, then the console returns to the
   XMB. If there was nothing new to install, the normal menu opens.

Auto mode never deletes, repairs or overwrites anything.

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
