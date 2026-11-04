#pragma once

#include <string>

namespace rgon_cli {
	/* Convert a UTF-16 string to the system ANSI code page, which is the
	 * encoding the upstream launcher code expects for paths. lossy (if not
	 * NULL) is set to true when a character cannot be represented.
	 */
	std::string WideToAcp(std::wstring const& str, bool* lossy = nullptr);

	std::wstring AcpToWide(std::string const& str);
	std::string WideToUtf8(std::wstring const& str);

	/* Convert a string produced by the upstream launcher code (ANSI code
	 * page) to UTF-8 for the JSON output.
	 */
	std::string AcpToUtf8(std::string const& str);
}
