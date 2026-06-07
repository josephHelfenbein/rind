#include <engine/Platform.h>

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#include <cstdio>

namespace engine {
namespace Platform {

	bool hasHdrDisplay(const std::vector<VkSurfaceFormatKHR>& /*surfaceFormats*/) {
		@autoreleasepool {
			bool anyHdr = false;
			for (NSScreen* screen in [NSScreen screens]) {
				CGFloat ref = [screen maximumReferenceExtendedDynamicRangeColorComponentValue];
				CGFloat pot = [screen maximumPotentialExtendedDynamicRangeColorComponentValue];
				CGFloat cur = [screen maximumExtendedDynamicRangeColorComponentValue];
				if (ref > 1.0) anyHdr = true;
			}
			return anyHdr;
		}
	}

}
}
#endif
