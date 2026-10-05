# udpfsd Server Manual (v2.0)

udpfsd is the PC side of the UDPFS Game Installer. It serves your game
folders to the PSX DESR over the network (read-only), prepares titles,
covers and game info, and offers OPL and OPL-Launcher to the console.

## 1. Starting it

- Windows: double-click `udpfsd-windows-amd64.exe`. Allow it through the
  firewall on Private networks when asked (UDP port 62966).
- Linux: `chmod +x udpfsd-linux-amd64`, then `./udpfsd-linux-amd64`.
- Keep the window open while the DESR installs games.

At start it reads `udpfsd.cfg` from its own folder and scans your game
folders. When the scan is done, the window shows a line like:

    prep: 15 games ready, 0 invalid images

A rescan runs every time the server starts. Only new or changed images
are read again, so later starts are fast.

## 2. udpfsd.cfg

One `key = value` per line. A `#` turns the rest of the line off.
Relative paths are relative to the server's folder. If a line is wrong,
the server stops with `config: ... line N`.

### Game folders

| Key | What it does |
|---|---|
| `dvd = DVD` | DVD games (.iso / .zso, subfolders included) |
| `cd = CD` | CD games |
| `games = <folder>` | games of either type (the disc type is read from the image) |
| `install = <folder>` | an extra folder, shown to the DESR as `/INSTALL` |
| `pops = POPS` | PS1 games (.VCD) plus `POPSTARTER.KELF`, `POPS.ELF`, `IOPRP252.IMG` |

Each folder you set must exist. You can point the folders anywhere, for
example at PFS-BatchKit-Manager's folders:
`dvd = F:\ps2\PFS-BatchKit-Manager\DVD`.

**Compressed games:** `.zso` and `.cso` files are listed to the DESR as
`<name>.zso.iso`. The console fetches the compressed file and unpacks it
itself; for blocks it cannot unpack, the server unpacks them instead.

### Titles, covers, game info

| Key | What it does |
|---|---|
| `cfg = CFG` | OPL per-game settings, `<GAME-ID>.cfg` (e.g. `SLUS_203.12.cfg`). Copied to the DESR's OPL folder at install, and only if OPL has none there yet. Its `Title=` line is used as the game title. |
| `art = ART` | Covers: `<GAME-ID>_COV.png/.jpg`, `<GAME-ID>_COV2.*` or `<GAME-ID>.*` |
| `gamelist = GameListPS2.txt` | List of game IDs and names (PFS-BatchKit-Manager format), used for titles |
| `gamedb = PS2DB.xml` | Game database (PFS-BatchKit-Manager `BAT\PS2DB.xml`). Adds the release date, developer, publisher and genre shown in the XMB game info. A game not in the database gets its install date as the release date. |
| `download_covers = yes` | Downloads covers missing from `art` (from xlenore/ps2-covers on GitHub) |

**Title order:** `Title=` in the game's CFG, then the game list, then the
file name. Trailing tags such as `(USA)` and `[!]` are removed from file
names.

**Cover order:** the ART folder, then an image next to the game with the
same name (`Game.iso` + `Game.png`), then a download. Each cover is
resized to the two sizes the DESR XMB uses: 140x200 (`jkt_001`) and
74x108 (`jkt_002`), kept in `udpfsd-cache/served/jkt/`. If the console reports "cover not
found on server", restart the server so it rebuilds them.

### Files the DESR receives

| Key | What it does |
|---|---|
| `opl_launcher = opl-launcher-EXECUTE.KELF` | Copied into every new game channel. Keep the shipped file. |
| `opl_elf = OPNPS2LD.ELF` | Open PS2 Loader, installed on the DESR only if it has none. An existing OPL is never replaced. |
| `cache = udpfsd-cache` | Prepared covers, game info and the game list. You can delete it; the next start rebuilds it. |

### Behaviour

| Key | What it does |
|---|---|
| `auto_install = yes` | When the installer starts and finds the server, it counts down 10 s (O cancels), then installs every game not yet on the DESR that fits. This only happens if nobody has chosen a menu entry yet. |
| `power_off_after_install = yes` | After auto-install, the DESR switches itself off after a 15 s countdown (any button cancels). |
| `read_only = yes` | The DESR can only read. Keep this on. |
| `port = 62966` | UDP port of the server |
| `bind = <PC IP>` | Only for PCs with two networks (e.g. Wi-Fi and cable): the IP of the network the DESR is on |

## 3. Command-line options

A command-line option overrides an environment variable, which
overrides `udpfsd.cfg`. Run the server with `-h` for the full list.

| Option | Environment | What it does |
|---|---|---|
| `-config <file>` | `CONFIG` | Use another config file |
| `-port <n>` | `PORT` | UDP port |
| `-bind <ip:port>` | `BIND` | Network address to answer on |
| `-ro` | `RO` | Read-only |
| `-no-prep` | `NO_PREP` | Skip the scan (no titles, covers, game list) |
| `-no-download` | `NO_DOWNLOAD` | Do not download covers |
| `-no-compression` | `NO_COMPRESSION` | Do not serve .zso/.cso as unpacked .iso |
| `-verbose` | `VERBOSE` | More log output |
| `-metrics` | `METRICS` | Log transfer statistics |

## 4. What the installer on the DESR uses from the server

- **Install Games from UDPFS:** browses every folder above. L2 sorts and
  R2 searches.
- **Install All Games:** the scanned list, with titles, sizes and
  duplicates (the same game as .iso and .zso is installed once).
- **Auto-install, OPL install, covers, OPL settings, game info:** all
  come from the scan.
- **PS1 games:** pick a `.VCD` from the `POPS` folder. The three
  POPStarter files must sit next to it (or in the `POPS` folder).
- **Without the server:** Installed Games, Remove Games, Repair, Back up
  to USB, Install from USB and Diagnostics all keep working.

## 5. Problems

| Message / symptom | Fix |
|---|---|
| DESR shows "udpfsd not found" | Server window open? Firewall allows UDP 62966? Same network as the DESR? Two networks on the PC: set `bind`. Then on the DESR: Network Settings > Restart network. |
| `config: ... line N` | Fix that line (usually a folder that does not exist). |
| `prep: N invalid images` | Those files are not PS2 disc images; fix or remove them. |
| Game listed as "duplicate" | Two images of the same game; only one is installed. |
| Plain "PS2 GAME" cover | Restart the server, then on the DESR: Installed Games > game > Repair XMB channel. |
| No release date / genre in the XMB | Set `gamedb`; the game must be in that database. |
| PS1 install says POPSTARTER.KELF / POPS.ELF missing | Put the three POPStarter files next to the .VCD or in the `POPS` folder. |
