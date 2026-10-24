#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DumpDecode.h"
#include <algorithm>

namespace
{
    // Debit MIDI DIN : 31250 bauds = environ 3125 octets/seconde.
    constexpr double kMidiBytesPerSecond = 3125.0;

    int makeKey (N364Target target, int drumIndex, int number)
    {
        return ((int) target << 16) | ((drumIndex + 1) << 8) | number;
    }

    // Programs de batterie de la banque GM (G129..G136) : numero de Program Change (table du manuel)
    constexpr int kGmDrumProgram[8] = { 0x00, 0x10, 0x19, 0x20, 0x28, 0x40, 0x18, 0x30 };

    struct MidiSlot { int msb; int lsb; int pc; };

    // (Bank Select MSB = mm, LSB = bb, Program Change = pp) d'apres le tableau "MIDI In (Hex)" du manuel
    MidiSlot slotToMidi (int bank, int number)
    {
        if (bank <= 3)
            return { 0, bank, juce::jlimit (0, 99, number) };

        if (number <= 128)
            return { 0x38, 0, juce::jlimit (0, 127, number - 1) };

        return { 0x3E, 0, kGmDrumProgram[juce::jlimit (0, 7, number - 129)] };
    }

    bool midiToSlot (int msb, int lsb, int pc, int& bank, int& number)
    {
        if (msb == 0 && lsb >= 0 && lsb <= 3)
        {
            bank = lsb;
            number = pc >= 100 ? pc - 100 : pc;
            return true;
        }

        if (msb == 0x38 || msb == 0x39)
        {
            bank = 4;
            number = pc + 1;
            return true;
        }

        if (msb == 0x3E)
        {
            bank = 4;
            number = pc <= 0x0F ? 129 : pc <= 0x17 ? 130 : pc == 0x18 ? 135 : pc == 0x19 ? 131
                   : pc <= 0x1F ? 135 : pc <= 0x27 ? 132 : pc <= 0x2F ? 133 : pc <= 0x37 ? 136
                   : pc <= 0x3F ? 129 : pc <= 0x47 ? 134 : 129;
            return true;
        }

        return false;
    }

    juce::String slotLabel (int bank, int number)
    {
        if (bank <= 3)
            return juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (number).paddedLeft ('0', 2);

        return "G" + juce::String (number).paddedLeft ('0', 3);
    }

    int slotIndex (bool combination, int bank, int number)
    {
        return ((combination ? 1 : 0) * 5 + bank) * 140 + number;
    }
}

//==============================================================================
N364Processor::N364Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "N364State", createLayout())
{
    const auto& table = getParamTable();
    raw.resize (table.size(), nullptr);
    params.resize (table.size(), nullptr);
    lastSent.reset (new std::atomic<int>[table.size()]);

    for (size_t i = 0; i < table.size(); ++i)
    {
        raw[i] = apvts.getRawParameterValue (table[i].id);
        params[i] = apvts.getParameter (table[i].id);
        lastSent[i].store (kUnknown);
        indexMap[makeKey (table[i].target, table[i].drumIndex, table[i].number)] = (int) i;
        idMap[table[i].id.toStdString()] = (int) i;
    }

    // Effets : "Param k" de chaque effet -> son indice k et le parametre "Type" correspondant
    effectK.assign (table.size(), -1);
    effectTypeIndex.assign (table.size(), -1);

    for (size_t i = 0; i < table.size(); ++i)
    {
        const auto& p = table[i];

        if (! p.name.startsWith ("Param ") || ! (p.group.endsWith ("Effet 1") || p.group.endsWith ("Effet 2")))
            continue;

        for (size_t j = 0; j < table.size(); ++j)
        {
            if (table[j].target == p.target && table[j].group == p.group && table[j].name == "Type")
            {
                effectK[i] = p.name.substring (6).getIntValue() - 1;
                effectTypeIndex[i] = (int) j;
                effectMembers[(int) j].push_back ((int) i);
                break;
            }
        }
    }

    liveNames.assign ((size_t) (2 * 5 * 140), juce::String());
    loadNamesCache();

    channelParam  = apvts.getRawParameterValue ("globalch");
    outModeParam  = apvts.getRawParameterValue ("outmode");
    targetParam   = apvts.getRawParameterValue ("target");
    kitParam      = apvts.getRawParameterValue ("drumkit");
    autoSyncParam = apvts.getRawParameterValue ("autosync");
    followParam   = apvts.getRawParameterValue ("follow");

    startTimer (20);   // 50 Hz : sert uniquement au mode "port direct"
}

N364Processor::~N364Processor()
{
    stopTimer();
    cancelPendingUpdate();
    saveNamesCache();

    if (midiIn != nullptr)
        midiIn->stop();

    midiIn.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout N364Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "globalch", 1 }, "Canal global", 1, 16, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "outmode", 1 }, "Sortie MIDI",
        juce::StringArray { "Hote (sortie MIDI du plugin)", "Port MIDI direct" }, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "target", 1 }, "Cible",
        juce::StringArray { "Program (Edit Program)", "Combination (Edit Combination)",
                            "Drum Kit (mode Global)", "Global (mode Global)" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "drumkit", 1 }, "Drum Kit",
        juce::StringArray { "Drum Kit A1", "Drum Kit A2", "Drum Kit B1", "Drum Kit B2" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "autosync", 1 }, "Synchro depuis le N364", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "follow", 1 }, "Suivre le N364 en temps reel", true));

    for (auto& p : getParamTable())
    {
        const juce::ParameterID pid { p.id, 1 };
        const juce::String name = p.group + " - " + p.name;

        if (! p.choices.isEmpty())
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, name, p.choices, p.defaultValue));
        else if (p.isToggle)
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, name, p.defaultValue != 0));
        else
            layout.add (std::make_unique<juce::AudioParameterInt> (pid, name, p.minValue, p.maxValue, p.defaultValue));
    }

    return layout;
}

//==============================================================================
// PARAMETER CHANGE : F0 42 3g 35 41 <no.LSB> <no.MSB> <val.LSB> <val.MSB> F7
//   g = canal global (0..15), valeur sur 14 bits (bits 15..13 = bit de signe,
//   donc complement a deux pour les valeurs negatives : NOTE 13 du manuel).
juce::MidiMessage N364Processor::makeParameterChange (int channel0, int number, int value)
{
    const int v14 = value & 0x3FFF;

    const juce::uint8 data[] =
    {
        0x42,
        (juce::uint8) (0x30 | (channel0 & 0x0F)),
        0x35,
        0x41,
        (juce::uint8) (number & 0x7F),
        (juce::uint8) ((number >> 7) & 0x7F),
        (juce::uint8) (v14 & 0x7F),
        (juce::uint8) ((v14 >> 7) & 0x7F)
    };

    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

// DRUM KIT PARAMETER CHANGE : F0 42 3g 35 53 <kit> <index> <param> <val.LSB> <val.MSB> F7
juce::MidiMessage N364Processor::makeDrumParameterChange (int channel0, int kit, int index, int number, int value)
{
    const int v14 = value & 0x3FFF;

    const juce::uint8 data[] =
    {
        0x42,
        (juce::uint8) (0x30 | (channel0 & 0x0F)),
        0x35,
        0x53,
        (juce::uint8) (kit & 0x03),
        (juce::uint8) (index & 0x7F),
        (juce::uint8) (number & 0x0F),
        (juce::uint8) (v14 & 0x7F),
        (juce::uint8) ((v14 >> 7) & 0x7F)
    };

    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

// GLOBAL DATA DUMP : F0 42 3g 35 51 00 <32 octets> F7
// Les 28 octets de donnees sont convertis en 7 bits (NOTE 2 du manuel).
juce::MidiMessage N364Processor::makeGlobalDump (int channel0, const std::array<int, 28>& data)
{
    std::vector<juce::uint8> out;
    out.push_back (0x42);
    out.push_back ((juce::uint8) (0x30 | (channel0 & 0x0F)));
    out.push_back (0x35);
    out.push_back (0x51);
    out.push_back (0x00);

    for (int set = 0; set < 4; ++set)
    {
        juce::uint8 msbs = 0;

        for (int i = 0; i < 7; ++i)
            if (data[(size_t) (set * 7 + i)] & 0x80)
                msbs = (juce::uint8) (msbs | (1 << i));

        out.push_back (msbs);

        for (int i = 0; i < 7; ++i)
            out.push_back ((juce::uint8) (data[(size_t) (set * 7 + i)] & 0x7F));
    }

    return juce::MidiMessage::createSysExMessage (out.data(), (int) out.size());
}

// Requetes : F0 42 3g 35 <fonction> [00] F7
juce::MidiMessage N364Processor::makeSimple (int channel0, int func, bool withZero)
{
    juce::uint8 data[5] = { 0x42, (juce::uint8) (0x30 | (channel0 & 0x0F)), 0x35, (juce::uint8) func, 0x00 };
    return juce::MidiMessage::createSysExMessage (data, withZero ? 5 : 4);
}

// PROGRAM WRITE REQUEST (0x11) / COMBINATION WRITE REQUEST (0x1A) : F0 42 3g 35 <fn> <banque> <numero> F7
juce::MidiMessage N364Processor::makeWriteRequest (int channel0, bool combination, int bank, int number)
{
    const juce::uint8 data[] =
    {
        0x42, (juce::uint8) (0x30 | (channel0 & 0x0F)), 0x35,
        (juce::uint8) (combination ? 0x1A : 0x11),
        (juce::uint8) (bank & 1),
        (juce::uint8) (number & 0x7F)
    };

    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

// MODE CHANGE (0x4E) : F0 42 3g 35 4E <mode> 00 F7 ; mode : 0 Combination, 2 Program, 6 Global (NOTE 12)
juce::MidiMessage N364Processor::makeModeChange (int channel0, int mode)
{
    const juce::uint8 data[] = { 0x42, (juce::uint8) (0x30 | (channel0 & 0x0F)), 0x35, 0x4E, (juce::uint8) (mode & 0x07), 0x00 };
    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

juce::MidiMessage N364Processor::buildMessage (const N364Param& p, int channel0, int kit, int value) const
{
    if (p.target == N364Target::drums)
        return makeDrumParameterChange (channel0, kit, p.drumIndex, p.number, value);

    return makeParameterChange (channel0, p.number, value);
}

//==============================================================================
// Parcourt les parametres de la cible active, et appelle emit(message) pour chaque
// valeur modifiee depuis le dernier envoi, dans la limite de maxBytes octets MIDI
// par appel (pour ne pas saturer le N364).
template <typename Fn>
int N364Processor::collectChanges (int maxBytes, Fn&& emit)
{
    int bytes = 0;
    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;
    const int target = juce::roundToInt (targetParam->load());
    const int kit = juce::roundToInt (kitParam->load());

    // Changement de cible ou de Drum Kit : base neuve, rien n'est envoye.
    const bool targetChanged = lastTarget.exchange (target) != target;
    const bool kitChanged = lastKit.exchange (kit) != kit;

    if (targetChanged || kitChanged)
        resetBaseline();

    // Messages ponctuels (requetes de dump, ...)
    {
        std::vector<juce::MidiMessage> pending;

        {
            const juce::SpinLock::ScopedLockType lock (queueLock);
            pending.swap (outgoing);
        }

        for (auto& m : pending)
        {
            emit (m);
            bytes += m.getRawDataSize();
        }
    }

    // Pendant l'envoi d'une banque entiere, aucun autre message ne doit s'intercaler
    if (juce::Time::getMillisecondCounterHiRes() < busyUntilMs.load())
        return bytes;

    const auto& table = getParamTable();

    if (forceRequested.exchange (false))
        for (size_t i = 0; i < table.size(); ++i)
            if ((int) table[i].target == target)
                lastSent[i].store (kForce);

    bool globalDirty = false;
    bool moreToSend = false;

    for (size_t i = 0; i < table.size(); ++i)
    {
        const auto& p = table[i];

        if ((int) p.target != target)
            continue;

        int value = juce::roundToInt (raw[i]->load()) + p.rawOffset;
        const int last = lastSent[i].load();

        if (effectK[i] >= 0)
        {
            const int type = juce::roundToInt (raw[(size_t) effectTypeIndex[i]]->load());
            const auto& info = getEffectInfo (type);

            if (effectK[i] >= info.count)
            {
                lastSent[i].store (value);   // parametre non utilise par cet effet : rien a envoyer
                continue;
            }

            value = juce::jlimit (info.params[effectK[i]].lo, info.params[effectK[i]].hi, value);
        }

        if (last == kUnknown)
        {
            // Premiere lecture : on memorise la valeur sans rien envoyer,
            // pour ne pas ecraser le son courant du N364 a l'ouverture du plugin.
            lastSent[i].store (value);
            continue;
        }

        if (last != kForce && value == last)
            continue;

        if (p.target == N364Target::global)
        {
            globalDirty = true;   // le Global part en un seul dump, plus bas
            continue;
        }

        if (bytes >= maxBytes)
        {
            moreToSend = true;
            break;
        }

        const juce::MidiMessage msg = buildMessage (p, channel0, kit, value);
        emit (msg);
        bytes += msg.getRawDataSize();
        lastSent[i].store (value);

        // Changement de type d'effet : les parametres sont reinterpretes, on les renvoie (bornes du nouveau type)
        const auto members = effectMembers.find ((int) i);

        if (members != effectMembers.end())
            for (int j : members->second)
                lastSent[(size_t) j].store (kForce);
    }

    if (globalDirty)
    {
        if (bytes < maxBytes)
        {
            std::array<int, 28> data {};   // octets reserves a 0

            for (size_t i = 0; i < table.size(); ++i)
                if (table[i].target == N364Target::global && table[i].number >= 0 && table[i].number < 28)
                    data[(size_t) table[i].number] = (juce::roundToInt (raw[i]->load()) + table[i].rawOffset) & 0xFF;

            const juce::MidiMessage msg = makeGlobalDump (channel0, data);
            emit (msg);
            bytes += msg.getRawDataSize();

            for (size_t i = 0; i < table.size(); ++i)
                if (table[i].target == N364Target::global)
                    lastSent[i].store (juce::roundToInt (raw[i]->load()) + table[i].rawOffset);
        }
        else
        {
            moreToSend = true;
        }
    }

    // Ecriture en memoire demandee : seulement une fois tous les reglages partis.
    const int pw = pendingWrite.load();

    if (pw >= 0 && ! moreToSend)
    {
        const juce::MidiMessage msg = makeWriteRequest (channel0, (pw >> 16) != 0, (pw >> 8) & 0xFF, pw & 0xFF);
        emit (msg);
        bytes += msg.getRawDataSize();
        pendingWrite.store (-1);
    }

    return bytes;
}

void N364Processor::resetBaseline()
{
    const auto count = getParamTable().size();

    for (size_t i = 0; i < count; ++i)
        lastSent[i].store (kUnknown);
}

//==============================================================================
void N364Processor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
}

bool N364Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void N364Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();   // le plugin ne produit aucun son

    // SysEx recus de l'hote (reponses du N364 si la sortie du N364 est routee vers le plugin)
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isSysEx() || message.isController() || message.isProgramChange())
            receive (message);
    }

    // En mode "port direct", c'est le timer qui envoie les messages.
    if (outModeParam->load() > 0.5f)
        return;

    const int numSamples = buffer.getNumSamples();
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int maxBytes = juce::jmax (12, (int) (numSamples / sr * kMidiBytesPerSecond));

    int position = 0;

    collectChanges (maxBytes, [&] (const juce::MidiMessage& m)
    {
        midi.addEvent (m, juce::jmin (position, numSamples - 1));
        position += juce::jmax (1, (int) (m.getRawDataSize() / kMidiBytesPerSecond * sr));
    });
}

void N364Processor::timerCallback()
{
    // Messages differes (selection de son : le N364 a besoin d'un court delai apres le changement de mode)
    {
        const juce::SpinLock::ScopedLockType lock (queueLock);

        for (auto it = scheduled.begin(); it != scheduled.end();)
        {
            if (--it->ticks <= 0)
            {
                outgoing.push_back (it->message);
                it = scheduled.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    advanceCrawl();
    advanceTask();

    // Suivi en temps reel : lecture du son courant peu apres un changement de son sur le N364
    if (dumpCountdown.load() > 0 && dumpCountdown.fetch_sub (1) == 1)
        requestCurrent();

    // Sauvegarde differee des noms lus sur le N364
    if (namesDirty && ++namesSaveCountdown >= 100)
        saveNamesCache();

    // Modes : 0 = hote (processBlock), 1 = port direct (ici)
    if (outModeParam->load() < 0.5f || midiOut == nullptr)
        return;

    collectChanges (62, [this] (const juce::MidiMessage& m)   // 62 octets = 20 ms de MIDI DIN
    {
        if (m.getRawDataSize() >= 1000)
        {
            // gros dump : envoye par le fil d'arriere-plan du port (l'interface reste utilisable)
            juce::MidiBuffer block;
            block.addEvent (m, 0);
            midiOut->sendBlockOfMessages (block, juce::Time::getMillisecondCounterHiRes() + 5.0, 44100.0);
        }
        else
        {
            midiOut->sendMessageNow (m);
        }
    });
}

//==============================================================================
// Envoi

void N364Processor::sendAll()
{
    forceRequested.store (true);
}

void N364Processor::setTarget (N364Target target)
{
    if (auto* p = apvts.getParameter ("target"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) target));
}

void N364Processor::writeToMemory (bool combination, int bank, int number)
{
    setTarget (combination ? N364Target::combination : N364Target::program);
    pendingWrite.store ((combination ? (1 << 16) : 0) | ((bank & 1) << 8) | (number & 0x7F));
    forceRequested.store (true);
    setStatus ("Envoi des reglages puis ecriture en memoire du N364...");
}

//==============================================================================
// Lecture depuis le N364

void N364Processor::requestFromSynth (Request request)
{
    const bool direct = outModeParam->load() > 0.5f;

    if (direct && (midiOut == nullptr || midiIn == nullptr))
    {
        setStatus ("Choisis d'abord le port MIDI de sortie ET l'entree MIDI du N364.");
        return;
    }

    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;
    int func = 0, expect = 0;
    bool withZero = false;

    switch (request)
    {
        case Request::program:         func = 0x10; expect = 0x40; break;
        case Request::combination:     func = 0x19; expect = 0x49; break;
        case Request::global:          func = 0x0E; expect = 0x51; withZero = true; break;
        case Request::drums:           func = 0x0D; expect = 0x52; withZero = true; break;
        case Request::allPrograms:     func = 0x1C; expect = 0x4C; withZero = true; break;
        case Request::allCombinations: func = 0x1D; expect = 0x4D; withZero = true; break;
    }

    awaiting.store (expect);

    {
        const juce::SpinLock::ScopedLockType lock (queueLock);
        outgoing.push_back (makeSimple (channel0, func, withZero));
    }

    setStatus ("Demande envoyee au N364, en attente de la reponse...");
}

void N364Processor::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isSysEx() || message.isController() || message.isProgramChange())
        receive (message);
}

void N364Processor::receive (const juce::MidiMessage& message)
{
    {
        const juce::SpinLock::ScopedLockType lock (queueLock);
        incoming.push_back (message);
    }

    triggerAsyncUpdate();
}

void N364Processor::handleAsyncUpdate()
{
    std::vector<juce::MidiMessage> messages;

    {
        const juce::SpinLock::ScopedLockType lock (queueLock);
        messages.swap (incoming);
    }

    for (auto& m : messages)
        processIncoming (m);
}

void N364Processor::processIncoming (const juce::MidiMessage& message)
{
    if (! message.isSysEx())
    {
        processChannelMessage (message);
        return;
    }

    const juce::uint8* d = message.getSysExData();
    const int size = message.getSysExDataSize();

    if (size < 4 || d[0] != 0x42 || (d[1] & 0xF0) != 0x30 || d[2] != 0x35)
        return;   // pas un message du N364

    const int func = d[3];
    const juce::uint8* payload = d + 4;
    const int n = size - 4;

    const bool autoSync = autoSyncParam->load() > 0.5f;
    const int expected = awaiting.load();
    const auto accept = [&] (int f) { return autoSync || expected == f; };
    const auto target = (N364Target) juce::roundToInt (targetParam->load());

    switch (func)
    {
        case 0x41:   // Parameter Change emis par la face avant du N364
        {
            if (! autoSync || n < 4 || (target != N364Target::program && target != N364Target::combination))
                return;

            const int number = payload[0] | (payload[1] << 7);
            int value = payload[2] | (payload[3] << 7);

            if (value & 0x2000)
                value -= 0x4000;

            const int index = indexFor (target, number, -1);

            if (index >= 0)
                applySent ((size_t) index, value);

            return;
        }

        case 0x53:   // Drum Kit Parameter Change
        {
            if (! autoSync || n < 5)
                return;

            const int kit = payload[0] & 3;
            const int drumIndex = payload[1];
            const int number = payload[2] & 0x0F;
            int value = payload[3] | (payload[4] << 7);

            if (value & 0x2000)
                value -= 0x4000;

            if (kit != juce::roundToInt (kitParam->load()))
                if (auto* kp = apvts.getParameter ("drumkit"))
                    kp->setValueNotifyingHost (kp->convertTo0to1 ((float) kit));

            const int index = indexFor (N364Target::drums, number, drumIndex);

            if (index >= 0)
                applySent ((size_t) index, value);

            return;
        }

        case 0x42:   // MODE DATA : mode courant du N364
        {
            if (n < 1)
                return;

            const int mode = payload[0] & 0x07;
            synthMode.store (mode);

            // le son suivi est une Combination en mode 0/1, un Program sinon
            const int code = slotCode.load();

            if (code >= 0)
            {
                const bool combination = (mode == 0 || mode == 1);
                const int bank = (code >> 8) & 0xFF;

                if (! (combination && bank > 3))
                    slotCode.store ((combination ? (1 << 16) : 0) | (code & 0xFFFF));
            }

            return;
        }

        case 0x40:   // Program (tampon courant)
        {
            if (! accept (0x40))
                return;

            const auto bytes = unpack7 (payload, (size_t) n);

            if (bytes.size() < 164)
                return;

            if (task.active && task.state == 1 && ! task.cur.combination)
            {
                task.regs[(size_t) task.cur.reg].assign (bytes.begin(), bytes.begin() + 164);
                task.state = 0;
                task.wait = 2;
                return;
            }

            lastRawProgram.assign (bytes.begin(), bytes.begin() + 164);
            const auto name = decodeName (bytes.data());

            if (crawl.active && ! crawl.combination && crawl.phase == 2)
            {
                // import son par son : on enregistre un preset, sans toucher aux potards
                const int number = crawl.bank == 4 ? crawl.index + 1 : crawl.index;

                if (writePresetFromDecoded (N364Target::program, slotLabel (crawl.bank, number) + " - " + name,
                                            decodeProgram (bytes.data(), bytes.size()), bytes.data(), 164))
                    ++crawl.saved;

                storeLiveName (false, crawl.bank, number, name);
                crawl.gotDump = true;
                return;
            }

            applyDecoded (N364Target::program, decodeProgram (bytes.data(), bytes.size()), 1);

            {
                const juce::ScopedLock lock (textLock);
                lastReadName = name;
            }

            const auto slot = getCurrentSlot();

            if (slot.bank >= 0 && ! slot.combination)
                storeLiveName (false, slot.bank, slot.number, name);

            setStatus ("Programme lu depuis le N364 : " + name);
            awaiting.store (0);
            return;
        }

        case 0x49:   // Combination (tampon courant)
        {
            if (! accept (0x49))
                return;

            const auto bytes = unpack7 (payload, (size_t) n);

            if (bytes.size() < 136)
                return;

            if (task.active && task.state == 1 && task.cur.combination)
            {
                task.regs[(size_t) task.cur.reg].assign (bytes.begin(), bytes.begin() + 136);
                task.state = 0;
                task.wait = 2;
                return;
            }

            lastRawCombination.assign (bytes.begin(), bytes.begin() + 136);
            const auto name = decodeName (bytes.data());

            if (crawl.active && crawl.combination && crawl.phase == 2)
            {
                if (writePresetFromDecoded (N364Target::combination, slotLabel (crawl.bank, crawl.index) + " - " + name,
                                            decodeCombination (bytes.data(), bytes.size()), bytes.data(), 136))
                    ++crawl.saved;

                storeLiveName (true, crawl.bank, crawl.index, name);
                crawl.gotDump = true;
                return;
            }

            applyDecoded (N364Target::combination, decodeCombination (bytes.data(), bytes.size()), 1);

            {
                const juce::ScopedLock lock (textLock);
                lastReadName = name;
            }

            const auto slot = getCurrentSlot();

            if (slot.bank >= 0 && slot.combination)
                storeLiveName (true, slot.bank, slot.number, name);

            setStatus ("Combination lue depuis le N364 : " + name);
            awaiting.store (0);
            return;
        }

        case 0x51:   // Global
        {
            if (! accept (0x51) || n < 2)
                return;

            const auto bytes = unpack7 (payload + 1, (size_t) (n - 1));
            applyDecoded (N364Target::global, decodeGlobal (bytes.data(), bytes.size()), 1);
            setStatus ("Global lu depuis le N364.");
            awaiting.store (0);
            return;
        }

        case 0x52:   // Drum Kits (les 4 kits ; on prend le kit choisi)
        {
            if (! accept (0x52) || n < 2)
                return;

            const auto bytes = unpack7 (payload + 1, (size_t) (n - 1));
            const int kit = juce::roundToInt (kitParam->load());
            applyDecoded (N364Target::drums, decodeDrums (bytes.data(), bytes.size(), kit), 100);
            setStatus ("Drum Kit lu depuis le N364.");
            awaiting.store (0);
            return;
        }

        case 0x4C:   // toute la banque de Programs (A et B)
        case 0x4D:   // toute la banque de Combinations
        {
            if (n < 2)
                return;

            if (task.active && task.state == 4 && (func == 0x4D) == task.cur.combination)
            {
                handleBankBackup (message, unpack7 (payload + 1, (size_t) (n - 1)));
                return;
            }

            if (expected != func)
                return;

            importBank (func == 0x4D, unpack7 (payload + 1, (size_t) (n - 1)));
            awaiting.store (0);
            return;
        }

        case 0x23:
            if (task.active && (task.state == 2 || task.state == 5))
            {
                task.state = 0;
                task.wait = 3;
                return;
            }

            setStatus ("Le N364 a bien recu les donnees.");
            return;

        case 0x24:
            if (task.active && (task.state == 2 || task.state == 5))
            {
                failTask ("le N364 a refuse les donnees (erreur de reception).");
                return;
            }

            setStatus ("Le N364 signale une erreur de reception de donnees.");
            return;

        case 0x21:
            if (task.active && task.state == 3)
            {
                // le son vient d'etre ecrit : on retient son nom pour l'affichage des banques
                if (task.cur.reg >= 0 && task.regs[(size_t) task.cur.reg].size() >= 10)
                    storeLiveName (task.cur.combination, task.cur.bank, task.cur.number,
                                   decodeName (task.regs[(size_t) task.cur.reg].data()));

                task.state = 0;
                task.wait = 3;
                return;
            }

            setStatus ("Ecriture terminee dans la memoire du N364.");
            return;

        case 0x22:
            if (task.active && task.state == 3)
            {
                failTask ("erreur d'ecriture du N364 (memoire protegee ?).");
                return;
            }

            setStatus ("Erreur d'ecriture du N364 (memoire protegee ?).");
            return;
        case 0x26: setStatus ("Le N364 signale une erreur de format MIDI."); return;
        default: return;
    }
}

void N364Processor::applyDecoded (N364Target target, const DecodedValues& values, int drumIndexScale)
{
    for (auto& kv : values)
    {
        const int number = drumIndexScale > 1 ? kv.first % drumIndexScale : kv.first;
        const int drumIndex = drumIndexScale > 1 ? kv.first / drumIndexScale : -1;
        const int index = indexFor (target, number, drumIndex);

        if (index >= 0)
            applySent ((size_t) index, kv.second);
    }
}

bool N364Processor::writePresetFromDecoded (N364Target target, const juce::String& label, const DecodedValues& decoded,
                                            const std::uint8_t* rawData, size_t rawSize)
{
    const auto folder = getPresetFolder (target);

    if (! folder.createDirectory())
        return false;

    std::unordered_map<int, int> byNumber;
    for (auto& kv : decoded)
        byNumber[kv.first] = kv.second;

    juce::XmlElement root ("N364Preset");
    root.setAttribute ("type", (int) target);
    root.setAttribute ("name", label);

    if (rawData != nullptr && rawSize > 0)
        root.setAttribute ("raw", toHex (std::vector<std::uint8_t> (rawData, rawData + rawSize)));

    for (auto& p : getParamTable())
    {
        if (p.target != target)
            continue;

        const auto it = byNumber.find (p.number);
        const int value = it != byNumber.end() ? toPluginValue (p, it->second) : p.defaultValue;

        auto* e = root.createNewChildElement ("P");
        e->setAttribute ("id", p.id);
        e->setAttribute ("v", value);
    }

    if (! root.writeTo (folder.getChildFile (juce::File::createLegalFileName (label) + ".n364preset")))
        return false;

    ++presetVersion;
    return true;
}

void N364Processor::importBank (bool combination, const std::vector<std::uint8_t>& bytes)
{
    const size_t recordSize = combination ? 136 : 164;
    const size_t count = juce::jmin ((size_t) 200, bytes.size() / recordSize);
    const auto target = combination ? N364Target::combination : N364Target::program;

    if (count == 0)
    {
        setStatus ("Banque recue, mais vide ou incomplete.");
        return;
    }

    int saved = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const std::uint8_t* b = bytes.data() + i * recordSize;
        const auto decoded = combination ? decodeCombination (b, recordSize) : decodeProgram (b, recordSize);
        const int bank = i < 100 ? 0 : 1;
        const int number = (int) (i % 100);
        const auto name = decodeName (b);

        if (writePresetFromDecoded (target, slotLabel (bank, number) + " - " + name, decoded, b, recordSize))
            ++saved;

        storeLiveName (combination, bank, number, name);
    }

    setStatus ("Banque importee : " + juce::String (saved) + " presets enregistres dans la bibliotheque.");
}

//==============================================================================
// Banques : choix d'un son, suivi en temps reel, import son par son, noms

void N364Processor::queueSlotMessages (bool combination, int bank, int number, bool withModeChange)
{
    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;
    const auto s = slotToMidi (bank, number);
    int delay = 1;

    const juce::SpinLock::ScopedLockType lock (queueLock);

    if (withModeChange)
    {
        outgoing.push_back (makeModeChange (channel0, combination ? 0 : 2));
        delay = 6;   // ~120 ms : le N364 change de mode avant de recevoir la banque et le programme
    }

    scheduled.push_back ({ delay, juce::MidiMessage::controllerEvent (channel0 + 1, 0, s.msb) });
    scheduled.push_back ({ delay, juce::MidiMessage::controllerEvent (channel0 + 1, 32, s.lsb) });
    scheduled.push_back ({ delay + 1, juce::MidiMessage::programChange (channel0 + 1, s.pc) });
}

void N364Processor::selectSlot (bool combination, int bank, int number)
{
    if ((combination && bank > 3) || bank < 0 || bank > 4)
        return;

    if (outModeParam->load() > 0.5f && midiOut == nullptr)
    {
        setStatus ("Choisis d'abord le port MIDI de sortie.");
        return;
    }

    queueSlotMessages (combination, bank, number, true);
    synthMode.store (combination ? 0 : 2);
    slotCode.store ((combination ? (1 << 16) : 0) | (bank << 8) | number);
    ++slotVersion;
    setStatus (juce::String (combination ? "Combination " : "Programme ") + slotLabel (bank, number) + " envoye au N364...");
    dumpCountdown.store (35);   // 0,7 s : puis lecture du son pour mettre les potards a jour
}

N364Processor::Slot N364Processor::getCurrentSlot() const
{
    const int code = slotCode.load();
    Slot s;

    if (code >= 0)
    {
        s.combination = (code >> 16) != 0;
        s.bank = (code >> 8) & 0xFF;
        s.number = code & 0xFF;
    }

    return s;
}

void N364Processor::requestCurrent()
{
    const int mode = synthMode.load();
    const bool combination = (mode == 0 || mode == 1) || (mode < 0 && getCurrentSlot().combination);
    requestFromSynth (combination ? Request::combination : Request::program);
}

juce::String N364Processor::getSlotName (bool combination, int bank, int number, bool* isLive) const
{
    if (isLive != nullptr)
        *isLive = false;

    if (bank < 0 || bank > 4 || number < 0 || number >= 140)
        return {};

    {
        const juce::ScopedLock lock (nameLock);
        const auto& live = liveNames[(size_t) slotIndex (combination, bank, number)];

        if (live.isNotEmpty())
        {
            if (isLive != nullptr)
                *isLive = true;

            return live;
        }
    }

    if (combination)
        return (bank <= 3 && number < 100) ? juce::String (n364factory::combinations[bank][number].name) : juce::String();

    if (bank <= 3)
        return number < 100 ? juce::String (n364factory::programs[bank][number].name) : juce::String();

    return (number >= 1 && number <= 136) ? juce::String (n364factory::gm[number - 1].name) : juce::String();
}

void N364Processor::storeLiveName (bool combination, int bank, int number, const juce::String& name)
{
    if (name.isEmpty() || bank < 0 || bank > 4 || number < 0 || number >= 140)
        return;

    {
        const juce::ScopedLock lock (nameLock);
        auto& slot = liveNames[(size_t) slotIndex (combination, bank, number)];

        if (slot == name)
            return;

        slot = name;
    }

    namesDirty = true;
    ++slotVersion;
}

juce::File N364Processor::getNamesFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("N364Editor")
               .getChildFile ("names.xml");
}

void N364Processor::loadNamesCache()
{
    const auto xml = juce::XmlDocument::parse (getNamesFile());

    if (xml == nullptr || ! xml->hasTagName ("N364Names"))
        return;

    const juce::ScopedLock lock (nameLock);

    for (auto* e : xml->getChildIterator())
    {
        const int c = e->getIntAttribute ("c"), b = e->getIntAttribute ("b"), n = e->getIntAttribute ("n");

        if (c >= 0 && c <= 1 && b >= 0 && b <= 4 && n >= 0 && n < 140)
            liveNames[(size_t) slotIndex (c != 0, b, n)] = e->getStringAttribute ("v");
    }
}

void N364Processor::saveNamesCache()
{
    namesDirty = false;
    namesSaveCountdown = 0;

    juce::XmlElement root ("N364Names");

    {
        const juce::ScopedLock lock (nameLock);

        for (int c = 0; c <= 1; ++c)
            for (int b = 0; b <= 4; ++b)
                for (int n = 0; n < 140; ++n)
                {
                    const auto& v = liveNames[(size_t) slotIndex (c != 0, b, n)];

                    if (v.isNotEmpty())
                    {
                        auto* e = root.createNewChildElement ("N");
                        e->setAttribute ("c", c);
                        e->setAttribute ("b", b);
                        e->setAttribute ("n", n);
                        e->setAttribute ("v", v);
                    }
                }
    }

    getNamesFile().getParentDirectory().createDirectory();
    root.writeTo (getNamesFile());
}

void N364Processor::processChannelMessage (const juce::MidiMessage& m)
{
    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;

    if (m.getChannel() - 1 != channel0)
        return;

    if (m.isController())
    {
        if (m.getControllerNumber() == 0)
            bankMsb.store (m.getControllerValue());
        else if (m.getControllerNumber() == 32)
            bankLsb.store (m.getControllerValue());

        return;
    }

    if (! m.isProgramChange() || crawl.active || task.active)
        return;

    int bank = 0, number = 0;

    if (! midiToSlot (bankMsb.load(), bankLsb.load(), m.getProgramChangeNumber(), bank, number))
        return;

    const int mode = synthMode.load();
    const bool combination = (mode == 0 || mode == 1);

    if (combination && bank > 3)
        return;

    slotCode.store ((combination ? (1 << 16) : 0) | (bank << 8) | number);
    ++slotVersion;
    setStatus ("N364 : " + juce::String (combination ? "Combination " : "Programme ") + slotLabel (bank, number)
               + "  " + getSlotName (combination, bank, number));

    if (followParam->load() > 0.5f)
    {
        // on demande le mode (Program / Combination) puis, un instant apres, le son lui-meme
        const int ch = channel0;
        {
            const juce::SpinLock::ScopedLockType lock (queueLock);
            outgoing.push_back (makeSimple (ch, 0x12, false));
        }

        dumpCountdown.store (12);
    }
}

void N364Processor::startCrawl (bool combination, int bank)
{
    if ((combination && bank > 3) || bank < 0 || bank > 4)
        return;

    if (outModeParam->load() > 0.5f && (midiOut == nullptr || midiIn == nullptr))
    {
        setStatus ("Choisis d'abord le port MIDI de sortie ET l'entree MIDI du N364.");
        return;
    }

    crawl = Crawl();
    crawl.active = true;
    crawl.combination = combination;
    crawl.bank = bank;
    crawl.count = (!combination && bank == 4) ? 136 : 100;
    setStatus ("Import de la banque " + juce::String (bank == 4 ? "GM" : juce::String::charToString ((juce::juce_wchar) ('A' + bank)))
               + " : demarrage...");
}

void N364Processor::cancelCrawl()
{
    if (crawl.active)
    {
        crawl.active = false;
        setStatus ("Import annule (" + juce::String (crawl.saved) + " presets enregistres).");
    }
}

void N364Processor::advanceCrawl()
{
    if (! crawl.active)
        return;

    if (crawl.phase == 2 && crawl.gotDump)
        crawl.wait = 0;
    else if (crawl.wait > 0)
    {
        --crawl.wait;
        return;
    }

    const int number = (! crawl.combination && crawl.bank == 4) ? crawl.index + 1 : crawl.index;
    const juce::String progress = slotLabel (crawl.bank, number) + "  ("
                                  + juce::String (crawl.index + 1) + "/" + juce::String (crawl.count) + ")";

    switch (crawl.phase)
    {
        case 0:
            queueSlotMessages (crawl.combination, crawl.bank, number, crawl.index == 0);
            synthMode.store (crawl.combination ? 0 : 2);
            crawl.phase = 1;
            crawl.wait = crawl.index == 0 ? 20 : 12;   // laisser le N364 changer de son
            setStatus ("Import en cours : " + progress);
            break;

        case 1:
            crawl.gotDump = false;
            requestFromSynth (crawl.combination ? Request::combination : Request::program);
            setStatus ("Import en cours : " + progress);
            crawl.phase = 2;
            crawl.wait = 40;   // 0,8 s d'attente maximum
            break;

        default:
            if (! crawl.gotDump)
                ++crawl.failed;

            if (++crawl.index >= crawl.count)
            {
                crawl.active = false;
                ++presetVersion;
                setStatus ("Import termine : " + juce::String (crawl.saved) + " presets enregistres"
                           + (crawl.failed > 0 ? ", " + juce::String (crawl.failed) + " sans reponse." : "."));
            }
            else
            {
                crawl.phase = 0;
                crawl.wait = 0;
            }
            break;
    }
}

//==============================================================================
// Operations sur les sons du N364 (lecture, ecriture, copie, echange, renommage) et echanges SysEx

juce::String N364Processor::toHex (const std::vector<std::uint8_t>& bytes)
{
    return juce::String::toHexString (bytes.data(), (int) bytes.size(), 0);
}

std::vector<std::uint8_t> N364Processor::fromHex (const juce::String& hex)
{
    juce::MemoryBlock block;
    block.loadFromHexString (hex);
    const auto* p = static_cast<const std::uint8_t*> (block.getData());
    return std::vector<std::uint8_t> (p, p + block.getSize());
}

juce::File N364Processor::getBackupFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("N364Editor")
               .getChildFile ("Backups");
}

std::unordered_map<int, int> N364Processor::currentSentValues (N364Target target) const
{
    std::unordered_map<int, int> values;
    const auto& table = getParamTable();

    for (size_t i = 0; i < table.size(); ++i)
        if (table[i].target == target)
            values[table[i].number] = juce::roundToInt (raw[i]->load()) + table[i].rawOffset;

    return values;
}

void N364Processor::pushRaw (const std::vector<std::uint8_t>& bytes)
{
    const juce::SpinLock::ScopedLockType lock (queueLock);
    outgoing.push_back (juce::MidiMessage (bytes.data(), (int) bytes.size()));
}

N364Processor::Op N364Processor::makeOp (Op::Type type, bool combination, int bank, int number, int reg,
                                         int ticks, const juce::String& text)
{
    Op op;
    op.type = type;
    op.combination = combination;
    op.bank = bank;
    op.number = number;
    op.reg = reg;
    op.ticks = ticks;
    op.text = text;
    return op;
}

bool N364Processor::startTask (const juce::String& title, std::deque<Op> ops, const juce::File& exportFile,
                               const std::vector<std::uint8_t>& reg0)
{
    if (task.active || crawl.active)
    {
        setStatus ("Une operation est deja en cours.");
        return false;
    }

    if (outModeParam->load() > 0.5f && (midiOut == nullptr || midiIn == nullptr))
    {
        setStatus ("Choisis d'abord le port MIDI de sortie ET l'entree MIDI du N364.");
        return false;
    }

    task = Task();
    task.active = true;
    task.title = title;
    task.ops = std::move (ops);
    task.exportFile = exportFile;
    task.regs[0] = reg0;
    setStatus (title + " : demarrage...");
    return true;
}

void N364Processor::cancelTask()
{
    if (task.active)
    {
        task.active = false;
        setStatus (task.title + " : annule.");
    }
}

void N364Processor::failTask (const juce::String& why)
{
    task.active = false;
    setStatus (task.title + " : interrompu, " + why);
}

void N364Processor::finishTask()
{
    task.active = false;
    ++slotVersion;
    setStatus (task.title + " : termine.");
}

void N364Processor::advanceTask()
{
    if (! task.active)
        return;

    if (task.wait > 0)
    {
        --task.wait;
        return;
    }

    if (task.state != 0)
    {
        if (--task.timeout <= 0)
            failTask ("pas de reponse du N364 (delai depasse).");

        return;
    }

    if (task.ops.empty())
    {
        finishTask();
        return;
    }

    const Op op = task.ops.front();
    task.ops.pop_front();
    task.cur = op;

    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;
    const size_t recordSize = op.combination ? 136 : 164;

    switch (op.type)
    {
        case Op::mode:
        {
            const juce::SpinLock::ScopedLockType lock (queueLock);
            outgoing.push_back (makeModeChange (channel0, op.combination ? 0 : 2));
            synthMode.store (op.combination ? 0 : 2);
            task.wait = 12;
            break;
        }

        case Op::select:
            queueSlotMessages (op.combination, op.bank, op.number, op.ticks != 0);
            synthMode.store (op.combination ? 0 : 2);
            slotCode.store ((op.combination ? (1 << 16) : 0) | (op.bank << 8) | op.number);
            ++slotVersion;
            task.wait = op.ticks != 0 ? 25 : 12;
            break;

        case Op::wait:
            task.wait = op.ticks;
            break;

        case Op::read:
        {
            const juce::SpinLock::ScopedLockType lock (queueLock);
            outgoing.push_back (makeSimple (channel0, op.combination ? 0x19 : 0x10, false));
            task.state = 1;
            task.timeout = 75;   // 1,5 s
            break;
        }

        case Op::patchName:
        {
            auto& reg = task.regs[(size_t) op.reg];

            if (reg.size() >= 10)
                setRecordName (reg.data(), op.text.toStdString());

            break;
        }

        case Op::send:
        {
            const auto& reg = task.regs[(size_t) op.reg];

            if (reg.size() < recordSize)
            {
                failTask ("donnees du son incompletes.");
                return;
            }

            pushRaw (buildKorgDump (channel0, op.combination ? 0x49 : 0x40, false,
                                    std::vector<std::uint8_t> (reg.begin(), reg.begin() + (long) recordSize)));
            task.state = 2;
            task.timeout = 75;
            break;
        }

        case Op::write:
        {
            const juce::SpinLock::ScopedLockType lock (queueLock);
            outgoing.push_back (makeWriteRequest (channel0, op.combination, op.bank, op.number));
            task.state = 3;
            task.timeout = 100;   // 2 s
            break;
        }

        case Op::backupBank:
        {
            const juce::SpinLock::ScopedLockType lock (queueLock);
            outgoing.push_back (makeSimple (channel0, op.combination ? 0x1D : 0x1C, true));
            task.state = 4;
            task.timeout = 500;   // 10 s
            break;
        }

        case Op::sendBank:
        {
            const auto& reg = task.regs[(size_t) op.reg];

            if (reg.size() < 200 * recordSize)
            {
                failTask ("donnees de la banque incompletes.");
                return;
            }

            const auto bytes = buildKorgDump (channel0, op.combination ? 0x4D : 0x4C, true,
                                              std::vector<std::uint8_t> (reg.begin(), reg.begin() + (long) (200 * recordSize)));

            // duree d'envoi estimee au debit du MIDI DIN (3125 octets/s) + marge : aucun autre message pendant ce temps
            const double seconds = (double) bytes.size() / 3125.0;
            busyUntilMs.store (juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0 + 1500.0);
            pushRaw (bytes);
            task.state = 5;
            task.timeout = (int) (seconds * 50.0) + 400;   // en ticks de 20 ms
            setStatus (task.title + " : envoi de la banque en cours (" + juce::String ((int) seconds + 1) + " s environ)...");
            break;
        }
    }
}

void N364Processor::handleBankBackup (const juce::MidiMessage& message, const std::vector<std::uint8_t>& records)
{
    const size_t recordSize = task.cur.combination ? 136 : 164;

    if (records.size() < 200 * recordSize)
    {
        failTask ("banque recue incomplete.");
        return;
    }

    juce::File file = task.exportFile;

    if (file == juce::File())
    {
        const auto folder = getBackupFolder();
        folder.createDirectory();
        file = folder.getChildFile (juce::String ("N364_sauvegarde_") + (task.cur.combination ? "combinations_" : "programmes_")
                                    + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".syx");
    }

    if (! file.replaceWithData (message.getRawData(), (size_t) message.getRawDataSize()))
    {
        failTask ("impossible d'ecrire le fichier " + file.getFullPathName());
        return;
    }

    task.state = 0;
    task.wait = 3;
    setStatus (task.title + " : sauvegarde enregistree dans " + file.getFileName());
}

void N364Processor::renameOnSynth (bool combination, int bank, int number, const juce::String& newName)
{
    const auto name = newName.trim().substring (0, 10);

    if (bank < 0 || bank > 1 || number < 0 || number > 99 || name.isEmpty())
    {
        setStatus ("Renommer : choisis un son des banques A ou B et saisis un nom (10 caracteres maximum).");
        return;
    }

    std::deque<Op> ops;
    ops.push_back (makeOp (Op::select, combination, bank, number, 0, 1));
    ops.push_back (makeOp (Op::read, combination, bank, number, 0));
    ops.push_back (makeOp (Op::patchName, combination, bank, number, 0, 0, name));
    ops.push_back (makeOp (Op::send, combination, bank, number, 0));
    ops.push_back (makeOp (Op::write, combination, bank, number, 0));
    startTask ("Renommer " + slotLabel (bank, number) + " en \"" + name + "\"", std::move (ops));
}

void N364Processor::copySlot (bool combination, int srcBank, int srcNumber, int dstBank, int dstNumber)
{
    if (dstBank < 0 || dstBank > 1 || dstNumber < 0 || dstNumber > 99 || srcBank < 0 || srcBank > 4)
    {
        setStatus ("Copier : la destination doit etre un emplacement des banques A ou B.");
        return;
    }

    std::deque<Op> ops;
    ops.push_back (makeOp (Op::select, combination, srcBank, srcNumber, 0, 1));
    ops.push_back (makeOp (Op::read, combination, srcBank, srcNumber, 0));
    ops.push_back (makeOp (Op::send, combination, srcBank, srcNumber, 0));
    ops.push_back (makeOp (Op::write, combination, dstBank, dstNumber, 0));
    startTask ("Copier " + slotLabel (srcBank, srcNumber) + " vers " + slotLabel (dstBank, dstNumber), std::move (ops));
}

void N364Processor::swapSlots (bool combination, int bankA, int numberA, int bankB, int numberB)
{
    if (bankA < 0 || bankA > 1 || bankB < 0 || bankB > 1 || numberA < 0 || numberA > 99 || numberB < 0 || numberB > 99
        || (bankA == bankB && numberA == numberB))
    {
        setStatus ("Echanger : choisis deux emplacements differents des banques A ou B.");
        return;
    }

    std::deque<Op> ops;
    ops.push_back (makeOp (Op::select, combination, bankA, numberA, 0, 1));
    ops.push_back (makeOp (Op::read, combination, bankA, numberA, 0));
    ops.push_back (makeOp (Op::select, combination, bankB, numberB, 1, 0));
    ops.push_back (makeOp (Op::read, combination, bankB, numberB, 1));
    // les deux sons sont lus : on peut maintenant ecrire
    ops.push_back (makeOp (Op::send, combination, bankA, numberA, 0));
    ops.push_back (makeOp (Op::write, combination, bankB, numberB, 0));
    ops.push_back (makeOp (Op::send, combination, bankB, numberB, 1));
    ops.push_back (makeOp (Op::write, combination, bankA, numberA, 1));
    startTask ("Echanger " + slotLabel (bankA, numberA) + " et " + slotLabel (bankB, numberB), std::move (ops));
}

bool N364Processor::sendPresetToSynth (const juce::File& preset, int writeBank, int writeNumber)
{
    const auto xml = juce::XmlDocument::parse (preset);

    if (xml == nullptr || ! xml->hasTagName ("N364Preset"))
    {
        setStatus ("Preset illisible.");
        return false;
    }

    const auto target = (N364Target) juce::jlimit (0, 3, xml->getIntAttribute ("type", 0));

    if (target != N364Target::program && target != N364Target::combination)
    {
        setStatus ("Seuls les Programmes et les Combinations peuvent etre envoyes en dump complet.");
        return false;
    }

    const bool combination = (target == N364Target::combination);
    const size_t need = combination ? 136 : 164;
    auto rawBytes = fromHex (xml->getStringAttribute ("raw"));

    if (rawBytes.size() < need)
    {
        setStatus ("Ce preset ne contient pas les donnees completes du son : fais d'abord \"Lire depuis le N364\" "
                   "(onglet Banques), puis enregistre-le de nouveau.");
        return false;
    }

    if (writeBank >= 0 && (writeBank > 1 || writeNumber < 0 || writeNumber > 99))
    {
        setStatus ("On ne peut ecrire que dans les banques A ou B (00 a 99).");
        return false;
    }

    rawBytes.resize (need);
    loadPreset (preset, false);   // les potards suivent le preset

    std::deque<Op> ops;
    ops.push_back (makeOp (Op::mode, combination));
    ops.push_back (makeOp (Op::send, combination, 0, 0, 0));

    if (writeBank >= 0)
        ops.push_back (makeOp (Op::write, combination, writeBank, writeNumber, 0));

    return startTask (writeBank >= 0 ? "Ecriture du preset dans " + slotLabel (writeBank, writeNumber)
                                     : juce::String ("Envoi du preset au N364"),
                      std::move (ops), juce::File(), rawBytes);
}

void N364Processor::backupBanksToFile (bool combination, const juce::File& file)
{
    std::deque<Op> ops;
    ops.push_back (makeOp (Op::backupBank, combination));
    startTask (combination ? "Sauvegarde des Combinations A+B" : "Sauvegarde des Programs A+B", std::move (ops), file);
}

void N364Processor::restoreBanksFromFile (const juce::File& file)
{
    if (outModeParam->load() <= 0.5f)
    {
        setStatus ("Restaurer une banque : utilise le mode \"Port MIDI direct\" (les gros envois ne passent pas par l'hote).");
        return;
    }

    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        setStatus ("Fichier illisible.");
        return;
    }

    const auto messages = splitSyx (static_cast<const std::uint8_t*> (data.getData()), data.getSize());

    for (auto& m : messages)
    {
        if (m.size() < 8 || m[1] != 0x42 || (m[2] & 0xF0) != 0x30 || m[3] != 0x35)
            continue;

        const int func = m[4];

        if (func != 0x4C && func != 0x4D)
            continue;

        const bool combination = (func == 0x4D);
        const size_t recordSize = combination ? 136 : 164;
        const auto records = unpack7 (m.data() + 6, m.size() - 7);

        if (records.size() < 200 * recordSize)
        {
            setStatus ("Ce fichier ne contient pas une banque A+B complete.");
            return;
        }

        std::deque<Op> ops;
        ops.push_back (makeOp (Op::backupBank, combination));   // sauvegarde automatique de l'etat actuel
        ops.push_back (makeOp (Op::sendBank, combination, 0, 0, 0));
        startTask (combination ? "Restauration des Combinations A+B" : "Restauration des Programs A+B", std::move (ops),
                   juce::File(), std::vector<std::uint8_t> (records.begin(), records.begin() + (long) (200 * recordSize)));
        return;
    }

    setStatus ("Aucune banque A+B (Programs ou Combinations) dans ce fichier.");
}

int N364Processor::importSyxFile (const juce::File& file)
{
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        setStatus ("Fichier illisible.");
        return 0;
    }

    const auto messages = splitSyx (static_cast<const std::uint8_t*> (data.getData()), data.getSize());
    int count = 0;

    for (auto& m : messages)
    {
        if (m.size() < 7 || m[1] != 0x42 || (m[2] & 0xF0) != 0x30 || m[3] != 0x35)
            continue;

        const int func = m[4];

        if (func == 0x40 || func == 0x49)
        {
            const bool combination = (func == 0x49);
            const size_t need = combination ? 136 : 164;
            const auto rec = unpack7 (m.data() + 5, m.size() - 6);

            if (rec.size() < need)
                continue;

            const auto name = decodeName (rec.data());
            const auto decoded = combination ? decodeCombination (rec.data(), rec.size()) : decodeProgram (rec.data(), rec.size());

            if (writePresetFromDecoded (combination ? N364Target::combination : N364Target::program,
                                        name.isEmpty() ? juce::String ("Sans nom") : name, decoded, rec.data(), need))
                ++count;
        }
        else if ((func == 0x4C || func == 0x4D) && m.size() > 8)
        {
            const bool combination = (func == 0x4D);
            const size_t recordSize = combination ? 136 : 164;
            const auto records = unpack7 (m.data() + 6, m.size() - 7);
            const size_t available = juce::jmin ((size_t) 200, records.size() / recordSize);

            for (size_t i = 0; i < available; ++i)
            {
                const std::uint8_t* b = records.data() + i * recordSize;
                const int bank = i < 100 ? 0 : 1;
                const int number = (int) (i % 100);
                const auto name = decodeName (b);
                const auto decoded = combination ? decodeCombination (b, recordSize) : decodeProgram (b, recordSize);

                if (writePresetFromDecoded (combination ? N364Target::combination : N364Target::program,
                                            slotLabel (bank, number) + " - " + name, decoded, b, recordSize))
                    ++count;
            }
        }
    }

    setStatus (count > 0 ? juce::String (count) + " son(s) importe(s) dans la bibliotheque depuis " + file.getFileName()
                         : juce::String ("Aucun son reconnu dans ce fichier .syx."));
    return count;
}

bool N364Processor::exportPresetSyx (const juce::File& preset, const juce::File& destination)
{
    const auto xml = juce::XmlDocument::parse (preset);

    if (xml == nullptr || ! xml->hasTagName ("N364Preset"))
    {
        setStatus ("Preset illisible.");
        return false;
    }

    const auto target = (N364Target) juce::jlimit (0, 3, xml->getIntAttribute ("type", 0));
    const bool combination = (target == N364Target::combination);
    const size_t need = combination ? 136 : 164;
    auto rawBytes = fromHex (xml->getStringAttribute ("raw"));

    if ((target != N364Target::program && target != N364Target::combination) || rawBytes.size() < need)
    {
        setStatus ("Ce preset ne contient pas les donnees completes du son : fais d'abord \"Lire depuis le N364\" "
                   "(onglet Banques), puis enregistre-le de nouveau.");
        return false;
    }

    rawBytes.resize (need);
    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;
    const auto message = buildKorgDump (channel0, combination ? 0x49 : 0x40, false, rawBytes);

    if (! destination.replaceWithData (message.data(), message.size()))
    {
        setStatus ("Impossible d'ecrire " + destination.getFullPathName());
        return false;
    }

    setStatus ("Preset exporte : " + destination.getFileName());
    return true;
}

bool N364Processor::exportLibraryBanksSyx (bool combination, const juce::File& destination)
{
    const auto target = combination ? N364Target::combination : N364Target::program;
    const size_t recordSize = combination ? 136 : 164;
    std::vector<std::vector<std::uint8_t>> slots (200);
    int found = 0;

    for (auto& f : listPresets (target))
    {
        const auto name = f.getFileNameWithoutExtension();

        if (name.length() < 3 || (name[0] != 'A' && name[0] != 'B')
            || ! juce::CharacterFunctions::isDigit (name[1]) || ! juce::CharacterFunctions::isDigit (name[2]))
            continue;

        const int index = (name[0] == 'B' ? 100 : 0) + name.substring (1, 3).getIntValue();
        const auto xml = juce::XmlDocument::parse (f);

        if (xml == nullptr)
            continue;

        const auto rec = fromHex (xml->getStringAttribute ("raw"));

        if (rec.size() < recordSize)
            continue;

        if (slots[(size_t) index].empty())
            ++found;

        slots[(size_t) index].assign (rec.begin(), rec.begin() + (long) recordSize);
    }

    if (found < 200)
    {
        setStatus ("Banque incomplete : il manque " + juce::String (200 - found)
                   + " son(s) avec donnees completes (importe d'abord les banques A+B du N364).");
        return false;
    }

    std::vector<std::uint8_t> records;
    records.reserve (200 * recordSize);

    for (auto& slot : slots)
        records.insert (records.end(), slot.begin(), slot.end());

    const int channel0 = juce::jlimit (1, 16, juce::roundToInt (channelParam->load())) - 1;
    const auto message = buildKorgDump (channel0, combination ? 0x4D : 0x4C, true, records);

    if (! destination.replaceWithData (message.data(), message.size()))
    {
        setStatus ("Impossible d'ecrire " + destination.getFullPathName());
        return false;
    }

    setStatus ("Banques A+B exportees : " + destination.getFileName());
    return true;
}

bool N364Processor::renamePreset (const juce::File& file, const juce::String& newName)
{
    const auto trimmed = newName.trim();

    if (trimmed.isEmpty())
        return false;

    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName ("N364Preset"))
        return false;

    const auto target = (N364Target) juce::jlimit (0, 3, xml->getIntAttribute ("type", 0));
    const auto destination = file.getParentDirectory().getChildFile (juce::File::createLegalFileName (trimmed) + ".n364preset");

    if (destination != file && destination.exists())
    {
        setStatus ("Un preset porte deja ce nom.");
        return false;
    }

    xml->setAttribute ("name", trimmed);
    auto rec = fromHex (xml->getStringAttribute ("raw"));

    // le nom du son fait partie des donnees du N364 (10 premiers caracteres)
    if (! rec.empty() && (target == N364Target::program || target == N364Target::combination))
    {
        setRecordName (rec.data(), trimmed.toStdString());
        xml->setAttribute ("raw", toHex (rec));
    }

    if (! xml->writeTo (destination))
        return false;

    if (destination != file)
        file.deleteFile();

    ++presetVersion;
    setStatus ("Preset renomme : " + trimmed);
    return true;
}

//==============================================================================
// Parametres

int N364Processor::indexFor (N364Target target, int number, int drumIndex) const
{
    const auto it = indexMap.find (makeKey (target, drumIndex, number));
    return it != indexMap.end() ? it->second : -1;
}

int N364Processor::toPluginValue (const N364Param& p, int sentValue) const
{
    const int value = sentValue - p.rawOffset;

    // Valeur hors plage (ex. pan "OFF" = 255) : valeur par defaut
    return (value < p.minValue || value > p.maxValue) ? p.defaultValue : value;
}

// Regle un parametre ; si suppressSend, le plugin considere la valeur comme deja connue du N364.
void N364Processor::setRaw (size_t index, int value, bool suppressSend)
{
    const auto& p = getParamTable()[index];
    value = juce::jlimit (p.minValue, p.maxValue, value);

    if (suppressSend)
        lastSent[index].store (value + p.rawOffset);

    if (params[index] != nullptr)
        params[index]->setValueNotifyingHost (params[index]->convertTo0to1 ((float) value));
}

void N364Processor::applySent (size_t index, int sentValue)
{
    setRaw (index, toPluginValue (getParamTable()[index], sentValue), true);
}

void N364Processor::setStatus (const juce::String& text)
{
    const juce::ScopedLock lock (textLock);
    status = text;
}

juce::String N364Processor::getStatus() const
{
    const juce::ScopedLock lock (textLock);
    return status;
}

juce::String N364Processor::getLastReadName() const
{
    const juce::ScopedLock lock (textLock);
    return lastReadName;
}

//==============================================================================
// Ports MIDI

void N364Processor::setMidiOutputDevice (const juce::String& deviceIdentifier)
{
    midiOut.reset();

    if (deviceIdentifier.isNotEmpty())
        midiOut = juce::MidiOutput::openDevice (deviceIdentifier);

    if (midiOut != nullptr)
        midiOut->startBackgroundThread();

    apvts.state.setProperty ("midiOut", deviceIdentifier, nullptr);
}

juce::String N364Processor::getMidiOutputDevice() const
{
    return apvts.state.getProperty ("midiOut").toString();
}

void N364Processor::setMidiInputDevice (const juce::String& deviceIdentifier)
{
    if (midiIn != nullptr)
        midiIn->stop();

    midiIn.reset();

    if (deviceIdentifier.isNotEmpty())
    {
        midiIn = juce::MidiInput::openDevice (deviceIdentifier, this);

        if (midiIn != nullptr)
            midiIn->start();
    }

    apvts.state.setProperty ("midiIn", deviceIdentifier, nullptr);
}

juce::String N364Processor::getMidiInputDevice() const
{
    return apvts.state.getProperty ("midiIn").toString();
}

//==============================================================================
// Bibliotheque de presets (fichiers XML)

juce::File N364Processor::getPresetFolder (N364Target target) const
{
    const char* names[] = { "Programmes", "Combinations", "DrumKits", "Global" };

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("N364Editor")
               .getChildFile ("Presets")
               .getChildFile (names[(int) target]);
}

juce::Array<juce::File> N364Processor::listPresets (N364Target target) const
{
    auto files = getPresetFolder (target).findChildFiles (juce::File::findFiles, false, "*.n364preset");

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });

    return files;
}

bool N364Processor::savePreset (N364Target target, const juce::String& name)
{
    const auto folder = getPresetFolder (target);

    if (name.trim().isEmpty() || ! folder.createDirectory())
        return false;

    juce::XmlElement root ("N364Preset");
    root.setAttribute ("type", (int) target);
    root.setAttribute ("name", name.trim());

    const auto& table = getParamTable();

    for (size_t i = 0; i < table.size(); ++i)
    {
        if (table[i].target != target)
            continue;

        auto* e = root.createNewChildElement ("P");
        e->setAttribute ("id", table[i].id);
        e->setAttribute ("v", juce::roundToInt (raw[i]->load()));
    }

    // Donnees brutes : derniere lecture du N364, avec les valeurs actuelles des potards recopiees dedans
    if (target == N364Target::program || target == N364Target::combination)
    {
        const bool combination = target == N364Target::combination;
        const auto& base = combination ? lastRawCombination : lastRawProgram;
        const size_t need = combination ? 136 : 164;

        if (base.size() >= need)
        {
            std::vector<std::uint8_t> rec (base.begin(), base.begin() + need);
            const auto vals = currentSentValues (target);

            if (combination)
                encodeCombination (rec.data(), rec.size(), vals);
            else
                encodeProgram (rec.data(), rec.size(), vals);

            setRecordName (rec.data(), name.trim().toStdString());
            root.setAttribute ("raw", toHex (rec));
        }
    }

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name.trim()) + ".n364preset");

    if (! root.writeTo (file))
        return false;

    ++presetVersion;
    return true;
}

bool N364Processor::loadPreset (const juce::File& file, bool sendToSynth)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName ("N364Preset"))
        return false;

    const auto target = (N364Target) juce::jlimit (0, 3, xml->getIntAttribute ("type", 0));

    for (auto* e : xml->getChildIterator())
    {
        if (! e->hasTagName ("P"))
            continue;

        const auto it = idMap.find (e->getStringAttribute ("id").toStdString());

        if (it != idMap.end())
            setRaw ((size_t) it->second, e->getIntAttribute ("v"), ! sendToSynth);
    }

    const auto rawBytes = fromHex (xml->getStringAttribute ("raw"));

    if (target == N364Target::program && rawBytes.size() >= 164)
        lastRawProgram.assign (rawBytes.begin(), rawBytes.begin() + 164);
    else if (target == N364Target::combination && rawBytes.size() >= 136)
        lastRawCombination.assign (rawBytes.begin(), rawBytes.begin() + 136);

    if (sendToSynth)
    {
        setTarget (target);
        sendAll();
        setStatus ("Preset charge et envoye au N364.");
    }
    else
    {
        setStatus ("Preset charge : " + xml->getStringAttribute ("name"));
    }

    return true;
}

//==============================================================================
void N364Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void N364Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            setMidiOutputDevice (apvts.state.getProperty ("midiOut").toString());
            setMidiInputDevice (apvts.state.getProperty ("midiIn").toString());

            // Recharger un projet ne doit rien envoyer au N364 :
            // on repart d'une base neuve ("Envoyer tout" reste disponible).
            resetBaseline();
        }
    }
}

juce::AudioProcessorEditor* N364Processor::createEditor()
{
    return new N364Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new N364Processor();
}
