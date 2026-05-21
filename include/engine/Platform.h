#pragma once

#include <functional>

namespace engine {
namespace Platform {

     void initialize();

     int runWithCrashReport(const std::function<void()>& body, const char* logName = "Rind.log");

}
}
