PSX XMB Manager reference: https://github.com/SvenGDK/PSX-XMB-Manager
Pinned source commit: 72713da7a5ce5478b235e30dab61a395fdabc368.

PS2 channels use the same hidden `__.ID..TITLE` HDL data and separate
`PP.ID..TITLE` 128 MiB PFS partition as InstallWindow.InstallPS2Game.
The PFS partition holds EXECUTE.KELF and res/; its OSD header boots
`pfs:/EXECUTE.KELF`. Removing the PFS partition also removes the launcher.

NewGameProject's SYSTEM.CNF uses VER 1.01 and CRLF. Its icon.sys uses
zero background colours and its specified light vectors/colours. GameEditor
info.sys uses CRLF and a final line ending. User/game metadata values remain
specific to the installed game. The manual uses the same tags and page IDs;
titles are XML-escaped to keep user-entered text well formed.

Cover preparation calls System.Drawing Bitmap(original, Size) at 140x200 and
74x108, nQuant 1.0.3 WuQuantizer with defaults, and ImageFormat.Png. The same
encoder prepares embedded defaults and manual images. This replaces the old
Go box filter/Plan9 dithering and Python PNG encoder for final output.

Old server cache images are not reused merely because their dimensions match.
A versioned .xmb-manager stamp hashes both final PNGs. Missing or mismatched
stamps force regeneration from local or downloaded source art.

The console checks dimensions, 8-bit indexed/non-interlaced encoding, the
256-entry palette, chunk boundaries and CRCs before accepting a server cover.
This does not decode the PNG's deflate stream on the console.

Host tests cannot prove the reported PSX1 freeze fixed. Existing HDD channels
need resource refresh and channel rebuild to receive the new files/header;
replacing the USB ELF alone leaves old resources on the HDD. Hardware validation
must include at least 12 channels and launching games after a cold boot.

Dynamic cover preparation and its integration tests require Windows .NET or
WSL with Windows interoperability. Native Linux serves Windows-prepared pairs
with their provenance stamps, or falls back to embedded canonical defaults.
Wine/Mono output is not labeled as Manager output. Earlier LF/VER 1.00 headers
remain accepted during discovery; rebuilding writes the new Manager header.
