UDPFS Game Installer for the PSX DESR - v3.2
============================================

Installs PS2 games (.iso / .zso / .cso / .chd, or split .iso.001 sets) from your PC onto the PSX DESR's
internal hard disk over the network. Every game gets its own icon in the
XMB with its title and cover. OPL is installed on the DESR automatically
if it is missing.

Status: PC tests/builds pass; the user reports the 3.2 test package worked.
Back up anything important on the DESR's hard disk before the first run.


WHAT IS IN THIS ZIP
-------------------

  CHANGELOG.txt                          what is new since 1.0
  SERVER-MANUAL.txt                      every udpfsd.cfg setting and option

  PS2\
    desr-udpfs-installer-bootstrap.elf   the installer, started on the DESR

  PC\udpfsd\                             the server, runs on the PC
    udpfsd-windows-amd64.exe             server for Windows
    udpfsd-linux-amd64                   server for Linux
    udpfsd.cfg                           server settings
    opl-launcher-EXECUTE.KELF            OPL-Launcher (signed), put into
                                         each game's boot header
    OPNPS2LD.ELF                         Open PS2 Loader 1.2.0-Beta-2245,
                                         installed on the DESR if missing
    OPL-LICENSE.txt                      OPL licence (AFL-3.0)
    DVD\  CD\                            put your game images here
    CFG\  ART\                           optional OPL settings and covers

Keep everything in PC\udpfsd together: the server looks for its files
next to itself. Do not rename them.


PC SIDE
-------

1. Copy the PC\udpfsd folder anywhere on the PC, e.g. C:\udpfsd.

2. Put your games in it:
     DVD games (.iso, .zso, .cso, .chd or split ISO)  ->  udpfsd\DVD
     CD games  (.iso, .zso, .cso, .chd or split ISO)  ->  udpfsd\CD
   Optional: game info shown in the XMB (release date, developer,
             genre): copy PFS-BatchKit-Manager's BAT\PS2DB.xml next to
             udpfsd and set gamedb = PS2DB.xml in udpfsd.cfg.
   Optional: OPL game settings (<GAME-ID>.cfg) -> udpfsd\CFG,
             covers (<GAME-ID>_COV.png / .jpg)  -> udpfsd\ART.
   Covers you don't have are downloaded automatically.

   Already have your games somewhere else (e.g. PFS-BatchKit-Manager)?
   Open udpfsd.cfg in Notepad and change the paths instead, e.g.
     dvd = F:\ps2\PFS-BatchKit-Manager\DVD
     cd  = F:\ps2\PFS-BatchKit-Manager\CD
     cfg = F:\ps2\PFS-BatchKit-Manager\CFG
     art = F:\ps2\PFS-BatchKit-Manager\ART

3. Start the server: double-click udpfsd-windows-amd64.exe
   (Linux: chmod +x udpfsd-linux-amd64, then ./udpfsd-linux-amd64).
   If Windows asks about the firewall, allow it on Private networks
   (it needs UDP port 62966). Leave the window open.

   After a few seconds the window shows a line like
     prep: 15 games ready, 0 invalid images
   "invalid" images are not usable PS2 disc images; fix or remove them.
   If it stops with "config: ... line N", fix that line in udpfsd.cfg.

4. Note the PC's IP address (Windows: run ipconfig), e.g. 192.168.0.25.
   The PC must be connected to the same router as the DESR, by cable or
   Wi-Fi; the DESR itself must use a network cable.


DESR SIDE
---------

Select Network / Console Settings > DESR generation (PSX1 or PSX2) before
installing PS2 games. The setting is saved in the installer partition.
New games get hidden HDL data and a separate 128 MiB cover/launch channel.

1. Copy PS2\desr-udpfs-installer-bootstrap.elf to a USB stick and start
   it on the DESR with wLaunchELF.

2. The installer gets its IP address from your router (DHCP); the top
   line shows it, e.g. "NETWORK_READY 192.168.0.57 DHCP". Nothing to set
   up in most homes.
   - No DHCP server (top line says "no DHCP, fixed"): it then uses the
     fixed address 192.168.1.10. Open Network Settings > Fallback IP,
     enter a free address in the PC's range (e.g. 192.168.0.200:
     Left/Right selects a digit, Up/Down changes it, X accepts), then
     choose "Save and restart network". "IP address" switches between
     automatic (DHCP) and fixed only.

   The main menu appears at once; the installer looks for udpfsd in the
   background (top line: "looking for udpfsd..."), so Installed Games,
   Remove Games, Repair and Diagnostics work even without the server.

3. With auto_install = yes (the default) the rest is automatic:
   - when udpfsd answers, a 10-second countdown starts (press O to
     cancel and use the menu). It only starts if no menu entry was
     chosen yet;
   - the installer creates its own XMB channel, installs OPL if missing,
     then copies every game that is not on the DESR yet and fits;
   - every game is read back and CRC-checked before its XMB icon is
     made. To save time, press START during "validating" to skip this
     check; the game is still installed and marked "NOT VERIFIED", and
     Installed Games > the game > Verify game data checks it later;
   - a summary is shown, then the DESR returns to the XMB (or switches
     itself off with power_off_after_install = yes in udpfsd.cfg; any
     button within 15 s cancels). Install All asks the same: Square on
     its confirm screen toggles "power off when done".
   Do not switch the DESR off while it is copying.

   With auto_install = no, use the menu:
     Install Games from UDPFS            pick one game (PS2 .iso/.zso,
                                         PS1 .VCD in the POPS folder)
     Install Games from USB              same, from a FAT32/exFAT USB drive
                                         (games over 4 GiB need exFAT)
     Install All Games from the server   pick several (Square toggles)
     Install Installer as XMB Channel    puts the installer itself in the
                                         XMB, so you no longer need the
                                         USB stick
     Installed Games / Repair XMB Channels   delete, fix, reinstall,
                                         rename the XMB title, resume an
                                         interrupted copy, verify, details,
                                         back up to USB (DVD\ CD\ or POPS\
                                         folder, OPL-style names)
     Remove Games                        delete several games at once
                                         (Square toggles, Start = all,
                                         hold R1 + X to confirm)

   In every game list: L2 changes the order (name A-Z, Z-A, size),
   R2 searches (shows only names containing the text you enter).

APPS (HOMEBREW AS XMB CHANNELS)
-------------------------------

   Put homebrew apps in PC\udpfsd\APPS (or APPS\ on a USB drive), one
   folder per app, e.g. APPS\wLaunchELF\BOOT.ELF plus the files it
   needs. On the DESR choose Apps > Install App from UDPFS (or USB),
   pick the .ELF, choose "The whole folder" or "Only the ELF", and
   enter the XMB title. The app gets its own XMB channel with an "APP"
   icon. Apps > Installed Apps deletes app channels.
   The channel starts the app through a small launcher; apps that look
   for their own files next to themselves (from where they were started)
   may not find them and use their defaults.

PS1 GAMES (POPSTARTER)
----------------------

   PS1 games must be in POPStarter's .VCD format (convert BIN/CUE with
   cue2pops; multi-file BIN/CUE must be merged first). Put the .VCD files
   in PC\udpfsd\POPS or PC\udpfsd\CD (on a USB drive: POPS\ or CD\);
   these files go in PC\udpfsd\POPS (USB: POPS\):
     POPSTARTER.KELF   POPStarter rev13 Beta (krHACKen): already in
                       PC\udpfsd\POPS; on USB copy it to POPS\ too
     POPS.ELF, IOPRP252.IMG   Sony's POPS files; not included, you must
                       supply them
   Choose the .VCD in Install Games from UDPFS (or USB). Each PS1 game
   gets its own XMB channel; POPS.ELF and IOPRP252.IMG are copied to
   __common/POPS once. PS1 games are not part of Install All /
   auto-install yet.

4. Go back to the XMB (or restart the DESR). Each game and the installer
   ("UDPFS Game Installer") have their own icon. Selecting a game starts
   it through OPL.


IF SOMETHING GOES WRONG
-----------------------

  "udpfsd not found"       Server window open? Firewall allows UDP 62966?
                           DESR IP in the same range as the PC? PC with
                           Wi-Fi and cable: set bind = <PC IP> in
                           udpfsd.cfg. Then Network Settings > Restart.
  No games listed          Check the dvd / cd lines in udpfsd.cfg and the
                           server window.
  Game shows "duplicate"   Two images of the same game (e.g. .iso and
                           .zso); only one is installed.
  Game shows a plain       Restart the server (it rebuilds the covers),
  "PS2 GAME" cover         then Installed Games > the game > Repair XMB
                           channel.
  Pause a copy             Press START while a game is copying. It stops at
                           once and keeps what is on the HDD.
  Copy failed / power cut  Installed Games > the game > Resume copy
                           continues where it stopped (the image must still
                           be at the same place); Install All resumes it too.
                           The last copied part is read back first; a part
                           that did not reach the disk is copied again.
  Installer channel shows  Made by v2.0 (or an earlier zip). Start this zip's
  a black screen, or the   bootstrap ELF from wLaunchELF and choose
  XMB freezes while        Install Installer as XMB Channel. Replace the
  loading                  PC\udpfsd folder with this zip's, then
                           Repair XMB Channels, then each game >
                           Rebuild XMB channel. Then restart the DESR.
  Install hangs or fails   Network Settings > Copy engine: basic (turns off
  with the fast copy       the overlapped network + HDD copy), Save.
  Anything else            Diagnostics on the DESR shows PASS / FAIL for
                           every part.


CREDITS
-------

  UDPFS / udpfsd and the Neutrino
  network modules (smap, ministack)   Maximus32
  Open PS2 Loader, OPL-Launcher       ps2homebrew and contributors
  APA/HDL driver (ps2hdd-hdl), hdlfs  HDLGameInstaller (sp193)
  PS2SDK                              ps2dev
  Cover art downloads                 xlenore/ps2-covers
  Partition naming, CFG/ART layout,
  GameListPS2.txt                     PFS-BatchKit-Manager (GDX-X)

Open PS2 Loader is distributed under the AFL-3.0 licence
(PC\udpfsd\OPL-LICENSE.txt).

NEW IN 3.2
LZ4 transfers, CSO/CHD/split images on the server, automatic single-BIN/CUE
conversion, PS1 multi-disc (Disc 1..4), and Saves, Cheats & Game Extras.
Existing cards/settings are preserved during automatic installs. Use the
extras menu for replacement or importing .psu saves to a physical PS2 card.
Descriptions and matching covers reside in each channel partition res/ folder.
These new features still need hardware testing.
