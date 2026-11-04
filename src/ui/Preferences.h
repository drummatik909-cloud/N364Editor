#pragma once
#include <filesystem>
#include <string>
#include "Strings.h"

namespace maq {

// Préférences de l'utilisateur, communes à tous les projets : langue et copie locale du manuel.
class Preferences {
public:
    Lang language = Lang::French;
    std::string manualPath;  // chemin UTF-8 du PDF du manuel (vide = aucun)

    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;

    // Même dossier que les réglages de dossiers : preferences.ini
    static std::filesystem::path defaultFile();
};

}  // namespace maq
