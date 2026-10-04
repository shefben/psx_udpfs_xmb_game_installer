#!/bin/sh
# Serve /srv/ps2 read-only to the DESR installer.
# Allow UDP 62966 through the firewall, e.g.:  sudo ufw allow 62966/udp
# Add "-bind <this-host-ip>" if the host has several interfaces on the LAN.
exec udpfsd -fsroot /srv/ps2 -ro
