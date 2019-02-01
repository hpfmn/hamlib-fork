#include <boost/dll/import.hpp> // for import_alias
#include <iostream>
#include "../plugin.hpp"

namespace dll = boost::dll;

int main(int argc, char* argv[]) {

	//std::string path = "/home/hpfmn/GIT/satNOGS/Hamlib/satnogs/plugins";
	boost::filesystem::path lib_path("../");		  // argv[1] contains path to directory with our plugin library
	boost::shared_ptr<satnogs_plugin_block> plugin;			// variable to hold a pointer to plugin variable
	std::map<std::string, boost::shared_ptr<satnogs_plugin_block>> plugin_registry;

	for (auto& entry: boost::filesystem::directory_iterator(lib_path)) {
			if (entry.path().extension() == ".so" || entry.path().extension() == ".dll") {
				std::cout << "Loading the plugin" << std::endl;
				plugin = dll::import<satnogs_plugin_block>(entry, "plugin");
				plugin_registry[plugin->name()] = plugin;
			}
	}
}
