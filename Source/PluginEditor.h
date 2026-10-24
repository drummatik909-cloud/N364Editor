#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <vector>
#include "PluginProcessor.h"

//==============================================================================
// Dessin d'une enveloppe (EG) a partir des valeurs courantes des parametres
class EnvelopeView : public juce::Component,
                     private juce::Timer
{
public:
    EnvelopeView (juce::AudioProcessorValueTreeState& state, const N364Graph& spec);
    ~EnvelopeView() override { stopTimer(); }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    std::array<std::atomic<float>*, 9> values {};
    int minLevel = -99;
    int maxLevel = 99;
};

//==============================================================================
// Une page : grille de potards, listes, interrupteurs et enveloppes, par groupes
class ParamPage : public juce::Component,
                  private juce::Timer
{
public:
    ParamPage (juce::AudioProcessorValueTreeState& state,
               const std::vector<const N364Param*>& params,
               const std::vector<N364Graph>& graphs);
    ~ParamPage() override { stopTimer(); }

    int getHeightForWidth (int width) { return layoutItems (width, false); }
    void resized() override { layoutItems (getWidth(), true); }

private:
    enum class Kind { header, knob, combo, toggle, graph };

    struct Item
    {
        Kind kind = Kind::knob;
        bool compact = false;                 // groupe sans potard : lignes plus basses
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> comp;
    };

    // Libelles qui changent : parametres d'effet (selon le type) et noms de Multisounds
    struct Dynamic
    {
        int kind = 0;                       // 0 = parametre d'effet, 1 = Multisound
        juce::Label* label = nullptr;
        juce::Slider* slider = nullptr;
        std::atomic<float>* typeRaw = nullptr;
        std::atomic<float>* valueRaw = nullptr;
        int k = 0;
        int lastKey = -1;
        double lo = -200.0, hi = 800.0;
        juce::String baseName;
    };

    void timerCallback() override;
    int layoutItems (int width, bool apply);

    std::vector<Dynamic> dynamics;
    std::vector<Item> items;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
};

//==============================================================================
// Page + ascenseur
class PageView : public juce::Component
{
public:
    PageView (juce::AudioProcessorValueTreeState& state,
              const std::vector<const N364Param*>& params,
              const std::vector<N364Graph>& graphs);

    void resized() override;

private:
    ParamPage page;
    juce::Viewport viewport;
};

//==============================================================================
// Page des Drum Kits : 60 instruments x 9 colonnes, pour le kit choisi
class DrumPage : public juce::Component
{
public:
    DrumPage (juce::AudioProcessorValueTreeState& state,
              const std::vector<const N364Param*>& params);

    void resized() override;

private:
    static constexpr int numColumns = 9;
    static constexpr int numRows = 60;

    juce::Label kitLabel;
    juce::ComboBox kitBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> kitAttachment;

    std::vector<std::unique_ptr<juce::Label>> headerLabels;
    juce::Component rows;                                   // contenu defilant
    std::vector<std::unique_ptr<juce::Label>> rowLabels;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    juce::Viewport viewport;
};

//==============================================================================
// Apparence sombre
class N364LookAndFeel : public juce::LookAndFeel_V4
{
public:
    N364LookAndFeel();
};

//==============================================================================
// Bibliotheque : presets (fichiers), lecture du N364, importation de banques, ecriture en memoire
class LibraryPage : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::Timer
{
public:
    explicit LibraryPage (N364Processor& p);
    ~LibraryPage() override { stopTimer(); }

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;

    void refresh();
    void updateButtons();
    N364Target currentTarget() const;
    juce::File selectedFile() const;
    void confirmWrite();

    N364Processor& processor;

    juce::Label typeLabel, filterLabel, nameLabel, synthLabel, noteLabel;
    juce::ComboBox typeBox, bankBox, numberBox;
    juce::TextEditor filterEditor, nameEditor;
    juce::ListBox listBox;
    juce::TextButton loadButton { "Charger" };
    juce::TextButton loadSendButton { "Charger + envoyer au N364" };
    juce::TextButton saveButton { "Enregistrer" };
    juce::TextButton deleteButton { "Supprimer" };
    juce::TextButton folderButton { "Ouvrir le dossier" };
    juce::TextButton renameButton { "Renommer le preset" };
    juce::TextButton sendDumpButton { "Envoyer au N364 (dump complet)" };
    juce::TextButton writeSlotButton { "Ecrire dans un emplacement..." };
    juce::TextButton exportButton { "Exporter ce preset (.syx)" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton getButton { "Lire depuis le N364" };
    juce::TextButton importButton { "Importer la banque A+B du N364" };
    juce::TextButton writeButton { "Ecrire dans la memoire du N364..." };

    juce::Array<juce::File> files;
    int lastVersion = -1;
};

//==============================================================================
// Banques : Programs A B C D GM et Combinations A B C D, choix sur le N364, suivi en temps reel
class BankPage : public juce::Component,
                 private juce::ListBoxModel,
                 private juce::Timer
{
public:
    explicit BankPage (N364Processor& p);
    ~BankPage() override { stopTimer(); }

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;

    bool isCombination() const;
    int currentBank() const;
    int numberForRow (int row) const;
    int rowForNumber (int number) const;
    void bankChanged();

    N364Processor& processor;

    juce::Label kindLabel, currentLabel, statusLabel, noteLabel;
    juce::ComboBox kindBox;
    std::array<juce::TextButton, 5> bankButtons;
    juce::ListBox listBox;
    juce::ToggleButton followToggle { "Suivre le N364 en temps reel" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAttachment;
    juce::TextButton readButton { "Lire le son courant" };
    juce::TextButton importButton { "Importer cette banque (son par son)" };
    juce::TextButton cancelButton { "Annuler l'operation" };
    juce::TextButton renameButton { "Renommer sur le N364..." };
    juce::TextButton copyButton { "Copier ce son vers..." };
    juce::TextButton swapButton { "Echanger avec..." };

    int selectedBank = 0;
    int lastSlotVersion = -1;
    int lastPresetVersion = -1;
};

//==============================================================================
// Echanges SysEx : sauvegardes et restaurations des banques A+B, import / export de fichiers .syx
class ExchangePage : public juce::Component,
                     private juce::Timer
{
public:
    explicit ExchangePage (N364Processor& p);
    ~ExchangePage() override { stopTimer(); }

    void resized() override;

private:
    void timerCallback() override;

    N364Processor& processor;

    juce::Label titleBackup, titleFiles, statusLabel, noteLabel;
    juce::TextButton backupProgramsButton { "Sauvegarder les Programs A+B du N364 (.syx)" };
    juce::TextButton backupCombisButton { "Sauvegarder les Combinations A+B du N364 (.syx)" };
    juce::TextButton restoreButton { "Restaurer des banques depuis un .syx..." };
    juce::TextButton openBackupsButton { "Ouvrir le dossier des sauvegardes automatiques" };
    juce::TextButton importButton { "Importer un .syx dans la bibliotheque..." };
    juce::TextButton exportProgramsButton { "Exporter la bibliotheque Programs A+B (.syx)" };
    juce::TextButton exportCombisButton { "Exporter la bibliotheque Combinations A+B (.syx)" };
    juce::TextButton cancelButton { "Annuler l'operation" };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class N364Tabs : public juce::TabbedComponent
{
public:
    N364Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}

    std::function<void (const juce::String&)> onTabChanged;

    void currentTabChanged (int, const juce::String& newName) override
    {
        if (onTabChanged)
            onTabChanged (newName);
    }
};

//==============================================================================
class N364Editor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit N364Editor (N364Processor&);
    ~N364Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    N364LookAndFeel laf;                 // declare en premier : detruit en dernier
    N364Processor& processor;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Label modeLabel, portLabel, inPortLabel, channelLabel, targetLabel, hintLabel, statusLabel;
    juce::ComboBox modeBox, portBox, inPortBox, targetBox;
    juce::Slider channelSlider;
    juce::ToggleButton autoSyncToggle { "Synchro depuis le N364" };
    juce::TextButton sendAllButton { "Envoyer tout au N364" };
    N364Tabs tabs;

    juce::Array<juce::MidiDeviceInfo> outputDevices, inputDevices;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, targetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> channelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoSyncAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (N364Editor)
};
