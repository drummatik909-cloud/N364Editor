#include "PluginEditor.h"

//==============================================================================
EnvelopeView::EnvelopeView (juce::AudioProcessorValueTreeState& state, const N364Graph& spec)
    : minLevel (spec.minLevel), maxLevel (spec.maxLevel)
{
    for (int i = 0; i < 9 && i < spec.ids.size(); ++i)
        if (spec.ids[i].isNotEmpty())
            values[(size_t) i] = state.getRawParameterValue (spec.ids[i]);

    startTimerHz (20);
}

void EnvelopeView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1a1f23));
    g.fillRoundedRectangle (bounds, 4.0f);

    auto area = bounds.reduced (6.0f);

    auto get = [this] (int i)
    {
        return values[(size_t) i] != nullptr ? values[(size_t) i]->load() : 0.0f;
    };

    // Points (temps, niveau) : start -> attack -> decay -> (break -> slope) -> sustain -> release
    std::vector<std::pair<float, float>> pts;
    float t = 0.0f;

    pts.push_back ({ t, values[0] != nullptr ? get (0) : (float) minLevel });
    t += get (1);
    pts.push_back ({ t, get (2) });
    t += get (3);

    if (values[4] != nullptr)
    {
        pts.push_back ({ t, get (4) });
        t += get (5);
    }

    const float sustain = values[6] != nullptr ? get (6) : 0.0f;
    pts.push_back ({ t, sustain });
    t += 40.0f;                                  // maintien : largeur visuelle fixe
    pts.push_back ({ t, sustain });
    t += get (7);
    pts.push_back ({ t, values[8] != nullptr ? get (8) : (float) minLevel });

    const float total = juce::jmax (1.0f, t);
    const float range = (float) juce::jmax (1, maxLevel - minLevel);

    auto toPoint = [&] (const std::pair<float, float>& p)
    {
        return juce::Point<float> (area.getX() + area.getWidth() * p.first / total,
                                   area.getBottom() - area.getHeight() * (p.second - (float) minLevel) / range);
    };

    if (minLevel < 0)
    {
        const float y0 = area.getBottom() - area.getHeight() * (0.0f - (float) minLevel) / range;
        g.setColour (juce::Colour (0xff3f4950));
        g.drawHorizontalLine ((int) y0, area.getX(), area.getRight());
    }

    juce::Path line;
    line.startNewSubPath (toPoint (pts.front()));
    for (size_t i = 1; i < pts.size(); ++i)
        line.lineTo (toPoint (pts[i]));

    juce::Path fill (line);
    fill.lineTo (toPoint (pts.back()).x, area.getBottom());
    fill.lineTo (toPoint (pts.front()).x, area.getBottom());
    fill.closeSubPath();

    g.setColour (juce::Colour (0x4042a2c8));
    g.fillPath (fill);

    g.setColour (juce::Colour (0xff42a2c8));
    g.strokePath (line, juce::PathStrokeType (2.0f));

    g.setColour (juce::Colour (0xffd5dde0));
    for (auto& p : pts)
        g.fillEllipse (toPoint (p).x - 2.5f, toPoint (p).y - 2.5f, 5.0f, 5.0f);
}

//==============================================================================
ParamPage::ParamPage (juce::AudioProcessorValueTreeState& state,
                      const std::vector<const N364Param*>& params,
                      const std::vector<N364Graph>& graphs)
{
    // Un groupe sans aucun potard (que des listes / interrupteurs) a des lignes plus basses.
    std::map<juce::String, bool> groupHasKnob;

    for (auto* p : params)
    {
        const bool isKnob = p->choices.isEmpty() && ! p->isToggle;
        groupHasKnob[p->group] = groupHasKnob[p->group] || isKnob;
    }

    auto addGraphFor = [&] (const juce::String& group)
    {
        for (auto& graph : graphs)
        {
            if (graph.group == group)
            {
                Item item;
                item.kind = Kind::graph;
                item.comp = std::make_unique<EnvelopeView> (state, graph);
                addAndMakeVisible (*item.comp);
                items.push_back (std::move (item));
            }
        }
    };

    juce::String lastGroup;

    for (auto* p : params)
    {
        if (p->group != lastGroup)
        {
            if (lastGroup.isNotEmpty())
                addGraphFor (lastGroup);

            lastGroup = p->group;

            Item header;
            header.kind = Kind::header;
            header.compact = ! groupHasKnob[lastGroup];
            header.label = std::make_unique<juce::Label>();
            header.label->setText (p->group, juce::dontSendNotification);
            header.label->setFont (header.label->getFont().boldened());
            addAndMakeVisible (*header.label);
            items.push_back (std::move (header));
        }

        Item cell;
        cell.label = std::make_unique<juce::Label>();
        cell.label->setText (p->name, juce::dontSendNotification);
        cell.label->setJustificationType (juce::Justification::centred);
        cell.label->setFont (cell.label->getFont().withHeight (12.0f));
        addAndMakeVisible (*cell.label);

        if (! p->choices.isEmpty())
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (p->choices, 1);
            comboAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                state, p->id, *box));
            cell.kind = Kind::combo;
            cell.comp = std::move (box);
        }
        else if (p->isToggle)
        {
            auto button = std::make_unique<juce::ToggleButton>();
            buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, p->id, *button));
            cell.kind = Kind::toggle;
            cell.comp = std::move (button);
        }
        else
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                          juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
            slider->setDoubleClickReturnValue (true, p->defaultValue);
            sliderAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, p->id, *slider));
            cell.kind = Kind::knob;
            cell.comp = std::move (slider);
        }

        addAndMakeVisible (*cell.comp);
        items.push_back (std::move (cell));
    }

    if (lastGroup.isNotEmpty())
        addGraphFor (lastGroup);
}

int ParamPage::layoutItems (int width, bool apply)
{
    const int cellW = 92, labelH = 18, headerH = 26, pad = 8, graphH = 110;
    const int columns = juce::jmax (1, (width - 2 * pad) / cellW);

    int y = pad;
    int column = 0;
    int rowH = 104;

    for (auto& item : items)
    {
        if (item.kind == Kind::header)
        {
            if (column != 0)
            {
                y += rowH;
                column = 0;
            }

            if (apply)
                item.label->setBounds (pad, y, width - 2 * pad, headerH);

            y += headerH;
            rowH = item.compact ? 54 : 104;
            continue;
        }

        if (item.kind == Kind::graph)
        {
            if (column != 0)
            {
                y += rowH;
                column = 0;
            }

            if (apply)
                item.comp->setBounds (pad, y, juce::jmin (520, width - 2 * pad), graphH);

            y += graphH + 4;
            continue;
        }

        if (apply)
        {
            const int x = pad + column * cellW;
            item.label->setBounds (x, y, cellW, labelH);

            if (item.kind == Kind::knob)
                item.comp->setBounds (x, y + labelH, cellW, rowH - labelH);
            else if (item.kind == Kind::combo)
                item.comp->setBounds (x + 4, y + labelH + 4, cellW - 8, 24);
            else
                item.comp->setBounds (x + (cellW - 24) / 2, y + labelH + 4, 24, 24);
        }

        if (++column >= columns)
        {
            column = 0;
            y += rowH;
        }
    }

    if (column != 0)
        y += rowH;

    return y + pad;
}

//==============================================================================
PageView::PageView (juce::AudioProcessorValueTreeState& state,
                    const std::vector<const N364Param*>& params,
                    const std::vector<N364Graph>& graphs)
    : page (state, params, graphs)
{
    viewport.setViewedComponent (&page, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void PageView::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = juce::jmax (200, viewport.getMaximumVisibleWidth());
    page.setSize (w, page.getHeightForWidth (w));
}

//==============================================================================
DrumPage::DrumPage (juce::AudioProcessorValueTreeState& state,
                    const std::vector<const N364Param*>& params)
{
    kitLabel.setText ("Drum Kit", juce::dontSendNotification);
    addAndMakeVisible (kitLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter ("drumkit")))
        kitBox.addItemList (choice->choices, 1);
    addAndMakeVisible (kitBox);
    kitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, "drumkit", kitBox);

    // En-tetes de colonnes (d'apres les 9 premiers parametres, qui sont ceux de l'instrument 0)
    for (int col = 0; col < numColumns && col < (int) params.size(); ++col)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText (params[(size_t) col]->name, juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        label->setFont (label->getFont().withHeight (12.0f));
        addAndMakeVisible (*label);
        headerLabels.push_back (std::move (label));
    }

    // Lignes : un instrument par ligne
    for (int row = 0; row < numRows; ++row)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText ("Inst " + juce::String (row), juce::dontSendNotification);
        label->setFont (label->getFont().withHeight (12.0f));
        rows.addAndMakeVisible (*label);
        rowLabels.push_back (std::move (label));

        for (int col = 0; col < numColumns; ++col)
        {
            const size_t index = (size_t) (row * numColumns + col);

            if (index >= params.size())
                break;

            auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
            slider->setDoubleClickReturnValue (true, params[index]->defaultValue);
            rows.addAndMakeVisible (*slider);
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, params[index]->id, *slider));
            sliders.push_back (std::move (slider));
        }
    }

    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void DrumPage::resized()
{
    const int labelW = 60, cellW = 78, rowH = 22, topBar = 30, headerH = 20;

    auto area = getLocalBounds().reduced (6);

    auto bar = area.removeFromTop (topBar);
    kitLabel.setBounds (bar.removeFromLeft (70));
    kitBox.setBounds (bar.removeFromLeft (160));

    auto header = area.removeFromTop (headerH);
    header.removeFromLeft (labelW);
    for (auto& label : headerLabels)
        label->setBounds (header.removeFromLeft (cellW));

    viewport.setBounds (area);
    rows.setSize (labelW + numColumns * cellW + 16, numRows * rowH);

    for (int row = 0; row < numRows; ++row)
    {
        rowLabels[(size_t) row]->setBounds (0, row * rowH, labelW, rowH);

        for (int col = 0; col < numColumns; ++col)
        {
            const size_t index = (size_t) (row * numColumns + col);

            if (index < sliders.size())
                sliders[index]->setBounds (labelW + col * cellW + 2, row * rowH + 1, cellW - 4, rowH - 2);
        }
    }
}

//==============================================================================
N364LookAndFeel::N364LookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff42a2c8));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3f4950));
    setColour (juce::Slider::thumbColourId, juce::Colour (0xffd5dde0));
    setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff42a2c8));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3f4950));
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe6eef2));
}

//==============================================================================
LibraryPage::LibraryPage (N364Processor& p)
    : processor (p), listBox ("presets", this)
{
    typeLabel.setText ("Type", juce::dontSendNotification);
    addAndMakeVisible (typeLabel);

    typeBox.addItem ("Programmes", 1);
    typeBox.addItem ("Combinations (performances)", 2);
    typeBox.addItem ("Drum Kit (kit affiche)", 3);
    typeBox.addItem ("Global", 4);
    typeBox.setSelectedId (1, juce::dontSendNotification);
    typeBox.onChange = [this] { refresh(); updateButtons(); };
    addAndMakeVisible (typeBox);

    filterLabel.setText ("Filtrer", juce::dontSendNotification);
    addAndMakeVisible (filterLabel);
    filterEditor.onTextChange = [this] { refresh(); };
    addAndMakeVisible (filterEditor);

    listBox.setRowHeight (22);
    addAndMakeVisible (listBox);

    nameLabel.setText ("Nom du preset", juce::dontSendNotification);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (nameEditor);

    loadButton.onClick = [this]
    {
        const auto f = selectedFile();
        if (f.existsAsFile())
            processor.loadPreset (f, false);
    };

    loadSendButton.onClick = [this]
    {
        const auto f = selectedFile();
        if (f.existsAsFile())
            processor.loadPreset (f, true);
    };

    saveButton.onClick = [this]
    {
        auto name = nameEditor.getText().trim();

        if (name.isEmpty())
            name = processor.getLastReadName();

        if (! processor.savePreset (currentTarget(), name))
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Enregistrement impossible")
                                              .withMessage ("Donne un nom au preset.")
                                              .withButton ("OK"),
                                          nullptr);
    };

    deleteButton.onClick = [this]
    {
        const auto f = selectedFile();

        if (f.existsAsFile())
        {
            f.deleteFile();
            refresh();
        }
    };

    folderButton.onClick = [this]
    {
        const auto folder = processor.getPresetFolder (currentTarget());
        folder.createDirectory();
        folder.revealToUser();
    };

    getButton.onClick = [this]
    {
        switch (currentTarget())
        {
            case N364Target::program:     processor.requestFromSynth (N364Processor::Request::program); break;
            case N364Target::combination: processor.requestFromSynth (N364Processor::Request::combination); break;
            case N364Target::drums:       processor.requestFromSynth (N364Processor::Request::drums); break;
            case N364Target::global:      processor.requestFromSynth (N364Processor::Request::global); break;
        }
    };

    importButton.onClick = [this]
    {
        processor.requestFromSynth (currentTarget() == N364Target::combination
                                        ? N364Processor::Request::allCombinations
                                        : N364Processor::Request::allPrograms);
    };

    writeButton.onClick = [this] { confirmWrite(); };

    for (auto* b : { &loadButton, &loadSendButton, &saveButton, &deleteButton, &folderButton,
                     &getButton, &importButton, &writeButton })
        addAndMakeVisible (*b);

    synthLabel.setText ("Synthe N364 (MIDI IN et OUT a configurer en haut de la fenetre)", juce::dontSendNotification);
    synthLabel.setFont (synthLabel.getFont().boldened());
    addAndMakeVisible (synthLabel);

    bankBox.addItem ("Banque A", 1);
    bankBox.addItem ("Banque B", 2);
    bankBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (bankBox);

    for (int i = 0; i < 100; ++i)
        numberBox.addItem ("No " + juce::String (i).paddedLeft ('0', 2), i + 1);
    numberBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (numberBox);

    noteLabel.setText ("Les presets sont des fichiers dans le dossier de l'application. "
                       "Importer la banque lit les 200 sons du N364 (lecture seule).",
                       juce::dontSendNotification);
    noteLabel.setFont (noteLabel.getFont().withHeight (12.0f));
    addAndMakeVisible (noteLabel);

    updateButtons();
    refresh();
    startTimer (500);
}

N364Target LibraryPage::currentTarget() const
{
    return (N364Target) juce::jlimit (0, 3, typeBox.getSelectedId() - 1);
}

juce::File LibraryPage::selectedFile() const
{
    const int row = listBox.getSelectedRow();
    return (row >= 0 && row < files.size()) ? files[row] : juce::File();
}

void LibraryPage::refresh()
{
    const auto all = processor.listPresets (currentTarget());
    const auto filter = filterEditor.getText().trim();

    files.clear();

    for (auto& f : all)
        if (filter.isEmpty() || f.getFileNameWithoutExtension().containsIgnoreCase (filter))
            files.add (f);

    listBox.updateContent();
    listBox.repaint();
}

void LibraryPage::updateButtons()
{
    const auto t = currentTarget();
    const bool programOrCombination = (t == N364Target::program || t == N364Target::combination);

    importButton.setEnabled (programOrCombination);
    writeButton.setEnabled (programOrCombination);
    bankBox.setEnabled (programOrCombination);
    numberBox.setEnabled (programOrCombination);
}

void LibraryPage::timerCallback()
{
    if (processor.getPresetVersion() != lastVersion)
    {
        lastVersion = processor.getPresetVersion();
        refresh();
    }
}

int LibraryPage::getNumRows()
{
    return files.size();
}

void LibraryPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
        g.fillAll (juce::Colour (0xff42a2c8).withAlpha (0.35f));

    if (row >= 0 && row < files.size())
    {
        g.setColour (juce::Colour (0xffe6eef2));
        g.drawText (files[row].getFileNameWithoutExtension(), 8, 0, width - 16, height,
                    juce::Justification::centredLeft, true);
    }
}

void LibraryPage::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < files.size())
        processor.loadPreset (files[row], false);
}

void LibraryPage::confirmWrite()
{
    const auto target = currentTarget();

    if (target != N364Target::program && target != N364Target::combination)
        return;

    const bool combination = (target == N364Target::combination);
    const int bank = bankBox.getSelectedId() - 1;
    const int number = numberBox.getSelectedId() - 1;
    const juce::String place = juce::String (combination ? "Combination " : "Programme ")
                               + (bank == 0 ? "A" : "B") + juce::String (number).paddedLeft ('0', 2);

    juce::Component::SafePointer<LibraryPage> safe (this);

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Ecrire dans la memoire du N364")
                                      .withMessage ("Les reglages du plugin vont etre envoyes puis ECRITS dans la memoire du N364 ("
                                                    + place + "). Le contenu actuel de cet emplacement sera ECRASE. Continuer ?")
                                      .withButton ("Ecrire")
                                      .withButton ("Annuler"),
                                  [safe, combination, bank, number] (int result)
                                  {
                                      if (safe != nullptr && result == 1)
                                          safe->processor.writeToMemory (combination, bank, number);
                                  });
}

void LibraryPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto row = area.removeFromTop (28);
    typeLabel.setBounds (row.removeFromLeft (40));
    typeBox.setBounds (row.removeFromLeft (230));
    row.removeFromLeft (16);
    filterLabel.setBounds (row.removeFromLeft (50));
    filterEditor.setBounds (row.removeFromLeft (220));

    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (150);

    auto right = area.removeFromRight (230);
    listBox.setBounds (area.reduced (0, 0).withTrimmedRight (10));

    loadButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    loadSendButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    deleteButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    folderButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (14);
    nameLabel.setBounds (right.removeFromTop (20));
    nameEditor.setBounds (right.removeFromTop (26));
    right.removeFromTop (6);
    saveButton.setBounds (right.removeFromTop (28));

    bottom.removeFromTop (8);
    synthLabel.setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (4);

    auto line1 = bottom.removeFromTop (28);
    getButton.setBounds (line1.removeFromLeft (190));
    line1.removeFromLeft (10);
    importButton.setBounds (line1.removeFromLeft (250));

    bottom.removeFromTop (6);
    auto line2 = bottom.removeFromTop (28);
    bankBox.setBounds (line2.removeFromLeft (110));
    line2.removeFromLeft (8);
    numberBox.setBounds (line2.removeFromLeft (90));
    line2.removeFromLeft (10);
    writeButton.setBounds (line2.removeFromLeft (270));

    bottom.removeFromTop (6);
    noteLabel.setBounds (bottom.removeFromTop (34));
}

//==============================================================================
N364Editor::N364Editor (N364Processor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    // --- Mode de sortie
    modeLabel.setText ("Sortie", juce::dontSendNotification);
    addAndMakeVisible (modeLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("outmode")))
        modeBox.addItemList (choice->choices, 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "outmode", modeBox);

    // --- Port MIDI de sortie (mode direct)
    portLabel.setText ("MIDI OUT", juce::dontSendNotification);
    addAndMakeVisible (portLabel);

    outputDevices = juce::MidiOutput::getAvailableDevices();
    const auto savedOut = processor.getMidiOutputDevice();

    for (int i = 0; i < outputDevices.size(); ++i)
    {
        portBox.addItem (outputDevices[i].name, i + 1);

        if (outputDevices[i].identifier == savedOut)
            portBox.setSelectedItemIndex (i, juce::dontSendNotification);
    }

    portBox.setTextWhenNothingSelected ("(vers le N364)");
    portBox.onChange = [this]
    {
        const int index = portBox.getSelectedItemIndex();

        if (index >= 0 && index < outputDevices.size())
            processor.setMidiOutputDevice (outputDevices[index].identifier);
    };
    addAndMakeVisible (portBox);

    // --- Port MIDI d'entree (pour lire le N364)
    inPortLabel.setText ("MIDI IN", juce::dontSendNotification);
    addAndMakeVisible (inPortLabel);

    inputDevices = juce::MidiInput::getAvailableDevices();
    const auto savedIn = processor.getMidiInputDevice();

    for (int i = 0; i < inputDevices.size(); ++i)
    {
        inPortBox.addItem (inputDevices[i].name, i + 1);

        if (inputDevices[i].identifier == savedIn)
            inPortBox.setSelectedItemIndex (i, juce::dontSendNotification);
    }

    inPortBox.setTextWhenNothingSelected ("(depuis le N364)");
    inPortBox.onChange = [this]
    {
        const int index = inPortBox.getSelectedItemIndex();

        if (index >= 0 && index < inputDevices.size())
            processor.setMidiInputDevice (inputDevices[index].identifier);
    };
    addAndMakeVisible (inPortBox);

    // --- Canal global
    channelLabel.setText ("Canal global", juce::dontSendNotification);
    addAndMakeVisible (channelLabel);

    channelSlider.setSliderStyle (juce::Slider::IncDecButtons);
    channelSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 36, 24);
    addAndMakeVisible (channelSlider);
    channelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "globalch", channelSlider);

    // --- Cible (Program / Combination / ...) : suit automatiquement l'onglet affiche
    targetLabel.setText ("Cible", juce::dontSendNotification);
    addAndMakeVisible (targetLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("target")))
        targetBox.addItemList (choice->choices, 1);
    addAndMakeVisible (targetBox);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "target", targetBox);

    // --- Synchro automatique depuis le N364
    addAndMakeVisible (autoSyncToggle);
    autoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "autosync", autoSyncToggle);

    // --- Bouton "Envoyer tout"
    sendAllButton.onClick = [this] { processor.sendAll(); };
    addAndMakeVisible (sendAllButton);

    // --- Onglets
    std::vector<const N364Param*> programPage, osc1Page, osc2Page, effectsPage, combinationPage, drumsPage, globalPage;

    for (auto& param : getParamTable())
    {
        if (param.target == N364Target::combination)
            combinationPage.push_back (&param);
        else if (param.target == N364Target::drums)
            drumsPage.push_back (&param);
        else if (param.target == N364Target::global)
            globalPage.push_back (&param);
        else if (param.group.startsWith ("OSC-1"))
            osc1Page.push_back (&param);
        else if (param.group.startsWith ("OSC-2"))
            osc2Page.push_back (&param);
        else if (param.group.startsWith ("Effets"))
            effectsPage.push_back (&param);
        else
            programPage.push_back (&param);
    }

    const auto& graphs = getGraphs();
    const auto tabColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);

    tabs.addTab ("Programme", tabColour, new PageView (processor.apvts, programPage, graphs), true);
    tabs.addTab ("OSC-1", tabColour, new PageView (processor.apvts, osc1Page, graphs), true);
    tabs.addTab ("OSC-2", tabColour, new PageView (processor.apvts, osc2Page, graphs), true);
    tabs.addTab ("Effets", tabColour, new PageView (processor.apvts, effectsPage, graphs), true);
    tabs.addTab ("Combination", tabColour, new PageView (processor.apvts, combinationPage, graphs), true);
    tabs.addTab ("Drums", tabColour, new DrumPage (processor.apvts, drumsPage), true);
    tabs.addTab ("Global", tabColour, new PageView (processor.apvts, globalPage, graphs), true);
    tabs.addTab ("Bibliotheque", tabColour, new LibraryPage (processor), true);

    tabs.onTabChanged = [this] (const juce::String& name)
    {
        if (name == "Bibliotheque")
            return;   // la bibliotheque ne change pas la cible

        const int index = name == "Combination" ? 1 : name == "Drums" ? 2 : name == "Global" ? 3 : 0;
        processor.setTarget ((N364Target) index);
    };
    addAndMakeVisible (tabs);
    tabs.onTabChanged (tabs.getCurrentTabName());   // aligne la cible sur l'onglet affiche

    // --- Etat et aide
    statusLabel.setFont (statusLabel.getFont().withHeight (13.0f));
    addAndMakeVisible (statusLabel);

    hintLabel.setText ("Filtre SysEx du N364 sur ENA, meme canal global. Modes du N364 : onglets Programme/OSC/Effets = Edit Program ; "
                       "Combination = Edit Combination ; Drums et Global = mode Global.",
                       juce::dontSendNotification);
    hintLabel.setFont (hintLabel.getFont().withHeight (12.0f));
    addAndMakeVisible (hintLabel);

    setResizable (true, true);
    setResizeLimits (960, 520, 1700, 1300);
    setSize (1000, 760);

    startTimer (250);
}

N364Editor::~N364Editor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void N364Editor::timerCallback()
{
    statusLabel.setText (processor.getStatus(), juce::dontSendNotification);
}

void N364Editor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void N364Editor::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto row1 = area.removeFromTop (28);
    modeLabel.setBounds (row1.removeFromLeft (46));
    modeBox.setBounds (row1.removeFromLeft (230));
    row1.removeFromLeft (10);
    portLabel.setBounds (row1.removeFromLeft (70));
    portBox.setBounds (row1.removeFromLeft (230));
    row1.removeFromLeft (10);
    channelLabel.setBounds (row1.removeFromLeft (84));
    channelSlider.setBounds (row1.removeFromLeft (100));

    area.removeFromTop (6);
    auto row2 = area.removeFromTop (28);
    targetLabel.setBounds (row2.removeFromLeft (46));
    targetBox.setBounds (row2.removeFromLeft (230));
    row2.removeFromLeft (10);
    inPortLabel.setBounds (row2.removeFromLeft (70));
    inPortBox.setBounds (row2.removeFromLeft (230));
    row2.removeFromLeft (10);
    sendAllButton.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (10);
    autoSyncToggle.setBounds (row2.removeFromLeft (190));

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (20));

    area.removeFromTop (4);
    hintLabel.setBounds (area.removeFromBottom (34));
    tabs.setBounds (area);
}
