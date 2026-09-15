#include "KinuMemory.h"

// This header must be included in exactly one translation unit. It defines
// the complete global C++ new/delete family and forwards it to mimalloc.
#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable : 4559)
#endif
#include <mimalloc-new-delete.h>
#ifdef _MSC_VER
	#pragma warning(pop)
#endif

namespace Rml::Detail {

int GetKinuMemoryAllocatorVersion()
{
	return mi_version();
}

} // namespace Rml::Detail
