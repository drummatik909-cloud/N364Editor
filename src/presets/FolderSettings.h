#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace maq {

// Usages pour lesquels l'utilisateur peut choisir un dossier.
enum class FolderPurpose {
    PresetImport,  // import de presets / banques
    PresetExport,  // export de presets / banques
    BankSave,      // sauvegarde de la banque
    SysexBackup,   // sauvegarde SysEx de la vraie MAQ (.syx)
    MidiExport     // export MIDI (.mid)
};
constexpr int kFolderPurposeCount = 5;

// Mémorise les dossiers choisis entre les sessions.
//  - keepSameFolder() == true  : un seul dossier pour tous les usages ; en choisir un pour l'un le change pour tous.
//  - keepSameFolder() == false : chaque usage garde son propre dossier.
// Si le dossier mémorisé n'existe plus, folderFor() retombe sur le dossier par défaut.
class FolderSettings {
public:
    explicit FolderSettings(std::filesystem::path defaultFolder = defaultPresetFolder());

    bool keepSameFolder() const { return keepSame_; }
    void setKeepSameFolder(bool keep);

    // Dossier à proposer dans le sélecteur de fichiers pour cet usage.
    std::filesystem::path folderFor(FolderPurpose p) const;
    // Vrai si un dossier avait été mémorisé mais n'existe plus (le plugin doit le signaler).
    bool usingFallback(FolderPurpose p) const;

    // À appeler après chaque choix de dossier par l'utilisateur.
    void setFolder(FolderPurpose p, const std::filesystem::path& folder);

    bool save(const std::filesystem::path& file) const;
    bool load(const std::filesystem::path& file);

    // Versions en UTF-8 (std::string) pour le code du plugin
    std::string folderForUtf8(FolderPurpose p) const;
    void setFolderUtf8(FolderPurpose p, const std::string& utf8Path);

    const std::filesystem::path& defaultFolder() const { return default_; }

    // %APPDATA%\MAQ16Plugin\settings.ini (Windows), ~/Library/Application Support/MAQ16Plugin/ (macOS)
    static std::filesystem::path defaultSettingsFile();
    // Documents/MAQ16 Presets
    static std::filesystem::path defaultPresetFolder();

private:
    std::filesystem::path candidate(FolderPurpose p) const;

    bool keepSame_ = true;
    std::filesystem::path shared_;  // dernier dossier choisi
    std::array<std::filesystem::path, kFolderPurposeCount> perPurpose_;
    std::filesystem::path default_;
};

}  // namespace maq
