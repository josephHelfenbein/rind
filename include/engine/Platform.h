#pragma once

#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

namespace engine {
namespace Platform {

     void initialize();

     int runWithCrashReport(const std::function<void()>& body, const char* logName = "Rind.log");

     bool hasHdrDisplay(const std::vector<VkSurfaceFormatKHR>& surfaceFormats);

}
}
