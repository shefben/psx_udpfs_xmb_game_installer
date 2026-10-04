udpfsd server setup for the DESR UDPFS XMB Game Installer
==========================================================

Use the udpfsd build shipped in dist/udpfsd/ (pinned upstream
pcm720/udpfsd plus the patches in patches/udpfsd/). It serves your game
images to the console over UDP and prepares everything the installer
needs. The console never writes to the server.

Quick start
-----------
1. Keep these files together in one folder (all shipped in dist/udpfsd/):
      udpfsd-windows-amd64.exe   (or udpfsd-linux-amd64)
      udpfsd.cfg                 settings, edit this
      opl-launcher-EXECUTE.KELF  signed OPL-Launcher for the game channels
      OPNPS2LD.ELF               Open PS2 Loader, installed on a DESR without OPL
      OPL-LICENSE.txt            its licence (AFL-3.0)
2. Edit udpfsd.cfg: set your game folders (dvd, cd, games, install) and,
   if you have them, your OPL cfg/art folders and a game list:
      dvd      = F:\ps2\PFS-BatchKit-Manager\DVD
      cd       = F:\ps2\PFS-BatchKit-Manager\CD
      cfg      = F:\ps2\PFS-BatchKit-Manager\CFG
      art      = F:\ps2\PFS-BatchKit-Manager\ART
      gamelist = F:\ps2\PFS-BatchKit-Manager\GameListPS2.txt
3. Start the server with no arguments:
      udpfsd-windows-amd64.exe
4. With auto_install = yes, start the installer ELF on the DESR (from
   wLaunchELF or its XMB channel). After a 10 second countdown (O cancels)
   it installs every game that is not on the HDD yet, then returns to
   the XMB, where each game has its own channel.

What the server prepares at startup
-----------------------------------
For every .iso/.zso in the game folders (subfolders included) it reads
the disc once (ZSO through the server's own decompressor) and finds:
* the game ID (always from the disc's SYSTEM.CNF),
* the title: cfg\<ID>.cfg "Title=", else the gamelist entry, else the
  file name without "(USA)"-style tags,
* the jacket: art\<ID>_COV, art\<ID>_COV2, art\<ID>, an image named like
  the game file, else (download_covers = yes) a cover downloaded from
  the public xlenore/ps2-covers repository; scaled to 74x108 PNG,
* the OPL per-game config cfg\<ID>.cfg, if present,
* size, disc type (CD/DVD folder name, UDF, size) and DVD9 layer break.
Results are cached in udpfsd-cache\ (only new or changed images are read
again on the next start). Covers that cannot be downloaded are skipped;
the game gets the installer's default jacket. Downloaded covers are
third-party images; their use is your responsibility.

What the console sees (all read-only)
-------------------------------------
  /DVD /CD /GAMES /INSTALL   your game folders
  /ART /CFG                  your OPL art and config folders
  /.udpfsd                   manifest.txt, jkt/<ID>.png, EXECUTE.KELF
  everything else            fsroot, if set

During an install the console copies the game, reads it back and
CRC-checks it, creates the XMB channel with the server's OPL-Launcher
(only if its SHA-256 matches the manifest; otherwise the copy built into
the installer), the title and the jacket, and copies cfg\<ID>.cfg to the
OPL partition's CFG folder if OPL has no settings for that game yet.

udpfsd.cfg reference
--------------------
  dvd, cd, games, install   game folders (any may be omitted)
  cfg, art                  OPL CFG and ART folders
  gamelist                  ID -> name list (GameListPS2.txt format)
  cache                     prepared data (default udpfsd-cache)
  download_covers           yes/no (default yes)
  opl_launcher              signed OPL-Launcher KELF
                            (default opl-launcher-EXECUTE.KELF)
  opl_elf                   OPL installed on consoles without OPL
                            (default OPNPS2LD.ELF; shipped: official
                            v1.2.0-Beta-2245-3e3f34e)
  auto_install              yes/no (default no)
  fsroot                    optional folder served as /
  read_only                 yes/no for fsroot (mounts are always read-only)
  port, bind                network (default port 62966)
Relative paths are relative to the server's folder. A misspelled key or
a missing folder stops the server with a message naming the line.
Command-line flags and environment variables override the file:
-config <file>, -fsroot, -install-dir, -no-prep, -no-download, -ro,
-port, -bind (run with -h for all).

Network
-------
* UDP port 62966 is used for discovery and data (default). Allow UDP
  62966 in the server's firewall (inbound and outbound).
* If the server has more than one network interface on the LAN (e.g.
  wired + Wi-Fi), set bind = <this PC's LAN IP> in udpfsd.cfg.
* Only one udpfsd server should be visible on the LAN. The console uses
  the first server that answers discovery.
* The console uses a static IP (Network Settings in the installer,
  default 192.168.1.10). It must be in the same subnet as the server.

Compressed images
-----------------
Leave transparent decompression ENABLED (do not pass -no-compression).
A file "Game B.zso" is shown to the console as "Game B.zso.iso"; the
server decompresses it while the console copies, so the HDD receives the
normal uncompressed disc image. CSO and CHD are not offered.
