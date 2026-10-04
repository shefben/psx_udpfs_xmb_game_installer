@echo off
rem Start udpfsd with the settings in udpfsd.cfg (same folder).
rem Allow UDP 62966 in Windows Firewall first.
cd /d "%~dp0"
udpfsd-windows-amd64.exe
