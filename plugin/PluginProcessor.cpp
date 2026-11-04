#include "PluginProcessor.h"
#include "Facade.h"
#include <cmath>

using namespace juce;

namespace {
int toInt(const std::atomic<float>* a) { return static_cast<int>(std::lround(a->load())); }
}  // namespace

MaqProcessor::MaqProcessor()
    : AudioProcessor(BusesProperties()),
      apvts(*this, nullptr, "MAQ16", createLayout()) {
    for (auto& s : uiStep) s.store(0);
    folders.load(maq::FolderSettings::defaultSettingsFile());
    prefs.load(maq::Preferences::defaultFile());
    scaleRoot_ = apvts.getRawParameterValue("scaleroot");
    scaleType_ = apvts.getRawParameterValue("scaletype");
    scaleMode_ = apvts.getRawParameterValue("scalemode");

    for (int r = 0; r < maq::kRows; ++r) {
        auto& p = rp_[static_cast<size_t>(r)];
        p.event = apvts.getRawParameterValue(rowId("event", r));
        p.cc = apvts.getRawParameterValue(rowId("cc", r));
        p.chan = apvts.getRawParameterValue(rowId("chan", r));
        p.first = apvts.getRawParameterValue(rowId("first", r));
        p.last = apvts.getRawParameterValue(rowId("last", r));
        p.gate = apvts.getRawParameterValue(rowId("gate", r));
        p.noteLen = apvts.getRawParameterValue(rowId("notelen", r));
        p.mode = apvts.getRawParameterValue(rowId("mode", r));
        for (int s = 0; s < maq::kSteps; ++s) {
            p.pot[static_cast<size_t>(s)] = apvts.getRawParameterValue(potId(r, s));
            p.mute[static_cast<size_t>(s)] = apvts.getRawParameterValue(muteId(r, s));
        }
    }
}

AudioProcessorValueTreeState::ParameterLayout MaqProcessor::createLayout() {
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Réglages globaux
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"valnote", 1}, "Show note names", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"notefr", 1}, "Note names in French", true));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID{"preset", 1}, "Preset slot", 0, maq::kPresetSlots, 0));

    {   // Gamme : fondamentale, type, mode (aucune / verrouillage / quantification en sortie)
        StringArray roots, scales, modes;
        for (int pc = 0; pc < 12; ++pc) roots.add(String::fromUTF8(maq::pitchClassName(pc, false).c_str()));
        for (int t = 0; t < maq::kScaleCount; ++t)
            scales.add(String::fromUTF8(maq::scaleName(t, maq::Lang::English).c_str()));
        modes.add("Off");
        modes.add("Lock");
        modes.add("Output quantize");
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scaleroot", 1}, "Scale root", roots, 0));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scaletype", 1}, "Scale type", scales, 0));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scalemode", 1}, "Scale mode", modes, 0));
    }

    for (int r = 0; r < maq::kRows; ++r) {
        const String rn = "Row " + String(r + 1);
        // Valeurs par défaut du preset 0 du manuel : nA1, canaux 1/2/3, pas 1 à 16, g = 6, n = 1, Forward
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("event", r), 1}, rn + " Event", 0, maq::kEventCount - 1, 2));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("cc", r), 1}, rn + " Free CC number", 0, 127, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("chan", r), 1}, rn + " MIDI channel", 1, 16, r + 1));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("first", r), 1}, rn + " First step", 1, 16, 1));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("last", r), 1}, rn + " Last step", 1, 16, 16));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("gate", r), 1}, rn + " Gate time g", 1, 99, 6));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("notelen", r), 1}, rn + " Note length n", 1, 99, 1));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{rowId("mode", r), 1}, rn + " Mode", 0, maq::kModeCount - 1, 0));
        for (int s = 0; s < maq::kSteps; ++s) {
            const String sn = rn + " Step " + String(s + 1);
            layout.add(std::make_unique<AudioParameterInt>(ParameterID{potId(r, s), 1}, sn + " Knob", 0, 127, 0));
            layout.add(std::make_unique<AudioParameterBool>(ParameterID{muteId(r, s), 1}, sn + " Mute", false));
        }
    }
    return layout;
}

void MaqProcessor::syncEngineFromParams() {
    unit_.scaleRoot = toInt(scaleRoot_);
    unit_.scaleType = toInt(scaleType_);
    unit_.scaleMode = toInt(scaleMode_);

    for (int r = 0; r < maq::kRows; ++r) {
        const auto& p = rp_[static_cast<size_t>(r)];
        auto& cfg = unit_.rows[static_cast<size_t>(r)];

        maq::EventConfig e = maq::eventForIndex(toInt(p.event));
        if (e.kind == maq::EventKind::FreeCC) e.ccNumber = toInt(p.cc);
        cfg.event = e;

        cfg.channel = toInt(p.chan);
        cfg.first = toInt(p.first);
        cfg.last = toInt(p.last);
        cfg.gate = toInt(p.gate);
        cfg.noteLen = toInt(p.noteLen);
        cfg.mode = static_cast<maq::PlayMode>(toInt(p.mode));
        // Une rangée de vélocité s'applique à toutes les autres rangées (l'assignation fine viendra plus tard).
        cfg.velocityTargets = (e.kind == maq::EventKind::Velocity) ? (0x7 & ~(1 << r)) : 0;

        for (int s = 0; s < maq::kSteps; ++s) {
            cfg.pot[static_cast<size_t>(s)] = toInt(p.pot[static_cast<size_t>(s)]);
            cfg.mute[static_cast<size_t>(s)] = p.mute[static_cast<size_t>(s)]->load() > 0.5f;
        }
    }
}

void MaqProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = sampleRate;
    wasPlaying_ = false;
}

void MaqProcessor::emit(const std::vector<maq::MidiMsg>& msgs, int samplePos, MidiBuffer& out) {
    using T = maq::MidiMsg::Type;
    for (const auto& m : msgs) {
        const int ch = m.channel + 1;  // JUCE : canaux 1..16
        switch (m.type) {
            case T::NoteOn:
                out.addEvent(MidiMessage::noteOn(ch, m.data1, static_cast<uint8>(m.data2)), samplePos); break;
            case T::NoteOff:
                out.addEvent(MidiMessage::noteOff(ch, m.data1, static_cast<uint8>(0)), samplePos); break;
            case T::ControlChange:
                out.addEvent(MidiMessage::controllerEvent(ch, m.data1, m.data2), samplePos); break;
            case T::PitchBend:
                out.addEvent(MidiMessage::pitchWheel(ch, m.data1), samplePos); break;
            case T::ChannelPressure:
                out.addEvent(MidiMessage::channelPressureChange(ch, m.data1), samplePos); break;
            case T::ProgramChange:
                out.addEvent(MidiMessage::programChange(ch, m.data1), samplePos); break;
        }
    }
}

void MaqProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi) {
    if (learnActive.load()) {  // apprentissage : on met de côté les notes jouées au clavier
        for (const auto metadata : midi) {
            const MidiMessage m = metadata.getMessage();
            if (m.isNoteOn()) {
                int start1, size1, start2, size2;
                noteFifo_.prepareToWrite(1, start1, size1, start2, size2);
                if (size1 > 0) {
                    NoteIn n;
                    n.note = m.getNoteNumber();
                    n.velocity = m.getVelocity();
                    noteBuf_[static_cast<size_t>(start1)] = n;
                    noteFifo_.finishedWrite(1);
                }
            }
        }
    }

    midi.clear();  // plugin générateur : les notes entrantes ne sont pas transmises
    const int numSamples = buffer.getNumSamples();

    if (panicRequested.exchange(false)) {  // bouton Panic : Note Off + All Notes Off immédiats
        std::vector<maq::MidiMsg> panicMsgs;
        unit_.panic(panicMsgs);
        emit(panicMsgs, 0, midi);
    }

    syncEngineFromParams();

    Optional<AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead()) pos = ph->getPosition();

    if (pos.hasValue()) {
        const double bpm = pos->getBpm().orFallback(0.0);
        if (bpm > 0.0) uiBpm.store(bpm);
    }

    const bool playing = pos.hasValue() && pos->getIsPlaying();
    uiPlaying.store(playing);
    std::vector<maq::MidiMsg> out;

    if (!playing) {
        if (wasPlaying_) {
            unit_.stop(out);
            emit(out, 0, midi);
        }
        wasPlaying_ = false;
        return;
    }

    const double ppq = pos->getPpqPosition().orFallback(0.0);
    const double bpm = pos->getBpm().orFallback(120.0);
    const double samplesPerClock = sampleRate_ * 60.0 / (bpm * 24.0);  // 24 horloges par noire
    const double startClock = ppq * 24.0;
    const double endClock = startClock + numSamples / samplesPerClock;

    if (!wasPlaying_) {
        if (ppq < 1e-6) unit_.start(); else unit_.continuePlaying();
        nextClock_ = std::ceil(startClock - 1e-9);
    } else if (std::abs(startClock - expectedStart_) > 2.0) {
        nextClock_ = std::ceil(startClock - 1e-9);  // saut de position (boucle, déplacement du curseur)
    }
    wasPlaying_ = true;

    while (nextClock_ < endClock - 1e-9) {
        int offset = static_cast<int>((nextClock_ - startClock) * samplesPerClock);
        if (offset < 0) offset = 0;
        if (offset > numSamples - 1) offset = numSamples - 1;
        out.clear();
        unit_.tick(out);
        emit(out, offset, midi);
        nextClock_ += 1.0;
    }
    expectedStart_ = endClock;

    for (int r = 0; r < maq::kRows; ++r) uiStep[static_cast<size_t>(r)].store(unit_.currentStep(r));
}

// ------------------------------------------------------------------ Presets

void MaqProcessor::setParamValue(const String& id, int value) {
    if (auto* p = apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(value)));
        p->endChangeGesture();
    }
}

void MaqProcessor::storePreset(int slot) {
    if (slot < 1 || slot > maq::kPresetSlots) return;

    maq::Preset p;
    {
        const ScopedLock sl(bankLock_);
        p = bank_.slot(slot);  // on garde le nom déjà donné
    }

    for (int r = 0; r < maq::kRows; ++r) {
        const auto& src = rp_[static_cast<size_t>(r)];
        auto& dst = p.rows[static_cast<size_t>(r)];

        maq::EventConfig e = maq::eventForIndex(toInt(src.event));
        if (e.kind == maq::EventKind::FreeCC) e.ccNumber = toInt(src.cc);
        dst.event = e;
        dst.channel = toInt(src.chan);
        dst.first = toInt(src.first);
        dst.last = toInt(src.last);
        dst.gate = toInt(src.gate);
        dst.noteLen = toInt(src.noteLen);
        dst.mode = static_cast<maq::PlayMode>(toInt(src.mode));
        for (int s = 0; s < maq::kSteps; ++s) {
            dst.pot[static_cast<size_t>(s)] = toInt(src.pot[static_cast<size_t>(s)]);
            dst.mute[static_cast<size_t>(s)] = src.mute[static_cast<size_t>(s)]->load() > 0.5f;
        }
    }
    p.used = true;

    const ScopedLock sl(bankLock_);
    bank_.slot(slot) = p;
}

bool MaqProcessor::recallPreset(int slot) {
    if (slot < 1 || slot > maq::kPresetSlots) return false;

    maq::Preset p;
    {
        const ScopedLock sl(bankLock_);
        if (!bank_.slot(slot).used) return false;
        p = bank_.slot(slot);
    }

    for (int r = 0; r < maq::kRows; ++r) {
        const auto& c = p.rows[static_cast<size_t>(r)];
        setParamValue(rowId("event", r), maq::eventIndexFor(c.event));
        if (c.event.kind == maq::EventKind::FreeCC) setParamValue(rowId("cc", r), c.event.ccNumber);
        setParamValue(rowId("chan", r), c.channel);
        setParamValue(rowId("first", r), c.first);
        setParamValue(rowId("last", r), c.last);
        setParamValue(rowId("gate", r), c.gate);
        setParamValue(rowId("notelen", r), c.noteLen);
        setParamValue(rowId("mode", r), static_cast<int>(c.mode));
        for (int s = 0; s < maq::kSteps; ++s) {
            setParamValue(potId(r, s), c.pot[static_cast<size_t>(s)]);
            setParamValue(muteId(r, s), c.mute[static_cast<size_t>(s)] ? 1 : 0);
        }
    }
    return true;
}

bool MaqProcessor::presetUsed(int slot) const {
    if (slot < 1 || slot > maq::kPresetSlots) return false;
    const ScopedLock sl(bankLock_);
    return bank_.slot(slot).used;
}

String MaqProcessor::presetName(int slot) const {
    if (slot < 1 || slot > maq::kPresetSlots) return {};
    const ScopedLock sl(bankLock_);
    return String::fromUTF8(bank_.slot(slot).name.c_str());
}

void MaqProcessor::setPresetName(int slot, const String& name) {
    if (slot < 1 || slot > maq::kPresetSlots) return;
    const ScopedLock sl(bankLock_);
    bank_.slot(slot).name = name.toStdString();
}

int MaqProcessor::usedPresetCount() const {
    const ScopedLock sl(bankLock_);
    int n = 0;
    for (int i = 1; i <= maq::kPresetSlots; ++i)
        if (bank_.slot(i).used) ++n;
    return n;
}

// ------------------------------------------------------------------ Banque et dossiers

bool MaqProcessor::exportBank(const File& file) const {
    std::string text;
    {
        const ScopedLock sl(bankLock_);
        text = bank_.serialize();
    }
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(String::fromUTF8(text.c_str()));
}

bool MaqProcessor::importBank(const File& file) {
    if (!file.existsAsFile()) return false;
    maq::Bank imported;
    if (!imported.deserialize(file.loadFileAsString().toStdString())) return false;
    const ScopedLock sl(bankLock_);
    bank_ = imported;
    return true;
}

void MaqProcessor::saveFolderSettings() { folders.save(maq::FolderSettings::defaultSettingsFile()); }

bool MaqProcessor::popNote(NoteIn& out) {
    int start1, size1, start2, size2;
    noteFifo_.prepareToRead(1, start1, size1, start2, size2);
    if (size1 <= 0) return false;
    out = noteBuf_[static_cast<size_t>(start1)];
    noteFifo_.finishedRead(1);
    return true;
}

void MaqProcessor::savePreferences() { prefs.save(maq::Preferences::defaultFile()); }

// ------------------------------------------------------------------ État du projet

AudioProcessorEditor* MaqProcessor::createEditor() { return new MaqEditor(*this); }

void MaqProcessor::getStateInformation(MemoryBlock& dest) {
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) {
        std::string text;
        {
            const ScopedLock sl(bankLock_);
            text = bank_.serialize();
        }
        xml->createNewChildElement("BANK")->addTextElement(String::fromUTF8(text.c_str()));
        copyXmlToBinary(*xml, dest);
    }
}

void MaqProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (xml->hasTagName(apvts.state.getType())) {
            if (auto* bankXml = xml->getChildByName("BANK")) {
                maq::Bank restored;
                if (restored.deserialize(bankXml->getAllSubText().toStdString())) {
                    const ScopedLock sl(bankLock_);
                    bank_ = restored;
                }
                xml->removeChildElement(bankXml, true);
            }
            apvts.replaceState(ValueTree::fromXml(*xml));
        }
    }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MaqProcessor(); }
