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

    channelParam  = apvts.getRawParameterValue ("globalch");
    outModeParam  = apvts.getRawParameterValue ("outmode");
    targetParam   = apvts.getRawParameterValue ("target");
    kitParam      = apvts.getRawParameterValue ("drumkit");
    autoSyncParam = apvts.getRawParameterValue ("autosync");

    startTimer (20);   // 50 Hz : sert uniquement au mode "port direct"
}

N364Processor::~N364Processor()
{
    stopTimer();
    cancelPendingUpdate();

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

        const int value = juce::roundToInt (raw[i]->load()) + p.rawOffset;
        const int last = lastSent[i].load();

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

        if (message.isSysEx())
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
    // Modes : 0 = hote (processBlock), 1 = port direct (ici)
    if (outModeParam->load() < 0.5f || midiOut == nullptr)
        return;

    collectChanges (62, [this] (const juce::MidiMessage& m)   // 62 octets = 20 ms de MIDI DIN
    {
        midiOut->sendMessageNow (m);
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
    if (message.isSysEx())
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
        return;

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

        case 0x40:   // Program (tampon courant)
        {
            if (! accept (0x40))
                return;

            const auto bytes = unpack7 (payload, (size_t) n);

            if (bytes.size() < 164)
                return;

            applyDecoded (N364Target::program, decodeProgram (bytes.data(), bytes.size()), 1);

            {
                const juce::ScopedLock lock (textLock);
                lastReadName = decodeName (bytes.data());
            }

            setStatus ("Programme lu depuis le N364 : " + decodeName (bytes.data()));
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

            applyDecoded (N364Target::combination, decodeCombination (bytes.data(), bytes.size()), 1);

            {
                const juce::ScopedLock lock (textLock);
                lastReadName = decodeName (bytes.data());
            }

            setStatus ("Combination lue depuis le N364 : " + decodeName (bytes.data()));
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
            if (expected != func || n < 2)
                return;

            importBank (func == 0x4D, unpack7 (payload + 1, (size_t) (n - 1)));
            awaiting.store (0);
            return;
        }

        case 0x23: setStatus ("Le N364 a bien recu les donnees."); return;
        case 0x24: setStatus ("Le N364 signale une erreur de reception de donnees."); return;
        case 0x21: setStatus ("Ecriture terminee dans la memoire du N364."); return;
        case 0x22: setStatus ("Erreur d'ecriture du N364 (memoire protegee ?)."); return;
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

void N364Processor::importBank (bool combination, const std::vector<std::uint8_t>& bytes)
{
    const size_t recordSize = combination ? 136 : 164;
    const size_t count = juce::jmin ((size_t) 200, bytes.size() / recordSize);
    const auto target = combination ? N364Target::combination : N364Target::program;
    const auto folder = getPresetFolder (target);

    if (count == 0 || ! folder.createDirectory())
    {
        setStatus ("Banque recue, mais impossible de creer les presets.");
        return;
    }

    const auto& table = getParamTable();
    int saved = 0;

    for (size_t i = 0; i < count; ++i)
    {
        const std::uint8_t* b = bytes.data() + i * recordSize;
        const auto decoded = combination ? decodeCombination (b, recordSize) : decodeProgram (b, recordSize);

        std::unordered_map<int, int> byNumber;
        for (auto& kv : decoded)
            byNumber[kv.first] = kv.second;

        const juce::String label = juce::String::charToString (i < 100 ? 'A' : 'B')
                                   + juce::String ((int) (i % 100)).paddedLeft ('0', 2)
                                   + " - " + decodeName (b);

        juce::XmlElement root ("N364Preset");
        root.setAttribute ("type", (int) target);
        root.setAttribute ("name", label);

        for (auto& p : table)
        {
            if (p.target != target)
                continue;

            const auto it = byNumber.find (p.number);
            const int value = it != byNumber.end() ? toPluginValue (p, it->second) : p.defaultValue;

            auto* e = root.createNewChildElement ("P");
            e->setAttribute ("id", p.id);
            e->setAttribute ("v", value);
        }

        if (root.writeTo (folder.getChildFile (juce::File::createLegalFileName (label) + ".n364preset")))
            ++saved;
    }

    ++presetVersion;
    setStatus ("Banque importee : " + juce::String (saved) + " presets enregistres dans la bibliotheque.");
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
