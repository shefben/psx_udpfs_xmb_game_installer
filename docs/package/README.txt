UDPFS Game Installer for the PSX DESR
=====================================

Installs PS2 games (.iso / .zso) from your PC onto the PSX DESR's
internal hard disk over the network. Every game gets its own icon in the
XMB with its title and cover. OPL is installed on the DESR automatically
if it is missing.

Status: tested on the PC side; first tests on a DESR are in progress.
Back up anything important on the DESR's hard disk before the first run.


WHAT IS IN THIS ZIP
-------------------

  PS2\
    desr-udpfs-installer-bootstrap.elf   the installer, started on the DESR

  PC\udpfsd\                             the server, runs on the PC
    udpfsd-windows-amd64.exe             server for Windows
    udpfsd-linux-amd64                   server for Linux
    udpfsd.cfg                           server settings
    opl-launcher-EXECUTE.KELF            OPL-Launcher (signed), put into
                                         each game's XMB channel
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
     DVD games (.iso or .zso)  ->  udpfsd\DVD
     CD games  (.iso or .zso)  ->  udpfsd\CD
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

1. Copy PS2\desr-udpfs-installer-bootstrap.elf to a USB stick and start
   it on the DESR with wLaunchELF.

2. The installer uses the fixed IP address 192.168.1.10.
   - PC address starts with 192.168.1.  -> nothing to do.
   - Otherwise the top line shows "udpfsd not found". Open
     Network Settings > Local IP, enter a free address in the PC's range
     (e.g. 192.168.0.200: Left/Right selects a digit, Up/Down changes it,
     X accepts), then choose "Save and restart network".

3. With auto_install = yes (the default) the rest is automatic:
   - a 10-second countdown starts (press O to cancel and use the menu);
   - the installer creates its own XMB channel, installs OPL if missing,
     then copies every game that is not on the DESR yet and fits;
   - every game is read back and checked before its XMB icon is made;
   - a summary is shown, then the DESR returns to the XMB.
   Do not switch the DESR off while it is copying.

   With auto_install = no, use the menu:
     Install Games from UDPFS            pick one game
     Install All Games from the server   pick several (Square toggles)
     Install Installer as XMB Channel    puts the installer itself in the
                                         XMB, so you no longer need the
                                         USB stick
     Installed Games / Repair XMB Channels   delete, fix, reinstall

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
  Copy failed / power cut  Repair XMB Channels lists unfinished installs;
                           delete and reinstall them.
  Anything else            Diagnostics on the DESR shows PASS / FAIL for
                           every part.
