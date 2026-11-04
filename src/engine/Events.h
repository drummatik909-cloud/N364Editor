#pragma once
// Table des Events et des modes, dans l'ordre de défilement du menu EVENT / MODE.
#include <string>
#include "Types.h"

namespace maq {

constexpr int kEventCount = 60;
constexpr int kModeCount = 8;
constexpr int kFreeCcEventIndex = 1;  // l'entrée « contrôleur libre » (0..127)

// Index 0 : ---   1 : contrôleur libre   2..6 : nA1..nA5   7..11 : nr1..nr5   12 : dYn
// 13..44 : contrôleurs fixes (C0..C19, C64..C74, C84)   45..47 : PIt, PIP, PI-   48 : At   49 : Prg
// 50..54 : PA1..PA5   55..59 : Pr1..Pr5
EventConfig eventForIndex(int index);   // pour le contrôleur libre, ccNumber = 0 (à remplacer par l'appelant)
std::string eventLabel(int index);      // 3 caractères au plus : "nA1", "C64", "PI-"...

const char* modeLabel(int mode);        // "FOr", "bAC", "PE1", "rnd", "PE2", "OnS", "Onr", "OnE"

// Correspondance inverse de eventForIndex (0 si inconnu). Sert à rappeler un preset.
int eventIndexFor(const EventConfig& e);

// Affichage VAL / NOTE des potards
bool isAbsoluteNoteEvent(const EventConfig& e);   // nA, PA : le potard donne une note
bool isPauseValue(const EventConfig& e, int pot); // PA / Pr avec le potard à fond à droite
int displayValue(const EventConfig& e, int pot);  // la valeur que verrait la MAQ
// Nom de note, convention de Bitwig (60 = 3) : "C1", "C#3" ou, en français, "Do1", "Do#3"
std::string noteName(int midiNote, bool french);
std::string valueText(const EventConfig& e, int pot, bool noteMode, bool french);

}  // namespace maq
