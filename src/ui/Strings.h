#pragma once
// Textes de l'interface en français et en anglais, et aide contextuelle d'après le manuel du MAQ16/3
// (reformulée, ce n'est pas une traduction officielle).
#include <string>

namespace maq {

enum class Lang { French = 0, English = 1 };

enum class Str {
    Notes, Store, Recall, Import, Export, ImportDir, ExportDir, KeepSameFolder, Manual, Panic,
    PresetName, SlotEmpty, ChooseSlot, PresetStored, PresetRecalled, PresetEmpty,
    BankExported, BankImported, ExportFailed, ImportFailed,
    FolderSet, OneFolder, PerUseFolder,
    ExportTitle, ImportTitle, ImportDirTitle, ExportDirTitle,
    ManualOnline, ManualLocal, ManualChoose, ManualChooseTitle, ManualSaved, ManualMissing,
    PanicDone,
    Learn, Pause, Skip, ScaleLabel, ScaleOff, ScaleLock, ScaleOutput, LearnOn, LearnOff,
    Count
};

std::string tr(Str s, Lang lang);
// Remplace {n} et {file} dans le texte
std::string fmt(Str s, Lang lang, const std::string& file = "", int n = 0);

Lang langFromCode(const std::string& code);  // "fr" ou "en" (français par défaut)
const char* langCode(Lang lang);

// ---- Aide contextuelle ----
enum class HelpMenu { None, Event, Channel, FirstLast, Prescale, Mode, SingleStep, Preset };

struct HelpContext {
    HelpMenu menu = HelpMenu::None;
    int eventIndex = 0;     // Event de la rangée choisie (voir Events.h)
    int modeIndex = 0;      // mode de la rangée choisie
    bool second = false;    // FIRST/LAST : dernier pas ; PRESCALE : durée de note n
    bool ccEdit = false;    // EVENT : réglage du numéro du contrôleur libre
    bool store = false;     // PRESET : mode enregistrement
    bool singleRow = false; // SINGLE STEP : une rangée à la fois
    bool learn = false;     // mode apprentissage actif (aucun menu choisi)
};

std::string helpText(const HelpContext& ctx, Lang lang);

}  // namespace maq
