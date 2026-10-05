# PSX DESR UDPFS XMB Game Installer

Console-side PS2 game installer for the PSX DESR. It runs as its own XMB
channel, browses PS2 games served by
[udpfsd](https://github.com/pcm720/udpfsd) over the network (plain
`.iso`, or `.zso` decompressed by the server), installs them to the
internal HDD in HDL format, and gives every installed game its own XMB
channel containing its own copy of the signed
[OPL-Launcher](https://github.com/ps2homebrew/OPL-Launcher).

```
PC/NAS .iso/.zso -> udpfsd -> UDPFS/UDPRDMA -> udpfs_ioman.irx -> udpfs:/...
  -> GameSource -> 1 MiB buffer -> hdl0: -> hidden __.<ID>..<TITLE>
  -> verify -> visible PP.<ID>..<TITLE> (EXECUTE.KELF, res/, PPAA system.cnf)
```

* Hidden game data `__.SLUS-20312..GRAN_TURISMO_4` and visible channel
  `PP.SLUS-20312..GRAN_TURISMO_4` differ only in the first two bytes —
  the contract OPL-Launcher uses to find the game.
* The channel is created only after the whole installed game was read
  back from the HDD and its CRC-32 equals the CRC of the stream received
  from udpfsd; a failed copy never leaves a visible channel.
* Every step is journaled under `PP.UDPF-00001..INSTALLER:/state/`; the
  journal, not the HDL format, records whether data is verified.
* Release builds: `desr-udpfs-installer-bootstrap.elf` (first run, embeds
  the signed app and OPL-Launcher KELFs) and the signed XMB app
  `installer-EXECUTE.KELF`. The HDD driver is a reproducible source
  build (`tools/driver/`).

Status: PC verified only. See the checklist for DESR status.

Docs: [install & use](docs/INSTALL.md) · [build](docs/BUILD.md) ·
[server](docs/udpfsd-example/README.txt) ·
[provenance](docs/PROVENANCE.md) ·
[hardware checklist](docs/HARDWARE_TEST_CHECKLIST.md) ·
[limitations](KNOWN_LIMITATIONS.md)

## Layout

```
src/        console code; pure modules are host-tested
test/host/  native unit tests        test/fixtures/  hdl_dump PPAA fixture
tools/      fetch, sign, fixture and asset scripts
patches/    patches applied to upstream build copies
assets/     embedded jacket PNGs
```

## Credits

| Component | Author / project |
|---|---|
| UDPFS / udpfsd, Neutrino network modules (smap, ministack, udpfs_ioman) | Maximus32 |
| Open PS2 Loader, OPL-Launcher | ps2homebrew and contributors |
| APA/HDL driver (`ps2hdd-hdl.irx`), `hdlfs.irx` | HDLGameInstaller (sp193) |
| PS2SDK | ps2dev |
| Cover art downloads | xlenore/ps2-covers |
| Partition naming, CFG/ART layout, `GameListPS2.txt` | PFS-BatchKit-Manager (GDX-X) |

Open PS2 Loader is distributed under the AFL-3.0 licence (shipped as
`udpfsd/OPL-LICENSE.txt`). Pinned upstream revisions:
[reference/REVISIONS.txt](reference/REVISIONS.txt) and
[provenance](docs/PROVENANCE.md).
