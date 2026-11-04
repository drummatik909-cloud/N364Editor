#include "Preferences.h"
#include <fstream>
#include "presets/FolderSettings.h"

namespace fs = std::filesystem;

namespace maq {

bool Preferences::save(const fs::path& file) const {
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "language=" << langCode(language) << "\n";
    f << "manual=" << manualPath << "\n";
    return static_cast<bool>(f);
}

bool Preferences::load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "language") language = langFromCode(val);
        else if (key == "manual") manualPath = val;
    }
    return true;
}

fs::path Preferences::defaultFile() {
    return FolderSettings::defaultSettingsFile().parent_path() / "preferences.ini";
}

}  // namespace maq
