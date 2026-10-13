Pinned nQuant 1.0.3 from PSX XMB Manager commit
72713da7a5ce5478b235e30dab61a395fdabc368, packages/nQuant.1.0.3/Lib/net40.
Assembly SHA-256: 1103c66196ace7b26feac9ea1d3684dd518721b3342acfbcdf553a738668095b.
nQuant is by Matt Wrock, licensed under Apache-2.0 (LICENSE.txt).

xmb-jackets.exe is built from tools/xmb-jackets.cs with
`python tools/build-xmb-jackets.py vendor/nquant` on Windows.
The implementation uses Manager's Bitmap(original, Size) resize and
WuQuantizer.QuantizeImage defaults, then System.Drawing's PNG encoder.
Its normal resize returns Format32bppArgb; an unexpected format fails
instead of entering Manager's erroneous 76x108 conversion fallback.

Keep the EXE and DLL together beside udpfsd. Windows requires .NET Framework
4.x. WSL uses Windows interoperability and wslpath. Native Linux hosts serve
Windows-prepared cached pairs or embedded Windows-encoded defaults; they do
not generate artwork using a substitute encoder.

To prepare art for a Linux server on Windows, run
`xmb-jackets.exe source.jpg ID_L.png ID.png` and copy both PNGs and the
generated ID.png.xmb-manager stamp into udpfsd-cache/served/jkt/.
Use the full boot ID, e.g. SLUS_203.12. Preserve the stamp alongside the files.
