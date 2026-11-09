// The instrumented build's profile (STREET2_PGO=generate, switch/CMakeLists.txt). The objects are built with
// -fprofile-info-section, so no constructor registers them and nothing is written at exit by libgcov: its own files
// would take the PC's paths, which the console has not. Instead the counters of every object go, as gcov-tool's
// merge-stream format (a gcfn record with the object's name, then its gcda data), into one file on the SD:
// pgo/session-NN.stream, written again every minute and when the applet closes. tools/pgo_merge.ps1 turns the
// streams into the build's .gcda files for STREET2_PGO=use.
#include "host.h"

#ifdef STREET2_PGO_GENERATE
// gcov.h declares libgcov's C functions without extern "C"
extern "C" {
#include <gcov.h>
}

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"

extern "C" const gcov_info* const __start_gcov_info[];
extern "C" const gcov_info* const __stop_gcov_info[];

namespace {

std::string g_path;
std::chrono::steady_clock::time_point g_last;

void Append(const void* p, unsigned n, void* arg) {
    auto* out = static_cast<std::vector<uint8_t>*>(arg);
    out->insert(out->end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
}

// the objects were built in build-switch-pgo; the profile is read by the same objects built in build-switch
void Name(const char* name, void* arg) {
    std::string n = name ? name : "";
    if (size_t at = n.find("build-switch-pgo"); at != std::string::npos) n.replace(at, 16, "build-switch");
    __gcov_filename_to_gcfn(n.c_str(), Append, arg);
}

void* Allocate(unsigned n, void*) { return std::malloc(n); }

}  // namespace

namespace host {

void PgoStart() {
    std::string dir = (app::ExeDir() / "pgo").string();
    std::filesystem::create_directories(dir);
    for (int i = 1; i < 100; i++) {
        char name[32];
        std::snprintf(name, sizeof(name), "/session-%02d.stream", i);
        if (!std::filesystem::exists(dir + name)) {
            g_path = dir + name;
            break;
        }
    }
    g_last = std::chrono::steady_clock::now();
    LOGR("pgo: instrumented build, %d objects, profile to %s", int(__stop_gcov_info - __start_gcov_info),
         g_path.empty() ? "(no free name)" : g_path.c_str());
}

void PgoDump(bool now) {
    if (g_path.empty()) return;
    if (!now && std::chrono::steady_clock::now() - g_last < std::chrono::seconds(60)) return;
    g_last = std::chrono::steady_clock::now();
    auto t = g_last;
    std::vector<uint8_t> out;
    out.reserve(8u << 20);
    for (const gcov_info* const* i = __start_gcov_info; i < __stop_gcov_info; i++)
        __gcov_info_to_gcda(*i, Name, Append, Allocate, &out);
    std::string tmp = g_path + ".tmp";
    bool ok = false;
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
        ok = std::fclose(f) == 0 && ok;
    }
    std::remove(g_path.c_str());
    ok = ok && std::rename(tmp.c_str(), g_path.c_str()) == 0;
    LOGR("pgo: %zu KiB written in %.0f ms%s", out.size() / 1024,
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(), ok ? "" : " (failed)");
}

}  // namespace host

#else

namespace host {
void PgoStart() {}
void PgoDump(bool) {}
}  // namespace host

#endif
