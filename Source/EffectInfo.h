#pragma once

// Genere a partir du tableau "Les parametres d'effets" (manuel de reference, chapitre 5, p. 90-91).
// Pour chaque effet : parametres 1..7 dans l'ordre du tableau (Dry:Effect Mix = Balance 1 / 2).
// lo/hi = plage envoyee au N364 ; note = unite ou correspondance.

struct EffectParamInfo { const char* name; int lo; int hi; const char* note; };
struct EffectInfo { const char* name; int count; int mixes; EffectParamInfo params[7]; };

inline const EffectInfo& getEffectInfo (int type)
{
    static const EffectInfo table[48] = {
        { "Thru", 0, 0, {} },
        { "Hall 1", 6, 1, { { "Reverb Time", 0, 97, "0.2-9.9 s" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 0, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Hall 2", 6, 1, { { "Reverb Time", 0, 97, "0.2-9.9 s" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 0, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Hall 3", 6, 1, { { "Reverb Time", 0, 97, "0.2-9.9 s" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 0, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Room 1", 6, 1, { { "Reverb Time", 0, 47, "0.2-4.9 s" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 0, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Room 2", 6, 1, { { "Reverb Time", 0, 47, "0.2-4.9 s" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 0, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Live Stage", 6, 1, { { "Reverb Time", 0, 47, "0.2-4.9 s" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 0, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Wet Plate", 6, 1, { { "Reverb Time", 0, 99, "" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 1, 10, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Dry Plate", 6, 1, { { "Reverb Time", 0, 99, "" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 1, 10, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Spring", 6, 1, { { "Reverb Time", 0, 99, "" }, { "Pre Delay", 0, 200, "ms" }, { "E.R Level", 1, 10, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Early Reflection 1", 4, 1, { { "E.R Time", 0, 70, "100-800 ms (100 + 10 x valeur)" }, { "Pre Delay", 0, 200, "ms" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Early Reflection 2", 4, 1, { { "E.R Time", 0, 70, "100-800 ms (100 + 10 x valeur)" }, { "Pre Delay", 0, 200, "ms" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Early Reflection 3", 4, 1, { { "E.R Time", 0, 70, "100-800 ms (100 + 10 x valeur)" }, { "Pre Delay", 0, 200, "ms" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Stereo Delay", 6, 1, { { "Delay Time L", 0, 500, "ms" }, { "Delay Time R", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Cross Delay", 6, 1, { { "Delay Time L", 0, 500, "ms" }, { "Delay Time R", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Dual Delay", 6, 2, { { "Delay Time L", 0, 500, "ms" }, { "Feedback L", -99, 99, "" }, { "High Damp L", 0, 99, "" }, { "Delay Time R", 0, 500, "ms" }, { "Feedback R", -99, 99, "" }, { "High Damp R", 0, 99, "" } } },
        { "Multi Tap Delay 1", 5, 1, { { "Delay Time 1", 0, 500, "ms" }, { "Delay Time 2", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Multi Tap Delay 2", 5, 1, { { "Delay Time 1", 0, 500, "ms" }, { "Delay Time 2", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Multi Tap Delay 3", 5, 1, { { "Delay Time 1", 0, 500, "ms" }, { "Delay Time 2", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Stereo Chorus 1", 6, 1, { { "Delay Time", 0, 200, "ms" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Mod Waveform", 0, 1, "0=SIN 1=TRI" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Stereo Chorus 2", 6, 1, { { "Delay Time", 0, 200, "ms" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Mod Waveform", 0, 1, "0=SIN 1=TRI" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Quadrature Chorus", 7, 1, { { "Delay Time L", 0, 250, "ms" }, { "Delay Time R", 0, 250, "ms" }, { "Mod Speed", 1, 99, "" }, { "Mod Depth", 0, 99, "" }, { "Mod Waveform", -10, 10, "T+10...S+10" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Crossover Chorus", 7, 1, { { "Delay Time L", 0, 250, "ms" }, { "Delay Time R", 0, 250, "ms" }, { "Mod Speed", 1, 99, "" }, { "Mod Depth", 0, 99, "" }, { "Mod Waveform", -10, 10, "T+10...S+10" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Harmonic Chorus", 5, 1, { { "Delay Time L", 0, 500, "ms" }, { "Delay Time R", 0, 500, "ms" }, { "Mod Speed", 1, 99, "" }, { "Mod Depth", 0, 99, "" }, { "Split Point", 0, 18, "" } } },
        { "Symphonic Ensemble", 3, 1, { { "Mod Depth", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Flanger 1", 6, 1, { { "Delay Time", 0, 200, "ms" }, { "Mod Depth", 0, 99, "" }, { "Mod Speed", 1, 99, "" }, { "Resonance", -99, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Flanger 2", 6, 1, { { "Delay Time", 0, 200, "ms" }, { "Mod Depth", 0, 99, "" }, { "Mod Speed", 1, 99, "" }, { "Resonance", -99, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Crossover Flanger", 6, 1, { { "Delay Time", 0, 200, "ms" }, { "Mod Depth", 0, 99, "" }, { "Mod Speed", 1, 99, "" }, { "Resonance", -99, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Exciter", 4, 1, { { "Blend", -99, 99, "" }, { "Emphatic Point", 1, 10, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Enhancer", 6, 1, { { "Harmonic Density", 1, 99, "" }, { "Hot Spot", 1, 20, "" }, { "Stereo Width", 0, 99, "" }, { "Delay Time", 1, 99, "ms" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Distortion", 6, 1, { { "Drive", 1, 111, "" }, { "Hot Spot", 0, 99, "" }, { "Resonance", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" }, { "Output Level", 0, 99, "" } } },
        { "Overdrive", 6, 1, { { "Drive", 1, 111, "" }, { "Hot Spot", 0, 99, "" }, { "Resonance", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" }, { "Output Level", 0, 99, "" } } },
        { "Phaser 1", 5, 1, { { "Manual", 0, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Feedback", -99, 99, "" }, { "Mod Waveform", 0, 1, "0=SIN 1=TRI" } } },
        { "Phaser 2", 5, 1, { { "Manual", 0, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Feedback", -99, 99, "" }, { "Mod Waveform", 0, 1, "0=SIN 1=TRI" } } },
        { "Rotary Speaker", 4, 1, { { "Vibrato Depth", 0, 15, "" }, { "Acceleration", 1, 15, "" }, { "Slow Speed", 1, 99, "" }, { "Fast Speed", 1, 99, "" } } },
        { "Auto Pan", 6, 1, { { "Mod Waveform", 0, 1, "0=SIN 1=TRI" }, { "Mod Wave Shape", -99, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Tremolo", 6, 1, { { "Mod Waveform", 0, 1, "0=SIN 1=TRI" }, { "Mod Wave Shape", -99, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "EQ Low", -12, 12, "dB" }, { "EQ High", -12, 12, "dB" } } },
        { "Parametric EQ", 7, 1, { { "Low Freq", 0, 29, "" }, { "Low Gain", -12, 12, "dB" }, { "Mid Freq", 0, 99, "" }, { "Mid Gain", -12, 12, "dB" }, { "Mid Width", 0, 99, "" }, { "High Freq", 0, 29, "" }, { "High Gain", -12, 12, "dB" } } },
        { "Chorus/Delay", 6, 1, { { "Fig/Cho Delay", 0, 50, "ms" }, { "Fig/Cho Feedback", -99, 99, "" }, { "Mod Speed", 1, 99, "" }, { "Mod Depth", 0, 99, "" }, { "Delay Time", 0, 450, "ms" }, { "Feedback", -99, 99, "" } } },
        { "Flanger/Delay", 6, 1, { { "Fig/Cho Delay", 0, 50, "ms" }, { "Fig/Cho Feedback", -99, 99, "" }, { "Mod Speed", 1, 99, "" }, { "Mod Depth", 0, 99, "" }, { "Delay Time", 0, 450, "ms" }, { "Feedback", -99, 99, "" } } },
        { "Delay/Hall", 6, 2, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "Reverb Time", 0, 97, "0.2-9.9 s" }, { "Pre Delay", 0, 150, "ms" }, { "High Damp", 0, 99, "" } } },
        { "Delay/Room", 6, 2, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "Reverb Time", 0, 47, "0.2-4.9 s" }, { "Pre Delay", 0, 150, "ms" }, { "High Damp", 0, 99, "" } } },
        { "Delay/Chorus", 6, 2, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Mod Waveform", 0, 1, "0=SIN 1=TRI" } } },
        { "Delay/Flanger", 6, 2, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Feedback", -99, 99, "" } } },
        { "Delay/Distortion", 6, 1, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "Drive", 1, 111, "" }, { "Hot Spot", 1, 99, "" }, { "Resonance", 0, 99, "" }, { "Output Level", 1, 99, "" } } },
        { "Delay/Overdrive", 6, 1, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "Drive", 1, 111, "" }, { "Hot Spot", 1, 99, "" }, { "Resonance", 0, 99, "" }, { "Output Level", 1, 99, "" } } },
        { "Delay/Phaser", 6, 2, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "High Damp", 0, 99, "" }, { "Mod Speed", 0, 216, "0.03-30 Hz (non lineaire)" }, { "Mod Depth", 0, 99, "" }, { "Feedback", -99, 99, "" } } },
        { "Delay/Rotary Speaker", 5, 2, { { "Delay Time", 0, 500, "ms" }, { "Feedback", -99, 99, "" }, { "Acceleration", 1, 15, "" }, { "Slow Speed", 1, 99, "" }, { "Fast Speed", 1, 99, "" } } },
    };
    return table[type < 0 ? 0 : (type > 47 ? 47 : type)];
}
