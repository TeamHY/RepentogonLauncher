/* rgon-cli: headless front-end for the REPENTOGON launcher.
 *
 * It reuses the launcher core (installation validation, Repentogon
 * installer, game patching and the regular launch routine) without any
 * wxWidgets dependency, so other programs can drive it and read its JSON
 * output. See headless/README.md for the command line and output contract.
 */
#include <WinSock2.h>
#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <variant>

#include "launcher/cli.h"
#include "launcher/injector.h"
#include "launcher/installation.h"
#include "launcher/launcher_configuration.h"
#include "launcher/repentogon_installer.h"
#include "launcher/standalone_rgon_folder.h"
#include "launcher/version.h"
#include "shared/github_executor.h"
#include "shared/logger.h"

#include "cli_parser.h"
#include "encoding.h"
#include "json_log_gui.h"
#include "output.h"

#ifndef RGON_CLI_VERSION
#define RGON_CLI_VERSION "dev"
#endif

namespace fs = std::filesystem;
namespace output = rgon_cli::output;
using output::JsonWriter;

namespace {
	enum ExitCode {
		EXIT_CODE_OK = 0,
		EXIT_CODE_FAILED = 1,
		EXIT_CODE_USAGE = 2,
		EXIT_CODE_BUSY = 3,
	};

	const char* const Usage =
		"Usage: rgon-cli <command> [options]\n"
		"\n"
		"Commands:\n"
		"  version    Print the rgon-cli and launcher core versions\n"
		"  status     Report the state of the Isaac and Repentogon installations\n"
		"  install    Install or update Repentogon\n"
		"  launch     Start the game (vanilla or Repentogon) and exit immediately\n"
		"\n"
		"Common options:\n"
		"  --isaac <path>                 Path to isaac-ng.exe or to the game folder (required\n"
		"                                 by status, install and launch)\n"
		"\n"
		"install options:\n"
		"  --force-repentogon-update      Reinstall even if Repentogon is up-to-date\n"
		"  --unstable-updates             Allow Repentogon pre-releases\n"
		"\n"
		"launch options:\n"
		"  --launch-mode <mode>           \"repentogon\" (default) or \"vanilla\"\n"
		"  --console                      Enable the Repentogon console window\n"
		"  --luadebug                     Enable system functions in the Lua API\n"
		"  --luaheapsize <size>           Size of the Lua heap (default: 1024M)\n"
		"  --set-stage <id>               Starting stage\n"
		"  --set-stage-type <id>          Starting stage variant\n"
		"  --load-room <path>             Path to a rooms xml file for testing\n";

	rgon_cli::JsonLogGUI __gui;

	/* Write the final result event of a command. */
	void WriteResult(const char* command, bool ok, const char* error, std::string const& message,
		std::function<void(JsonWriter&)> const& extra = nullptr) {
		rapidjson::StringBuffer buffer;
		JsonWriter writer(buffer);
		writer.StartObject();
		writer.Key("type");
		writer.String("result");
		writer.Key("command");
		writer.String(command);
		writer.Key("ok");
		writer.Bool(ok);

		if (error) {
			writer.Key("error");
			writer.String(error);
		}

		if (!message.empty()) {
			writer.Key("message");
			output::WriteAcpString(writer, message);
		}

		if (extra) {
			extra(writer);
		}

		writer.EndObject();
		output::WriteLine(buffer.GetString());
	}

	const char* RepentogonStateName(RepentogonInstallationStatus state) {
		switch (state) {
		case REPENTOGON_INSTALLATION_STATUS_NONE:
			return "none";
		case REPENTOGON_INSTALLATION_STATUS_BROKEN:
			return "broken";
		case REPENTOGON_INSTALLATION_STATUS_LEGACY:
			return "legacy";
		case REPENTOGON_INSTALLATION_STATUS_MODERN:
			return "modern";
		default:
			return "unknown";
		}
	}

	const char* InstallationResultName(Launcher::RepentogonInstallationResult result) {
		using namespace Launcher;
		switch (result) {
		case REPENTOGON_INSTALLATION_RESULT_OK:
			return "ok";
		case REPENTOGON_INSTALLATION_RESULT_DSOUND_REMOVAL_FAILED:
			return "dsound_removal_failed";
		case REPENTOGON_INSTALLATION_RESULT_MISSING_ASSET:
			return "missing_asset";
		case REPENTOGON_INSTALLATION_RESULT_DOWNLOAD_ERROR:
			return "download_error";
		case REPENTOGON_INSTALLATION_RESULT_BAD_HASH:
			return "bad_hash";
		case REPENTOGON_INSTALLATION_RESULT_EXTRACT_FAILED:
			return "extract_failed";
		case REPENTOGON_INSTALLATION_RESULT_INVALID_OUTPUT_DIR:
			return "invalid_output_dir";
		case REPENTOGON_INSTALLATION_RESULT_NO_REPENTOGON:
			return "no_repentogon_folder";
		case REPENTOGON_INSTALLATION_RESULT_NO_MARKER:
			return "no_marker";
		case REPENTOGON_INSTALLATION_RESULT_NO_ISAAC_COPY:
			return "isaac_copy_failed";
		case REPENTOGON_INSTALLATION_RESULT_FUCK_METHOD:
			return "method_file_failed";
		case REPENTOGON_INSTALLATION_RESULT_NO_STEAM_APPID:
			return "steam_appid_failed";
		case REPENTOGON_INSTALLATION_RESULT_NO_ISAAC_PATCH:
			return "isaac_patch_failed";
		case REPENTOGON_INSTALLATION_RESULT_LAUNCHER_UPDATE_REQUIRED:
			return "launcher_update_required";
		case REPENTOGON_INSTALLATION_RESULT_NONE:
		default:
			return "none";
		}
	}

	void WriteInstallationData(JsonWriter& writer, InstallationData const& data) {
		writer.StartObject();
		writer.Key("valid");
		writer.Bool(data.IsValid());
		writer.Key("exePath");
		output::WriteAcpString(writer, data.GetExePath());
		writer.Key("folderPath");
		output::WriteAcpString(writer, data.GetFolderPath());
		writer.Key("version");
		output::WriteAcpString(writer, data.GetVersion());
		writer.Key("compatible");
		writer.Bool(data.IsCompatibleWithRepentogon());
		writer.Key("needsPatch");
		writer.Bool(data.NeedsPatch());
		writer.Key("reason");
		output::WriteAcpString(writer, data.GetReason());
		writer.EndObject();
	}

	/* Same rule as LauncherMainWindow::GetIsaacLaunchability. */
	bool IsRepentogonLaunchable(Launcher::Installation const& installation) {
		return installation.GetRepentogonInstallation().IsValid() &&
			installation.GetIsaacInstallation().GetRepentogonInstallation().IsCompatibleWithRepentogon();
	}

	void WriteStatus(JsonWriter& writer, Launcher::Installation const& installation) {
		IsaacInstallation const& isaac = installation.GetIsaacInstallation();
		RepentogonInstallation const& repentogon = installation.GetRepentogonInstallation();
		InstallationData const& main = isaac.GetMainInstallation();
		InstallationData const& repentogonExe = isaac.GetRepentogonInstallation();

		writer.Key("isaac");
		WriteInstallationData(writer, main);

		writer.Key("repentogon");
		writer.StartObject();

		fs::path folder;
		writer.Key("folderPath");
		if (main.IsValid() && standalone_rgon::GenerateRepentogonPath(main.GetFolderPath(), folder, false)) {
			output::WriteAcpString(writer, folder.string());
		} else {
			writer.Null();
		}

		writer.Key("installed");
		writer.Bool(repentogon.IsValid() && repentogonExe.IsValid());
		writer.Key("launchable");
		writer.Bool(IsRepentogonLaunchable(installation));
		writer.Key("state");
		writer.String(RepentogonStateName(repentogon.GetState()));
		writer.Key("version");
		output::WriteAcpString(writer, repentogon.GetRepentogonVersion());
		writer.Key("zhlVersion");
		output::WriteAcpString(writer, repentogon.GetZHLVersion());
		writer.Key("loaderVersion");
		output::WriteAcpString(writer, repentogon.GetZHLLoaderVersion());
		writer.Key("versionsMatch");
		writer.Bool(repentogon.RepentogonZHLVersionMatch());

		writer.Key("files");
		writer.StartArray();
		for (FoundFile const& file : repentogon.GetRepentogonInstallationFilesState()) {
			writer.StartObject();
			writer.Key("name");
			output::WriteAcpString(writer, file.filename);
			writer.Key("found");
			writer.Bool(file.found);
			writer.EndObject();
		}
		writer.EndArray();

		writer.Key("executable");
		WriteInstallationData(writer, repentogonExe);

		writer.EndObject();
	}

	/* Copy the relevant command line options into the configuration overrides,
	 * mirroring LauncherConfiguration::LoadFromCLI. The configuration file of
	 * the GUI launcher is never read nor written.
	 */
	void ApplyOverrides(LauncherConfiguration& configuration) {
		configuration.SetIsaacExecutablePathOverride(sCLI->IsaacPath());
		configuration.SetIsaacLaunchModeOverride(sCLI->GetLaunchMode().value_or(LAUNCH_MODE_REPENTOGON));

		if (sCLI->Stage()) {
			configuration.SetStageOverride((long)*sCLI->Stage());
			configuration.SetStageTypeOverride((long)sCLI->StageType().value_or(0));
		}

		configuration.SetLuaDebugOverride(sCLI->LuaDebug());

		if (!sCLI->LoadRoom().empty()) {
			configuration.SetLoadRoomOverride(sCLI->LoadRoom());
		}

		if (!sCLI->LuaHeapSize().empty()) {
			configuration.SetLuaHeapSizeOverride(sCLI->LuaHeapSize());
		}

		configuration.SetRepentogonConsoleOverride(sCLI->RepentogonConsole());
		configuration.SetUnstableUpdatesOverride(sCLI->UnstableUpdates());
	}

	/* Validate the Isaac executable given on the command line. Write a failed
	 * result and return false if it is missing or invalid.
	 */
	bool LoadInstallation(const char* command, Launcher::Installation& installation, int* exitCode) {
		if (sCLI->IsaacPath().empty()) {
			WriteResult(command, false, "usage", "--isaac is required", [](JsonWriter& writer) {
				writer.Key("usage");
				writer.String(Usage);
			});
			*exitCode = EXIT_CODE_USAGE;
			return false;
		}

		int flags = installation.SetIsaacExecutable(sCLI->IsaacPath());
		if (!(flags & Launcher::Installation::IsaacInstallationValid)) {
			WriteResult(command, false, "invalid_isaac",
				sCLI->IsaacPath() + " is not a valid Isaac executable",
				[&](JsonWriter& writer) { WriteStatus(writer, installation); });
			*exitCode = EXIT_CODE_FAILED;
			return false;
		}

		return true;
	}

	int CommandVersion() {
		WriteResult("version", true, nullptr, "", [](JsonWriter& writer) {
			writer.Key("cliVersion");
			writer.String(RGON_CLI_VERSION);
			writer.Key("launcherVersion");
			writer.String(Launcher::LAUNCHER_VERSION);
		});
		return EXIT_CODE_OK;
	}

	int CommandStatus() {
		LauncherConfiguration configuration;
		ApplyOverrides(configuration);
		Launcher::Installation installation(&__gui, &configuration);

		int exitCode = EXIT_CODE_OK;
		if (!LoadInstallation("status", installation, &exitCode)) {
			return exitCode;
		}

		WriteResult("status", true, nullptr, "", [&](JsonWriter& writer) {
			WriteStatus(writer, installation);
		});
		return EXIT_CODE_OK;
	}

	/* Accumulate download notifications and emit throttled progress events. */
	class DownloadTracker {
	public:
		void Add(Launcher::RepentogonInstallationNotification::FileDownload const& download) {
			Entry& entry = _entries[download._id];
			entry.filename = download._filename;
			entry.bytes += download._size;

			auto now = std::chrono::steady_clock::now();
			if (now - entry.lastEmit >= std::chrono::milliseconds(250)) {
				entry.lastEmit = now;
				entry.emittedBytes = entry.bytes;
				output::Download(entry.filename, entry.bytes);
			}
		}

		void Flush() {
			for (auto& [id, entry] : _entries) {
				if (entry.emittedBytes != entry.bytes) {
					entry.emittedBytes = entry.bytes;
					output::Download(entry.filename, entry.bytes);
				}
			}
		}

	private:
		struct Entry {
			std::string filename;
			uint64_t bytes = 0;
			uint64_t emittedBytes = 0;
			std::chrono::steady_clock::time_point lastEmit;
		};

		std::map<uint32_t, Entry> _entries;
	};

	void DrainNotifications(Threading::Monitor<Launcher::RepentogonInstallationNotification>* monitor,
		DownloadTracker& tracker) {
		using Notification = Launcher::RepentogonInstallationNotification;

		bool timedout = false;
		while (std::optional<Notification> notification = monitor->Get(&timedout)) {
			if (auto general = std::get_if<Notification::GeneralNotification>(&notification->_data)) {
				output::Log(general->_isError ? "error" : "info", general->_text);
			} else if (auto removal = std::get_if<Notification::FileRemoval>(&notification->_data)) {
				if (removal->_ok) {
					output::Log("info", "Removed " + removal->_name);
				} else {
					output::Log("error", "Unable to remove " + removal->_name);
				}
			} else if (auto download = std::get_if<Notification::FileDownload>(&notification->_data)) {
				tracker.Add(*download);
			}
		}
	}

	int CommandInstall() {
		/* Only one installation at a time: two installers would write the same
		 * files in the Repentogon folder and in launcher-data.
		 */
		HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\rgon-cli-install");
		if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
			CloseHandle(mutex);
			WriteResult("install", false, "busy", "another installation is already running");
			return EXIT_CODE_BUSY;
		}

		LauncherConfiguration configuration;
		ApplyOverrides(configuration);
		Launcher::Installation installation(&__gui, &configuration);

		int exitCode = EXIT_CODE_OK;
		if (!LoadInstallation("install", installation, &exitCode)) {
			if (mutex) {
				CloseHandle(mutex);
			}
			return exitCode;
		}

		InstallationData const& main = installation.GetIsaacInstallation().GetMainInstallation();
		if (!main.IsCompatibleWithRepentogon()) {
			WriteResult("install", false, "isaac_incompatible", main.GetReason(),
				[&](JsonWriter& writer) { WriteStatus(writer, installation); });
			if (mutex) {
				CloseHandle(mutex);
			}
			return EXIT_CODE_FAILED;
		}

		installation.UpdateShadowVersions();

		using Installer = Launcher::RepentogonInstaller;
		Installer::DownloadInstallRepentogonResult result;
		Launcher::RepentogonInstallationResult installationResult;
		{
			Installer installer(&installation);
			auto [future, monitor] = installer.InstallLatestRepentogon(sCLI->ForceRepentogonUpdate(),
				sCLI->UnstableUpdates());

			DownloadTracker tracker;
			while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
				DrainNotifications(monitor, tracker);
			}

			DrainNotifications(monitor, tracker);
			tracker.Flush();

			result = future.get();
			installationResult = installer.GetState().result;
		}

		/* Refresh the state from the disk for the final report. */
		installation.CheckRepentogonInstallation();

		bool ok = false;
		const char* error = nullptr;
		const char* outcome = nullptr;
		switch (result) {
		case Installer::DOWNLOAD_INSTALL_REPENTOGON_OK:
			ok = true;
			outcome = "installed";
			break;

		case Installer::DOWNLOAD_INSTALL_REPENTOGON_UTD:
			ok = true;
			outcome = "up_to_date";
			break;

		case Installer::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES:
			error = "check_updates_failed";
			break;

		case Installer::DOWNLOAD_INSTALL_REPENTOGON_ERR:
		default:
			error = installationResult == Launcher::REPENTOGON_INSTALLATION_RESULT_LAUNCHER_UPDATE_REQUIRED
				? "launcher_update_required" : "install_failed";
			break;
		}

		WriteResult("install", ok, error, "", [&](JsonWriter& writer) {
			if (outcome) {
				writer.Key("outcome");
				writer.String(outcome);
			}
			writer.Key("installResult");
			writer.String(InstallationResultName(installationResult));
			WriteStatus(writer, installation);
		});

		if (mutex) {
			CloseHandle(mutex);
		}

		return ok ? EXIT_CODE_OK : EXIT_CODE_FAILED;
	}

	int CommandLaunch() {
		LauncherConfiguration configuration;
		ApplyOverrides(configuration);
		Launcher::Installation installation(&__gui, &configuration);

		int exitCode = EXIT_CODE_OK;
		if (!LoadInstallation("launch", installation, &exitCode)) {
			return exitCode;
		}

		const bool vanilla = configuration.IsaacLaunchMode() == LAUNCH_MODE_VANILLA;
		IsaacInstallation const& isaac = installation.GetIsaacInstallation();
		std::string exePath;

		if (vanilla) {
			exePath = isaac.GetMainInstallation().GetExePath();
		} else {
			if (!installation.GetRepentogonInstallation().IsValid()) {
				WriteResult("launch", false, "repentogon_not_installed",
					"the REPENTOGON installation is missing or incomplete",
					[&](JsonWriter& writer) { WriteStatus(writer, installation); });
				return EXIT_CODE_FAILED;
			}

			if (!IsRepentogonLaunchable(installation)) {
				WriteResult("launch", false, "repentogon_incompatible",
					"the Isaac executable is not compatible with REPENTOGON",
					[&](JsonWriter& writer) { WriteStatus(writer, installation); });
				return EXIT_CODE_FAILED;
			}

			exePath = isaac.GetRepentogonInstallation().GetExePath();
		}

		DWORD processId = 0;
		/* Same arguments as LauncherMainWindow::LaunchIsaac. */
		if (!Launcher::LaunchDetached(&__gui, exePath.c_str(), vanilla, &configuration, &processId)) {
			WriteResult("launch", false, "launch_failed", "unable to start the game, check rgon-cli-launch.log");
			return EXIT_CODE_FAILED;
		}

		WriteResult("launch", true, nullptr, "", [&](JsonWriter& writer) {
			writer.Key("mode");
			writer.String(vanilla ? "vanilla" : "repentogon");
			writer.Key("pid");
			writer.Uint(processId);
			writer.Key("exePath");
			output::WriteAcpString(writer, exePath);
		});
		return EXIT_CODE_OK;
	}

	/* The launcher core resolves its data files (launcher-data/...) relative
	 * to the working directory, like the GUI launcher does.
	 */
	void MoveToExecutableFolder() {
		wchar_t exePath[MAX_PATH];
		DWORD length = GetModuleFileNameW(NULL, exePath, MAX_PATH);
		if (length == 0 || length == MAX_PATH) {
			return;
		}

		fs::path folder = fs::path(exePath).parent_path();
		SetCurrentDirectoryW(folder.wstring().c_str());

		std::error_code ec;
		fs::create_directories(folder / L"launcher-data", ec);
	}
}

int wmain(int argc, wchar_t** argv) {
	output::Init();

	if (argc < 2) {
		WriteResult("help", false, "usage", "missing command", [](JsonWriter& writer) {
			writer.Key("usage");
			writer.String(Usage);
		});
		return EXIT_CODE_USAGE;
	}

	const std::string command = rgon_cli::WideToUtf8(argv[1]);
	if (command == "help" || command == "--help" || command == "-h") {
		WriteResult("help", true, nullptr, "", [](JsonWriter& writer) {
			writer.Key("usage");
			writer.String(Usage);
		});
		return EXIT_CODE_OK;
	}

	using Handler = int (*)();
	static const std::map<std::string, Handler> handlers = {
		{ "version", &CommandVersion },
		{ "status", &CommandStatus },
		{ "install", &CommandInstall },
		{ "launch", &CommandLaunch },
	};

	auto handler = handlers.find(command);
	if (handler == handlers.end()) {
		WriteResult(command.c_str(), false, "usage", "unknown command: " + command, [](JsonWriter& writer) {
			writer.Key("usage");
			writer.String(Usage);
		});
		return EXIT_CODE_USAGE;
	}

	/* argv[1] (the command) takes the place of the program name. */
	if (sCLI->Parse(argc - 1, argv + 1) != 0) {
		WriteResult(command.c_str(), false, "usage", rgon_cli::GetParseError(), [](JsonWriter& writer) {
			writer.Key("usage");
			writer.String(Usage);
		});
		return EXIT_CODE_USAGE;
	}

	MoveToExecutableFolder();

	std::string logFile = "rgon-cli-" + command + ".log";
	Logger::Init(logFile.c_str(), false);
	Logger::Info("rgon-cli %s (launcher core %s), command: %s\n", RGON_CLI_VERSION,
		Launcher::LAUNCHER_VERSION, command.c_str());

	sGithubExecutor->Start();

	return handler->second();
}
