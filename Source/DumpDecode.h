#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdint>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------
// Decodage des dumps SysEx du Korg N264/N364 (manuel, "Format de donnees MIDI").
//
//  - Les donnees 8 bits sont envoyees en 7 bits (NOTE 2) : pour chaque groupe de
//    7 octets, un octet regroupe les bits de poids fort (bit k = bit 7 de l'octet k).
//  - Un Program (dump 0x40, 164 octets) suit la TABLE 1 ; chaque parametre du
//    Parameter Change (TABLE 8) renvoie a un octet (et des bits) de la TABLE 1.
//  - Une Combination (dump 0x49, 136 octets) suit la TABLE 2 ; Global : TABLE 3 ;
//    Drum Kits : TABLE 4.
//
// Chaque fonction de decodage renvoie une liste (numero, valeur envoyee), c'est-a-dire
// dans le meme referentiel que les messages Parameter Change.
// Non decodes : les parametres des effets (sauf leur type), car leur disposition
// depend du type d'effet.
// -----------------------------------------------------------------------------

using DecodedValues = std::vector<std::pair<int, int>>;

inline std::vector<std::uint8_t> unpack7 (const std::uint8_t* d, size_t n)
{
    std::vector<std::uint8_t> out;
    out.reserve (n);
    size_t i = 0;

    while (i < n)
    {
        const std::uint8_t msbs = d[i++];

        for (int k = 0; k < 7 && i < n; ++k, ++i)
            out.push_back ((std::uint8_t) ((d[i] & 0x7F) | (((msbs >> k) & 1) << 7)));
    }

    return out;
}

inline juce::String decodeName (const std::uint8_t* b)
{
    juce::String name;

    for (int i = 0; i < 10; ++i)
        name += juce::String::charToString ((juce::juce_wchar) ((b[i] >= 0x20 && b[i] < 0x7F) ? b[i] : ' '));

    return name.trim();
}

namespace n364dump
{
    enum Kind { U = 0, S = 1, SOUND = 2, SIGN3 = 3 };

    struct Field { int pc; int byte; int shift; int bits; int kind; };

    // Table 8 (numeros du Parameter Change) -> Table 1 (octets du dump), OSC-1 et parametres communs.
    inline const std::vector<Field>& programFields()
    {
        static const std::vector<Field> f = []
        {
            std::vector<Field> v = {
                {0,10,0,0,U},{1,11,0,1,U},{2,11,1,1,U},
                {3,21,0,0,S},{4,22,0,0,U},{5,23,0,0,S},{6,24,0,0,U},{7,25,0,0,U},{8,26,0,0,S},{9,28,0,0,S},{10,27,0,0,S},
                {11,29,0,3,U},{12,30,0,0,U},{13,32,0,0,U},{14,31,0,0,U},{15,29,5,2,U},{16,29,7,1,U},
                {17,33,0,0,S},{18,34,0,0,S},{19,35,0,0,U},{20,36,0,0,S},
                {21,39,0,0,U},{22,37,0,0,S},{23,38,0,0,S},
                {24,12,0,0,SOUND},{25,65,0,0,U},{26,14,0,0,S},{27,40,0,0,S},{28,86,0,0,U},{29,82,4,4,U},{30,82,0,4,U},
                {31,50,0,0,U},{32,53,0,0,U},{33,83,0,0,U},{34,84,0,0,S},
                {35,57,0,0,U},{36,58,0,0,S},{37,59,0,0,U},{38,60,0,0,S},{39,61,0,0,U},{40,62,0,0,S},{41,63,0,0,U},{42,64,0,0,S},
                {43,56,0,0,S},{44,55,0,0,U},
                {45,79,0,4,SIGN3},{46,79,1,5,SIGN3},{47,79,2,6,SIGN3},{48,79,3,7,SIGN3},
                {49,51,0,0,U},{50,85,0,2,U},{51,52,0,0,S},{52,54,0,0,U},
                {53,78,0,4,SIGN3},{54,78,1,5,SIGN3},{55,78,2,6,SIGN3},{56,78,3,7,SIGN3},
                {57,71,0,0,U},{58,72,0,0,U},{59,73,0,0,U},{60,74,0,0,U},{61,75,0,0,U},{62,76,0,0,U},{63,77,0,0,U},
                {64,68,0,0,S},{65,70,0,0,U},
                {66,81,0,4,SIGN3},{67,81,1,5,SIGN3},{68,81,2,6,SIGN3},{69,81,3,7,SIGN3},
                {70,66,0,0,U},{71,85,4,2,U},{72,67,0,0,S},{73,69,0,0,U},
                {74,80,0,4,SIGN3},{75,80,1,5,SIGN3},{76,80,2,6,SIGN3},{77,80,3,7,SIGN3},
                {78,41,0,3,U},{79,42,0,0,U},{80,45,0,0,U},{81,43,0,0,U},{82,44,0,0,U},{83,41,7,1,U},
                {84,46,0,0,S},{85,49,0,0,U},{86,47,0,0,U},{87,48,0,0,U},
                {88,18,0,0,S},{89,19,0,0,S},{90,20,0,0,U} };

            // OSC-2 : numeros 91..154 = OSC-1 (24..87) + 67 ; octets : +47 (sauf son et octave)
            const size_t base = v.size();
            for (size_t i = 0; i < base; ++i)
            {
                if (v[i].pc < 24 || v[i].pc > 87)
                    continue;

                Field g = v[i];
                g.pc += 67;
                g.byte = (g.byte == 12) ? 15 : (g.byte == 14) ? 17 : g.byte + 47;
                v.push_back (g);
            }

            return v;
        }();

        return f;
    }

    inline int extract (const std::uint8_t* b, size_t n, const Field& f)
    {
        if ((size_t) f.byte >= n)
            return 0;

        const int raw = b[f.byte];

        switch (f.kind)
        {
            case S:     return (int) (std::int8_t) raw;
            case SOUND: return raw | (((size_t) f.byte + 1 < n ? b[f.byte + 1] : 0) << 8);
            case SIGN3:
            {
                const int sw = (raw >> f.shift) & 1;
                const int pol = (raw >> f.bits) & 1;
                return sw ? (pol ? -1 : 1) : 0;
            }
            default:    return f.bits == 0 ? raw : (raw >> f.shift) & ((1 << f.bits) - 1);
        }
    }
}

// Program : 164 octets
inline DecodedValues decodeProgram (const std::uint8_t* b, size_t n)
{
    DecodedValues out;

    if (n < 164)
        return out;

    for (auto& f : n364dump::programFields())
        out.push_back ({ f.pc, n364dump::extract (b, n, f) });

    out.push_back ({ 155, b[135] });   // type de l'effet 1
    out.push_back ({ 156, b[136] });   // type de l'effet 2
    return out;
}

// Combination : 136 octets, 8 timbres de 12 octets (40..135)
inline DecodedValues decodeCombination (const std::uint8_t* b, size_t n)
{
    DecodedValues out;

    if (n < 136)
        return out;

    for (int tb = 0; tb < 8; ++tb)
    {
        const int o = 12 * tb;
        const int prog = b[40 + o];
        const int off = (b[51 + o] >> 4) & 1;
        const int bankBits = (b[50 + o] >> 6) & 3;

        out.push_back ({ 0 + tb, off ? 0 : (bankBits == 2 ? juce::jmin (236, 101 + prog) : (prog % 100) + 1) });
        out.push_back ({ 8 + tb, b[41 + o] });
        out.push_back ({ 16 + tb, b[51 + o] & 0x0F });
        out.push_back ({ 24 + tb, b[46 + o] });
        out.push_back ({ 32 + tb, b[47 + o] });
        out.push_back ({ 40 + tb, b[48 + o] });
        out.push_back ({ 48 + tb, b[49 + o] });
        out.push_back ({ 56 + tb, (int) (std::int8_t) b[42 + o] });
        out.push_back ({ 64 + tb, (int) (std::int8_t) b[43 + o] });
        out.push_back ({ 72 + tb, b[50 + o] & 1 });
        out.push_back ({ 80 + tb, (b[50 + o] >> 1) & 1 });
        out.push_back ({ 88 + tb, (b[50 + o] >> 2) & 1 });
        out.push_back ({ 96 + tb, (b[50 + o] >> 3) & 1 });
        out.push_back ({ 104 + tb, b[44 + o] });
        out.push_back ({ 112 + tb, (b[45 + o] >> 4) & 0x0F });
        out.push_back ({ 120 + tb, b[45 + o] & 0x0F });
        out.push_back ({ 128 + tb, (b[51 + o] >> 4) & 3 });
    }

    out.push_back ({ 136, b[11] });   // type de l'effet 1
    out.push_back ({ 137, b[12] });   // type de l'effet 2
    return out;
}

// Global : 28 octets (TABLE 3). isSigned(index) dit si l'octet est signe.
inline DecodedValues decodeGlobal (const std::uint8_t* b, size_t n)
{
    DecodedValues out;

    if (n < 23)
        return out;

    for (int i = 0; i < 23; ++i)
    {
        if (i == 4)
            continue;   // octet reserve

        const bool isSigned = (i == 0 || i == 1 || (i >= 7 && i <= 18));
        out.push_back ({ i, isSigned ? (int) (std::int8_t) b[i] : (int) b[i] });
    }

    return out;
}

// Drum Kits : 4 kits (A1, A2, B1, B2) x 60 instruments x 7 octets (TABLE 4).
// Numero renvoye : index * 100 + numero de parametre (TABLE 13).
inline DecodedValues decodeDrums (const std::uint8_t* b, size_t n, int kit)
{
    DecodedValues out;

    if (n < 1680 || kit < 0 || kit > 3)
        return out;

    for (int idx = 0; idx < 60; ++idx)
    {
        const std::uint8_t* d = b + kit * 420 + idx * 7;

        out.push_back ({ idx * 100 + 0, d[0] });
        out.push_back ({ idx * 100 + 1, d[1] });
        out.push_back ({ idx * 100 + 2, (int) (std::int8_t) d[3] });
        out.push_back ({ idx * 100 + 3, (int) (std::int8_t) d[4] });
        out.push_back ({ idx * 100 + 4, (int) (std::int8_t) d[5] });
        out.push_back ({ idx * 100 + 5, (d[2] >> 5) & 7 });
        out.push_back ({ idx * 100 + 6, d[2] & 0x1F });
        out.push_back ({ idx * 100 + 7, (d[6] >> 4) & 0x0F });
        out.push_back ({ idx * 100 + 8, d[6] & 0x0F });
    }

    return out;
}
