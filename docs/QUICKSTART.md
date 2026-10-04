# Quick install guide

From game images on a PC to games in the PSX DESR's XMB. Details are in
`docs/INSTALL.md` and `udpfsd-example/README.txt`.

**Status:** this has passed all PC-side tests but has not yet been run on a
DESR. Back up the HDD partition table before the first run (e.g.
`hdl_dump toc` from a PC, or pfsshell).

---

## What you need

* A PSX DESR whose internal HDD has **OPL** installed (`OPNPS2LD.ELF` in
  `+OPL`, or wherever `hdd0:__common/OPL/conf_hdd.cfg` points).
* A way to start an ELF on the DESR (e.g. wLaunchELF from a USB stick).
* A PC on the same network (wired) as the DESR.
* The release files from `dist/`:

  | File | Goes to |
  |---|---|
  | `desr-udpfs-installer-bootstrap.elf` | USB stick (PS2 side) |
  | `udpfsd/` folder (server, `udpfsd.cfg`, `opl-launcher-EXECUTE.KELF`) | the PC |

---

## PC side

1. **Copy the server folder.** Copy `dist/udpfsd/` to the PC, e.g. to
   `F:\ps2\udpfsd\`. Keep its four files together:
   `udpfsd-windows-amd64.exe`, `udpfsd-linux-amd64`, `udpfsd.cfg`,
   `opl-launcher-EXECUTE.KELF`.

2. **Edit `udpfsd.cfg`.** Point it at your folders; delete or `#` out
   lines you don't need:

   ```ini
   dvd      = F:\ps2\PFS-BatchKit-Manager\DVD
   cd       = F:\ps2\PFS-BatchKit-Manager\CD
   cfg      = F:\ps2\PFS-BatchKit-Manager\CFG
   art      = F:\ps2\PFS-BatchKit-Manager\ART
   gamelist = F:\ps2\PFS-BatchKit-Manager\GameListPS2.txt
   download_covers = yes
   auto_install    = yes
   ```

   * Game folders hold `.iso` and `.zso` files (subfolders are included).
   * `auto_install = yes` makes the DESR install every new game without
     any button presses. Use `no` to choose games yourself.
   * If the PC has both Wi-Fi and wired connections, add
     `bind = <PC's LAN IP>`.

3. **Open the firewall.** Allow **UDP port 62966** (inbound and outbound)
   for `udpfsd-windows-amd64.exe`. Windows usually asks the first time it
   runs; choose *Private networks*.

4. **Start the server.** Double-click `udpfsd-windows-amd64.exe` (or run
   `server-example.bat` from `udpfsd-example/`). Leave the window open.
   Within a few seconds the log should show:

   ```
   config: loaded ...\udpfsd.cfg
   fs: mounted ...\DVD as /DVD (read-only)
   prep: 15 games ready, 1 invalid images; ...
   ```

   An image listed as invalid is not a usable PS2 disc image (e.g. an
   incomplete download); fix or remove it. If the server stops with
   `config: ... line N`, fix that line in `udpfsd.cfg`.

5. **Note the PC's IP address** (`ipconfig`, e.g. `192.168.0.25`). The
   DESR needs a free address in the same range.

---

## PS2 (DESR) side

1. **Copy** `desr-udpfs-installer-bootstrap.elf` to a USB stick and start
   it with wLaunchELF.

2. **Network.** The installer uses a fixed IP (default `192.168.1.10`).
   * If your PC is on `192.168.1.x`, nothing to do: the status line shows
     `NETWORK_READY`.
   * Otherwise the status shows `udpfsd not found`. Go to
     **Network Settings > Local IP**, set a free address in the PC's
     range (e.g. `192.168.0.200`; Left/Right picks a number, Up/Down
     changes it, X accepts), then **Save and restart network**.
     The address is kept only after step 3, so after changing it:
     do step 3, choose **Exit**, and start the ELF again.

3. **First run (recommended before any game):**
   * **Diagnostics > Pre-hardware checks**: every line should be `PASS`
     (installer-partition lines fail until the installer is installed).
   * **Diagnostics > HDD self-test**: must end with `self-test passed`.
   * **Install/Repair Installer XMB App**: creates `PP.UDPFS-INSTALLER`
     (128 MiB), which stores the network setting and the install
     journals. The installer also appears in the XMB afterwards.
     With `auto_install = yes` this happens automatically, but doing it
     by hand first is the safest first test.

4. **Install games.**
   * **Automatic** (`auto_install = yes`): start the ELF (or the
     installer from the XMB). A 10-second countdown starts; press **O**
     to cancel. Every game not yet on the HDD that fits is copied, read
     back, CRC-checked and given its own XMB channel. A summary shows for
     15 seconds, then the console returns to the XMB.
   * **Manual**: **Install All Games from the server** (Square toggles a
     game, X starts), or **Install Games from UDPFS** for one game.
   * Hold **SELECT + O** to abort the game being copied.

5. **Play.** Return to the XMB (or reboot). Each game has its own channel
   with its title and cover. Selecting it starts the game through OPL.

---

## If something goes wrong

| Symptom | Fix |
|---|---|
| `udpfsd not found` | Firewall (UDP 62966), same IP range, only one udpfsd running, `bind =` on multi-network PCs; then **Restart network**. |
| "No games found on the server" | Check the game folder lines in `udpfsd.cfg` and the server log. |
| "OPL runtime not found" | Install OPL on the HDD; auto mode installs nothing without it. |
| A game shows `duplicate` | Two images have the same game ID (e.g. `.iso` and `.zso` of one game); only one is installed. |
| A game shows `already on HDD` | Use **Installed Games** to delete it first if you want to reinstall. |
| Install failed / power cut | **Repair XMB Channels** lists unfinished installs; delete and reinstall. |
| Installer channel missing from the XMB after a reboot | Report it: this tests whether the DESR accepts the KELF signing mode (checklist D1/D2). |
