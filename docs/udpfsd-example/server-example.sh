#!/bin/sh
# Start udpfsd with the settings in udpfsd.cfg (same folder).
# Allow UDP 62966 through the firewall, e.g.:  sudo ufw allow 62966/udp
cd "$(dirname "$0")" && exec ./udpfsd-linux-amd64
