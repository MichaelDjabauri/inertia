#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app.hpp"

namespace {

void usage() {
    std::puts(
        "inertia [options]\n"
        "  --scene N       start on scene N (0-11)\n"
        "  --size WxH      window size in points\n"
        "  --frames N      run N fixed frames, then exit\n"
        "  --shot PATH     save a PNG of the last frame (needs --frames)\n"
        "  --no-ui         start with the interface hidden\n"
        "  --settings      start with the settings panel open\n"
        "  --tool NAME     ball, box, gem, bomb or pull\n"
        "  --benchmark     print frame timings on exit (needs --frames)\n"
        "  --render-scale  scene resolution relative to the window; automatic if unset\n"
        "  --script TEXT   scripted input for captures, such as \"60:press:400,300;90:release\"\n"
        "  --report        print the final state on exit");
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--scene") {
            options.scene = std::atoi(next());
        } else if (arg == "--size") {
            if (std::sscanf(next(), "%dx%d", &options.width, &options.height) != 2) {
                std::fputs("--size expects WxH, such as 1280x800\n", stderr);
                return 2;
            }
        } else if (arg == "--frames") {
            options.frames = std::atoi(next());
        } else if (arg == "--shot") {
            options.shotPath = next();
        } else if (arg == "--no-ui") {
            options.hideUi = true;
        } else if (arg == "--settings") {
            options.showSettings = true;
        } else if (arg == "--tool") {
            options.toolName = next();
        } else if (arg == "--benchmark") {
            options.benchmark = true;
        } else if (arg == "--script") {
            options.script = next();
        } else if (arg == "--report") {
            options.report = true;
        } else if (arg == "--render-scale") {
            options.renderScale = static_cast<float>(std::atof(next()));
        } else {
            usage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }

    App app(options);
    return app.run();
}
