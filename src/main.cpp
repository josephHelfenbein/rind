#include <rind/GameInstance.h>

#if defined(__APPLE__)
#include <cstdlib>
#include <filesystem>
#include <limits.h>
#include <mach-o/dyld.h>
#include <string>
#include <system_error>
#include <vector>

namespace {
  void configureBundledVulkanIcd() {
    if(std::getenv("VK_ICD_FILENAMES") || std::getenv("VK_DRIVER_FILES")) {
      return;
    }

    uint32_t executablePathSize = PATH_MAX;
    std::vector<char> executablePath(executablePathSize + 1, '\0');
    if(_NSGetExecutablePath(executablePath.data(), &executablePathSize) != 0) {
      executablePath.assign(executablePathSize + 1, '\0');
      if(_NSGetExecutablePath(executablePath.data(), &executablePathSize) != 0) {
        return;
      }
    }

    std::error_code ec;
    std::filesystem::path resolvedExecutable = std::filesystem::weakly_canonical(std::filesystem::path(executablePath.data()), ec);
    if(ec) {
      return;
    }

    const std::filesystem::path bundledIcdPath = resolvedExecutable.parent_path() / "icd" / "MoltenVK_icd.json";
    if(!std::filesystem::exists(bundledIcdPath, ec) || ec) {
      return;
    }

    const std::string bundledIcdPathString = bundledIcdPath.string();
    setenv("VK_DRIVER_FILES", bundledIcdPathString.c_str(), 0);
    setenv("VK_ICD_FILENAMES", bundledIcdPathString.c_str(), 0);
  }
}
#endif

int main() {
#if defined(__APPLE__)
  configureBundledVulkanIcd();
#endif

  rind::GameInstance game;
  game.run();
  return 0;
}
