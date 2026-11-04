#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include "launcher/launcher_configuration.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	/* Start Isaac and wait for it to exit. Return the exit code of the game,
	 * or 0xFFFFFFFF if the game could not be started.
	 */
	DWORD Launch(ILoggableGUI* gui, const char* path, bool isLegacy,
		LauncherConfiguration const* configuration);

	/* Start Isaac and return as soon as its main thread has been resumed.
	 * Return true on success, in which case processId (if not NULL) receives
	 * the ID of the game process.
	 */
	bool LaunchDetached(ILoggableGUI* gui, const char* path, bool isLegacy,
		LauncherConfiguration const* configuration, DWORD* processId);
}
