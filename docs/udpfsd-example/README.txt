udpfsd server setup for the DESR UDPFS XMB Game Installer
==========================================================

Use the udpfsd build shipped in dist/udpfsd/ (pinned upstream
pcm720/udpfsd plus one patch that adds -install-dir; see
patches/udpfsd/). It serves a folder of game images to the console over
UDP. The console never writes to the server; run it read-only.

Windows:   udpfsd-windows-amd64.exe -fsroot D:\PS2 -ro
Linux:     udpfsd-linux-amd64 -fsroot /srv/ps2 -ro

Batch install folder (-install-dir)
-----------------------------------
-install-dir <folder> (environment variable INSTALL_DIR) serves a second
folder read-only to the console as udpfs:/INSTALL. The installer's
"Install All Games from udpfs:/INSTALL" menu entry lists every .iso and
.zso in it (and in its subfolders one level deep, e.g. CD/ and DVD/),
checks each one, and installs the selected games one after another.

    udpfsd-windows-amd64.exe -fsroot D:\PS2 -install-dir D:\PS2\ToInstall -ro
    udpfsd-linux-amd64 -install-dir /srv/ps2/queue -ro

* The install folder is always read-only, even without -ro.
* -install-dir works on its own (without -fsroot): the console then sees
  only udpfs:/INSTALL.
* If -fsroot also contains a folder named INSTALL, it is hidden while
  -install-dir is set.
* Games already on the HDD, duplicates, and files that are not PS2 disc
  images are listed but not selected.

Network
-------
* UDP port 62966 is used for discovery and data (default). Allow UDP
  62966 in the server's firewall (inbound and outbound).
* If the server has more than one network interface on the LAN (e.g.
  wired + Wi-Fi), bind udpfsd to the interface the console uses:
      udpfsd -fsroot /srv/ps2 -ro -bind 192.168.1.100
  Otherwise the console may time out or see duplicate packets.
* Only one udpfsd server should be visible on the LAN. The console uses
  the first server that answers discovery.
* The console uses a static IP (Network Settings in the installer,
  default 192.168.1.10). It must be in the same subnet as the server.

Compressed images
-----------------
Leave transparent decompression ENABLED (do not pass -no-compression).
A file "Game B.zso" is shown to the console as "Game B.zso.iso"; the
server decompresses it while the console copies, so the HDD receives the
normal uncompressed disc image. The console never decompresses ZSO.
CSO and CHD are not offered by this installer version.

Folder layout
-------------
PS2/
    DVD/                 DVD games (.iso or .zso)
        Game A.iso
        Game B.zso
    CD/                  CD games
        Game C.iso
    ART/                 optional jacket art, PNG, named by startup ID
        SLUS_203.12.png
        SCUS_971.99.png
    ToInstall/           optional -install-dir folder for batch installs
        DVD/Game D.zso
        CD/Game E.iso

Nothing else is needed on the server: the signed installer and
OPL-Launcher KELFs are embedded in the release ELFs. (A PAYLOAD/ folder
is only read by unsigned development builds.)

The CD/ and DVD/ folder names tell the installer the disc type, like OPL.
Outside those folders the type is detected (UDF present or > 870 MB =
DVD). Jacket art may also sit next to the image as "<image name>.png";
art for batch installs is looked up in ART/ under -fsroot. Jacket PNGs
are copied as-is; 74x108 pixels is the usual DESR size.
