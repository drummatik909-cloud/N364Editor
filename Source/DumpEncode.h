#pragma once

#include "DumpDecode.h"
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------------
// Encodage des dumps SysEx du Korg N264/N364 (operation inverse de DumpDecode.h).
//
//  - pack7        : donnees 8 bits -> 7 bits (NOTE 2 du manuel)
//  - encodeProgram / encodeCombination : recopie les valeurs des parametres du plugin dans un
//    enregistrement brut (164 / 136 octets) ; les octets que le plugin ne modelise pas
//    (par exemple les parametres d'effets) restent ceux de l'enregistrement de depart.
//  - buildKorgDump : message complet F0 42 3g 35 <fonction> [00] <donnees 7 bits> F7
//  - splitSyx      : decoupe le contenu d'un fichier .syx en messages F0 ... F7
// -----------------------------------------------------------------------------

inline std::vector<std::uint8_t> pack7 (const std::uint8_t* d, size_t n)
{
    std::vector<std::uint8_t> out;
    out.reserve (n + n / 7 + 2);

    for (size_t i = 0; i < n; i += 7)
    {
        const size_t count = (n - i < 7) ? (n - i) : 7;
        std::uint8_t msbs = 0;

        for (size_t k = 0; k < count; ++k)
            if (d[i + k] & 0x80)
                msbs = (std::uint8_t) (msbs | (1 << k));

        out.push_back (msbs);

        for (size_t k = 0; k < count; ++k)
            out.push_back ((std::uint8_t) (d[i + k] & 0x7F));
    }

    return out;
}

// Nom de 10 caracteres ASCII (0x20..0x7E), complete par des espaces
inline void setRecordName (std::uint8_t* rec, const std::string& name)
{
    for (size_t i = 0; i < 10; ++i)
    {
        char c = i < name.size() ? name[i] : ' ';

        if (c < 0x20 || c > 0x7E)
            c = ' ';

        rec[i] = (std::uint8_t) c;
    }
}

namespace n364dump
{
    // Inverse de extract() : ecrit la valeur v du champ f dans l'enregistrement
    inline void inject (std::uint8_t* rec, size_t n, const Field& f, int v)
    {
        if ((size_t) f.byte >= n)
            return;

        std::uint8_t& b = rec[f.byte];

        switch (f.kind)
        {
            case S:
                b = (std::uint8_t) (std::int8_t) v;
                break;

            case SOUND:
                b = (std::uint8_t) (v & 0xFF);

                if ((size_t) f.byte + 1 < n)
                    rec[f.byte + 1] = (std::uint8_t) ((v >> 8) & 0xFF);
                break;

            case SIGN3:
            {
                const int sw = v != 0 ? 1 : 0;
                const int pol = v < 0 ? 1 : 0;
                b = (std::uint8_t) ((b & ~((1 << f.shift) | (1 << f.bits))) | (sw << f.shift) | (pol << f.bits));
                break;
            }

            default:
                if (f.bits == 0)
                    b = (std::uint8_t) (v & 0xFF);
                else
                {
                    const int mask = ((1 << f.bits) - 1) << f.shift;
                    b = (std::uint8_t) ((b & ~mask) | ((v << f.shift) & mask));
                }
                break;
        }
    }
}

// vals : numero du Parameter Change -> valeur "envoyee" (meme referentiel que decodeProgram)
inline void encodeProgram (std::uint8_t* rec, size_t n, const std::unordered_map<int, int>& vals)
{
    if (n < 164)
        return;

    for (auto& f : n364dump::programFields())
    {
        const auto it = vals.find (f.pc);

        if (it != vals.end())
            n364dump::inject (rec, n, f, it->second);
    }

    const auto t1 = vals.find (155);
    const auto t2 = vals.find (156);

    if (t1 != vals.end()) rec[135] = (std::uint8_t) (t1->second & 0xFF);
    if (t2 != vals.end()) rec[136] = (std::uint8_t) (t2->second & 0xFF);
}

inline void encodeCombination (std::uint8_t* rec, size_t n, const std::unordered_map<int, int>& vals)
{
    if (n < 136)
        return;

    auto get = [&] (int pc, int& out)
    {
        const auto it = vals.find (pc);

        if (it == vals.end())
            return false;

        out = it->second;
        return true;
    };

    for (int tb = 0; tb < 8; ++tb)
    {
        const int o = 12 * tb;
        int v = 0;

        // Program No. : 0 = timbre OFF, 1..100 = programs 00..99, 101..236 = banque G
        if (get (0 + tb, v))
        {
            const int basePrg = rec[40 + o];

            if (v > 0)
            {
                int prg;

                if (v <= 100)
                {
                    prg = v - 1;
                    rec[50 + o] = (std::uint8_t) (rec[50 + o] & ~0x80);              // pas la banque G
                    if (basePrg >= 100 && basePrg % 100 == prg)
                        prg = basePrg;                                               // on garde la banque B/D d'origine
                }
                else
                {
                    prg = v - 101;
                    rec[50 + o] = (std::uint8_t) ((rec[50 + o] & ~0x40) | 0x80);     // banque G
                }

                rec[40 + o] = (std::uint8_t) (prg & 0xFF);
            }
        }

        if (get (8 + tb, v))  rec[41 + o] = (std::uint8_t) (v & 0xFF);
        if (get (24 + tb, v)) rec[46 + o] = (std::uint8_t) (v & 0xFF);
        if (get (32 + tb, v)) rec[47 + o] = (std::uint8_t) (v & 0xFF);
        if (get (40 + tb, v)) rec[48 + o] = (std::uint8_t) (v & 0xFF);
        if (get (48 + tb, v)) rec[49 + o] = (std::uint8_t) (v & 0xFF);
        if (get (56 + tb, v)) rec[42 + o] = (std::uint8_t) (std::int8_t) v;
        if (get (64 + tb, v)) rec[43 + o] = (std::uint8_t) (std::int8_t) v;

        // filtres de controle : bits 0..3 de l'octet 50+12n (les bits 4..7 sont conserves)
        for (int k = 0; k < 4; ++k)
            if (get (72 + 8 * k + tb, v))
                rec[50 + o] = (std::uint8_t) ((rec[50 + o] & ~(1 << k)) | ((v ? 1 : 0) << k));

        if (get (104 + tb, v)) rec[44 + o] = (std::uint8_t) (v & 0xFF);

        // envois : octet 45+12n (C = bits 4..7, D = bits 0..3)
        int c = 0, d = 0;
        const bool hasC = get (112 + tb, c), hasD = get (120 + tb, d);

        if (hasC) rec[45 + o] = (std::uint8_t) ((rec[45 + o] & 0x0F) | ((c & 0x0F) << 4));
        if (hasD) rec[45 + o] = (std::uint8_t) ((rec[45 + o] & 0xF0) | (d & 0x0F));

        // canal MIDI (bits 0..3) et mode du timbre (bits 4..5) de l'octet 51+12n
        if (get (16 + tb, v)) rec[51 + o] = (std::uint8_t) ((rec[51 + o] & 0xF0) | (v & 0x0F));
        if (get (128 + tb, v)) rec[51 + o] = (std::uint8_t) ((rec[51 + o] & ~0x30) | ((v & 3) << 4));

        // Program No. = 0 force le timbre a OFF (bit 4)
        if (get (0 + tb, v) && v == 0)
            rec[51 + o] = (std::uint8_t) (rec[51 + o] | 0x10);
    }

    int v = 0;
    if (get (136, v)) rec[11] = (std::uint8_t) (v & 0xFF);
    if (get (137, v)) rec[12] = (std::uint8_t) (v & 0xFF);
}

// Message complet : F0 42 3g 35 <fonction> [00] <donnees en 7 bits> F7
inline std::vector<std::uint8_t> buildKorgDump (int channel0, int func, bool withZero, const std::vector<std::uint8_t>& records)
{
    std::vector<std::uint8_t> out { 0xF0, 0x42, (std::uint8_t) (0x30 | (channel0 & 0x0F)), 0x35, (std::uint8_t) func };

    if (withZero)
        out.push_back (0x00);

    const auto packed = pack7 (records.data(), records.size());
    out.insert (out.end(), packed.begin(), packed.end());
    out.push_back (0xF7);
    return out;
}

// Decoupe un fichier .syx en messages complets F0 ... F7
inline std::vector<std::vector<std::uint8_t>> splitSyx (const std::uint8_t* d, size_t n)
{
    std::vector<std::vector<std::uint8_t>> out;
    size_t i = 0;

    while (i < n)
    {
        if (d[i] != 0xF0)
        {
            ++i;
            continue;
        }

        size_t j = i + 1;

        while (j < n && d[j] != 0xF7)
            ++j;

        if (j < n)
            out.emplace_back (d + i, d + j + 1);

        i = j + 1;
    }

    return out;
}
