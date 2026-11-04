// Tests du moteur (sans dépendance externe).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include "engine/Unit.h"
#include "engine/Events.h"
#include "engine/Scale.h"
#include "presets/Bank.h"
#include "presets/FolderSettings.h"
#include "ui/Preferences.h"
#include "ui/Strings.h"

using namespace maq;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) { ++g_fail; std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static fs::path tempDir(const char* name);

struct Ev { int tick; MidiMsg m; };

static Unit makeUnit() {
    Unit u;
    u.rows[1].event.kind = EventKind::Off;
    u.rows[2].event.kind = EventKind::Off;
    u.rows[0].channel = 1;
    return u;
}

static std::vector<Ev> run(Unit& u, int ticks, bool doStart = true) {
    std::vector<Ev> all;
    if (doStart) u.start();
    for (int t = 0; t < ticks; ++t) {
        std::vector<MidiMsg> out;
        u.tick(out);
        for (auto& m : out) all.push_back({t, m});
    }
    return all;
}

// Avec g = 1 chaque horloge joue un pas : on relève le pas courant à chaque horloge.
static std::vector<int> steps(Unit& u, int ticks) {
    std::vector<int> s;
    u.start();
    for (int t = 0; t < ticks; ++t) {
        std::vector<MidiMsg> out;
        u.tick(out);
        s.push_back(u.currentStep(0));
    }
    return s;
}

static std::vector<int> noteOns(const std::vector<Ev>& evs) {
    std::vector<int> n;
    for (auto& e : evs) if (e.m.type == MidiMsg::Type::NoteOn) n.push_back(e.m.data1);
    return n;
}

static void testValueMapping() {
    CHECK(noteFromPot(1, 0) == 36);
    CHECK(noteFromPot(1, 127) == 48);
    CHECK(noteFromPot(2, 127) == 60);
    CHECK(noteFromPot(5, 127) == 96);
    CHECK(fixedCcNumber(0) == 0);
    CHECK(fixedCcNumber(19) == 19);
    CHECK(fixedCcNumber(20) == 64);
    CHECK(fixedCcNumber(30) == 74);
    CHECK(fixedCcNumber(31) == 84);
    CHECK(pitchBendFromPot(EventKind::PitchBend, 64) == 64);
    CHECK(pitchBendFromPot(EventKind::PitchBend, 66) == 64);  // plateau
    CHECK(pitchBendFromPot(EventKind::PitchBend, 0) == 0);
    CHECK(pitchBendFromPot(EventKind::PitchBend, 127) == 127);
    CHECK(pitchBendFromPot(EventKind::PitchBendPos, 0) == 64);
    CHECK(pitchBendFromPot(EventKind::PitchBendPos, 127) == 127);
    CHECK(pitchBendFromPot(EventKind::PitchBendNeg, 0) == 0);
    CHECK(pitchBendFromPot(EventKind::PitchBendNeg, 127) == 64);
}

static void testForwardNotes() {
    Unit u = makeUnit();
    for (int i = 0; i < 16; ++i) u.rows[0].pot[i] = i * 8;
    auto evs = run(u, 16 * 6);
    auto on = noteOns(evs);
    CHECK(on.size() == 16);
    for (int i = 0; i < 16 && i < static_cast<int>(on.size()); ++i) CHECK(on[i] == noteFromPot(1, i * 8));
    // un NoteOn toutes les 6 horloges
    int k = 0;
    for (auto& e : evs) if (e.m.type == MidiMsg::Type::NoteOn) { CHECK(e.tick == k * 6); ++k; }
}

static void testNoteLength() {
    {   // n = 3, g = 6
        Unit u = makeUnit();
        u.rows[0].noteLen = 3;
        auto evs = run(u, 7);
        int offTick = -1;
        for (auto& e : evs) if (e.m.type == MidiMsg::Type::NoteOff) { offTick = e.tick; break; }
        CHECK(offTick == 3);
    }
    {   // n = 1 : égal à g, le Note Off précède le Note On suivant au même instant
        Unit u = makeUnit();
        auto evs = run(u, 7);
        int offIdx = -1, secondOnIdx = -1, ons = 0;
        for (size_t i = 0; i < evs.size(); ++i) {
            if (evs[i].m.type == MidiMsg::Type::NoteOff && offIdx < 0) offIdx = static_cast<int>(i);
            if (evs[i].m.type == MidiMsg::Type::NoteOn && ++ons == 2) secondOnIdx = static_cast<int>(i);
        }
        CHECK(offIdx >= 0 && secondOnIdx >= 0 && offIdx < secondOnIdx);
        CHECK(evs[offIdx].tick == 6 && evs[secondOnIdx].tick == 6);
    }
}

static void testModes() {
    auto make = [](PlayMode m) {
        Unit u = makeUnit();
        u.rows[0].gate = 1;
        u.rows[0].first = 1; u.rows[0].last = 4;
        u.rows[0].mode = m;
        return u;
    };
    { Unit u = make(PlayMode::Forward);   CHECK((steps(u, 8)  == std::vector<int>{1,2,3,4,1,2,3,4})); }
    { Unit u = make(PlayMode::Backward);  CHECK((steps(u, 8)  == std::vector<int>{4,3,2,1,4,3,2,1})); }
    { Unit u = make(PlayMode::Pendulum1); CHECK((steps(u, 10) == std::vector<int>{1,2,3,4,4,3,2,1,1,2})); }
    { Unit u = make(PlayMode::Pendulum2); CHECK((steps(u, 10) == std::vector<int>{1,2,3,4,3,2,1,2,3,4})); }
    { Unit u = make(PlayMode::OneEvent);  CHECK((steps(u, 3)  == std::vector<int>{1,1,1})); }
    {   // One-Shot : s'arrête au dernier pas
        Unit u = make(PlayMode::OneShotSingle);
        auto evs = run(u, 12);
        CHECK(noteOns(evs).size() == 4);
        CHECK(u.currentStep(0) == 4);
    }
    {   // Random : déterministe avec la même graine, reste dans First/Last, utilise plusieurs pas
        Unit a = make(PlayMode::Random); a.seedRandom(42);
        Unit b = make(PlayMode::Random); b.seedRandom(42);
        auto sa = steps(a, 40), sb = steps(b, 40);
        CHECK(sa == sb);
        bool inRange = true; int distinct = 0; bool seen[5] = {false};
        for (int s : sa) { if (s < 1 || s > 4) inRange = false; else if (!seen[s]) { seen[s] = true; ++distinct; } }
        CHECK(inRange);
        CHECK(distinct >= 2);
    }
    {   // First/Last
        Unit u = make(PlayMode::Forward);
        u.rows[0].first = 3; u.rows[0].last = 5;
        CHECK((steps(u, 5) == std::vector<int>{3,4,5,3,4}));
    }
}

static void testPauseVelocityMute() {
    {   // PA : potard à fond à droite = pause
        Unit u = makeUnit();
        u.rows[0].event.kind = EventKind::NoteAbsPause;
        u.rows[0].gate = 1;
        u.rows[0].last = 3;
        u.rows[0].pot[0] = 10; u.rows[0].pot[1] = 127; u.rows[0].pot[2] = 20;
        auto on = noteOns(run(u, 3));
        CHECK(on.size() == 2);
    }
    {   // Vélocité par rangée de longueur différente (3 pas) sur notes de 4 pas
        Unit u = makeUnit();
        u.rows[0].gate = 1; u.rows[0].last = 4;
        u.rows[1].event.kind = EventKind::Velocity;
        u.rows[1].gate = 1; u.rows[1].last = 3;
        u.rows[1].velocityTargets = 1;  // rangée 1
        u.rows[1].pot[0] = 100; u.rows[1].pot[1] = 64; u.rows[1].pot[2] = 0;
        auto evs = run(u, 6);
        std::vector<int> vels;
        std::vector<int> ticks;
        for (auto& e : evs) if (e.m.type == MidiMsg::Type::NoteOn) { vels.push_back(e.m.data2); ticks.push_back(e.tick); }
        CHECK((vels == std::vector<int>{100, 64, 100, 64}));
        CHECK((ticks == std::vector<int>{0, 1, 3, 4}));  // vélocité 0 = pause
    }
    {   // Vélocité par défaut = 100
        Unit u = makeUnit();
        auto evs = run(u, 1);
        CHECK(!evs.empty() && evs[0].m.data2 == 100);
    }
    {   // Mute d'un pas
        Unit u = makeUnit();
        u.rows[0].gate = 1; u.rows[0].last = 3;
        u.rows[0].mute[1] = true;
        CHECK(noteOns(run(u, 3)).size() == 2);
    }
}

static void testOtherEvents() {
    Unit u = makeUnit();
    u.rows[0].event.kind = EventKind::FixedCC; u.rows[0].event.ccNumber = 20; u.rows[0].pot[0] = 77;
    u.rows[0].channel = 5;
    auto evs = run(u, 1);
    CHECK(evs.size() == 1 && evs[0].m.type == MidiMsg::Type::ControlChange);
    CHECK(evs[0].m.channel == 4 && evs[0].m.data1 == 64 && evs[0].m.data2 == 77);

    Unit f = makeUnit();
    f.rows[0].event.kind = EventKind::FreeCC; f.rows[0].event.ccNumber = 100; f.rows[0].pot[0] = 33;
    evs = run(f, 1);
    CHECK(evs.size() == 1 && evs[0].m.data1 == 100 && evs[0].m.data2 == 33);

    Unit p = makeUnit();
    p.rows[0].event.kind = EventKind::PitchBend; p.rows[0].pot[0] = 64;
    evs = run(p, 1);
    CHECK(evs.size() == 1 && evs[0].m.type == MidiMsg::Type::PitchBend && evs[0].m.data1 == 8192);
}

static void testPanicAndStop() {
    Unit u = makeUnit();
    run(u, 1);
    std::vector<MidiMsg> out;
    u.panic(out);
    bool off = false, allOff = false;
    for (auto& m : out) {
        if (m.type == MidiMsg::Type::NoteOff) off = true;
        if (m.type == MidiMsg::Type::ControlChange && m.data1 == 123) allOff = true;
    }
    CHECK(off && allOff);

    Unit v = makeUnit();
    run(v, 1);
    out.clear();
    v.stop(out);
    CHECK(out.size() == 1 && out[0].type == MidiMsg::Type::NoteOff);
    CHECK(!v.isPlaying());
}


static void testEventTable() {
    CHECK(eventForIndex(0).kind == EventKind::Off);
    CHECK(eventForIndex(1).kind == EventKind::FreeCC);
    CHECK(eventForIndex(2).kind == EventKind::NoteAbs && eventForIndex(2).octaves == 1);
    CHECK(eventForIndex(6).kind == EventKind::NoteAbs && eventForIndex(6).octaves == 5);
    CHECK(eventForIndex(7).kind == EventKind::NoteRel && eventForIndex(7).octaves == 1);
    CHECK(eventForIndex(11).kind == EventKind::NoteRel && eventForIndex(11).octaves == 5);
    CHECK(eventForIndex(12).kind == EventKind::Velocity);
    CHECK(eventForIndex(13).kind == EventKind::FixedCC && eventForIndex(13).ccNumber == 0);
    CHECK(eventForIndex(44).kind == EventKind::FixedCC && eventForIndex(44).ccNumber == 31);
    CHECK(eventForIndex(45).kind == EventKind::PitchBend);
    CHECK(eventForIndex(47).kind == EventKind::PitchBendNeg);
    CHECK(eventForIndex(48).kind == EventKind::AfterTouch);
    CHECK(eventForIndex(49).kind == EventKind::ProgramChange);
    CHECK(eventForIndex(50).kind == EventKind::NoteAbsPause && eventForIndex(50).octaves == 1);
    CHECK(eventForIndex(54).kind == EventKind::NoteAbsPause && eventForIndex(54).octaves == 5);
    CHECK(eventForIndex(55).kind == EventKind::NoteRelPause && eventForIndex(55).octaves == 1);
    CHECK(eventForIndex(59).kind == EventKind::NoteRelPause && eventForIndex(59).octaves == 5);
    CHECK(eventLabel(0) == "---");
    CHECK(eventLabel(2) == "nA1" && eventLabel(6) == "nA5");
    CHECK(eventLabel(7) == "nr1" && eventLabel(12) == "dYn");
    CHECK(eventLabel(13) == "C0" && eventLabel(32) == "C19" && eventLabel(33) == "C64" && eventLabel(43) == "C74" && eventLabel(44) == "C84");
    CHECK(eventLabel(45) == "PIt" && eventLabel(47) == "PI-" && eventLabel(49) == "Prg");
    CHECK(eventLabel(50) == "PA1" && eventLabel(59) == "Pr5");
    for (int i = 0; i < kEventCount; ++i) CHECK(eventLabel(i).size() <= 3);
    // la table des modes suit l'ordre de l'énumération du moteur
    CHECK(static_cast<int>(PlayMode::Forward) == 0 && static_cast<int>(PlayMode::OneEvent) == kModeCount - 1);
    CHECK(std::string(modeLabel(0)) == "FOr" && std::string(modeLabel(2)) == "PE1" && std::string(modeLabel(7)) == "OnE");
}


static void testNotesAndPresetHelpers() {
    // noms de notes, convention de Bitwig (60 = 3)
    CHECK(noteName(60, false) == "C3" && noteName(36, false) == "C1" && noteName(61, false) == "C#3");
    CHECK(noteName(69, false) == "A3" && noteName(0, false) == "C-2" && noteName(127, false) == "G8");
    CHECK(noteName(60, true) == "Do3" && noteName(62, true) == "R\xC3\xA9" "3" && noteName(68, true) == "Sol#3");

    EventConfig na1 = eventForIndex(2);    // nA1
    EventConfig pa2 = eventForIndex(51);   // PA2
    EventConfig nr1 = eventForIndex(7);    // nr1
    EventConfig prA = eventForIndex(55);   // Pr1
    CHECK(valueText(na1, 0, false, false) == "36" && valueText(na1, 127, false, false) == "48");
    CHECK(valueText(na1, 0, true, false) == "C1" && valueText(na1, 127, true, false) == "C2");
    CHECK(valueText(pa2, 127, true, false) == "---" && valueText(pa2, 126, true, false) != "---");
    CHECK(valueText(nr1, 127, true, false) == "+12" && valueText(nr1, 127, false, false) == "12");
    CHECK(valueText(prA, 127, true, true) == "---");
    CHECK(valueText(eventForIndex(0), 50, true, false) == "---");
    CHECK(valueText(eventForIndex(13), 77, true, false) == "77");  // contrôleur : toujours la valeur
    CHECK(isAbsoluteNoteEvent(na1) && !isAbsoluteNoteEvent(nr1));

    // la correspondance index <-> Event est exacte pour les 60 entrées
    for (int i = 0; i < kEventCount; ++i) CHECK(eventIndexFor(eventForIndex(i)) == i);
}

static void testFolderUtf8() {
    fs::path root = tempDir("maq_test_utf8");
    fs::path d = root / "dossier";
    fs::create_directories(d);
    FolderSettings s(root);
    s.setFolderUtf8(FolderPurpose::PresetExport, d.u8string());
    CHECK(s.folderForUtf8(FolderPurpose::PresetImport) == d.u8string());  // « garder le même dossier » par défaut
    CHECK(s.folderFor(FolderPurpose::PresetExport) == d);
}


static void testStringsAndHelp() {
    for (int i = 0; i < static_cast<int>(Str::Count); ++i) {
        const std::string f = tr(static_cast<Str>(i), Lang::French), e = tr(static_cast<Str>(i), Lang::English);
        CHECK(!f.empty() && !e.empty());
    }
    CHECK(tr(Str::Store, Lang::French) == "Enregistrer" && tr(Str::Store, Lang::English) == "Store");
    CHECK(fmt(Str::BankExported, Lang::English, "ma.maqbank", 7) == "Bank exported: ma.maqbank (7 presets)");
    CHECK(fmt(Str::PresetStored, Lang::French, "", 12) == "Preset 12 enregistr\xC3\xA9");
    CHECK(langFromCode("en") == Lang::English && langFromCode("fr") == Lang::French && langFromCode("xx") == Lang::French);
    CHECK(std::string(langCode(Lang::English)) == "en");

    // l'aide existe, diffère d'une langue à l'autre et ne contient aucun marqueur resté en place
    auto good = [](const HelpContext& c) {
        const std::string f = helpText(c, Lang::French), e = helpText(c, Lang::English);
        return !f.empty() && !e.empty() && f != e && f.find('{') == std::string::npos && e.find('{') == std::string::npos;
    };
    HelpContext c;
    for (HelpMenu m : {HelpMenu::None, HelpMenu::Channel, HelpMenu::SingleStep, HelpMenu::Preset}) {
        c.menu = m;
        for (bool flag : {false, true}) { c.singleRow = flag; c.store = flag; CHECK(good(c)); }
    }
    for (HelpMenu m : {HelpMenu::FirstLast, HelpMenu::Prescale}) {
        c.menu = m;
        for (bool flag : {false, true}) { c.second = flag; CHECK(good(c)); }
    }
    c.menu = HelpMenu::Event;
    for (int i = 0; i < kEventCount; ++i) { c.eventIndex = i; CHECK(good(c)); }
    c.ccEdit = true; CHECK(good(c)); c.ccEdit = false;
    c.menu = HelpMenu::Mode;
    for (int m = 0; m < kModeCount; ++m) { c.modeIndex = m; CHECK(good(c)); }
    c.menu = HelpMenu::Event; c.eventIndex = 2;
    CHECK(helpText(c, Lang::English).find("MIDI 36 to 48") != std::string::npos);
    CHECK(helpText(c, Lang::French).find("Do1") != std::string::npos);
}

static void testPreferences() {
    fs::path root = tempDir("maq_test_prefs");
    Preferences p;
    CHECK(p.language == Lang::French && p.manualPath.empty());   // défauts
    p.language = Lang::English;
    p.manualPath = "/chemin/vers/MAQ16-3 manuel.pdf";
    fs::path file = root / "sous" / "preferences.ini";
    CHECK(p.save(file));
    Preferences q;
    CHECK(q.load(file));
    CHECK(q.language == Lang::English && q.manualPath == p.manualPath);
    CHECK(!q.load(root / "absent.ini"));
    CHECK(!Preferences::defaultFile().empty());
}


static void testScales() {
    // appartenance et quantification (Do majeur)
    CHECK(inScale(60, 0, 1) && inScale(62, 0, 1) && !inScale(61, 0, 1) && !inScale(63, 0, 1) && inScale(71, 0, 1));
    CHECK(quantizeNote(61, 0, 1) == 60);   // à égalité : la plus grave
    CHECK(quantizeNote(63, 0, 1) == 62);
    CHECK(quantizeNote(66, 0, 1) == 65);
    CHECK(quantizeNote(60, 0, 1) == 60);
    // la fondamentale décale la gamme : Ré majeur (Fa# et Do# présents)
    CHECK(inScale(66, 2, 1) && inScale(61, 2, 1) && !inScale(60, 2, 1));
    // pentatonique mineure de La : La Do Ré Mi Sol
    CHECK(inScale(69, 9, 11) && inScale(72, 9, 11) && inScale(74, 9, 11) && !inScale(71, 9, 11));
    CHECK(quantizeNote(70, 9, 11) == 69);
    // chromatique : rien ne change
    for (int n = 0; n < 128; ++n) CHECK(quantizeNote(n, 5, 0) == n);
    // dans une plage
    CHECK(quantizeNoteInRange(49, 36, 48, 0, 1) == 48);   // 49 est hors plage : on reste dans 36..48
    CHECK(quantizeNoteInRange(37, 36, 48, 0, 1) == 36);
    // noms
    CHECK(scaleName(1, Lang::English) == "Major / Ionian" && scaleName(6, Lang::French) == "Phrygien");
    CHECK(pitchClassName(1, false) == "C#" && pitchClassName(3, true) == "R\xC3\xA9#" && pitchClassName(13, false) == "C#");
    for (int t = 0; t < kScaleCount; ++t) CHECK(!scaleName(t, Lang::French).empty() && scaleMask(t) != 0);
}

static void testPotConversions() {
    // note <-> potard : exact pour toutes les plages
    for (int oct = 1; oct <= 5; ++oct) {
        for (int n = 36; n <= 36 + 12 * oct; ++n) {
            const int p = potForNote(oct, n, false);
            CHECK(p >= 0 && noteFromPot(oct, p) == n);
            const int q = potForNote(oct, n, true);
            CHECK(q >= 0 && q <= 126 && noteFromPot(oct, q) == n);
        }
        CHECK(potForNote(oct, 35, false) == -1 && potForNote(oct, 37 + 12 * oct, false) == -1);
        for (int o = 0; o <= 12 * oct; ++o) {
            const int p = potForOffset(oct, o, true);
            CHECK(p >= 0 && p <= 126 && noteOffsetFromPot(oct, p) == o);
        }
    }
    // verrouillage : un potard de note tombe sur une note de la gamme, la pause reste une pause
    EventConfig na1 = eventForIndex(2);    // nA1
    EventConfig pa1 = eventForIndex(50);   // PA1
    for (int pot = 0; pot < 127; ++pot) {
        const int s = snapPot(na1, pot, 0, 1);
        CHECK(inScale(noteFromPot(1, s), 0, 1));
        CHECK(snapPot(na1, s, 0, 1) == s);   // idempotent
    }
    CHECK(snapPot(pa1, 127, 0, 1) == 127);
    EventConfig cc = eventForIndex(13);
    CHECK(snapPot(cc, 77, 0, 1) == 77);      // pas une rangée de notes
}

static void testLearn() {
    int ref = -1;
    EventConfig na2 = eventForIndex(3);    // nA2 : 36..60
    // une note de la plage
    int p = learnPot(na2, 48, ref, false, 0, 0);
    CHECK(p >= 0 && noteFromPot(2, p) == 48);
    // note trop grave ou trop aiguë : repliée dans la plage, même classe de hauteur
    p = learnPot(na2, 24, ref, false, 0, 0);
    CHECK(noteFromPot(2, p) == 36);
    p = learnPot(na2, 79, ref, false, 0, 0);   // Sol : repli vers 55
    CHECK(noteFromPot(2, p) == 55);
    // verrouillage : une note hors gamme est ramenée dans la gamme
    p = learnPot(na2, 49, ref, true, 0, 1);    // Do# en Do majeur -> Do
    CHECK(noteFromPot(2, p) == 48);
    // PA : la note la plus haute reste jouable (pas de pause)
    EventConfig pa1 = eventForIndex(50);
    p = learnPot(pa1, 48, ref, false, 0, 0);
    CHECK(p >= 0 && p <= 126 && noteFromPot(1, p) == 48);
    // notes relatives : la première note sert de référence
    int refR = -1;
    EventConfig nr1 = eventForIndex(7);
    p = learnPot(nr1, 60, refR, false, 0, 0);
    CHECK(refR == 60 && noteOffsetFromPot(1, p) == 0);
    p = learnPot(nr1, 67, refR, false, 0, 0);
    CHECK(noteOffsetFromPot(1, p) == 7);
    p = learnPot(nr1, 55, refR, false, 0, 0);  // plus grave que la référence : repli d'une octave vers le haut
    CHECK(noteOffsetFromPot(1, p) == 7);
    // les autres Events n'apprennent pas
    int ref2 = -1;
    CHECK(learnPot(eventForIndex(13), 60, ref2, false, 0, 0) == -1);
    CHECK(learnPot(eventForIndex(0), 60, ref2, false, 0, 0) == -1);
}

static void testOutputQuantize() {
    Unit u;
    u.rows[1].event.kind = EventKind::Off;
    u.rows[2].event.kind = EventKind::Off;
    u.rows[0].gate = 1;
    u.rows[0].last = 4;
    // Do#1 (37), Ré#1 (39), Fa#1 (42) : toutes hors Do majeur
    u.rows[0].pot[0] = potForNote(1, 37, false);
    u.rows[0].pot[1] = potForNote(1, 39, false);
    u.rows[0].pot[2] = potForNote(1, 42, false);
    u.rows[0].pot[3] = potForNote(1, 36, false);
    {   // sans gamme : les notes sont jouées telles quelles
        Unit a = u;
        auto on = noteOns(run(a, 4));
        CHECK((on == std::vector<int>{37, 39, 42, 36}));
    }
    {   // quantification en sortie (Do majeur) : Do#->Do, Ré#->Ré, Fa#->Fa
        Unit a = u;
        a.scaleMode = kScaleModeOutput; a.scaleRoot = 0; a.scaleType = 1;
        auto on = noteOns(run(a, 4));
        CHECK((on == std::vector<int>{36, 38, 41, 36}));
    }
    {   // verrouillage : le moteur ne quantifie pas, c'est l'interface qui ramène les potards
        Unit a = u;
        a.scaleMode = kScaleModeLock; a.scaleRoot = 0; a.scaleType = 1;
        auto on = noteOns(run(a, 4));
        CHECK((on == std::vector<int>{37, 39, 42, 36}));
    }
}


static void testScaleCatalogue() {
    CHECK(kScaleCount == 70 && kScaleCategoryCount == 8);
    // toutes les gammes sont valides, distinctes, contiennent la fondamentale ; les index 0..14 sont inchangés
    for (int t = 0; t < kScaleCount; ++t) {
        CHECK((scaleMask(t) & 1u) != 0);
        CHECK(!scaleName(t, Lang::French).empty() && !scaleName(t, Lang::English).empty());
        CHECK(scaleCategory(t) >= 0 && scaleCategory(t) < kScaleCategoryCount);
        for (int u = t + 1; u < kScaleCount; ++u) CHECK(scaleMask(t) != scaleMask(u));
    }
    for (int c = 0; c < kScaleCategoryCount; ++c) CHECK(!scaleCategoryName(c, Lang::French).empty());
    CHECK(scaleMask(0) == 0xFFFu);
    CHECK(inScale(62, 0, 4) && inScale(63, 0, 4) && inScale(71, 0, 4) && !inScale(61, 0, 4) && !inScale(70, 0, 4));  // mineure mélodique : Do Ré Mib Fa Sol La Si
    // quelques gammes du catalogue
    auto notes = [](int type) { std::vector<int> v; for (int i = 0; i < 12; ++i) if (scaleMask(type) & (1u << i)) v.push_back(i); return v; };
    auto find = [](const char* en) {
        for (int t = 0; t < kScaleCount; ++t)
            if (scaleName(t, Lang::English).find(en) == 0) return t;
        return -1;
    };
    CHECK((notes(find("Hirajoshi")) == std::vector<int>{0, 2, 3, 7, 8}));
    CHECK((notes(find("Hungarian Major")) == std::vector<int>{0, 3, 4, 6, 7, 9, 10}));
    CHECK((notes(find("Phrygian Dominant")) == std::vector<int>{0, 1, 4, 5, 7, 8, 10}));
    CHECK((notes(find("Double Harmonic Major")) == std::vector<int>{0, 1, 4, 5, 7, 8, 11}));
    CHECK((notes(find("Bebop Dominant")) == std::vector<int>{0, 2, 4, 5, 7, 9, 10, 11}));
    CHECK((notes(find("Six Tone")) == std::vector<int>{0, 1, 4, 5, 8, 9}));
    CHECK((notes(find("Persian")) == std::vector<int>{0, 1, 4, 5, 6, 8, 11}));
    CHECK((notes(find("Kumoi")) == std::vector<int>{0, 2, 3, 7, 9}));
    CHECK((notes(find("Pentatonic Dominant")) == std::vector<int>{0, 2, 4, 7, 10}));
    CHECK((notes(find("Altered")) == std::vector<int>{0, 1, 3, 4, 6, 8, 10}));
    CHECK(find("Todi") >= 60 && scaleName(find("Todi"), Lang::English).find("approx.") != std::string::npos);
    // la quantification fonctionne avec les nouvelles gammes (Hirajoshi en Do : Do Ré Mib Sol Lab)
    const int hira = find("Hirajoshi");
    CHECK(quantizeNote(65, 0, hira) == 63 || quantizeNote(65, 0, hira) == 67);   // Fa -> Mib ou Sol (à égalité : la plus grave)
    CHECK(quantizeNote(65, 0, hira) == 63);
    CHECK(quantizeNote(70, 0, hira) == 68);   // Sib -> Lab
}

static fs::path tempDir(const char* name) {
    fs::path d = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

static void testBank() {
    Bank b;
    Preset& p = b.slot(42);
    p.used = true;
    p.name = "Test preset  avec espaces";
    p.bpm = 133;
    p.rows[0].event.kind = EventKind::NoteAbsPause;
    p.rows[0].event.octaves = 3;
    p.rows[0].mode = PlayMode::Pendulum2;
    p.rows[0].first = 2; p.rows[0].last = 11; p.rows[0].gate = 12; p.rows[0].noteLen = 5;
    p.rows[0].pot[7] = 99; p.rows[0].mute[3] = true;
    p.rows[1].event.kind = EventKind::Velocity; p.rows[1].velocityTargets = 5;
    p.rows[2].event.kind = EventKind::FreeCC; p.rows[2].event.ccNumber = 111; p.rows[2].channel = 9;

    Bank c;
    CHECK(c.deserialize(b.serialize()));
    const Preset& q = c.slot(42);
    CHECK(q.used && q.name == p.name && q.bpm == 133);
    CHECK(!c.slot(41).used && !c.slot(43).used);
    CHECK(b.serialize().find("slot 42\n") != std::string::npos && b.serialize().find("slot 41\n") == std::string::npos);
    CHECK(q.rows[0].event.kind == EventKind::NoteAbsPause && q.rows[0].event.octaves == 3);
    CHECK(q.rows[0].mode == PlayMode::Pendulum2 && q.rows[0].first == 2 && q.rows[0].last == 11);
    CHECK(q.rows[0].gate == 12 && q.rows[0].noteLen == 5 && q.rows[0].pot[7] == 99 && q.rows[0].mute[3]);
    CHECK(q.rows[1].velocityTargets == 5);
    CHECK(q.rows[2].event.ccNumber == 111 && q.rows[2].channel == 9);
    CHECK(c.slot(99).rows[2].channel == 3);  // slot par défaut : canaux 1/2/3

    CHECK(!Bank().deserialize("pas une banque"));

    fs::path dir = tempDir("maq_test_bank");
    fs::path file = dir / "sous" / "dossier" / "ma_banque.maqbank";
    CHECK(b.saveToFile(file));
    Bank d;
    CHECK(d.loadFromFile(file));
    CHECK(d.slot(42).name == p.name);
    CHECK(!d.loadFromFile(dir / "inexistant.maqbank"));
}

static void testFolderSettings() {
    fs::path root = tempDir("maq_test_folders");
    fs::path def = root / "defaut";
    fs::create_directories(def);
    fs::path A = root / "A"; fs::create_directories(A);
    fs::path B = root / "B"; fs::create_directories(B);

    {   // Même dossier pour tout : choisir pour un usage le change pour tous
        FolderSettings s(def);
        CHECK(s.keepSameFolder());
        CHECK(s.folderFor(FolderPurpose::PresetExport) == def);  // rien de choisi : dossier par défaut
        s.setFolder(FolderPurpose::PresetExport, A);
        CHECK(s.folderFor(FolderPurpose::PresetImport) == A);
        CHECK(s.folderFor(FolderPurpose::BankSave) == A);
        CHECK(s.folderFor(FolderPurpose::SysexBackup) == A);
        CHECK(s.folderFor(FolderPurpose::MidiExport) == A);
    }
    {   // Dossiers séparés
        FolderSettings s(def);
        s.setKeepSameFolder(false);
        s.setFolder(FolderPurpose::PresetExport, A);
        s.setFolder(FolderPurpose::PresetImport, B);
        CHECK(s.folderFor(FolderPurpose::PresetExport) == A);
        CHECK(s.folderFor(FolderPurpose::PresetImport) == B);
        // passer en "même dossier" prend le dernier dossier choisi
        s.setKeepSameFolder(true);
        CHECK(s.folderFor(FolderPurpose::BankSave) == B);
        CHECK(s.folderFor(FolderPurpose::PresetExport) == B);
        // repasser en séparé conserve les dossiers par usage
        s.setKeepSameFolder(false);
        CHECK(s.folderFor(FolderPurpose::PresetExport) == A);
    }
    {   // Mémorisation entre sessions
        FolderSettings s(def);
        s.setKeepSameFolder(false);
        s.setFolder(FolderPurpose::PresetExport, A);
        s.setFolder(FolderPurpose::SysexBackup, B);
        fs::path file = root / "cfg" / "settings.ini";
        CHECK(s.save(file));
        FolderSettings t(def);
        CHECK(t.load(file));
        CHECK(!t.keepSameFolder());
        CHECK(t.folderFor(FolderPurpose::PresetExport) == A);
        CHECK(t.folderFor(FolderPurpose::SysexBackup) == B);
        CHECK(!t.load(root / "absent.ini"));
    }
    {   // Dossier mémorisé disparu : retombe sur le défaut et le signale
        FolderSettings s(def);
        fs::path gone = root / "disparu"; fs::create_directories(gone);
        s.setFolder(FolderPurpose::BankSave, gone);
        CHECK(!s.usingFallback(FolderPurpose::BankSave));
        fs::remove_all(gone);
        CHECK(s.usingFallback(FolderPurpose::BankSave));
        CHECK(s.folderFor(FolderPurpose::BankSave) == def);
    }
    CHECK(!FolderSettings::defaultSettingsFile().empty());
    CHECK(!FolderSettings::defaultPresetFolder().empty());
}

int main() {
    testValueMapping();
    testForwardNotes();
    testNoteLength();
    testModes();
    testPauseVelocityMute();
    testOtherEvents();
    testPanicAndStop();
    testBank();
    testFolderSettings();
    testEventTable();
    testNotesAndPresetHelpers();
    testFolderUtf8();
    testStringsAndHelp();
    testPreferences();
    testScales();
    testScaleCatalogue();
    testPotConversions();
    testLearn();
    testOutputQuantize();
    std::printf("%d verifications, %d echec(s)\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
