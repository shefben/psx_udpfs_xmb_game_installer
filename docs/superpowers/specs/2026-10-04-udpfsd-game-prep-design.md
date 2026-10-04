# udpfsd game preparation and transfer — design

Date: 2026-10-04. Status: approved in chat (design + user changes); this
spec awaits review.

## Goal

When the udpfsd server starts it prepares everything the PS2 installer
needs for each game — game ID, real title, jacket image, OPL per-game
config and image metadata — so the installer does not have to work it
out game by game over the network, and so each installed game gets its
proper title, jacket and OPL settings. All paths come from a config file
next to the server binary; no path parameters are required.

## Inputs (the user's existing PFS-BatchKit-Manager layout)

Observed in `F:\ps2\PFS-BatchKit-Manager`:

| Source | Format |
|---|---|
| `DVD\`, `CD\`, other game folders | `*.iso`, `*.zso` |
| `CFG\<ID>.cfg` (456 files) | OPL per-game config, `key=value` lines, LF. Relevant: `Title=`, `Serial=` (e.g. `SCUS-97101`), `$Compatibility=`, `$VMC_0=`, `$EnableGSM=`, ... |
| `ART\<ID>_<KIND>.<ext>` (~4100 files) | OPL art. Kinds: `COV`, `COV2`, `ICO`, `LAB`, `LGO`, `BG`, `SCR`, `SCR2`; `.jpg` or `.png` |
| `GameListPS2.txt` | header `type size flags dma startup name`, then whitespace columns, e.g. `DVD 666094KB 0 *u4 SLUS_210.90 Alien Homind` (name may be truncated) |

`<ID>` is the BOOT form `XXXX_NNN.NN` throughout.

## 1. Server configuration: `udpfsd.cfg`

Read from the directory of the udpfsd binary (override: `-config <file>`).
Format: `key = value`, `#` comments, blank lines ignored, Windows or
POSIX paths; relative paths are resolved against the binary's directory.
Unknown keys are a startup error (catches typos).

```ini
# udpfsd.cfg
dvd        = F:\ps2\PFS-BatchKit-Manager\DVD
cd         = F:\ps2\PFS-BatchKit-Manager\CD
games      = F:\ps2\Games            # "all games" folder (any disc type)
install    = F:\ps2\ToInstall        # optional batch queue (old -install-dir)
cfg        = F:\ps2\PFS-BatchKit-Manager\CFG
art        = F:\ps2\PFS-BatchKit-Manager\ART
gamelist   = GameListPS2.txt         # reference ID -> name list
cache      = udpfsd-cache            # prepared jackets + scan cache
download_covers = yes                # fetch missing covers (section 3)
opl_launcher = opl-launcher-EXECUTE.KELF   # section 5a
auto_install = yes                   # section 5b
fsroot     =                         # optional legacy root served as /
read_only  = yes
port       = 62966
bind       =
```

Every key is optional. Precedence: command-line flag > environment
variable > `udpfsd.cfg` > built-in default. Existing flags keep working.
The server starts with no flags at all when `udpfsd.cfg` names at least
one game folder.

### What the console sees (all read-only)

| udpfs path | Host folder |
|---|---|
| `/DVD` | `dvd` |
| `/CD` | `cd` |
| `/GAMES` | `games` |
| `/INSTALL` | `install` |
| `/ART` | `art` |
| `/CFG` | `cfg` |
| `/.udpfsd` | prepared data (section 4) |
| everything else under `/` | `fsroot`, if set (a same-named real folder is hidden by a mount) |

This generalizes the existing `-install-dir` mount into a mount table;
`-install-dir` becomes the `install` mount. All mounts are read-only to
the console regardless of `read_only`; the disc-type hint keeps working
because the folder names are `CD`/`DVD`.

## 2. Game scan (server, at startup)

For every `*.iso` / `*.zso` in `dvd`, `cd`, `games`, `install` (and in
their subfolders, recursively) the server:

1. Opens the logical ISO stream (ZSO through udpfsd's existing
   decompressor, so both formats share one code path).
2. Parses ISO9660 exactly like the PS2 app (`src/iso9660.c`, ported to
   Go): PVD `CD001`, volume ID, size, `SYSTEM.CNF` `BOOT2` -> game ID,
   UDF/size/folder disc type, DVD9 layer-1 start. Images that fail are
   listed in the manifest as invalid with the reason.
3. **Game ID** always comes from the disc (`SYSTEM.CNF`). `CFG`
   `Serial=` and `GameListPS2.txt` are only references; a mismatch
   between a cfg's `Serial` and its file name is logged.
4. **Title**, first match wins:
   1. `cfg\<ID>.cfg` `Title=`
   2. `gamelist` entry for the ID (the `name` column; also accepts
      simple `ID name` lines; `^!` -> `!` unescaping)
   3. image file name without `.iso`/`.zso` and without trailing
      `(...)`/`[...]` groups
5. **Jacket**, first match wins: `art\<ID>_COV.(png|jpg)`,
   `art\<ID>_COV2.*`, `art\<ID>.*`, an image named like the game file
   next to it, then download (section 3), else none (the PS2 uses its
   built-in default). The chosen image is scaled to 74x108, saved as PNG
   in the cache (`jkt/<ID>.png`). `ICO`/`LAB`/`BG`/`SCR` are not used.
6. **OPL config**: if `cfg\<ID>.cfg` exists it is offered to the PS2
   as-is (section 5).

Results are cached in `cache/scan.json` keyed by path + size + mtime;
restarts only re-scan new or changed images. Scanning runs in the
background; the server answers discovery immediately and the manifest
appears when the scan finishes (the PS2 app falls back to probing until
then).

## 3. Cover download

Only when `download_covers = yes` and no local jacket exists: GET
`https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/<XXXX-NNNNN>.jpg`
(10 s timeout, at most 4 in parallel). The original file is saved to
`cache/covers/<ID>.jpg` and converted like local art. Failures (404,
offline) are logged once per ID and remembered for the session; they
never delay startup or serving. The user is responsible for the
licensing of downloaded covers.

## 4. Delivery: `udpfs:/.udpfsd/`

* `manifest.txt` — first line `udpfsd-manifest 1`, then one line per
  image, tab-separated:
  `path  status  id  title  bytes  disc  layer1  jacket  cfg`
  * `path`: client path, e.g. `/DVD/HALF LIFE.zso.iso` (the virtual
    `.zso.iso` name for ZSO)
  * `status`: `ok` or `invalid:<reason>`
  * `disc`: `CD`/`DVD`; `layer1`: decimal, 0 if none
  * `jacket`: `jkt/<ID>.png` or `-`; `cfg`: `/CFG/<ID>.cfg` or `-`
  * titles are sanitized (no tab/CR/LF/control characters)
  Written atomically (temp + rename) after each scan.
* `jkt/<ID>.png` — prepared 74x108 jackets.

## 5. PS2 app changes

* **Manifest reader** (`src/manifest.c`, pure, host-tested): parses the
  format above, ignores unknown future columns, rejects malformed lines.
* **Install All** lists every `ok` game from the manifest (all game
  folders, not only `/INSTALL`) instantly; status per game as today
  (new / already on HDD / duplicate / too big). Without a manifest it
  falls back to the current `/INSTALL` scan.
* **Single-game browser** and Install All use the manifest title as the
  default display title (editable in the single-game screen).
* **Jacket** lookup: `udpfs:/.udpfsd/<jacket>` first, then the existing
  `ART/<ID>.png` / next-to-image / built-in default chain.
* **Safety unchanged**: at install time the image is still re-probed on
  the PS2 and the install refuses if ID or size differ from what the
  manifest said. Copy, CRC read-back, journal and channel order are not
  touched.
* **OPL config transfer**: OPL in HDD mode — including OPL-Launcher's
  `mini` boot — reads per-game settings from `<OPL partition>/CFG/<ID>.cfg`
  (`gHDDPrefix` = `pfs0:` for `+OPL`, `pfs0:OPL/` otherwise;
  `hddsupport.c`, `opl.c` `autoLaunchHDDGame`). After a game reaches
  `TX_COMPLETE`, if the manifest offers a cfg, the app copies it to that
  folder on the OPL partition resolved by `opl_dependency.c`:
  * only if no `<ID>.cfg` exists there (the user's OPL settings win;
    never overwritten, never deleted);
  * written as `<ID>.cfg.tmp`, read back, renamed;
  * best effort: a failure is reported in the result/summary but does
    not fail the game install (the game boots without it);
  * recorded in the journal (`opl_cfg=copied|kept|failed|none`).
  `$VMC_*` entries are copied unchanged; whether OPL tolerates a VMC
  name that does not exist yet on the HDD is a hardware check (below).

## 5a. OPL-Launcher next to the server

* `dist/udpfsd/` ships `opl-launcher-EXECUTE.KELF` beside the udpfsd
  binaries; `udpfsd.cfg` key `opl_launcher` (default: that file next to
  the binary) points to it. The server serves it read-only as
  `udpfs:/.udpfsd/EXECUTE.KELF` and lists its size and SHA-256 in the
  manifest header line: `udpfsd-manifest 1 launcher=<sha256>:<bytes>`.
* The PS2 uses the server copy for every new game channel when it is
  present, passes `kelf_looks_valid()`, and its downloaded bytes match the
  manifest hash; otherwise the KELF embedded in the installer. The
  per-game copy rule is unchanged (each channel gets its own physical
  `EXECUTE.KELF`). The source used (`server <sha>` / `embedded`) is
  recorded in the journal and shown in Diagnostics.
* An install never fails only because the server copy is missing or
  bad: the embedded copy is the fallback, so the plan's rule that a
  normal install must not depend on the server still holds.

## 5b. Auto-install (no human intervention)

`udpfsd.cfg`: `auto_install = yes` (default `no`), published in the
manifest header (`auto=1`). When the PS2 app starts and:

1. the HDD and all HDD modules are OK,
2. the network is `NETWORK_READY` and the manifest has `auto=1`,

it shows a 10-second countdown ("Auto-installing N new games — O to
cancel"; O or Triangle cancels into the normal menu, no button press is
needed to proceed) and then, without further prompts:

1. **First run:** if `PP.UDPFS-INSTALLER` does not exist, creates and
   verifies it exactly like *Install/Repair Installer XMB App*
   (user decision: the installer also appears as an XMB app). If that
   fails, auto mode stops and reports.
2. Resolves the OPL runtime. If OPL is missing, **no game is installed**
   (no channel-less copies); the reason is shown and the app stays in
   the menu.
3. Installs every manifest game whose state is *new* (not on the HDD,
   not a duplicate, fits APA and free space — games that do not fit in
   the remaining space are skipped), one after another, each through
   the unchanged `game_install()` (copy, full CRC read-back, journal,
   channel), then copies the OPL cfg (5.).
4. Never deletes, repairs or overwrites anything. Existing,
   UNVERIFIED or failed games are left for the manual menus; a failed
   game is reported and is retried on the next run only if its
   partitions were cleaned up (otherwise it is *already on HDD*).
5. Shows the summary for 15 seconds (any button skips) and exits to the
   system menu (XMB), where the new games appear.

Without a pad, auto mode still runs (it needs no input); the existing
"no pad never accepts a prompt" rule is unaffected because auto mode
asks nothing.

## 6. Error handling

* Bad `udpfsd.cfg` (unknown key, missing folder, unreadable gamelist):
  clear startup error naming the line; the server does not start.
* A single bad image or art file: logged, listed as invalid or without
  jacket; the scan continues.
* Missing `.udpfsd` / old manifest version on the PS2: behave exactly as
  the current release.

## 7. Testing

Go (`make test-udpfsd`):
* `udpfsd.cfg` parsing, precedence, relative paths, unknown keys.
* Mount table: listing, read-only, path escapes, mounts shadowing fsroot.
* ISO parsing on synthetic ISO and ZSO images (existing ZSO test data).
* Title precedence (cfg > gamelist > file name), gamelist formats,
  `^!` unescaping.
* Art precedence and 74x108 conversion (png, jpg).
* Download via a local `httptest` server (no internet), timeouts, 404.
* Manifest output, sanitizing, atomic write, scan cache invalidation.

C (`make test`): manifest parser (incl. header `launcher=`/`auto=`);
title/jacket selection from manifest entries; OPL cfg copy decision
(exists / missing / no cfg); launcher source selection (valid server
copy / hash mismatch / missing -> embedded); auto-install planning
(which games are queued, skipped for space, OPL-missing stop).

Hardware checklist additions: manifest-driven Install All (N10);
jackets from `ART` appear in the XMB (D19); OPL cfg copied and its
compatibility modes take effect, VMC entry behaviour (D20); server
OPL-Launcher used for channels (D21); full auto run from a fresh console
— no installer partition, two games (ISO + ZSO) on the server — to
both games booting from the XMB with no input after starting the ELF
(D22).

"Flawless" is the target, not a claim: it is proven only when D22 passes
on a real DESR.

## 8. Out of scope

Bundled third-party title databases (the user's `GameListPS2.txt` and
CFG titles are the reference); CHD; resizing on the PS2; copying OPL ART
to the OPL partition (not used by the XMB or OPL's `mini` boot);
server-side checksums.
