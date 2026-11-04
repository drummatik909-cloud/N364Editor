#pragma once
// Types de base du moteur MAQ 16/3 (étape 1 : une unité, sortie MIDI).
#include <array>
#include <cstdint>
#include <string>

namespace maq {

constexpr int kRows = 3;
constexpr int kSteps = 16;
constexpr int kPresetSlots = 99;

// Events pris en charge à l'étape 1.
// Reste à faire (étapes suivantes) : Cn1, Ad1, A.P 1/2 (Poly-AT), t.4/8/16/32 (durée de pas).
enum class EventKind {
    Off,            // ---
    FreeCC,         // contrôleur libre 0..127 (firmware 3.5+)
    FixedCC,        // un des 32 contrôleurs fixes (index 0..31)
    NoteAbs,        // nA1..nA5
    NoteRel,        // nr1..nr5
    NoteAbsPause,   // PA1..PA5 (potard à fond à droite = pause)
    NoteRelPause,   // Pr1..Pr5
    Velocity,       // dYn
    PitchBend,      // PIt  (0..127 avec plateau central)
    PitchBendPos,   // PIP  (64..127)
    PitchBendNeg,   // PI-  (0..64)
    AfterTouch,     // At
    ProgramChange   // Prg
};

enum class PlayMode {
    Forward,           // FOr
    Backward,          // bAC
    Pendulum1,         // PE1 : premier et dernier pas joués deux fois
    Random,            // rnd
    Pendulum2,         // PE2 : premier et dernier pas joués une fois
    OneShotSingle,     // OnS : s'arrête au dernier pas
    OneShotRetrigger,  // Onr : (chaînage entre rangées : à faire, se comporte comme OnS)
    OneEvent           // OnE : un seul pas
};

struct EventConfig {
    EventKind kind = EventKind::NoteAbs;
    int octaves = 1;   // 1..5 pour les Events de notes
    int ccNumber = 0;  // FreeCC : 0..127 ; FixedCC : index 0..31
};

struct RowConfig {
    EventConfig event;
    int channel = 1;        // 1..16
    int first = 1;          // 1..16
    int last = 16;          // 1..16
    int gate = 6;           // g : horloges entre deux pas
    int noteLen = 1;        // n : 1 = égal à g, sinon <= g
    PlayMode mode = PlayMode::Forward;
    bool noteTriggered = false;  // modes déclenchés par notes : à faire
    int velocityTargets = 0;     // pour Velocity : masque des rangées concernées (bit r)
    std::array<int, kSteps> pot{};     // position du potard 0..127
    std::array<bool, kSteps> mute{};   // pas muet
};

struct MidiMsg {
    enum class Type { NoteOn, NoteOff, ControlChange, PitchBend, ChannelPressure, ProgramChange };
    Type type;
    int channel;  // 0..15
    int data1;    // note / contrôleur / programme / pression ; PitchBend : valeur 14 bits
    int data2;    // vélocité / valeur
};

// Conversions potard -> valeur (hypothèses documentées dans la spécification).
int noteFromPot(int octaves, int pot);          // 36 .. 36 + 12*octaves
int noteOffsetFromPot(int octaves, int pot);    // 0 .. 12*octaves (notes relatives)
int fixedCcNumber(int index);                   // 0..19, 64..74, 84
int pitchBendFromPot(EventKind kind, int pot);  // valeur 7 bits (0..127)

}  // namespace maq
