"""Compare our PNGs with the actual VB resize method from pinned Manager source.

Windows-only oracle test; requires reference/PSX-XMB-Manager and .NET Framework.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
utils = (ROOT / 'reference/PSX-XMB-Manager/PSX XMB Manager/Classes/Utils.vb').read_text(encoding='utf-8-sig')
start = utils.index('    Public Shared Function GetResizedBitmap(')
end = utils.index('    End Function',start) + len('    End Function')
method = utils[start:end].replace('Public Shared Function','Public Function')
framework = Path('C:/Windows/Microsoft.NET/Framework64/v4.0.30319')
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    oracle = td / 'oracle.vb'
    oracle.write_text('Imports System.Drawing\nImports System.Net\nImports System.IO\nImports nQuant\nModule Oracle\n' + method + '''
Sub Main(args As String())
 Using bitmap = GetResizedBitmap(args(0), Integer.Parse(args(2)), Integer.Parse(args(3)))
  Using compressed = New WuQuantizer().QuantizeImage(bitmap)
   compressed.Save(args(1), Imaging.ImageFormat.Png)
  End Using
 End Using
End Sub
End Module
''')
    subprocess.run([str(framework/'vbc.exe'), '/nologo', '/target:exe',
                    '/vbruntime:'+str(framework/'Microsoft.VisualBasic.dll'),
                    '/reference:System.Drawing.dll', '/reference:'+str(ROOT/'vendor/nquant/nQuant.Core.dll'),
                    '/out:'+str(td/'oracle.exe'), str(oracle)],check=True)
    shutil.copy2(ROOT/'vendor/nquant/nQuant.Core.dll',td/'nQuant.Core.dll')
    for source in ['assets/game/default_jkt_001.png','assets/manual/blank.png']:
        for w,h in [(140,200),(74,108)]:
            ref,actual=td/'ref.png',td/'actual.png'
            path=(ROOT/source).resolve()
            subprocess.run([str(td/'oracle.exe'),path.as_uri(),str(ref),str(w),str(h)],check=True)
            subprocess.run([str(ROOT/'vendor/nquant/xmb-jackets.exe'),str(path),str(actual),str(w),str(h)],check=True)
            assert ref.read_bytes()==actual.read_bytes(),(source,w,h)
    large,small=td/'pair_L.png',td/'pair.png'
    subprocess.run([str(ROOT/'vendor/nquant/xmb-jackets.exe'),str(ROOT/'assets/game/default_jkt_001.png'),str(large),str(small)],check=True)
    expected='system-drawing-nquant-1.0.3-v1\n'+hashlib.sha256(large.read_bytes()).hexdigest()+'\n'+hashlib.sha256(small.read_bytes()).hexdigest()+'\n'
    assert Path(str(small)+'.xmb-manager').read_text()==expected
print('4 PNG outputs byte-identical to the pinned Manager VB resize + nQuant encoder')
