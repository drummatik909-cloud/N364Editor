#include "Strings.h"
#include <cstddef>
#include "engine/Events.h"

namespace maq {

namespace {

struct Pair {
    const char* fr;
    const char* en;
};

// Dans l'ordre de l'énumération Str
const Pair kStrings[] = {
    {"Notes", "Notes"},                                                  // Notes
    {"Enregistrer", "Store"},                                            // Store
    {"Rappeler", "Recall"},                                              // Recall
    {"Importer...", "Import..."},                                        // Import
    {"Exporter...", "Export..."},                                        // Export
    {"Dossier import", "Import folder"},                                 // ImportDir
    {"Dossier export", "Export folder"},                                 // ExportDir
    {"Garder le même dossier", "Keep the same folder"},                  // KeepSameFolder
    {"Manuel", "Manual"},                                                // Manual
    {"Panic", "Panic"},                                                  // Panic
    {"nom du preset", "preset name"},                                    // PresetName
    {"(slot vide)", "(empty slot)"},                                     // SlotEmpty
    {"Choisis un slot de 1 à 99", "Choose a slot from 1 to 99"},         // ChooseSlot
    {"Preset {n} enregistré", "Preset {n} stored"},                      // PresetStored
    {"Preset {n} rappelé", "Preset {n} recalled"},                       // PresetRecalled
    {"Preset {n} : slot vide", "Preset {n}: empty slot"},                // PresetEmpty
    {"Banque exportée : {file} ({n} presets)", "Bank exported: {file} ({n} presets)"},   // BankExported
    {"Banque importée : {file} ({n} presets)", "Bank imported: {file} ({n} presets)"},   // BankImported
    {"Échec de l'export", "Export failed"},                              // ExportFailed
    {"Échec de l'import : fichier non valide", "Import failed: invalid file"},           // ImportFailed
    {"Dossier : {file}", "Folder: {file}"},                              // FolderSet
    {"Un seul dossier pour tous les usages", "One folder for every use"},                // OneFolder
    {"Un dossier par usage", "One folder per use"},                      // PerUseFolder
    {"Exporter la banque de presets", "Export the preset bank"},         // ExportTitle
    {"Importer une banque de presets", "Import a preset bank"},          // ImportTitle
    {"Dossier d'import des presets", "Preset import folder"},            // ImportDirTitle
    {"Dossier d'export des presets", "Preset export folder"},            // ExportDirTitle
    {"Ouvrir le manuel en ligne (Doepfer)", "Open the manual online (Doepfer)"},         // ManualOnline
    {"Ouvrir ma copie locale (PDF)", "Open my local copy (PDF)"},        // ManualLocal
    {"Choisir ma copie locale...", "Choose my local copy..."},           // ManualChoose
    {"Choisir le manuel du MAQ16/3 (PDF)", "Choose the MAQ16/3 manual (PDF)"},          // ManualChooseTitle
    {"Manuel local enregistré", "Local manual saved"},                   // ManualSaved
    {"Fichier introuvable", "File not found"},                           // ManualMissing
    {"Panic : toutes les notes sont coupées", "Panic: all notes are off"},               // PanicDone
    {"Apprentissage", "Learn"},                                          // Learn
    {"Pause", "Rest"},                                                   // Pause
    {"Saut", "Skip"},                                                    // Skip
    {"Gamme", "Scale"},                                                  // ScaleLabel
    {"Aucune", "Off"},                                                   // ScaleOff
    {"Verrouillage", "Lock"},                                            // ScaleLock
    {"Quantif. en sortie", "Output quantize"},                           // ScaleOutput
    {"Apprentissage : clique un potard, puis joue des notes", "Learn: click a knob, then play notes"},  // LearnOn
    {"Apprentissage terminé", "Learning finished"},                      // LearnOff
};
static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == static_cast<std::size_t>(Str::Count),
              "la table de textes doit suivre l'énumération Str");

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string pick(Lang l, const std::string& fr, const std::string& en) { return l == Lang::French ? fr : en; }

std::string eventHelp(int index, Lang l) {
    const EventConfig e = eventForIndex(index);
    const bool f = (l == Lang::French);
    const std::string label = eventLabel(index);
    const std::string k = std::to_string(e.octaves);
    const std::string top = std::to_string(36 + 12 * e.octaves);
    const std::string hi = std::to_string(12 * e.octaves);

    switch (e.kind) {
        case EventKind::Off:
            return pick(l, label + " : rangée désactivée, elle n'envoie rien.",
                           label + ": row switched off, it sends nothing.");
        case EventKind::FreeCC:
            return pick(l, "Contrôleur libre : le potard envoie un Control Change (0 à 127). Appuie sur PRESET pour régler le numéro du contrôleur avec DATA.",
                           "Free controller: the knob sends a Control Change (0 to 127). Press PRESET to set the controller number with DATA.");
        case EventKind::NoteAbs:
            return pick(l, label + " – Note absolue, " + k + " octave(s) : le potard parcourt les notes de " + noteName(36, f) + " à " +
                           noteName(36 + 12 * e.octaves, f) + " (MIDI 36 à " + top + "). Elles ne dépendent pas des notes reçues.",
                           label + " – Absolute note, " + k + " octave(s): the knob covers " + noteName(36, f) + " to " +
                           noteName(36 + 12 * e.octaves, f) + " (MIDI 36 to " + top + "). They do not depend on incoming notes.");
        case EventKind::NoteRel:
            return pick(l, label + " – Note relative, " + k + " octave(s) : le potard donne un décalage de 0 à " + hi +
                           " demi-tons, ajouté aux notes reçues sur le canal de la rangée (transposition).",
                           label + " – Relative note, " + k + " octave(s): the knob gives an offset of 0 to " + hi +
                           " semitones, added to the notes received on the row's channel (transposition).");
        case EventKind::NoteAbsPause:
            return pick(l, label + " – Note absolue avec pause, " + k + " octave(s) : comme nA, mais le potard à fond à droite coupe le pas (pause).",
                           label + " – Absolute note with pause, " + k + " octave(s): like nA, but the knob fully right mutes the step (pause).");
        case EventKind::NoteRelPause:
            return pick(l, label + " – Note relative avec pause, " + k + " octave(s) : comme nr, mais le potard à fond à droite coupe le pas (pause).",
                           label + " – Relative note with pause, " + k + " octave(s): like nr, but the knob fully right mutes the step (pause).");
        case EventKind::Velocity:
            return pick(l, "dYn – Vélocité : cette rangée fixe la force des notes des autres rangées. Potard à fond à gauche = pas muet (pause).",
                           "dYn – Velocity: this row sets the strength of the notes of the other rows. Knob fully left = muted step (pause).");
        case EventKind::FixedCC: {
            const std::string n = std::to_string(fixedCcNumber(e.ccNumber));
            return pick(l, label + " – Contrôleur fixe n°" + n + " : le potard envoie la valeur 0 à 127 du Control Change " + n + ".",
                           label + " – Fixed controller no. " + n + ": the knob sends the value 0 to 127 of Control Change " + n + ".");
        }
        case EventKind::PitchBend:
            return pick(l, "PIt – Pitch Bend complet (0 à 127) : un plateau au centre aide à retrouver la hauteur neutre.",
                           "PIt – Full Pitch Bend (0 to 127): a plateau around the centre makes the neutral pitch easy to find.");
        case EventKind::PitchBendPos:
            return pick(l, "PIP – Pitch Bend positif seulement (64 à 127).", "PIP – Positive Pitch Bend only (64 to 127).");
        case EventKind::PitchBendNeg:
            return pick(l, "PI- – Pitch Bend négatif seulement (0 à 64).", "PI- – Negative Pitch Bend only (0 to 64).");
        case EventKind::AfterTouch:
            return pick(l, "At – Aftertouch monophonique : pression de 0 à 127 sur le canal de la rangée.",
                           "At – Monophonic aftertouch: pressure from 0 to 127 on the row's channel.");
        case EventKind::ProgramChange:
            return pick(l, "Prg – Program Change : change de son à chaque pas (le synthétiseur doit le permettre sans retard).",
                           "Prg – Program Change: switches sound at every step (the synthesizer must allow it without delay).");
    }
    return std::string();
}

std::string modeHelp(int mode, Lang l) {
    switch (mode) {
        case 0: return pick(l, "FOr – Avant : de First à Last, puis la rangée recommence.",
                               "FOr – Forward: from First to Last, then the row starts again.");
        case 1: return pick(l, "bAC – Arrière : de Last à First, puis la rangée recommence.",
                               "bAC – Backward: from Last to First, then the row starts again.");
        case 2: return pick(l, "PE1 – Pendule 1 : va-et-vient ; le premier et le dernier pas sont joués deux fois.",
                               "PE1 – Pendulum 1: back and forth; the first and the last step are played twice.");
        case 3: return pick(l, "rnd – Aléatoire : un pas au hasard entre First et Last à chaque pas.",
                               "rnd – Random: a random step between First and Last at every step.");
        case 4: return pick(l, "PE2 – Pendule 2 : va-et-vient ; le premier et le dernier pas sont joués une seule fois (longueur effective 15 pour 16 pas).",
                               "PE2 – Pendulum 2: back and forth; the first and the last step are played only once (effective length 15 for 16 steps).");
        case 5: return pick(l, "OnS – Un passage : la rangée s'arrête au dernier pas.",
                               "OnS – One shot: the row stops at the last step.");
        case 6: return pick(l, "Onr – Un passage avec relance : à la fin, elle doit lancer la rangée suivante (ce chaînage n'est pas encore disponible ici : elle s'arrête comme OnS).",
                               "Onr – One shot with retrigger: at the end it should start the next row (this chaining is not available here yet: it stops like OnS).");
        default: return pick(l, "OnE – Un seul pas : seul le premier pas est joué.",
                                "OnE – One event: only the first step is played.");
    }
}

}  // namespace

std::string tr(Str s, Lang lang) {
    const auto i = static_cast<std::size_t>(s);
    if (i >= static_cast<std::size_t>(Str::Count)) return std::string();
    return lang == Lang::French ? kStrings[i].fr : kStrings[i].en;
}

std::string fmt(Str s, Lang lang, const std::string& file, int n) {
    return replaceAll(replaceAll(tr(s, lang), "{file}", file), "{n}", std::to_string(n));
}

Lang langFromCode(const std::string& code) { return code == "en" ? Lang::English : Lang::French; }
const char* langCode(Lang lang) { return lang == Lang::English ? "en" : "fr"; }

std::string helpText(const HelpContext& c, Lang l) {
    if (c.learn && c.menu == HelpMenu::None)
        return pick(l, "Apprentissage : clique sur un potard pour placer le curseur (LED vive), puis joue des notes au clavier. Chaque note règle le pas et le curseur avance. Pause et Saut passent un pas.",
                       "Learn: click a knob to place the cursor (bright LED), then play notes on your keyboard. Each note sets the step and the cursor moves on. Rest and Skip pass over a step.");
    switch (c.menu) {
        case HelpMenu::None:
            return pick(l, "Tempo : le séquenceur suit l'horloge de Bitwig (24 impulsions par noire). Choisis un menu avec les 8 boutons ; l'encodeur DATA règle la valeur.",
                           "Tempo: the sequencer follows Bitwig's clock (24 pulses per quarter note). Pick a menu with the 8 buttons; the DATA encoder sets the value.");
        case HelpMenu::Event:
            return c.ccEdit ? eventHelp(maq::kFreeCcEventIndex, l) : eventHelp(c.eventIndex, l);
        case HelpMenu::Channel:
            return pick(l, "Canal MIDI de la rangée (1 à 16). Rappuie sur CHN pour passer à la rangée suivante. Sans effet sur une rangée de vélocité.",
                           "MIDI channel of the row (1 to 16). Press CHN again to move to the next row. No effect on a velocity row.");
        case HelpMenu::FirstLast:
            return c.second
                ? pick(l, "Dernier pas (L) de la rangée : après lui, la lecture revient au premier pas. Des longueurs différentes d'une rangée à l'autre créent des motifs qui se décalent.",
                          "Last step (L) of the row: after it, playback goes back to the first step. Different lengths from row to row create patterns that shift against each other.")
                : pick(l, "Premier pas (F) de la rangée : la lecture commence ici. Rappuie sur FIRST/LAST pour passer aux autres rangées, puis aux derniers pas (L).",
                          "First step (F) of the row: playback starts here. Press FIRST/LAST again to go through the other rows, then the last steps (L).");
        case HelpMenu::Prescale:
            return c.second
                ? pick(l, "Durée de note n : temps entre Note On et Note Off, toujours inférieur ou égal à g. 1 = égal à g.",
                          "Note length n: time between Note On and Note Off, always less than or equal to g. 1 = equal to g.")
                : pick(l, "Gate g : nombre d'impulsions d'horloge entre deux pas (24 = noire, 12 = croche, 6 = double croche). Rappuie sur PRESCALE pour changer de rangée, puis régler n.",
                          "Gate g: number of clock pulses between two steps (24 = quarter note, 12 = eighth, 6 = sixteenth). Press PRESCALE again to change row, then set n.");
        case HelpMenu::Mode:
            return modeHelp(c.modeIndex, l);
        case HelpMenu::SingleStep:
            return c.singleRow
                ? pick(l, "Single Step, une rangée : DATA choisit le pas et l'afficheur montre sa valeur. START/STOP coupe ou réactive le pas (LED éteinte = muet). Rappuie pour la rangée suivante.",
                          "Single Step, one row: DATA picks the step and the display shows its value. START/STOP mutes or unmutes the step (LED off = muted). Press again for the next row.")
                : pick(l, "Single Step : DATA choisit le pas (1 à 16), sélectionné dans les 3 rangées. Rappuie sur SINGLE STEP pour agir sur une seule rangée.",
                          "Single Step: DATA picks the step (1 to 16), selected in all 3 rows. Press SINGLE STEP again to work on a single row.");
        case HelpMenu::Preset:
            return c.store
                ? pick(l, "Enregistrement : DATA choisit le slot (1 à 99), START/STOP (Enter) écrase ce slot avec les réglages actuels. Un autre bouton de menu annule.",
                          "Store: DATA picks the slot (1 to 99), START/STOP (Enter) overwrites that slot with the current settings. Another menu button cancels.")
                : pick(l, "Presets : DATA choisit le slot (1 à 99, un point = déjà enregistré), START/STOP (Enter) le rappelle. Rappuie sur PRESET pour passer en enregistrement.",
                          "Presets: DATA picks the slot (1 to 99, a dot = already stored), START/STOP (Enter) recalls it. Press PRESET again to switch to storing.");
    }
    return std::string();
}

}  // namespace maq
