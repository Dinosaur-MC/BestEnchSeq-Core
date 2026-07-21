// =============================================================================
// Generic algorithm plugin entry point.
//
// Compile this file into a shared library to produce a runtime-loadable
// algorithm plugin.  The strategy class and name are injected via macros:
//
//   BESQ_PLUGIN_HEADER  - #include path for the strategy header
//   BESQ_PLUGIN_CLASS   - fully qualified class name (e.g. GreedyAlgorithm)
//   BESQ_PLUGIN_NAME    - string literal for the algorithm name
//
// Example command line (Linux / WSL):
//
//   clang++ -shared -fPIC -o greedy.so                               \
//     src/algorithm/strategies/greedy/GreedyAlgorithm.cpp             \
//     src/algorithm/PluginEntry.cpp                                   \
//     -DBESQ_PLUGIN_HEADER="\"algorithm/strategies/greedy/GreedyAlgorithm.h\"" \
//     -DBESQ_PLUGIN_CLASS=GreedyAlgorithm                             \
//     -DBESQ_PLUGIN_NAME=greedy                                       \
//     -I src -I include -std=c++20
//
// The generated .so can then be loaded at runtime:
//
//   BesqContext ctx;
//   ctx.load_plugins("/path/to/plugins");
//   // or: besq --plugin-dir /path/to/plugins ...
// =============================================================================

#include "plugin/PluginAPI.h"

// The strategy header selected by the build system
#include BESQ_PLUGIN_HEADER

extern "C" {

BESQ_PLUGIN_EXPORT const char* besq_plugin_name = BESQ_PLUGIN_NAME;

BESQ_PLUGIN_EXPORT const char* besq_plugin_version = "1.0.0";

BESQ_PLUGIN_EXPORT void* besq_plugin_create_algorithm() {
    return new BESQ_PLUGIN_CLASS();
}

BESQ_PLUGIN_EXPORT void besq_plugin_destroy_algorithm(void* ptr) {
    delete static_cast<IAlgorithm*>(ptr);
}

} // extern "C"
