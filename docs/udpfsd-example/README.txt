udpfsd server setup for the DESR UDPFS XMB Game Installer
==========================================================

udpfsd (https://github.com/pcm720/udpfsd) serves a folder of game images
to the console over UDP. The console never writes to the server; run it
read-only.

Windows:   udpfsd.exe -fsroot D:\PS2 -ro
Linux:     udpfsd -fsroot /srv/ps2 -ro

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
    PAYLOAD/             signed payloads for the installer
        installer-EXECUTE.KELF
        opl-launcher-EXECUTE.KELF

The CD/ and DVD/ folder names tell the installer the disc type, like OPL.
Outside those folders the type is detected (UDF present or > 870 MB =
DVD). Jacket art may also sit next to the image as "<image name>.png".
Jacket PNGs are copied as-is; 74x108 pixels is the usual DESR size.
