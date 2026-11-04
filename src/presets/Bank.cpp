#include "Bank.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace maq {

namespace {
int clampSlot(int n) { return std::max(1, std::min(kPresetSlots, n)); }
}  // namespace

Bank::Bank() {
    for (int i = 0; i < kPresetSlots; ++i) {
        Preset& p = slots_[i];
        p.name = "";
        // Réglages du preset 0 du manuel : nA1, canaux 1/2/3, pas 1..16, g = 6, Forward.
        for (int r = 0; r < kRows; ++r) {
            p.rows[r].channel = r + 1;
        }
    }
}

Preset& Bank::slot(int n) { return slots_[clampSlot(n) - 1]; }
const Preset& Bank::slot(int n) const { return slots_[clampSlot(n) - 1]; }

std::string Bank::serialize() const {
    std::ostringstream o;
    o << "MAQBANK 1\n";
    for (int i = 0; i < kPresetSlots; ++i) {
        const Preset& p = slots_[i];
        if (!p.used) continue;
        o << "slot " << (i + 1) << "\n";
        o << "name " << p.name << "\n";
        o << "bpm " << p.bpm << "\n";
        for (int r = 0; r < kRows; ++r) {
            const RowConfig& c = p.rows[r];
            o << "row " << r
              << " kind " << static_cast<int>(c.event.kind)
              << " oct " << c.event.octaves
              << " cc " << c.event.ccNumber
              << " ch " << c.channel
              << " first " << c.first
              << " last " << c.last
              << " g " << c.gate
              << " n " << c.noteLen
              << " mode " << static_cast<int>(c.mode)
              << " trig " << (c.noteTriggered ? 1 : 0)
              << " vel " << c.velocityTargets << "\n";
            o << "pot";
            for (int s = 0; s < kSteps; ++s) o << ' ' << c.pot[s];
            o << "\nmute";
            for (int s = 0; s < kSteps; ++s) o << ' ' << (c.mute[s] ? 1 : 0);
            o << "\n";
        }
        o << "end\n";
    }
    return o.str();
}

bool Bank::deserialize(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("MAQBANK 1", 0) != 0) return false;
    Bank fresh;
    int cur = -1;
    int curRow = -1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "slot") {
            int n = 0; ls >> n;
            if (n < 1 || n > kPresetSlots) return false;
            cur = n - 1; curRow = -1;
            fresh.slots_[cur].used = true;
        } else if (cur < 0) {
            continue;
        } else if (key == "name") {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            fresh.slots_[cur].name = rest;
        } else if (key == "bpm") {
            ls >> fresh.slots_[cur].bpm;
        } else if (key == "row") {
            int r = -1; ls >> r;
            if (r < 0 || r >= kRows) return false;
            curRow = r;
            RowConfig& c = fresh.slots_[cur].rows[r];
            std::string k2;
            int v = 0;
            while (ls >> k2 >> v) {
                if (k2 == "kind") c.event.kind = static_cast<EventKind>(v);
                else if (k2 == "oct") c.event.octaves = v;
                else if (k2 == "cc") c.event.ccNumber = v;
                else if (k2 == "ch") c.channel = v;
                else if (k2 == "first") c.first = v;
                else if (k2 == "last") c.last = v;
                else if (k2 == "g") c.gate = v;
                else if (k2 == "n") c.noteLen = v;
                else if (k2 == "mode") c.mode = static_cast<PlayMode>(v);
                else if (k2 == "trig") c.noteTriggered = (v != 0);
                else if (k2 == "vel") c.velocityTargets = v;
            }
        } else if (key == "pot" && curRow >= 0) {
            RowConfig& c = fresh.slots_[cur].rows[curRow];
            for (int s = 0; s < kSteps; ++s) { int v = 0; ls >> v; c.pot[s] = v; }
        } else if (key == "mute" && curRow >= 0) {
            RowConfig& c = fresh.slots_[cur].rows[curRow];
            for (int s = 0; s < kSteps; ++s) { int v = 0; ls >> v; c.mute[s] = (v != 0); }
        }
    }
    *this = fresh;
    return true;
}

bool Bank::saveToFile(const std::filesystem::path& file) const {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << serialize();
    return static_cast<bool>(f);
}

bool Bank::loadFromFile(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::ostringstream buf;
    buf << f.rdbuf();
    return deserialize(buf.str());
}

}  // namespace maq
