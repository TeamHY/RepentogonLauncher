/* Headless implementation of CLIParser.
 *
 * The GUI launcher parses its command line with wxCmdLineParser. rgon-cli
 * does not link against wxWidgets, so it provides its own parser that fills
 * the exact same fields. This keeps every upstream consumer of sCLI (curl
 * options, injector options, configuration overrides) working unchanged.
 *
 * Accepted syntax: --name, --name=value and --name value.
 */
#include <WinSock2.h>
#include <Windows.h>

#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include "launcher/cli.h"
#include "shared/logger.h"

#include "cli_parser.h"
#include "encoding.h"

namespace {
	std::string __parseError;
}

namespace rgon_cli {
	std::string const& GetParseError() {
		return __parseError;
	}
}

CLIParser::CLIParser() {

}

int CLIParser::Parse(int argc, wxChar** argv) {
	using rgon_cli::WideToAcp;
	using rgon_cli::WideToUtf8;

	__parseError.clear();

	for (int i = 1; i < argc; ++i) {
		std::wstring arg = argv[i];
		if (arg.rfind(L"--", 0) != 0 || arg.size() == 2) {
			__parseError = "unexpected argument: " + WideToUtf8(arg);
			return 1;
		}

		std::wstring name = arg.substr(2);
		std::optional<std::wstring> inlineValue;
		size_t eq = name.find(L'=');
		if (eq != std::wstring::npos) {
			inlineValue = name.substr(eq + 1);
			name = name.substr(0, eq);
		}

		const std::string key = WideToUtf8(name);

		auto takeValue = [&](std::wstring& out) -> bool {
			if (inlineValue) {
				out = *inlineValue;
				return true;
			}

			if (i + 1 >= argc) {
				__parseError = "missing value for --" + key;
				return false;
			}

			out = argv[++i];
			return true;
		};

		auto takeNumber = [&](long& out) -> bool {
			std::wstring value;
			if (!takeValue(value)) {
				return false;
			}

			wchar_t* end = nullptr;
			errno = 0;
			long number = wcstol(value.c_str(), &end, 10);
			if (value.empty() || *end != L'\0' || errno != 0) {
				__parseError = "invalid number for --" + key;
				return false;
			}

			out = number;
			return true;
		};

		auto takeString = [&](std::string& out) -> bool {
			std::wstring value;
			if (!takeValue(value)) {
				return false;
			}

			bool lossy = false;
			out = WideToAcp(value, &lossy);
			if (lossy) {
				__parseError = "--" + key + " contains characters that the system code page cannot represent";
				return false;
			}

			return true;
		};

		auto isSwitch = [&]() -> bool {
			if (inlineValue) {
				__parseError = "--" + key + " does not take a value";
				return false;
			}

			return true;
		};

#define SWITCH(OPTION, FIELD) \
		if (key == Options::OPTION) { \
			if (!isSwitch()) return 1; \
			FIELD = true; \
			continue; \
		}

		SWITCH(trapIsaacLaunch, _trapIsaacLaunch);
		SWITCH(forceWizard, _forceWizard);
		SWITCH(skipWizard, _skipWizard);
		SWITCH(forceRepentogonUpdate, _forceRepentogonUpdate);
		SWITCH(skipRepentogonUpdate, _skipRepentogonUpdate);
		SWITCH(repentogonInstallerWait, _repentogonInstallerWaitOnFinished);
		SWITCH(skipUniqueCheck, _skipUnique);
		SWITCH(unstableLauncher, _unstableLauncher);
		SWITCH(checkSelfUpdate, _checkSelfUpdate);
		SWITCH(stealthMode, _stealthMode);
		SWITCH(basementRenovator, _basementRenovator);
		SWITCH(strictThreadCancel, _strictThreadCancel);
		SWITCH(steam, _steamLaunch);
		SWITCH(luaDebug, _luaDebug);
		SWITCH(networkTest, _networkTest);
		SWITCH(repentogonConsole, _repentogonConsole);
		SWITCH(unstableUpdates, _unstableUpdates);
		SWITCH(automaticUpdates, _automaticUpdates);
#undef SWITCH

		if (key == Options::repentogonInstallerRefresh) {
			long refreshRate = 0;
			if (!takeNumber(refreshRate)) return 1;
			_repentogonInstallerRefreshRate = refreshRate > 0 ? refreshRate : Options::_repentogonInstallerDefaultRefreshRate;
			continue;
		}

		if (key == Options::curlLimit || key == Options::curlTimeout || key == Options::curlConnectTimeout) {
			long value = 0;
			if (!takeNumber(value)) return 1;
			if (value > 0) {
				if (key == Options::curlLimit) {
					_curlLimit = value;
				} else if (key == Options::curlTimeout) {
					_curlTimeout = value;
				} else {
					_curlConnectTimeout = value;
				}
			}
			continue;
		}

		if (key == Options::isaacWaitTime) {
			long waitTime = 0;
			if (!takeNumber(waitTime)) return 1;
			_isaacWaitTime = waitTime < 0 ? -1 : waitTime;
			continue;
		}

		if (key == Options::setStage || key == Options::setStageType) {
			long value = 0;
			if (!takeNumber(value)) return 1;
			if (value >= 0) {
				if (key == Options::setStage) {
					_stage = value;
				} else {
					_stageType = value;
				}
			}
			continue;
		}

		if (key == Options::configurationPath) {
			std::string path;
			if (!takeString(path)) return 1;
			_configurationPath = path;
			continue;
		}

		if (key == Options::isaacPath) {
			/* Resolve the path against the caller's working directory (rgon-cli
			 * later moves to its own folder) and accept the game folder as
			 * well as the executable itself.
			 */
			std::wstring value;
			if (!takeValue(value)) return 1;

			std::error_code ec;
			std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(value), ec);
			if (ec) {
				path = value;
			}

			if (std::filesystem::is_directory(path, ec)) {
				path /= L"isaac-ng.exe";
			}

			bool lossy = false;
			_isaacPath = WideToAcp(path.wstring(), &lossy);
			if (lossy) {
				__parseError = "--" + key + " contains characters that the system code page cannot represent";
				return 1;
			}
			continue;
		}

		if (key == Options::luaHeapSize) {
			if (!takeString(_luaHeapSize)) return 1;
			continue;
		}

		if (key == Options::loadRoom) {
			if (!takeString(_loadRoom)) return 1;
			continue;
		}

		if (key == Options::launchMode) {
			std::string mode;
			if (!takeString(mode)) return 1;
			if (mode == "vanilla") {
				_launchMode = LAUNCH_MODE_VANILLA;
			} else if (mode == "repentogon") {
				_launchMode = LAUNCH_MODE_REPENTOGON;
			} else {
				__parseError = "unknown launch mode: " + mode + " (expected \"vanilla\" or \"repentogon\")";
				return 1;
			}
			continue;
		}

		__parseError = "unknown option: --" + key;
		return 1;
	}

	if (_forceWizard && _skipWizard) {
		__parseError = "cannot force and skip the wizard at the same time";
		return 1;
	}

	if (_forceRepentogonUpdate && _skipRepentogonUpdate) {
		__parseError = "cannot force and skip the Repentogon update at the same time";
		return 1;
	}

	return 0;
}
