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
* The channel is created only after the copied data verified; a failed
  copy never leaves a visible channel.
* Every step is journaled under `PP.UDPFS-INSTALLER:/state/`.

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
