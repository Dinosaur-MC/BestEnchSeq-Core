/// @file worker/main.cpp
/// besq-worker — sandboxed algorithm execution worker.
///
/// A dedicated process that dlopens a plugin and executes its IAlgorithm
/// against an AlgorithmInput received over stdin/stdout IPC.  Runs under
/// a seccomp (Linux) / Job Object (Windows) sandbox so untrusted
/// third-party plugins cannot touch the host process, its files, or the
/// network.
///
/// Sandbox lifecycle (Linux):
///   1. parse --plugin <path> [--capability <level>]
///   2. dlopen + create the plugin's IAlgorithm
///   3. install seccomp filter (compute-only syscall whitelist)
///   4. serve IPC: read AlgorithmInput → execute → stream events → AlgorithmOutput
///
/// Windows: the Job Object / restricted token are imposed by the parent;
/// this worker only serves the IPC loop.
///
/// The worker links ONLY the algorithm domain + common (never
/// business/orchestration/interface) — smallest trusted surface.

#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include <cstdio>
#include <cstring>

namespace {

void usage(const char *prog) {
    std::fprintf(stderr,
                 "usage: %s --plugin <path.so> [--capability none|filesystem|network]\n",
                 prog);
}

} // anonymous namespace

int main(int argc, char **argv) {
    const char *plugin_path = nullptr;
    const char *capability  = "none";

    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--plugin") == 0) {
            plugin_path = argv[i + 1];
        } else if (std::strcmp(argv[i], "--capability") == 0) {
            capability = argv[i + 1];
        }
    }

    if (!plugin_path) {
        usage(argv[0]);
        return 2;
    }

    // M1 scaffolding — the full IPC service loop lands here.
    // For now: verify the plugin loads, so the build + link path is proven.
    algorithm::AlgorithmLoader loader;
    if (loader.load_plugin(plugin_path)) {
        std::fprintf(stderr, "besq-worker: plugin loaded: %s (capability=%s)\n",
                     plugin_path, capability);
        return 0;
    }
    std::fprintf(stderr, "besq-worker: failed to load plugin: %s\n", plugin_path);
    return 1;
}
