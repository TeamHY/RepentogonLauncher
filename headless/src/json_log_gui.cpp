#include <cstdarg>
#include <cstdio>
#include <string>

#include "json_log_gui.h"
#include "output.h"

namespace rgon_cli {
	static std::string Format(const char* fmt, va_list va) {
		va_list copy;
		va_copy(copy, va);
		int size = vsnprintf(NULL, 0, fmt, copy);
		va_end(copy);

		if (size <= 0) {
			return {};
		}

		std::string result(size + 1, '\0');
		vsnprintf(result.data(), result.size(), fmt, va);
		result.resize(size);
		return result;
	}

	void JsonLogGUI::Log(const char* prefix, bool, const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		std::string message = Format(fmt, va);
		va_end(va);
		output::Log("info", std::string(prefix ? prefix : "") + message);
	}

	void JsonLogGUI::Log(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		output::Log("info", Format(fmt, va));
		va_end(va);
	}

	void JsonLogGUI::LogInfo(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		output::Log("info", Format(fmt, va));
		va_end(va);
	}

	void JsonLogGUI::LogNoNL(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		output::Log("info", Format(fmt, va));
		va_end(va);
	}

	void JsonLogGUI::LogWarn(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		output::Log("warn", Format(fmt, va));
		va_end(va);
	}

	void JsonLogGUI::LogError(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		output::Log("error", Format(fmt, va));
		va_end(va);
	}
}
