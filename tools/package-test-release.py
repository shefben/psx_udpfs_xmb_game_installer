#!/usr/bin/env python3
"""Package the template-wrapped test build without modifying dist/."""
import hashlib
import argparse
import importlib.util
from pathlib import Path
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--release', help='Promote the tested payloads to a versioned release')
parser.add_argument('--stage', type=Path, help='Keep the verified distribution tree for end-user packaging')
args = parser.parse_args()
if args.release and not all(c.isdigit() or c == '.' for c in args.release):
    parser.error('release must be a numeric version')
OUTPUT = ROOT / (f'PSX-UDPFS-Installer_V{args.release}.zip' if args.release else 'desr-udpfs-installer-test.zip')
LABEL = f'version {args.release}' if args.release else 'test release'
spec = importlib.util.spec_from_file_location('psx1_wrapper', ROOT / 'tools/build-psx1-kelf.py')
wrapper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wrapper)

updates = {
    'desr-udpfs-installer-app.elf': ROOT / 'build/app/desr-udpfs-installer-app.elf',
    'desr-udpfs-installer-bootstrap.elf': ROOT / 'build/bootstrap-test/desr-udpfs-installer-bootstrap.elf',
    'installer-EXECUTE.KELF': ROOT / 'build/kelf/installer-test-EXECUTE.KELF',
    'opl-launcher-PSX1.KELF': ROOT / 'build/kelf/opl-launcher-PSX1.KELF',
    'opl-launcher-EXECUTE.KELF': ROOT / 'build/kelf/opl-launcher-EXECUTE.KELF',
    'udpfsd/opl-launcher-EXECUTE.KELF': ROOT / 'build/kelf/opl-launcher-EXECUTE.KELF',
    'udpfsd/udpfsd-windows-amd64.exe': ROOT / 'build/udpfsd/udpfsd-windows-amd64.exe',
    'udpfsd/udpfsd-linux-amd64': ROOT / 'build/udpfsd/udpfsd-linux-amd64',
}
template = (ROOT / 'build/psx1-wrapper/KRYPTO.KHN').read_bytes()
if not wrapper.pinned(template, 'KRYPTO.KHN'):
    raise ValueError('PSX1 template hash mismatch')
wrapper.verify_output(updates['installer-EXECUTE.KELF'].read_bytes(),
                      updates['desr-udpfs-installer-app.elf'].read_bytes(), template)
wrapper.verify_output(updates['opl-launcher-PSX1.KELF'].read_bytes(),
                      (ROOT / 'build/opl-launcher/OPL-Launcher-stripped.elf').read_bytes(), template)
bootstrap = updates['desr-udpfs-installer-bootstrap.elf'].read_bytes()
for name in ['installer-EXECUTE.KELF', 'opl-launcher-PSX1.KELF', 'opl-launcher-EXECUTE.KELF']:
    if updates[name].read_bytes() not in bootstrap:
        raise ValueError('bootstrap does not embed ' + name)
app = updates['desr-udpfs-installer-app.elf'].read_bytes()
if updates['opl-launcher-PSX1.KELF'].read_bytes() not in app:
    raise ValueError('app does not embed PSX1 launcher')

with tempfile.TemporaryDirectory(prefix='psxi-test-release-') as temp:
    stage = Path(temp)
    shutil.copytree(ROOT / 'dist', stage, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('udpfsd-cache', '*.zip'))
    for name, source in updates.items():
        shutil.copy2(source, stage / name)
    for name in ['CHANGELOG.md', 'KNOWN_LIMITATIONS.md']:
        shutil.copy2(ROOT / name, stage / name)
    for source in (ROOT / 'docs').glob('*.md'):
        shutil.copy2(source, stage / 'docs' / source.name)
        if (stage / source.name).exists():
            shutil.copy2(source, stage / source.name)
    (stage / ('RELEASE-NOTES.txt' if args.release else 'TEST-RELEASE.txt')).write_text(
        f'DESR installer {LABEL}\n\n'
        'Select Network / Console Settings > DESR generation before PS2 game installation.\n'
        'PSX1: hidden HDL data plus PFS channel and pinned v1.2 template wrapper.\n'
        'PSX2: hidden HDL data plus automatic PFS cover / launch channel, existing PSX2 wrapper.\n'
        'Server automatically downloads missing covers before publishing the ready game list.\n'
        'Both generations install ID-matched artwork and descriptions into each channel res/ folder.\n'
        'LZ4 transfers; server CSO/CHD/split-ISO decoding; single-BIN/CUE conversion.\n'
        'PS1 multi-disc and automatic CFG/CHT/VMC/ART installation; extras management menu.\n'
        'Existing cards and settings are preserved during automatic installs.\n'
        'Installed Games / Repair flag IMAGE NEEDED; fetch XMB resources from the server.\n'
        'Installer EXECUTE.KELF uses the pinned v1.2 template wrapper from the tested package.\n'
        'The bootstrap embeds this updated installer and both game-launcher variants.\n'
        + ('The user reports the earlier compatibility test worked; newly added features need hardware testing.\n'
         if args.release else
         'Hardware verification is pending: the IOP-loading hang and multi-channel XMB freeze\n'
         'are not confirmed resolved. Test one channel, then two, on the DESR-7000.\n'),
        encoding='utf-8')
    (stage / 'BUILD-MANIFEST.txt').write_text(
        f'DESR installer {LABEL}\n'
        'Installer wrapper: released XMB Manager v1.2 KRYPTO.KHN / SCEDoormat_NoME\n'
        'PSX1 game wrapper: same pinned release template, project OPL ELF\n'
        'PSX2 game wrapper and app launcher: existing build/kelf payloads\n'
        'Server binaries: CHD-enabled, LZ4/image-format/cover/description patches\n'
        'OPL runtime: existing dist payload\n'
        'Validation: make test, EE app/bootstrap compilation, wrapper ELF/header verification,\n'
        'embedded payload verification, ZIP SHA-256 and CRC verification.\n'
        + ''.join(f'{name}: {sha}\n' for name, (_, sha) in wrapper.PINS.items()), encoding='utf-8')
    sums = []
    for path in sorted(stage.rglob('*')):
        if path.is_file() and path.name != 'SHA256SUMS':
            sums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(stage).as_posix())
    (stage / 'SHA256SUMS').write_text('\n'.join(sums) + '\n', encoding='utf-8', newline='\n')
    if args.stage:
        if args.stage.exists():
            raise ValueError('stage already exists: ' + str(args.stage))
        shutil.copytree(stage, args.stage)
    temporary_output = OUTPUT.with_suffix('.zip.new')
    with zipfile.ZipFile(temporary_output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(stage).as_posix())
    with zipfile.ZipFile(temporary_output) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP CRC verification failed')
        for entry in sums:
            digest, name = entry.split('  ', 1)
            if hashlib.sha256(archive.read(name)).hexdigest() != digest:
                raise ValueError('ZIP checksum mismatch: ' + name)
    temporary_output.replace(OUTPUT)
print(OUTPUT)
print('SHA256:', hashlib.sha256(OUTPUT.read_bytes()).hexdigest())
print('Bytes:', OUTPUT.stat().st_size)
