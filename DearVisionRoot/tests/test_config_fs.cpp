// Host-only fixtures may use std::filesystem; the Android implementation must not.
#include "Config.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

int main() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("dear-config-fs-"+std::to_string(getpid()));
    const auto nested=root/"private"/"configs";
    config::setDirectory(nested.string());
    AllVars vars;
    assert(config::list().empty()); // Missing directories are normal on first launch.
    assert(!config::load("missing",vars));
    assert(!config::remove("missing"));
    assert(config::save("zeta",vars)); // Recursively creates missing parents.
    assert(fs::is_directory(nested));
    assert(!fs::exists(nested/"zeta.cfg.tmp"));
    vars.Aimbot.fov=200;
    assert(config::save("zeta",vars)); // Existing directory/file; atomic replacement.
    AllVars loaded;
    assert(config::load("zeta",loaded) && loaded.Aimbot.fov==200);
    config::setDirectory(nested.string()+"//");
    assert(config::save("alpha_1",vars)); // Trailing/repeated separators.
    std::ofstream(nested/"invalid.name.cfg")<<"";
    std::ofstream(nested/"zeta.cfg.tmp")<<"";
    std::ofstream(nested/".cfg")<<"";
    std::ofstream(nested/"notes.txt")<<"";
    fs::create_directory(nested/"folder.cfg");
    assert((config::list()==std::vector<std::string>{"alpha_1","zeta"}));
    assert(!config::save("folder",vars)); // Rename over a directory must fail cleanly.
    assert(fs::is_directory(nested/"folder.cfg"));
    assert(!fs::exists(nested/"folder.cfg.tmp"));
    assert(config::remove("alpha_1"));
    assert(!config::remove("../escape"));
    assert(!config::save("",vars));

    const auto blocker=root/"not-a-directory";
    std::ofstream(blocker)<<"keep";
    config::setDirectory(blocker.string());
    assert(!config::save("test",vars)); // EEXIST is not enough: it must be a directory.
    config::setDirectory((blocker/"configs").string());
    assert(!config::save("test",vars)); // A file in an intermediate path is also an error.
    std::string content;std::ifstream(blocker)>>content;
    assert(content=="keep");

    const auto original=fs::current_path();
    fs::current_path(root);
    config::setDirectory("relative/nested");
    assert(config::save("relative",vars));
    assert(config::load("relative",loaded));
    assert(fs::exists(root/"relative/nested/relative.cfg"));
    config::setDirectory("");
    assert(!config::save("empty",vars));
    assert(!config::load("empty",loaded));
    assert(config::list().empty());
    assert(!fs::exists(root/"empty.cfg"));
    fs::current_path(original);
    fs::remove_all(root);
}
