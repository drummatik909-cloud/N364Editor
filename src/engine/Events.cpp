#include "Events.h"
#include <algorithm>

namespace maq {

namespace {
EventConfig make(EventKind kind, int octaves = 1, int cc = 0) {
    EventConfig e;
    e.kind = kind;
    e.octaves = octaves;
    e.ccNumber = cc;
    return e;
}
}  // namespace

EventConfig eventForIndex(int index) {
    index = std::max(0, std::min(kEventCount - 1, index));
    if (index == 0) return make(EventKind::Off);
    if (index == 1) return make(EventKind::FreeCC);
    if (index <= 6) return make(EventKind::NoteAbs, index - 1);          // 2..6  -> 1..5 octaves
    if (index <= 11) return make(EventKind::NoteRel, index - 6);         // 7..11 -> 1..5 octaves
    if (index == 12) return make(EventKind::Velocity);
    if (index <= 44) return make(EventKind::FixedCC, 1, index - 13);     // 13..44 -> index 0..31
    if (index == 45) return make(EventKind::PitchBend);
    if (index == 46) return make(EventKind::PitchBendPos);
    if (index == 47) return make(EventKind::PitchBendNeg);
    if (index == 48) return make(EventKind::AfterTouch);
    if (index == 49) return make(EventKind::ProgramChange);
    if (index <= 54) return make(EventKind::NoteAbsPause, index - 49);   // 50..54 -> 1..5 octaves
    return make(EventKind::NoteRelPause, index - 54);                    // 55..59 -> 1..5 octaves
}

std::string eventLabel(int index) {
    index = std::max(0, std::min(kEventCount - 1, index));
    if (index == 0) return "---";
    if (index == 1) return "CC";
    if (index <= 6) return "nA" + std::to_string(index - 1);
    if (index <= 11) return "nr" + std::to_string(index - 6);
    if (index == 12) return "dYn";
    if (index <= 44) return "C" + std::to_string(fixedCcNumber(index - 13));
    if (index == 45) return "PIt";
    if (index == 46) return "PIP";
    if (index == 47) return "PI-";
    if (index == 48) return "At";
    if (index == 49) return "Prg";
    if (index <= 54) return "PA" + std::to_string(index - 49);
    return "Pr" + std::to_string(index - 54);
}

const char* modeLabel(int mode) {
    static const char* const labels[kModeCount] = {"FOr", "bAC", "PE1", "rnd", "PE2", "OnS", "Onr", "OnE"};
    mode = std::max(0, std::min(kModeCount - 1, mode));
    return labels[mode];
}

int eventIndexFor(const EventConfig& e) {
    const bool usesOctaves = e.kind == EventKind::NoteAbs || e.kind == EventKind::NoteRel ||
                             e.kind == EventKind::NoteAbsPause || e.kind == EventKind::NoteRelPause;
    for (int i = 0; i < kEventCount; ++i) {
        const EventConfig a = eventForIndex(i);
        if (a.kind != e.kind) continue;
        if (usesOctaves && a.octaves != e.octaves) continue;
        if (a.kind == EventKind::FixedCC && a.ccNumber != e.ccNumber) continue;
        return i;
    }
    return 0;
}

bool isAbsoluteNoteEvent(const EventConfig& e) {
    return e.kind == EventKind::NoteAbs || e.kind == EventKind::NoteAbsPause;
}

bool isPauseValue(const EventConfig& e, int pot) {
    return (e.kind == EventKind::NoteAbsPause || e.kind == EventKind::NoteRelPause) && pot >= 127;
}

int displayValue(const EventConfig& e, int pot) {
    switch (e.kind) {
        case EventKind::NoteAbs:
        case EventKind::NoteAbsPause: return noteFromPot(e.octaves, pot);
        case EventKind::NoteRel:
        case EventKind::NoteRelPause: return noteOffsetFromPot(e.octaves, pot);
        case EventKind::PitchBend:
        case EventKind::PitchBendPos:
        case EventKind::PitchBendNeg: return pitchBendFromPot(e.kind, pot);
        default: return std::max(0, std::min(127, pot));
    }
}

std::string noteName(int midiNote, bool french) {
    static const char* const en[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* const fr[12] = {"Do", "Do#", "R\xC3\xA9", "R\xC3\xA9#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
    const int n = std::max(0, std::min(127, midiNote));
    return std::string(french ? fr[n % 12] : en[n % 12]) + std::to_string(n / 12 - 2);
}

std::string valueText(const EventConfig& e, int pot, bool noteMode, bool french) {
    if (e.kind == EventKind::Off) return "---";
    if (isPauseValue(e, pot)) return "---";
    const int v = displayValue(e, pot);
    if (noteMode && isAbsoluteNoteEvent(e)) return noteName(v, french);
    if (noteMode && (e.kind == EventKind::NoteRel || e.kind == EventKind::NoteRelPause))
        return "+" + std::to_string(v);
    return std::to_string(v);
}

}  // namespace maq
