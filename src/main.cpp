#include <engine/Platform.h>
#include <rind/GameInstance.h>
#if RIND_ENABLE_STEAM
#include <rind/SteamManager.h>
#endif

int main() {
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
#endif
	int result = engine::Platform::runWithCrashReport([] {
		rind::GameInstance game;
		game.run();
	});
#if RIND_ENABLE_STEAM
	rind::steam::shutdown();
#endif
	return result;
}
