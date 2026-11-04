#include <WinSock2.h>
#include <Windows.h>

#include <mutex>

#include "encoding.h"
#include "output.h"

namespace rgon_cli::output {
	static HANDLE __stdout = INVALID_HANDLE_VALUE;
	static std::mutex __outputMutex;

	void Init() {
		HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
			if (AttachConsole(ATTACH_PARENT_PROCESS)) {
				handle = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL, OPEN_EXISTING, 0, NULL);
			}
		}

		__stdout = handle;
		if (GetFileType(__stdout) == FILE_TYPE_CHAR) {
			/* Interactive console: make UTF-8 output readable. */
			SetConsoleOutputCP(CP_UTF8);
		}
	}

	void WriteLine(std::string const& json) {
		std::unique_lock<std::mutex> lck(__outputMutex);
		if (__stdout == NULL || __stdout == INVALID_HANDLE_VALUE) {
			return;
		}

		std::string line = json;
		line += '\n';

		const char* data = line.data();
		DWORD remaining = (DWORD)line.size();
		while (remaining > 0) {
			DWORD written = 0;
			if (!WriteFile(__stdout, data, remaining, &written, NULL) || written == 0) {
				return;
			}
			data += written;
			remaining -= written;
		}
	}

	void WriteAcpString(JsonWriter& writer, std::string const& value) {
		std::string utf8 = AcpToUtf8(value);
		writer.String(utf8.c_str(), (rapidjson::SizeType)utf8.size());
	}

	void Log(const char* level, std::string const& message) {
		std::string trimmed = message;
		while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
			trimmed.pop_back();
		}

		if (trimmed.empty()) {
			return;
		}

		rapidjson::StringBuffer buffer;
		JsonWriter writer(buffer);
		writer.StartObject();
		writer.Key("type");
		writer.String("log");
		writer.Key("level");
		writer.String(level);
		writer.Key("message");
		WriteAcpString(writer, trimmed);
		writer.EndObject();
		WriteLine(buffer.GetString());
	}

	void Download(std::string const& file, uint64_t bytes) {
		rapidjson::StringBuffer buffer;
		JsonWriter writer(buffer);
		writer.StartObject();
		writer.Key("type");
		writer.String("download");
		writer.Key("file");
		WriteAcpString(writer, file);
		writer.Key("bytes");
		writer.Uint64(bytes);
		writer.EndObject();
		WriteLine(buffer.GetString());
	}
}
