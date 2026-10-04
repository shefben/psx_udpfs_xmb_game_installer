@echo off
rem Serve D:\PS2 read-only to the DESR installer, plus D:\PS2\ToInstall
rem as udpfs:/INSTALL for "Install All Games".
rem Allow UDP 62966 in Windows Firewall first. Add -bind <this-PC-IP>
rem if the PC has several network interfaces on the same LAN.
udpfsd-windows-amd64.exe -fsroot D:\PS2 -install-dir D:\PS2\ToInstall -ro
