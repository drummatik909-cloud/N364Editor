#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "FolderSettings.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace maq {

namespace {
const char* const kKeys[kFolderPurposeCount] = {"import", "export", "bank", "sysex", "midi"};

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

bool dirExists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

fs::path fromUtf8(const std::string& s) { return fs::u8path(s); }
}  // namespace

FolderSettings::FolderSettings(fs::path defaultFolder) : default_(std::move(defaultFolder)) {}

void FolderSettings::setKeepSameFolder(bool keep) { keepSame_ = keep; }

fs::path FolderSettings::candidate(FolderPurpose p) const {
    if (keepSame_) return shared_;
    const fs::path& own = perPurpose_[static_cast<int>(p)];
    return own.empty() ? shared_ : own;
}

fs::path FolderSettings::folderFor(FolderPurpose p) const {
    fs::path c = candidate(p);
    return dirExists(c) ? c : default_;
}

bool FolderSettings::usingFallback(FolderPurpose p) const {
    fs::path c = candidate(p);
    return !c.empty() && !dirExists(c);
}

void FolderSettings::setFolder(FolderPurpose p, const fs::path& folder) {
    shared_ = folder;
    if (keepSame_) {
        for (auto& f : perPurpose_) f = folder;  // tous les usages suivent
    } else {
        perPurpose_[static_cast<int>(p)] = folder;
    }
}

std::string FolderSettings::folderForUtf8(FolderPurpose p) const { return folderFor(p).u8string(); }

void FolderSettings::setFolderUtf8(FolderPurpose p, const std::string& utf8Path) {
    setFolder(p, fromUtf8(utf8Path));
}

bool FolderSettings::save(const fs::path& file) const {
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "keepSame=" << (keepSame_ ? 1 : 0) << "\n";
    f << "shared=" << shared_.u8string() << "\n";
    for (int i = 0; i < kFolderPurposeCount; ++i)
        f << kKeys[i] << "=" << perPurpose_[i].u8string() << "\n";
    return static_cast<bool>(f);
}

bool FolderSettings::load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "keepSame") keepSame_ = (val != "0");
        else if (key == "shared") shared_ = val.empty() ? fs::path() : fromUtf8(val);
        else {
            for (int i = 0; i < kFolderPurposeCount; ++i)
                if (key == kKeys[i]) perPurpose_[i] = val.empty() ? fs::path() : fromUtf8(val);
        }
    }
    return true;
}

fs::path FolderSettings::defaultSettingsFile() {
#if defined(_WIN32)
    fs::path base = envOr("APPDATA", envOr("USERPROFILE", "."));
#elif defined(__APPLE__)
    fs::path base = fs::path(envOr("HOME", ".")) / "Library" / "Application Support";
#else
    fs::path base = envOr("XDG_CONFIG_HOME", envOr("HOME", ".") + "/.config");
#endif
    return base / "MAQ16Plugin" / "settings.ini";
}

fs::path FolderSettings::defaultPresetFolder() {
#if defined(_WIN32)
    fs::path home = envOr("USERPROFILE", ".");
#else
    fs::path home = envOr("HOME", ".");
#endif
    return home / "Documents" / "MAQ16 Presets";
}

}  // namespace maq
