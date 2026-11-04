#pragma once
#include <array>
#include <filesystem>
#include <string>
#include "engine/Types.h"

namespace maq {

// Un preset d'unité (étape 1) : nom, tempo et les 3 rangées.
// Les presets de configuration complète à 4 unités viendront à l'étape 3.
struct Preset {
    bool used = false;  // seul un slot utilisé est sauvegardé
    std::string name;
    int bpm = 120;
    std::array<RowConfig, kRows> rows;
};

// Banque de 99 slots, sauvegardée en fichier texte .maqbank.
class Bank {
public:
    Bank();

    Preset& slot(int n);               // n = 1..99
    const Preset& slot(int n) const;

    std::string serialize() const;
    bool deserialize(const std::string& text);

    bool saveToFile(const std::filesystem::path& file) const;
    bool loadFromFile(const std::filesystem::path& file);

private:
    std::array<Preset, kPresetSlots> slots_;
};

}  // namespace maq
