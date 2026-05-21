#include <engine/Platform.h>
#include <rind/GameInstance.h>

int main() {
	engine::Platform::initialize();
	return engine::Platform::runWithCrashReport([] {
		rind::GameInstance game;
		game.run();
	});
}
