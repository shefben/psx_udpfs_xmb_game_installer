#!/bin/sh
# Serve /srv/ps2 read-only to the DESR installer, plus /srv/ps2/queue as
# udpfs:/INSTALL for "Install All Games".
# Allow UDP 62966 through the firewall, e.g.:  sudo ufw allow 62966/udp
# Add "-bind <this-host-ip>" if the host has several interfaces on the LAN.
exec ./udpfsd-linux-amd64 -fsroot /srv/ps2 -install-dir /srv/ps2/queue -ro
