#include "Scale.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "Events.h"

namespace maq {

namespace {

constexpr unsigned bit(int n) { return 1u << n; }

struct ScaleInfo {
    const char* fr;
    const char* en;
    unsigned mask;
    int category;
};

const ScaleInfo kScales[kScaleCount] = {
    {"Chromatique", "Chromatic", 0xFFFu, 0},  // 0
    {"Majeure / Ionien", "Major / Ionian", 0xAB5u, 1},  // 1
    {"Mineure naturelle / Éolien", "Natural minor / Aeolian", 0x5ADu, 1},  // 2
    {"Mineure harmonique / Mohammedan", "Harmonic minor / Mohammedan", 0x9ADu, 2},  // 3
    {"Mineure mélodique / Jazz minor / Hawaiian", "Melodic minor / Jazz minor / Hawaiian", 0xAADu, 3},  // 4
    {"Dorien / Kafi raga", "Dorian / Kafi raga", 0x6ADu, 1},  // 5
    {"Phrygien", "Phrygian", 0x5ABu, 1},  // 6
    {"Lydien", "Lydian", 0xAD5u, 1},  // 7
    {"Mixolydien", "Mixolydian", 0x6B5u, 1},  // 8
    {"Locrien", "Locrian", 0x56Bu, 1},  // 9
    {"Pentatonique majeure / Ritusen / Chinese Major", "Major pentatonic / Ritusen / Chinese Major", 0x295u, 5},  // 10
    {"Pentatonique mineure / Man Gong / Yo", "Minor pentatonic / Man Gong / Yo", 0x4A9u, 5},  // 11
    {"Blues / Blues mineur", "Blues / Blues minor", 0x4E9u, 5},  // 12
    {"Ton entier", "Whole tone", 0x555u, 6},  // 13
    {"Diminuée (demi-ton / ton)", "Diminished (half-whole)", 0x6DBu, 6},  // 14
    {"Locrien 6 naturel", "Locrian nat6", 0x66Bu, 2},  // 15
    {"Ionien augmenté (Ionien #5)", "Ionian Augmented (Ionian #5)", 0xB35u, 2},  // 16
    {"Dorien #4 / Mineur roumain / Zilofi", "Dorian #4 / Romanian Minor / Zilofi", 0x6CDu, 2},  // 17
    {"Phrygien dominant / Spanish Gypsy / Flamenco", "Phrygian Dominant / Spanish Gypsy / Flamenco", 0x5B3u, 2},  // 18
    {"Lydien #2", "Lydian #2", 0xAD9u, 2},  // 19
    {"Ultralocrien / Altéré bb7", "Ultralocrian / Altered bb7", 0x35Bu, 2},  // 20
    {"Dorien b2 / Phrygien 6 naturel / Javanese", "Dorian b2 / Phrygian nat6 / Javanese", 0x6ABu, 3},  // 21
    {"Lydien augmenté", "Lydian Augmented", 0xB55u, 3},  // 22
    {"Lydien dominant / Overtone / Podolian", "Lydian Dominant / Overtone / Podolian", 0x6D5u, 3},  // 23
    {"Mixolydien b6 / Hindu", "Mixolydian b6 / Hindu", 0x5B5u, 3},  // 24
    {"Locrien 2 naturel / Semi-diminué", "Locrian nat2 / Half-Diminished", 0x56Du, 3},  // 25
    {"Altérée / Super Locrien", "Altered / Super Locrian", 0x55Bu, 3},  // 26
    {"Majeure harmonique", "Harmonic Major", 0x9B5u, 4},  // 27
    {"Dorien b5", "Dorian b5", 0x66Du, 4},  // 28
    {"Phrygien b4", "Phrygian b4", 0x59Bu, 4},  // 29
    {"Lydien mineur / Lydien b3", "Lydian Minor / Lydian b3", 0xACDu, 4},  // 30
    {"Mixolydien b2", "Mixolydian b2", 0x6B3u, 4},  // 31
    {"Lydien augmenté #2", "Lydian Augmented #2", 0xB59u, 4},  // 32
    {"Locrien b7", "Locrian b7", 0x36Bu, 4},  // 33
    {"Pentatonique égyptienne / Egyptian", "Pentatonic Egyptian / Egyptian", 0x4A5u, 5},  // 34
    {"Pentatonique mineure 6te", "Pentatonic Minor 6th", 0x2A9u, 5},  // 35
    {"Blues majeur", "Blues Major", 0x29Du, 5},  // 36
    {"Diminuée (ton / demi-ton)", "Diminished (whole-half)", 0xB6Du, 6},  // 37
    {"Bebop majeur", "Bebop Major", 0xBB5u, 6},  // 38
    {"Bebop mineur", "Bebop Minor", 0xB5Du, 6},  // 39
    {"Bebop dominant / Dominant Bebop", "Bebop Dominant / Dominant Bebop", 0xEB5u, 6},  // 40
    {"Bebop dorien", "Bebop Dorian", 0x6EDu, 6},  // 41
    {"Gamme augmentée", "Augmented Scale", 0x999u, 6},  // 42
    {"Double harmonique majeure / Byzantine / Bhairav", "Double Harmonic Major / Byzantine / Bhairav", 0x9B3u, 7},  // 43
    {"Double harmonique mineure / Gypsy", "Double Harmonic Minor / Gypsy", 0x9CDu, 7},  // 44
    {"Orientale", "Oriental", 0x673u, 7},  // 45
    {"Iwato (Japon)", "Iwato (Japan)", 0x463u, 7},  // 46
    {"In Sen (Japon)", "In Sen (Japan)", 0x4A3u, 7},  // 47
    {"Hirajoshi (Japon)", "Hirajoshi (Japan)", 0x18Du, 7},  // 48
    {"Hongroise majeure", "Hungarian Major", 0x6D9u, 7},  // 49
    {"Napolitaine majeure", "Neapolitan Major", 0xAABu, 7},  // 50
    {"Napolitaine mineure", "Neapolitan Minor", 0x9ABu, 7},  // 51
    {"Énigmatique / Verdi", "Enigmatic / Verdi", 0xD53u, 7},  // 52
    {"Prométhée (Scriabine)", "Prometheus (Scriabin)", 0x655u, 7},  // 53
    {"Prométhée napolitaine", "Prometheus Neapolitan", 0x353u, 7},  // 54
    {"Ton entier avec sensible", "Leading Whole Tone", 0xD55u, 7},  // 55
    {"Arabe", "Arabian", 0x575u, 7},  // 56
    {"Dorien chromatique", "Chromatic Dorian", 0x6B9u, 7},  // 57
    {"Phrygien mauresque", "Moorish Phrygian", 0x4D7u, 7},  // 58
    {"Symétrique à six sons", "Six Tone Symmetrical", 0x333u, 7},  // 59
    {"Pentatonique dominante (corrigée)", "Pentatonic Dominant (corrected)", 0x495u, 5},  // 60
    {"Persane (corrigée)", "Persian (corrected)", 0x973u, 7},  // 61
    {"Kumoi (Japon, corrigée)", "Kumoi (Japan, corrected)", 0x28Du, 7},  // 62
    {"Espagnole à 8 notes (corrigée)", "Spanish 8-Note (corrected)", 0x57Bu, 7},  // 63
    {"Balinaise (corrigée)", "Balinese (corrected)", 0x18Bu, 7},  // 64
    {"Raga Todi (approx.)", "Todi raga (approx.)", 0x9CBu, 7},  // 65
    {"Raga Marva (approx.)", "Marva raga (approx.)", 0xAD3u, 7},  // 66
    {"Raga Purvi (approx.)", "Purvi raga (approx.)", 0x9D3u, 7},  // 67
    {"Algérienne (approx.)", "Algerian (approx.)", 0x9EDu, 7},  // 68
    {"In (approx.)", "In (approx.)", 0x1A3u, 7},  // 69
};

const char* const kCategoryFr[kScaleCategoryCount] = {"Chromatique", "Diatoniques & modes", "Modes du mineur harmonique", "Modes du mineur mélodique", "Modes du majeur harmonique", "Pentatoniques & blues", "Symétriques & bebop", "Exotiques & monde"};
const char* const kCategoryEn[kScaleCategoryCount] = {"Chromatic", "Diatonic & modes", "Harmonic minor modes", "Melodic minor modes", "Harmonic major modes", "Pentatonic & blues", "Symmetric & bebop", "Exotic & world"};

int clampType(int type) { return std::max(0, std::min(kScaleCount - 1, type)); }
int mod12(int v) { return ((v % 12) + 12) % 12; }

bool isPauseKind(EventKind k) { return k == EventKind::NoteAbsPause || k == EventKind::NoteRelPause; }

// Ramène une note dans [lo, hi] en conservant sa classe de hauteur si possible
int foldNote(int note, int lo, int hi) {
    if (hi - lo >= 11) {
        while (note < lo) note += 12;
        while (note > hi) note -= 12;
    }
    return std::max(lo, std::min(hi, note));
}

}  // namespace

unsigned scaleMask(int type) { return kScales[clampType(type)].mask; }

bool inScale(int midiNote, int root, int type) {
    return (scaleMask(type) & bit(mod12(midiNote - root))) != 0;
}

int quantizeNote(int midiNote, int root, int type) {
    const int n = std::max(0, std::min(127, midiNote));
    for (int d = 0; d < 12; ++d) {
        if (n - d >= 0 && inScale(n - d, root, type)) return n - d;
        if (n + d <= 127 && inScale(n + d, root, type)) return n + d;
    }
    return n;
}

int quantizeNoteInRange(int midiNote, int lo, int hi, int root, int type) {
    const int n = std::max(lo, std::min(hi, midiNote));
    for (int d = 0; d < 12; ++d) {
        if (n - d >= lo && inScale(n - d, root, type)) return n - d;
        if (n + d <= hi && inScale(n + d, root, type)) return n + d;
    }
    return n;
}

std::string scaleName(int type, Lang lang) {
    const ScaleInfo& s = kScales[clampType(type)];
    return lang == Lang::French ? s.fr : s.en;
}

int scaleCategory(int type) { return kScales[clampType(type)].category; }

std::string scaleCategoryName(int category, Lang lang) {
    const int c = std::max(0, std::min(kScaleCategoryCount - 1, category));
    return lang == Lang::French ? kCategoryFr[c] : kCategoryEn[c];
}

std::string pitchClassName(int pitchClass, bool french) {
    static const char* const en[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* const fr[12] = {"Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
    const int pc = mod12(pitchClass);
    return french ? fr[pc] : en[pc];
}

int potForNote(int octaves, int note, bool avoidPause) {
    octaves = std::max(1, std::min(5, octaves));
    if (note < 36 || note > 36 + 12 * octaves) return -1;
    const int maxPot = avoidPause ? 126 : 127;
    const int centre = static_cast<int>(std::lround((note - 36) * 127.0 / (12.0 * octaves)));
    for (int d = 0; d <= 3; ++d) {
        for (int sign : {-1, +1}) {
            const int p = centre + sign * d;
            if (p >= 0 && p <= maxPot && noteFromPot(octaves, p) == note) return p;
        }
    }
    for (int p = 0; p <= maxPot; ++p)
        if (noteFromPot(octaves, p) == note) return p;
    return -1;
}

int potForOffset(int octaves, int offset, bool avoidPause) {
    octaves = std::max(1, std::min(5, octaves));
    if (offset < 0 || offset > 12 * octaves) return -1;
    const int maxPot = avoidPause ? 126 : 127;
    const int centre = static_cast<int>(std::lround(offset * 127.0 / (12.0 * octaves)));
    for (int d = 0; d <= 3; ++d) {
        for (int sign : {-1, +1}) {
            const int p = centre + sign * d;
            if (p >= 0 && p <= maxPot && noteOffsetFromPot(octaves, p) == offset) return p;
        }
    }
    for (int p = 0; p <= maxPot; ++p)
        if (noteOffsetFromPot(octaves, p) == offset) return p;
    return -1;
}

int snapPot(const EventConfig& e, int pot, int root, int type) {
    if (!isAbsoluteNoteEvent(e)) return pot;
    const int lo = 36, hi = 36 + 12 * e.octaves;
    const int note = noteFromPot(e.octaves, pot);
    if (isPauseValue(e, pot)) return pot;  // la pause reste une pause
    const int q = quantizeNoteInRange(note, lo, hi, root, type);
    if (q == note) return pot;
    const int p = potForNote(e.octaves, q, isPauseKind(e.kind));
    return p >= 0 ? p : pot;
}

int learnPot(const EventConfig& e, int note, int& reference, bool lockToScale, int root, int type) {
    const bool pause = isPauseKind(e.kind);
    if (isAbsoluteNoteEvent(e)) {
        const int lo = 36, hi = 36 + 12 * e.octaves;
        int n = foldNote(note, lo, hi);
        if (lockToScale) n = quantizeNoteInRange(n, lo, hi, root, type);
        return potForNote(e.octaves, n, pause);
    }
    if (e.kind == EventKind::NoteRel || e.kind == EventKind::NoteRelPause) {
        if (reference < 0) reference = note;  // la première note jouée sert de référence
        const int hi = 12 * e.octaves;
        const int offset = foldNote(note - reference, 0, hi);
        return potForOffset(e.octaves, offset, pause);
    }
    return -1;
}

}  // namespace maq
