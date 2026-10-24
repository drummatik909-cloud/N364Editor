#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>
#include "ParamTable.h"
#include "DumpDecode.h"
#include "DumpEncode.h"
#include "EffectInfo.h"
#include "FactoryLists.h"

// Plugin qui ne produit aucun son : il traduit les mouvements de ses parametres
// en messages SysEx pour le Korg N264/N364, lit ses sons et gere ses banques.
//
//  Envoi   : Program / Combination = PARAMETER CHANGE (0x41) ; Drum Kit = 0x53 ;
//            Global = GLOBAL DATA DUMP (0x51) envoye en entier.
//  Lecture : requetes de dump (0x10, 0x19, 0x0E, 0x0D, 0x1C, 0x1D) et reception des dumps ;
//            les Parameter Change emis par le N364 (face avant) mettent les potards a jour.
//  Banques : Bank Select (CC0/CC32) + Program Change pour choisir un son sur le N364 ;
//            le plugin suit en temps reel les changements de son faits sur le N364.
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

    // Emplacement d'un son : bank 0..3 = A..D, 4 = GM (programs seulement)
    struct Slot { bool combination = false; int bank = -1; int number = -1; };

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

    // --- Banques : choix d'un son sur le N364, suivi en temps reel, import son par son
    void selectSlot (bool combination, int bank, int number);
    Slot getCurrentSlot() const;
    int getSlotVersion() const { return slotVersion.load(); }
    juce::String getSlotName (bool combination, int bank, int number, bool* isLive = nullptr) const;
    void startCrawl (bool combination, int bank);
    void cancelCrawl();
    bool isCrawling() const { return crawl.active; }

    // --- Operations sur les sons du N364 (banques A et B ecrivables) et echanges SysEx
    void renameOnSynth (bool combination, int bank, int number, const juce::String& newName);
    void copySlot (bool combination, int srcBank, int srcNumber, int dstBank, int dstNumber);
    void swapSlots (bool combination, int bankA, int numberA, int bankB, int numberB);
    bool sendPresetToSynth (const juce::File& preset, int writeBank, int writeNumber);   // writeBank < 0 : tampon d'edition seulement
    void backupBanksToFile (bool combination, const juce::File& file);
    void restoreBanksFromFile (const juce::File& file);
    int importSyxFile (const juce::File& file);
    bool exportPresetSyx (const juce::File& preset, const juce::File& destination);
    bool exportLibraryBanksSyx (bool combination, const juce::File& destination);
    bool renamePreset (const juce::File& file, const juce::String& newName);
    void cancelTask();
    bool isBusy() const { return task.active || crawl.active; }
    juce::File getBackupFolder() const;

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
    struct Scheduled { int ticks; juce::MidiMessage message; };

    struct Crawl
    {
        bool active = false;
        bool combination = false;
        int bank = 0;
        int index = 0;        // 0..count-1
        int count = 100;
        int phase = 0;        // 0 = selection, 1 = demande, 2 = attente du dump
        int wait = 0;
        bool gotDump = false;
        int saved = 0;
        int failed = 0;
    };

    struct Op
    {
        enum Type { mode, select, wait, read, patchName, send, write, backupBank, sendBank };

        Type type = wait;
        bool combination = false;
        int bank = 0;
        int number = 0;
        int reg = 0;          // registre 0..2 (donnees brutes d'un son ou d'une banque)
        int ticks = 0;        // attente (ticks de 20 ms) ; pour select : 1 = avec changement de mode
        juce::String text;    // nouveau nom (patchName)
    };

    struct Task
    {
        bool active = false;
        std::deque<Op> ops;
        Op cur;
        int wait = 0;
        int state = 0;        // 0 pret, 1 attend un dump, 2 attend l'accuse d'un envoi, 3 attend le resultat d'une ecriture,
                              // 4 attend une banque, 5 attend l'accuse d'un envoi de banque
        int timeout = 0;
        juce::File exportFile;
        std::array<std::vector<std::uint8_t>, 3> regs;
        juce::String title;
    };

    static Op makeOp (Op::Type type, bool combination = false, int bank = 0, int number = 0, int reg = 0,
                      int ticks = 0, const juce::String& text = juce::String());

    bool startTask (const juce::String& title, std::deque<Op> ops, const juce::File& exportFile = juce::File(),
                    const std::vector<std::uint8_t>& reg0 = {});
    void advanceTask();
    void failTask (const juce::String& why);
    void finishTask();
    void pushRaw (const std::vector<std::uint8_t>& bytes);
    void handleBankBackup (const juce::MidiMessage& message, const std::vector<std::uint8_t>& records);
    std::unordered_map<int, int> currentSentValues (N364Target target) const;
    static juce::String toHex (const std::vector<std::uint8_t>& bytes);
    static std::vector<std::uint8_t> fromHex (const juce::String& hex);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::MidiMessage makeParameterChange (int channel0, int number, int value);
    static juce::MidiMessage makeDrumParameterChange (int channel0, int kit, int index, int number, int value);
    static juce::MidiMessage makeGlobalDump (int channel0, const std::array<int, 28>& data);
    static juce::MidiMessage makeSimple (int channel0, int func, bool withZero);
    static juce::MidiMessage makeWriteRequest (int channel0, bool combination, int bank, int number);
    static juce::MidiMessage makeModeChange (int channel0, int mode);

    template <typename Fn>
    int collectChanges (int maxBytes, Fn&& emit);

    void resetBaseline();
    void timerCallback() override;
    void advanceCrawl();
    void queueSlotMessages (bool combination, int bank, int number, bool withModeChange);
    void requestCurrent();

    // reception
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void receive (const juce::MidiMessage& message);
    void processIncoming (const juce::MidiMessage& message);
    void processChannelMessage (const juce::MidiMessage& message);
    void applyDecoded (N364Target target, const DecodedValues& values, int drumIndexScale);
    void importBank (bool combination, const std::vector<std::uint8_t>& bytes);
    bool writePresetFromDecoded (N364Target target, const juce::String& label, const DecodedValues& decoded,
                                 const std::uint8_t* raw = nullptr, size_t rawSize = 0);
    void setStatus (const juce::String& text);
    void storeLiveName (bool combination, int bank, int number, const juce::String& name);
    void loadNamesCache();
    void saveNamesCache();
    juce::File getNamesFile() const;

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

    // effets : pour chaque parametre "Param k" -> k et indice du parametre "Type" ; pour chaque Type -> ses parametres
    std::vector<int> effectK;
    std::vector<int> effectTypeIndex;
    std::unordered_map<int, std::vector<int>> effectMembers;

    std::atomic<float>* channelParam = nullptr;
    std::atomic<float>* outModeParam = nullptr;
    std::atomic<float>* targetParam = nullptr;
    std::atomic<float>* kitParam = nullptr;
    std::atomic<float>* autoSyncParam = nullptr;
    std::atomic<float>* followParam = nullptr;
    std::atomic<int> lastTarget { -1 };
    std::atomic<int> lastKit { -1 };
    std::atomic<bool> forceRequested { false };
    std::atomic<int> pendingWrite { -1 };             // (combination<<16)|(bank<<8)|numero
    std::atomic<int> awaiting { 0 };                  // code de fonction du dump attendu
    std::atomic<int> presetVersion { 0 };

    // suivi du N364
    std::atomic<int> synthMode { -1 };                // 0..7 (NOTE 12 du manuel) ou -1
    std::atomic<int> bankMsb { 0 };
    std::atomic<int> bankLsb { 0 };
    std::atomic<int> slotCode { -1 };                 // (combination<<16)|(bank<<8)|numero
    std::atomic<int> slotVersion { 0 };
    std::atomic<int> dumpCountdown { 0 };             // ticks de 20 ms avant lecture du son courant
    Crawl crawl;
    Task task;
    std::vector<std::uint8_t> lastRawProgram, lastRawCombination;   // derniers enregistrements bruts lus sur le N364
    std::atomic<double> busyUntilMs { 0.0 };                        // pendant l'envoi d'une banque : aucun autre message

    juce::SpinLock queueLock;
    std::vector<juce::MidiMessage> incoming;
    std::vector<juce::MidiMessage> outgoing;
    std::vector<Scheduled> scheduled;

    juce::CriticalSection textLock;
    juce::String status, lastReadName;

    juce::CriticalSection nameLock;
    std::vector<juce::String> liveNames;              // (combination*5 + bank) * 140 + numero
    bool namesDirty = false;
    int namesSaveCountdown = 0;

    std::unique_ptr<juce::MidiOutput> midiOut;
    std::unique_ptr<juce::MidiInput> midiIn;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (N364Processor)
};
