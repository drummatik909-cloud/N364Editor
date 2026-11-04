#pragma once
#include <vector>
#include "Types.h"

namespace maq {

// Une unité MAQ : 3 rangées de 16 pas pilotées par l'horloge MIDI (24 impulsions par noire).
class Unit {
public:
    Unit();

    std::array<RowConfig, kRows> rows;
    int relativeBase = 36;  // note de référence pour NoteRel (hypothèse : note reçue absente = 36)

    // Gamme (lot 3) : en mode « quantification en sortie » (2), chaque note jouée est ramenée dans la gamme
    int scaleMode = 0;
    int scaleRoot = 0;
    int scaleType = 0;

    void start();                                   // départ avec remise au premier pas (MIDI Start)
    void continuePlaying();                         // reprise (MIDI Continue)
    void stop(std::vector<MidiMsg>& out);           // arrêt, envoie les Note Off en attente
    void panic(std::vector<MidiMsg>& out);          // Note Off + All Notes Off sur les canaux utilisés
    void tick(std::vector<MidiMsg>& out);           // à appeler à chaque horloge MIDI

    bool isPlaying() const { return playing_; }
    int currentStep(int row) const;                 // dernier pas joué (1..16), 0 si aucun
    void seedRandom(uint32_t seed);

private:
    struct RowState {
        int t = 0;            // compteur de pas joués depuis le départ
        int phase = 0;        // horloges écoulées dans le pas courant
        int lastStep = 0;     // 1..16
        bool running = true;  // faux après la fin d'un One-Shot
        bool noteActive = false;
        int note = 0;
        int noteChannel = 0;
        int noteLeft = 0;
        int currentPot = 0;   // pour les rangées Velocity
        bool hasCurrent = false;
        uint32_t rng = 1;
    };

    std::array<RowState, kRows> st_;
    bool playing_ = false;
    uint32_t seed_ = 12345;

    void resetStates();
    void fireRow(int r, std::vector<MidiMsg>& out);
    int position(int r);  // 1..16 ou -1 si la rangée a fini
    void noteOffIfActive(int r, std::vector<MidiMsg>& out);
    static uint32_t nextRandom(uint32_t& s);
};

}  // namespace maq
