#include <engine/Platform.h>
#include <rind/GameInstance.h>
#if RIND_ENABLE_STEAM
#include <rind/SteamManager.h>
#include <rind/SteamInput.h>
#endif

#if RIND_ENABLE_STEAM && defined(__linux__)
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static void fixupSteamOverlayPreload(char** argv) {
	if (std::getenv("RIND_OVERLAY_PRELOAD_FIXED")) return;
	if (std::getenv("RIND_NO_OVERLAY_FIX")) return;
	if (!std::getenv("SteamAppId") && !std::getenv("SteamGameId")) return;

	auto fileExists = [](const std::string& p) {
		struct stat st;
		return stat(p.c_str(), &st) == 0;
	};

	const char* compat = std::getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH");
	const char* home = std::getenv("HOME");
	std::string overlay;
	for (const std::string& dir : {
			compat ? std::string(compat) : std::string(),
			home ? std::string(home) + "/.steam/steam" : std::string(),
			home ? std::string(home) + "/.local/share/Steam" : std::string() }) {
		if (dir.empty()) continue;
		std::string candidate = dir + "/ubuntu12_64/gameoverlayrenderer.so";
		if (fileExists(candidate)) { overlay = candidate; break; }
	}
	if (overlay.empty()) return;

	setenv("LD_PRELOAD", overlay.c_str(), 1);
	setenv("RIND_OVERLAY_PRELOAD_FIXED", "1", 1);
	execv("/proc/self/exe", argv);
}
#endif

int main(int argc, char** argv) {
	(void)argc;
#if RIND_ENABLE_STEAM && defined(__linux__)
	fixupSteamOverlayPreload(argv);
#else
	(void)argv;
#endif
#if RIND_ENABLE_STEAM
	// must be the first Steam call
	// true means Steam is relaunching, so exit
	if (rind::steam::restartAppIfNecessary()) {
		return 0;
	}
#endif
	engine::Platform::initialize();
#if RIND_ENABLE_STEAM
	rind::steam::init();
	rind::steaminput::init();
#endif
	int result = engine::Platform::runWithCrashReport([] {
		rind::GameInstance game;
		game.run();
	});
#if RIND_ENABLE_STEAM
	rind::steaminput::shutdown();
	rind::steam::shutdown();
#endif
	return result;
}
