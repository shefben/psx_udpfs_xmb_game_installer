"""Build the exact XMB Manager System.Drawing / nQuant image converter on Windows."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'vendor/nquant/nQuant.Core.dll'
SHA = '1103c66196ace7b26feac9ea1d3684dd518721b3342acfbcdf553a738668095b'

def main():
    if hashlib.sha256(DLL.read_bytes()).hexdigest() != SHA:
        raise ValueError('nQuant 1.0.3 assembly hash mismatch')
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/xmb-jackets'
    out.mkdir(parents=True, exist_ok=True)
    csc = Path('C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe')
    subprocess.run([str(csc), '/nologo', '/target:exe', '/optimize+',
                    '/reference:System.Drawing.dll', '/reference:' + str(DLL),
                    '/out:' + str(out / 'xmb-jackets.exe'), str(ROOT / 'tools/xmb-jackets.cs')], check=True)
    if DLL.resolve() != (out / DLL.name).resolve():
        shutil.copy2(DLL, out / DLL.name)

if __name__ == '__main__':
    main()
