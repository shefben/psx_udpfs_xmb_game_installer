@echo off
rem Serve D:\PS2 read-only to the DESR installer.
rem Allow UDP 62966 in Windows Firewall first. Add -bind <this-PC-IP>
rem if the PC has several network interfaces on the same LAN.
udpfsd.exe -fsroot D:\PS2 -ro
