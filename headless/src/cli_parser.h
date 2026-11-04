#pragma once

#include <string>

namespace rgon_cli {
	/* Human readable description of the last command line parsing error. */
	std::string const& GetParseError();
}
