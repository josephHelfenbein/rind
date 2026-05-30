#include <engine/Platform.h>

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#include <cstdio>

namespace engine {
namespace Platform {

	bool hasHdrDisplay(const std::vector<VkSurfaceFormatKHR>& /*surfaceFormats*/) {
		@autoreleasepool {
			static bool logged = false;
			bool anyHdr = false;
			for (NSScreen* screen in [NSScreen screens]) {
				CGFloat ref = [screen maximumReferenceExtendedDynamicRangeColorComponentValue];
				CGFloat pot = [screen maximumPotentialExtendedDynamicRangeColorComponentValue];
				CGFloat cur = [screen maximumExtendedDynamicRangeColorComponentValue];
				if (!logged) {
					std::fprintf(stderr, "[HDR] screen EDR - reference=%.2f potential=%.2f current=%.2f\n", ref, pot, cur);
				}
				if (ref > 1.0) anyHdr = true;
			}
			logged = true;
			return anyHdr;
		}
	}

}
}
#endif
