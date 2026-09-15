# Third-party notices

KinuDAW and the bundled KinuUI/RmlUi sources are distributed under the MIT license in LICENSE.txt. Dependencies retain their own copyright and license notices.

| Component | Version/source | License |
| --- | --- | --- |
| VST3 SDK | Steinberg SDK 3.8.1, pinned recursive Git submodules | MIT, Dependencies/vst3sdk/LICENSE.txt |
| miniaudio | 0.11.23 | MIT or public domain; this distribution uses MIT, Dependencies/audio/LICENSE.miniaudio |
| nlohmann/json | 3.12.0 | MIT, Dependencies/audio/LICENSE.json |
| mimalloc | pinned Git submodule | MIT, Dependencies/mimalloc/LICENSE |
| FreeType | 2.14.1, pinned Git submodule | FreeType License (FTL), Dependencies/freetype/docs/FTL.TXT |
| LINE Seed JP | bundled unmodified font | SIL Open Font License 1.1, Samples/basic/daw_timeline/data/fonts/OFL.txt |
| Lato, Noto Emoji and other sample fonts | bundled unmodified fonts | Individual notices in Samples/assets/LICENSE.txt |

Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. This build uses the FreeType License, rather than its alternative GPL license. The release ZIP includes the FTL notice.

Tracy is an optional upstream profiling dependency, licensed BSD-3-Clause in Dependencies/tracy/LICENSE. It is disabled in the DAW production build.

Installed VST3 plug-ins and their sample libraries remain the property of their vendors. They are loaded from the user's computer and are not included in the source or release ZIP. Their licenses are independent of KinuDAW's MIT license. VST is a trademark of Steinberg Media Technologies GmbH.

The Windows build uses the Microsoft Visual C++ runtime. Install the Microsoft Visual C++ v14 x64 Redistributable if it is absent; this runtime is not included in the MIT license or ZIP.
