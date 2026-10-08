#!/usr/bin/env python3
"""Wrap the pinned OPL ELF using XMB Manager v1.2's released PSX1 template.

Runs on Windows or WSL with Windows executable interoperability enabled.
The released executable returns 1 on success; validate the output bytes.
"""
import hashlib
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import urllib.request
import zipfile

URL = ('https://github.com/SvenGDK/PSX-XMB-Manager/releases/download/v1.2/'
       'PSX.XMB.Manager.v1.2.x64.zip')
PINS = {
    'KRYPTO.KHN': (20971782, 'f597f8178b9fa739ac0daf0286e90c262863b8091e463e9386845755e05ba3f7'),
    'SCEDoormat_NoME.exe': (16896, 'b4736263c071cea6b7e763102b839681a6f6c23479a04485097e1cf4c7d4c595'),
}

def pinned(data, name):
    size, sha = PINS[name]
    return len(data) == size and hashlib.sha256(data).hexdigest() == sha

def windows_path(path):
    if os.name == 'nt':
        return str(path.resolve())
    return subprocess.check_output(['wslpath', '-w', str(path.resolve())], text=True).strip()

def verify_output(data, elf, template):
    # makeKELF writes 80 fixed bytes, 16 per block and 16 final hashes,
    # then the input ELF and padding, then 8 bytes per hashed block.
    blocks, hashed = template[12], template[13]
    offset = 96 + blocks * 16
    if len(data) < offset + len(elf) + hashed * 8 or data[offset:offset + len(elf)] != elf:
        raise ValueError('wrapper output does not contain the exact input ELF')
    padding = len(data) - offset - len(elf) - hashed * 8
    if padding > 15 or any(data[offset + len(elf):offset + len(elf) + padding]):
        raise ValueError('unexpected KELF padding or size')
    if data[:4] == b'\x7fELF' or struct.unpack_from('<H', data, 20)[0] != offset:
        raise ValueError('invalid KELF header length')

def main():
    source, target = map(Path, sys.argv[1:])
    elf = source.read_bytes()
    if elf[:4] != b'\x7fELF':
        raise ValueError('input is not an ELF')
    cache = Path(__file__).resolve().parent.parent / 'build' / 'psx1-wrapper'
    cache.mkdir(parents=True, exist_ok=True)
    if not all((cache / n).exists() and pinned((cache / n).read_bytes(), n) for n in PINS):
        with urllib.request.urlopen(URL, timeout=120) as response:
            archive = zipfile.ZipFile(io.BytesIO(response.read()))
        for name in PINS:
            matches = [n for n in archive.namelist() if n.endswith('/Tools/' + name) or n == 'Tools/' + name]
            if len(matches) != 1:
                raise ValueError('missing/ambiguous released tool: ' + name)
            data = archive.read(matches[0])
            if not pinned(data, name):
                raise ValueError('released tool hash mismatch: ' + name)
            (cache / name).write_bytes(data)
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(target.suffix + '.new')
    temp.unlink(missing_ok=True)
    exe = cache / 'SCEDoormat_NoME.exe'
    command = [str(exe.resolve()), windows_path(source), windows_path(temp), windows_path(cache / 'KRYPTO.KHN')]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    try:
        verify_output(temp.read_bytes(), elf, (cache / 'KRYPTO.KHN').read_bytes())
    except Exception:
        temp.unlink(missing_ok=True)
        sys.stderr.write(result.stdout.decode(errors='replace'))
        raise
    temp.replace(target)
    print('Verified PSX1 wrapper:', target, hashlib.sha256(target.read_bytes()).hexdigest())

if __name__ == '__main__':
    main()
