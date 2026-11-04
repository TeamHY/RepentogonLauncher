#pragma once

#include <cstdint>
#include <string>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

/* Every line written on stdout is a single JSON object (JSON Lines). The
 * "type" field identifies the event:
 *
 *   {"type":"log","level":"info|warn|error","message":"..."}
 *   {"type":"download","file":"...","bytes":123}
 *   {"type":"result","ok":true|false, ...}
 *
 * The result event is always the last line written by a command.
 */
namespace rgon_cli::output {
	using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;

	/* Locate stdout. When the (GUI subsystem) executable is started from a
	 * terminal without redirection, attach to the parent console instead.
	 */
	void Init();

	/* Write one already serialized JSON object followed by a newline. */
	void WriteLine(std::string const& json);

	/* Emit a log event. message is expected in the ANSI code page. */
	void Log(const char* level, std::string const& message);

	/* Emit a download progress event. */
	void Download(std::string const& file, uint64_t bytes);

	/* Helper writing a string that comes from upstream code (ANSI code page). */
	void WriteAcpString(JsonWriter& writer, std::string const& value);
}
