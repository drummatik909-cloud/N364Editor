#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>
#include "ParamTable.h"
#include "DumpDecode.h"

// Plugin qui ne produit aucun son : il traduit les mouvements de ses parametres
// en messages SysEx pour le Korg N264/N364, et lit les sons du N364.
//
//  Envoi : Program / Combination = PARAMETER CHANGE (0x41) ; Drum Kit = 0x53 ;
//          Global = GLOBAL DATA DUMP (0x51) envoye en entier.
//  Lecture : requetes de dump (0x10, 0x19, 0x0E, 0x0D, 0x1C, 0x1D) et reception des dumps ;
//            les Parameter Change emis par le N364 (face avant) mettent les potards a jour.
//  Memoire : PROGRAM / COMBINATION WRITE REQUEST (0x11 / 0x1A).
//
// Deux modes de sortie : "Hote" (sortie MIDI du plugin) ou "Direct" (le plugin ouvre
// lui-meme les ports MIDI choisis).
class N364Processor : public juce::AudioProcessor,
                      private juce::Timer,
                      private juce::AsyncUpdater,
                      private juce::MidiInputCallback
{
public:
    enum class Request { program, combination, global, drums, allPrograms, allCombinations };

    N364Processor();
    ~N364Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "N364 Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Envoi
    void sendAll();                                         // renvoie tous les parametres de la cible active
    void setTarget (N364Target target);
    void writeToMemory (bool combination, int bank, int number);   // bank 0=A 1=B ; number 0..99

    // --- Lecture depuis le N364
    void requestFromSynth (Request request);

    // --- Ports MIDI
    void setMidiOutputDevice (const juce::String& deviceIdentifier);
    juce::String getMidiOutputDevice() const;
    void setMidiInputDevice (const juce::String& deviceIdentifier);
    juce::String getMidiInputDevice() const;

    // --- Etat affiche par l'editeur
    juce::String getStatus() const;
    juce::String getLastReadName() const;

    // --- Bibliotheque de presets (fichiers)
    juce::File getPresetFolder (N364Target target) const;
    juce::Array<juce::File> listPresets (N364Target target) const;
    bool savePreset (N364Target target, const juce::String& name);
    bool loadPreset (const juce::File& file, bool sendToSynth);
    int getPresetVersion() const { return presetVersion.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::MidiMessage makeParameterChange (int channel0, int number, int value);
    static juce::MidiMessage makeDrumParameterChange (int channel0, int kit, int index, int number, int value);
    static juce::MidiMessage makeGlobalDump (int channel0, const std::array<int, 28>& data);
    static juce::MidiMessage makeSimple (int channel0, int func, bool withZero);
    static juce::MidiMessage makeWriteRequest (int channel0, bool combination, int bank, int number);

    template <typename Fn>
    int collectChanges (int maxBytes, Fn&& emit);

    void resetBaseline();
    void timerCallback() override;

    // reception
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void receive (const juce::MidiMessage& message);
    void processIncoming (const juce::MidiMessage& message);
    void applyDecoded (N364Target target, const DecodedValues& values, int drumIndexScale);
    void importBank (bool combination, const std::vector<std::uint8_t>& bytes);
    void setStatus (const juce::String& text);

    // parametres
    int indexFor (N364Target target, int number, int drumIndex) const;
    void setRaw (size_t index, int value, bool suppressSend);
    void applySent (size_t index, int sentValue);
    int toPluginValue (const N364Param& p, int sentValue) const;
    juce::MidiMessage buildMessage (const N364Param& p, int channel0, int kit, int value) const;

    static constexpr int kUnknown = std::numeric_limits<int>::min();
    static constexpr int kForce   = std::numeric_limits<int>::min() + 1;

    std::vector<std::atomic<float>*> raw;             // une entree par parametre de la table
    std::vector<juce::RangedAudioParameter*> params;
    std::unique_ptr<std::atomic<int>[]> lastSent;     // derniere valeur envoyee
    std::unordered_map<int, int> indexMap;            // (cible, index de drum, numero) -> indice
    std::unordered_map<std::string, int> idMap;       // id -> indice

    std::atomic<float>* channelParam = nullptr;
    std::atomic<float>* outModeParam = nullptr;
    std::atomic<float>* targetParam = nullptr;
    std::atomic<float>* kitParam = nullptr;
    std::atomic<float>* autoSyncParam = nullptr;
    std::atomic<int> lastTarget { -1 };
    std::atomic<int> lastKit { -1 };
    std::atomic<bool> forceRequested { false };
    std::atomic<int> pendingWrite { -1 };             // (combination<<16)|(bank<<8)|numero
    std::atomic<int> awaiting { 0 };                  // code de fonction du dump attendu
    std::atomic<int> presetVersion { 0 };

    juce::SpinLock queueLock;
    std::vector<juce::MidiMessage> incoming;
    std::vector<juce::MidiMessage> outgoing;

    juce::CriticalSection textLock;
    juce::String status, lastReadName;

    std::unique_ptr<juce::MidiOutput> midiOut;
    std::unique_ptr<juce::MidiInput> midiIn;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (N364Processor)
};
