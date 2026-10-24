#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

// -----------------------------------------------------------------------------
// Table des parametres du Korg N264/N364.
//
// Source : manuel, "Format de donnees MIDI" (pages 223 a 229 du manuel).
//
//  - Program      : TABLE 8  (numeros du message PARAMETER CHANGE, 0x41), 0..187
//  - Effets       : TABLE 9  (Program : 155..187 ; Combination : 136..168)
//  - Combination  : TABLE 11 (n = timbre 0..7), 0..168
//  - Drum Kit     : TABLE 13 (message DRUM KIT PARAMETER CHANGE, 0x53)
//  - Global       : TABLE 3  (octets d'un GLOBAL DATA DUMP, 0x51, 28 octets)
//
// ATTENTION : les numeros de Program et de Combination se recoupent. Le N364
// interprete le message selon son mode courant (Edit Program, Edit Combination,
// mode Global pour les Drum Kits et le Global).
//
// POUR AJOUTER UN PARAMETRE : ajoute une ligne knob / toggle / choice ci-dessous.
// -----------------------------------------------------------------------------

enum class N364Target { program = 0, combination = 1, drums = 2, global = 3 };

struct N364Param
{
    N364Target target = N364Target::program;
    int number = 0;          // numero du message (Program/Combination/Drums) ou index d'octet (Global)
    int drumIndex = -1;      // 0..59 pour les Drum Kits
    juce::String id;
    juce::String name;
    juce::String group;
    int minValue = 0;
    int maxValue = 0;
    int defaultValue = 0;
    int rawOffset = 0;       // valeur envoyee = valeur du parametre + rawOffset
    bool isToggle = false;
    int nameSet = 0;         // 1 = noms de Multisounds (0..429), 2 = noms de sons de batterie (valeur 0 = OFF)
    juce::StringArray choices;
};

struct N364Graph     // enveloppe dessinee apres un groupe de potards
{
    juce::String group;
    juce::StringArray ids;     // start, attT, attL, decT, break, slopeT, sustain, relT, relL
    int minLevel;
    int maxLevel;
};

inline juce::StringArray getEffectNames()
{
    return juce::StringArray {
        "Thru", "Hall 1", "Hall 2", "Hall 3", "Room 1", "Room 2", "Live Stage", "Wet Plate", "Dry Plate",
        "Spring", "Early Reflection 1", "Early Reflection 2", "Early Reflection 3", "Stereo Delay",
        "Cross Delay", "Dual Delay", "Multi Tap Delay 1", "Multi Tap Delay 2", "Multi Tap Delay 3",
        "Stereo Chorus 1", "Stereo Chorus 2", "Quadrature Chorus", "Crossover Chorus", "Harmonic Chorus",
        "Symphonic Ensemble", "Flanger 1", "Flanger 2", "Crossover Flanger", "Exciter", "Enhancer",
        "Distortion", "Overdrive", "Phaser 1", "Phaser 2", "Rotary Speaker", "Auto Pan", "Tremolo",
        "Parametric EQ", "Chorus/Delay", "Flanger/Delay", "Delay/Hall", "Delay/Room", "Delay/Chorus",
        "Delay/Flanger", "Delay/Distortion", "Delay/Overdrive", "Delay/Phaser", "Delay/Rotary Speaker" };
}

inline const std::vector<N364Param>& getParamTable()
{
    static const std::vector<N364Param> table = []
    {
        std::vector<N364Param> t;
        N364Target target = N364Target::program;

        auto makeId = [&target] (int n)
        {
            const char* prefix = target == N364Target::program ? "p"
                               : target == N364Target::combination ? "c" : "g";
            return juce::String (prefix) + juce::String (n).paddedLeft ('0', 3);
        };

        auto knob = [&] (int n, const juce::String& group, const juce::String& name, int lo, int hi, int def)
        {
            N364Param p;
            p.target = target; p.number = n; p.id = makeId (n);
            p.name = name; p.group = group;
            p.minValue = lo; p.maxValue = hi; p.defaultValue = def;
            t.push_back (p);
        };

        auto toggle = [&] (int n, const juce::String& group, const juce::String& name, int def)
        {
            knob (n, group, name, 0, 1, def);
            t.back().isToggle = true;
        };

        auto choice = [&] (int n, const juce::String& group, const juce::String& name,
                           const juce::StringArray& items, int def)
        {
            knob (n, group, name, 0, items.size() - 1, def);
            t.back().choices = items;
        };

        // trois etats "-", "0", "+" : valeur envoyee = -1, 0, +1
        auto signChoice = [&] (int n, const juce::String& group, const juce::String& name)
        {
            choice (n, group, name, juce::StringArray { "-", "0", "+" }, 1);
            t.back().rawOffset = -1;
        };

        const juce::StringArray waves { "Triangle", "Up Saw", "Down Saw", "Square 1", "Random", "Square 2" };
        const juce::StringArray kbdModes { "OFF", "LOW", "HIGH", "ALL" };
        const juce::StringArray notes { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const juce::StringArray scales { "Equal Temp", "Equal Temp 2", "Pure Major", "Pure Minor", "Arabic",
                                         "Pythagorean", "Werkmeister", "Kirnberger", "Slendro", "Pelog", "User Scale" };
        const juce::StringArray modSources { "None", "Joy Stick (+Y)", "Joy Stick (-Y)", "After Touch", "Assign Pedal 1",
                                             "Assign Pedal 2", "VDA EG", "Value Slider", "V.S + J.S (+Y)", "V.S + J.S (-Y)",
                                             "V.S + A.Touch", "V.S + Pedal 1", "V.S + Pedal 2", "V.S + VDA EG" };

        // Effets : TABLE 9. Program : 155..187. Combination : 136..168.
        auto addEffects = [&] (int base, const juce::String& prefix)
        {
            const juce::String gr = prefix + " - Routage";
            const juce::String g1 = prefix + " - Effet 1";
            const juce::String g2 = prefix + " - Effet 2";

            toggle (base + 2, gr, "Effet 1 ON", 1);
            toggle (base + 3, gr, "Effet 2 ON", 1);
            choice (base + 10, gr, "Placement",
                    juce::StringArray { "Serial", "Parallel 1", "Parallel 2", "Parallel 3", "Serial Sub", "Parallel Sub" }, 0);
            knob (base + 4, gr, "Out 3 Pan (0-101)", 0, 101, 50);
            knob (base + 5, gr, "Out 4 Pan (0-101)", 0, 101, 50);
            knob (base + 6, gr, "Out 3-L Level", 0, 100, 100);
            knob (base + 7, gr, "Out 3-R Level", 0, 100, 100);
            knob (base + 8, gr, "Out 4-L Level", 0, 100, 100);
            knob (base + 9, gr, "Out 4-R Level", 0, 100, 100);

            choice (base + 0, g1, "Type", getEffectNames(), 0);
            choice (base + 11, g1, "Dynamic Mod Source", modSources, 0);
            knob (base + 12, g1, "Dynamic Mod Int", -15, 15, 0);
            for (int i = 0; i < 7; ++i)
                knob (base + 13 + i, g1, "Param " + juce::String (i + 1), -200, 800, 0);
            knob (base + 20, g1, "Balance 1 (Dry:Effect Mix)", 0, 100, 100);
            knob (base + 21, g1, "Balance 2 (Mix 2 / R)", 0, 100, 100);

            choice (base + 1, g2, "Type", getEffectNames(), 0);
            choice (base + 22, g2, "Dynamic Mod Source", modSources, 0);
            knob (base + 23, g2, "Dynamic Mod Int", -15, 15, 0);
            for (int i = 0; i < 7; ++i)
                knob (base + 24 + i, g2, "Param " + juce::String (i + 1), -200, 800, 0);
            knob (base + 31, g2, "Balance 1 (Dry:Effect Mix)", 0, 100, 100);
            knob (base + 32, g2, "Balance 2 (Mix 2 / R)", 0, 100, 100);
        };

        // ================================================== PROGRAM (TABLE 8)
        choice (0, "Programme", "Mode", juce::StringArray { "Single", "Double", "Drums" }, 0);
        choice (1, "Programme", "Assign", juce::StringArray { "Poly", "Mono" }, 0);
        toggle (2, "Programme", "Hold", 0);
        knob (88, "Programme", "Interval (Double)", -12, 12, 0);
        knob (89, "Programme", "Detune (Double)", -50, 50, 0);
        knob (90, "Programme", "Delay Start", 0, 99, 0);

        knob (3, "Pitch EG", "Start Level", -99, 99, 0);
        knob (4, "Pitch EG", "Attack Time", 0, 99, 0);
        knob (5, "Pitch EG", "Attack Level", -99, 99, 0);
        knob (6, "Pitch EG", "Decay Time", 0, 99, 0);
        knob (7, "Pitch EG", "Release Time", 0, 99, 0);
        knob (8, "Pitch EG", "Release Level", -99, 99, 0);
        knob (9, "Pitch EG", "Level Vel Sense", -99, 99, 0);
        knob (10, "Pitch EG", "Time Vel Sense", -99, 99, 0);

        choice (11, "Cutoff MG (filtre)", "Wave Form", waves, 0);
        choice (15, "Cutoff MG (filtre)", "OSC Select", juce::StringArray { "OFF", "OSC-1", "OSC-2", "OSC-1+2" }, 0);
        toggle (16, "Cutoff MG (filtre)", "Key Sync", 0);
        knob (12, "Cutoff MG (filtre)", "Frequency", 0, 99, 0);
        knob (13, "Cutoff MG (filtre)", "Intensity", 0, 99, 0);
        knob (14, "Cutoff MG (filtre)", "Delay", 0, 99, 0);

        knob (17, "After Touch", "Pitch Bend Range", -12, 12, 0);
        knob (18, "After Touch", "VDF Cutoff", -99, 99, 0);
        knob (19, "After Touch", "VDF MG Int", 0, 99, 0);
        knob (20, "After Touch", "VDA Amplitude", -99, 99, 0);

        knob (22, "Joystick", "Pitch Bend Range", -12, 12, 0);
        knob (23, "Joystick", "VDF Sweep Int", -99, 99, 0);
        knob (21, "Joystick", "VDF MG Int", 0, 99, 0);

        // OSC-1 = numeros 24..87 ; OSC-2 = les memes +67 (91..154)
        auto addOsc = [&] (int osc, int off)
        {
            const juce::String o = "OSC-" + juce::String (osc) + " ";

            knob (24 + off, o + "Oscillateur", "Multisound (0-429)", 0, 429, 0);
            t.back().nameSet = 1;
            knob (26 + off, o + "Oscillateur", "Octave (-2=32' .. 1=4')", -2, 1, 0);
            knob (28 + off, o + "Oscillateur", "Pan A:B (15=CNT)", 0, 30, 15);
            knob (29 + off, o + "Oscillateur", "C Send Level", 0, 9, 0);
            knob (30 + off, o + "Oscillateur", "D Send Level", 0, 9, 0);

            knob (27 + off, o + "Pitch EG", "Pitch EG Int", -99, 99, 0);

            choice (78 + off, o + "Pitch MG", "Wave Form", waves, 0);
            toggle (83 + off, o + "Pitch MG", "Key Sync", 0);
            knob (79 + off, o + "Pitch MG", "Frequency", 0, 99, 0);
            knob (80 + off, o + "Pitch MG", "Intensity", 0, 99, 0);
            knob (81 + off, o + "Pitch MG", "Delay", 0, 99, 0);
            knob (82 + off, o + "Pitch MG", "Fade In", 0, 99, 0);
            knob (84 + off, o + "Pitch MG", "Freq Mod Kbd Trk", -99, 99, 0);
            knob (85 + off, o + "Pitch MG", "Freq Mod AT+JS", 0, 9, 0);
            knob (86 + off, o + "Pitch MG", "Int Mod by AT", 0, 99, 0);
            knob (87 + off, o + "Pitch MG", "Int Mod by JS", 0, 99, 0);

            knob (31 + off, o + "VDF (filtre)", "Cutoff", 0, 99, 99);
            knob (32 + off, o + "VDF (filtre)", "EG Intensity", 0, 99, 0);
            knob (43 + off, o + "VDF (filtre)", "EG Int Vel Sens", -99, 99, 0);
            knob (44 + off, o + "VDF (filtre)", "EG Time Vel Sens", 0, 99, 0);
            knob (49 + off, o + "VDF (filtre)", "Kbd Track Key", 0, 127, 60);
            choice (50 + off, o + "VDF (filtre)", "Kbd Track Mode", kbdModes, 0);
            knob (51 + off, o + "VDF (filtre)", "Cutoff Kbd Track", -99, 99, 0);
            knob (52 + off, o + "VDF (filtre)", "EG Time Kbd Track", 0, 99, 0);

            knob (35 + off, o + "VDF EG", "Attack Time", 0, 99, 0);
            knob (36 + off, o + "VDF EG", "Attack Level", -99, 99, 0);
            knob (37 + off, o + "VDF EG", "Decay Time", 0, 99, 0);
            knob (38 + off, o + "VDF EG", "Break Point", -99, 99, 0);
            knob (39 + off, o + "VDF EG", "Slope Time", 0, 99, 0);
            knob (40 + off, o + "VDF EG", "Sustain Level", -99, 99, 0);
            knob (41 + off, o + "VDF EG", "Release Time", 0, 99, 0);
            knob (42 + off, o + "VDF EG", "Release Level", -99, 99, 0);

            const juce::StringArray segs { "Attack", "Decay", "Slope", "Release" };
            for (int s = 0; s < 4; ++s)
                signChoice (45 + s + off, o + "VDF EG Time Mod by Vel Sense", segs[s]);
            for (int s = 0; s < 4; ++s)
                signChoice (53 + s + off, o + "VDF EG Time Mod by Kbd Track", segs[s]);

            knob (25 + off, o + "VDA (ampli)", "Osc Level", 0, 99, 99);
            knob (64 + off, o + "VDA (ampli)", "EG Int Vel Sens", -99, 99, 0);
            knob (65 + off, o + "VDA (ampli)", "EG Time Vel Sens", 0, 99, 0);
            knob (70 + off, o + "VDA (ampli)", "Kbd Track Key", 0, 127, 60);
            choice (71 + off, o + "VDA (ampli)", "Kbd Track Mode", kbdModes, 0);
            knob (72 + off, o + "VDA (ampli)", "Amp Kbd Track Int", -99, 99, 0);
            knob (73 + off, o + "VDA (ampli)", "EG Time Kbd Track", 0, 99, 0);

            knob (57 + off, o + "VDA EG", "Attack Time", 0, 99, 0);
            knob (58 + off, o + "VDA EG", "Attack Level", 0, 99, 99);
            knob (59 + off, o + "VDA EG", "Decay Time", 0, 99, 0);
            knob (60 + off, o + "VDA EG", "Break Point", 0, 99, 99);
            knob (61 + off, o + "VDA EG", "Slope Time", 0, 99, 0);
            knob (62 + off, o + "VDA EG", "Sustain Level", 0, 99, 99);
            knob (63 + off, o + "VDA EG", "Release Time", 0, 99, 0);

            for (int s = 0; s < 4; ++s)
                signChoice (66 + s + off, o + "VDA EG Time Mod by Vel Sense", segs[s]);
            for (int s = 0; s < 4; ++s)
                signChoice (74 + s + off, o + "VDA EG Time Mod by Kbd Track", segs[s]);

            knob (33 + off, o + "Color", "Intensity", 0, 99, 0);
            knob (34 + off, o + "Color", "Int by Vel Sense", -99, 99, 0);
        };

        addOsc (1, 0);
        addOsc (2, 67);

        addEffects (155, "Effets");

        // ============================================== COMBINATION (TABLE 11)
        target = N364Target::combination;

        for (int tb = 0; tb < 8; ++tb)
        {
            const juce::String g = "Timbre " + juce::String (tb + 1);

            knob (0 + tb, g, "Program (0=OFF, 1-100=0-99, 101+=G)", 0, 236, 0);
            knob (8 + tb, g, "Output Level", 0, 127, 127);
            knob (16 + tb, g, "MIDI Channel", 1, 16, 1);
            t.back().rawOffset = -1;
            knob (24 + tb, g, "Key Window Top", 0, 127, 127);
            knob (32 + tb, g, "Key Window Bottom", 0, 127, 0);
            knob (40 + tb, g, "Vel Window Top", 1, 127, 127);
            knob (48 + tb, g, "Vel Window Bottom", 1, 127, 1);
            knob (56 + tb, g, "Transpose", -24, 24, 0);
            knob (64 + tb, g, "Detune", -50, 50, 0);
            toggle (72 + tb, g, "Program Change Filter", 1);
            toggle (80 + tb, g, "Damper Filter", 1);
            toggle (88 + tb, g, "After Touch Filter", 1);
            toggle (96 + tb, g, "Control Change Filter", 1);
            knob (104 + tb, g, "Pan A:B (15=CNT)", 0, 30, 15);
            knob (112 + tb, g, "C Send Level", 0, 9, 0);
            knob (120 + tb, g, "D Send Level", 0, 9, 0);
            choice (128 + tb, g, "Timbre Mode", juce::StringArray { "INT", "OFF (INT)", "EXT", "OFF (EXT)" }, 0);
        }

        addEffects (136, "Combination - Effets");

        // ================================================= DRUM KIT (TABLE 13)
        target = N364Target::drums;

        {
            struct Col { const char* name; int lo; int hi; int def; };
            const Col cols[9] = {
                { "Inst No (0=OFF)", 0, 255, 0 }, { "Key", 12, 115, 60 }, { "Tune", -120, 120, 0 },
                { "Level", -99, 99, 0 },          { "Decay", -99, 99, 0 }, { "Excl", 0, 7, 0 },
                { "Pan (15=CNT)", 0, 30, 15 },    { "C Send", 0, 9, 0 },   { "D Send", 0, 9, 0 } };

            for (int idx = 0; idx < 60; ++idx)
            {
                for (int c = 0; c < 9; ++c)
                {
                    N364Param p;
                    p.target = N364Target::drums;
                    p.number = c;
                    p.drumIndex = idx;
                    p.id = "d" + juce::String (idx).paddedLeft ('0', 2) + "_" + juce::String (c);
                    p.name = juce::String (cols[c].name);
                    p.group = "Drum Kit - Inst " + juce::String (idx);
                    p.minValue = cols[c].lo;
                    p.maxValue = cols[c].hi;
                    p.defaultValue = cols[c].def;
                    p.nameSet = (c == 0) ? 2 : 0;
                    t.push_back (p);
                }
            }
        }

        // ============================================== GLOBAL (TABLE 3, dump)
        target = N364Target::global;

        knob (0, "Global", "Master Tune", -50, 50, 0);
        knob (1, "Global", "Key Transpose", -12, 12, 0);
        choice (2, "Global", "Damper Polarity", juce::StringArray { "-", "+" }, 0);
        choice (3, "Global", "Assign Pedal",
                juce::StringArray { "OFF", "Program/Combi Up", "Program/Combi Down", "Seq Start/Stop", "Seq Punch In/Out",
                                    "Effect 1 On/Off", "Effect 2 On/Off", "Scale Change", "Volume", "Expression",
                                    "VDF Cutoff", "Effect Control", "Data Entry" }, 0);
        choice (5, "Global", "Main Scale Type", scales, 0);
        choice (6, "Global", "Main Scale Key", notes, 0);
        choice (21, "Global", "Sub Scale Type", scales, 0);
        choice (22, "Global", "Sub Scale Key", notes, 0);
        choice (19, "Global", "Velocity Curve", juce::StringArray { "1", "2", "3", "4", "5", "6", "7", "8" }, 0);
        choice (20, "Global", "After Touch Curve", juce::StringArray { "1", "2", "3", "4", "5", "6", "7", "8" }, 0);

        for (int i = 0; i < 12; ++i)
            knob (7 + i, "User Scale (demi-tons, -50..50)", notes[i], -50, 50, 0);

        return t;
    }();

    return table;
}

inline const std::vector<N364Graph>& getGraphs()
{
    static const std::vector<N364Graph> graphs = []
    {
        std::vector<N364Graph> g;

        auto arr = [] (std::initializer_list<juce::String> l)
        {
            juce::StringArray a;
            for (auto& s : l)
                a.add (s);
            return a;
        };

        auto id = [] (int n) { return "p" + juce::String (n).paddedLeft ('0', 3); };

        g.push_back ({ "Pitch EG", arr ({ id (3), id (4), id (5), id (6), "", "", "", id (7), id (8) }), -99, 99 });

        for (int osc = 1; osc <= 2; ++osc)
        {
            const int o = (osc == 1) ? 0 : 67;
            const juce::String pre = "OSC-" + juce::String (osc) + " ";

            g.push_back ({ pre + "VDF EG",
                           arr ({ "", id (35 + o), id (36 + o), id (37 + o), id (38 + o),
                                  id (39 + o), id (40 + o), id (41 + o), id (42 + o) }), -99, 99 });

            g.push_back ({ pre + "VDA EG",
                           arr ({ "", id (57 + o), id (58 + o), id (59 + o), id (60 + o),
                                  id (61 + o), id (62 + o), id (63 + o), "" }), 0, 99 });
        }

        return g;
    }();

    return graphs;
}
