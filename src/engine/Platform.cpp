#include <engine/Platform.h>

#include <exception>
#include <iostream>

#if defined(__APPLE__)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <mach-o/dyld.h>
#include <string>
#include <system_error>
#include <vector>
#endif

namespace engine {
namespace Platform {

#if defined(__APPLE__)
	static void configureBundledVulkanIcd() {
		if (std::getenv("VK_ICD_FILENAMES") || std::getenv("VK_DRIVER_FILES")) {
			return;
		}

		uint32_t executablePathSize = PATH_MAX;
		std::vector<char> executablePath(executablePathSize + 1, '\0');
		if (_NSGetExecutablePath(executablePath.data(), &executablePathSize) != 0) {
			executablePath.assign(executablePathSize + 1, '\0');
			if (_NSGetExecutablePath(executablePath.data(), &executablePathSize) != 0) {
				return;
			}
		}

		std::error_code ec;
		std::filesystem::path resolvedExecutable = std::filesystem::weakly_canonical(std::filesystem::path(executablePath.data()), ec);
		if (ec) {
			return;
		}

		const std::filesystem::path bundledIcdPath = resolvedExecutable.parent_path() / "icd" / "MoltenVK_icd.json";
		if (!std::filesystem::exists(bundledIcdPath, ec) || ec) {
			return;
		}

		const std::string bundledIcdPathString = bundledIcdPath.string();
		setenv("VK_DRIVER_FILES", bundledIcdPathString.c_str(), 0);
		setenv("VK_ICD_FILENAMES", bundledIcdPathString.c_str(), 0);
	}
#endif

	void initialize() {
#if defined(__APPLE__)
		configureBundledVulkanIcd();
#endif
	}

#if !defined(__APPLE__)
	bool hasHdrDisplay(const std::vector<VkSurfaceFormatKHR>& surfaceFormats) {
		for (const auto& fmt : surfaceFormats) {
			if (fmt.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
			    fmt.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
				return true;
			}
		}
		return false;
	}
#endif

	int runWithCrashReport(const std::function<void()>& body, const char* logName) {
		try {
			body();
		} catch (const std::exception& e) {
			std::cerr << "Fatal error: " << e.what() << "\n";
#if defined(__APPLE__)
			if (const char* home = std::getenv("HOME")) {
				std::ofstream log(std::string(home) + "/Library/Logs/" + logName, std::ios::app);
				if (log) log << "Fatal error: " << e.what() << "\n";
			}
#else
			(void)logName;
#endif
			return 1;
		}
		return 0;
	}

}
}
