# udpfsd Server Manual (v4.0)

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
| `dvd = DVD` | DVD games (.iso / .zso / .cso / .chd, or split `.iso.001`, `.002` ..., subfolders included) |
| `cd = CD` | CD games; PS1 `.VCD` (or BIN/CUE) games may be here too |
| `games = <folder>` | games of either type (the disc type is read from the image) |
| `install = <folder>` | an extra folder, shown to the DESR as `/INSTALL` |
| `apps = APPS` | Homebrew apps (.ELF, one folder per app), installed as XMB channels |
| `pops = POPS` | PS1 games (.VCD, or BIN/CUE) plus `POPSTARTER.KELF` (included), `POPS.ELF`, `IOPRP252.IMG` (yours) |
| `vmc = VMC` | Memory cards and saves: OPL cards `<GAME-ID>_0.bin` / `_1.bin`, PCSX2 `.ps2` cards, `.psu` saves; PS1 cards `<GAME-ID>.VMC` / `.mcr` / `.mcd` / `.mc` / `.gme` / `.vmp`, single PS1 saves `.mcs` |
| `cht = CHT` | Cheats: OPL `<GAME-ID>.cht`, PS1 (POPStarter) `<GAME-ID>.txt` |

Each folder you set must exist. You can point the folders anywhere, for
example at PFS-BatchKit-Manager's folders:
`dvd = F:\ps2\PFS-BatchKit-Manager\DVD`.

**Compressed games:** `.zso`, `.cso` and `.chd` files are listed to the
DESR as `<name>.zso.iso`, `<name>.cso.iso` and `<name>.chd.iso`, unpacked
by the server. **Split images** `Name.iso.001`, `.002`, ... (numbered
from .001 without gaps) are listed as one `Name.iso`; a real `Name.iso`
next to them wins.

**PS1 BIN/CUE:** in `POPS` and `CD`, a `Name.cue` with one `.bin` (in the
same folder) is listed as `Name.VCD`: the bytes cue2pops 2.0 would write
(header, gaps), made on the fly without a copy. Its optional game fixes,
trainer and NTSC patch are not applied. Cues cue2pops refuses (several
files, WAVE, no MODE2/2352 first track) are not offered. A real
`Name.VCD` wins.

**Compressed transfers:** every image is also served under `/.lz4f/...`
as LZ4 frames (the manifest says `wire=lz4f`); the 4.0 installer uses
them, so compressible data (padding, empty sectors) costs almost nothing
on the network. One CPU core per transfer compresses on the fly.

### Titles, covers, game info

| Key | What it does |
|---|---|
| `cfg = CFG` | OPL per-game settings, `<GAME-ID>.cfg` (e.g. `SLUS_203.12.cfg`). Copied to the DESR's OPL folder at install (kept if OPL has one there, unless replaced from *Saves, Cheats & Game Extras*). Its `Title=` line is used as the game title. |
| `art = ART` | Covers: `<GAME-ID>_COV.png/.jpg`, `<GAME-ID>_COV2.*` or `<GAME-ID>.*`. Also OPL art, installed into OPL's ART folder as PNG (JPEG converted, served under `/.oplart/`): `_COV _COV2 _ICO _LAB _LGO _BG` (or `_BG_00`) `_SCR` (or `_SCR_00`) `_SCR2` (or `_SCR_01`) |
| `gamelist = GameListPS2.txt` | List of game IDs and names (PFS-BatchKit-Manager format), used for titles |
| `gamedb = PS2DB.xml` | Game database (PFS-BatchKit-Manager `BAT\PS2DB.xml`). Adds the release date, developer, publisher and genre shown in the XMB game info. A game not in the database gets its install date as the release date. |
| `download_covers = yes` | Downloads covers missing from `art` (from xlenore/ps2-covers on GitHub) |

**Title order:** `Title=` in the game's CFG, then the game list, then the
file name. Trailing tags such as `(USA)` and `[!]` are removed from file
names.

**Cover order:** the ART folder, then an image next to the game with the
same name (`Game.iso` + `Game.png`), then a download. Each cover is
resized to the two sizes PSX-XMB-Manager uses for the DESR XMB:
140x200 (`jkt_001`) and
74x108 (`jkt_002`), 256 colours, kept in `udpfsd-cache/served/jkt/`. If the console reports "cover not
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
- **Game extras** (`CFG`, `CHT`, `VMC`, `ART`): installed with each game
  and from *Saves, Cheats & Game Extras*, into the OPL partition (PS2) or
  `__common/POPS/<game>/` (PS1). `.psu` saves go onto a real memory card.
- **PS1 games:** pick a `.VCD` (or a BIN/CUE shown as `.VCD`) from the
  `POPS` or the `CD` folder. Multi-disc games named `(Disc 1)`, `(Disc 2)`
  ... install as one game.
  `POPSTARTER.KELF` (shipped), `POPS.ELF` and `IOPRP252.IMG` are taken
  from next to the `.VCD`, else from the `POPS` folder.
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
| PS1 install says POPSTARTER.KELF / POPS.ELF missing | Keep the shipped `POPSTARTER.KELF` and add `POPS.ELF` and `IOPRP252.IMG` next to the .VCD or in the `POPS` folder. Installers before 3.1 reported these files missing on the server even when present; update the installer. |
