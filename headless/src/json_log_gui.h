#pragma once

#include "shared/loggable_gui.h"

namespace rgon_cli {
	/* ILoggableGUI implementation that forwards upstream GUI messages to the
	 * JSON output as log events.
	 */
	class JsonLogGUI : public ILoggableGUI {
	public:
		void Log(const char* prefix, bool nl, const char* fmt, ...) override;
		void Log(const char* fmt, ...) override;
		void LogInfo(const char* fmt, ...) override;
		void LogNoNL(const char* fmt, ...) override;
		void LogWarn(const char* fmt, ...) override;
		void LogError(const char* fmt, ...) override;
	};
}
