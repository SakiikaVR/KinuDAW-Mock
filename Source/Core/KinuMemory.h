#pragma once

namespace Rml::Detail {

// Referenced by Core.cpp so the translation unit containing the global
// allocation overrides is retained when rmlui_core is built as a static lib.
int GetKinuMemoryAllocatorVersion();

} // namespace Rml::Detail
