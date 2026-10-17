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
class ParamPage : public juce::Component
{
public:
    ParamPage (juce::AudioProcessorValueTreeState& state,
               const std::vector<const N364Param*>& params,
               const std::vector<N364Graph>& graphs);

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

    int layoutItems (int width, bool apply);

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
    juce::TextButton getButton { "Lire depuis le N364" };
    juce::TextButton importButton { "Importer la banque A+B du N364" };
    juce::TextButton writeButton { "Ecrire dans la memoire du N364..." };

    juce::Array<juce::File> files;
    int lastVersion = -1;
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
