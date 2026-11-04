#include <WinSock2.h>
#include <Windows.h>

#include "encoding.h"

namespace rgon_cli {
	std::string WideToAcp(std::wstring const& str, bool* lossy) {
		if (lossy) {
			*lossy = false;
		}

		if (str.empty()) {
			return {};
		}

		BOOL usedDefault = FALSE;
		int size = WideCharToMultiByte(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0, NULL, &usedDefault);
		std::string result(size, '\0');
		WideCharToMultiByte(CP_ACP, 0, str.c_str(), (int)str.size(), result.data(), size, NULL, &usedDefault);

		if (lossy) {
			*lossy = usedDefault != FALSE;
		}

		return result;
	}

	std::wstring AcpToWide(std::string const& str) {
		if (str.empty()) {
			return {};
		}

		int size = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
		std::wstring result(size, L'\0');
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), result.data(), size);
		return result;
	}

	std::string WideToUtf8(std::wstring const& str) {
		if (str.empty()) {
			return {};
		}

		int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0, NULL, NULL);
		std::string result(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), result.data(), size, NULL, NULL);
		return result;
	}

	std::string AcpToUtf8(std::string const& str) {
		return WideToUtf8(AcpToWide(str));
	}
}
