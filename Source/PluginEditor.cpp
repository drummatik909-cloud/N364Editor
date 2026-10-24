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

            juce::Slider* sliderPtr = slider.get();
            cell.kind = Kind::knob;
            cell.comp = std::move (slider);

            // Libelles dynamiques : nom du Multisound, ou parametre d'effet selon le type choisi
            if (p->nameSet == 1)
            {
                Dynamic d;
                d.kind = 1;
                d.label = cell.label.get();
                d.slider = sliderPtr;
                d.valueRaw = state.getRawParameterValue (p->id);
                d.baseName = p->name;
                dynamics.push_back (d);
            }
            else if (p->name.startsWith ("Param ") && (p->group.endsWith ("Effet 1") || p->group.endsWith ("Effet 2")))
            {
                juce::String typeId;

                for (auto* q : params)
                    if (q->group == p->group && q->name == "Type")
                    {
                        typeId = q->id;
                        break;
                    }

                if (typeId.isNotEmpty())
                {
                    Dynamic d;
                    d.kind = 0;
                    d.label = cell.label.get();
                    d.slider = sliderPtr;
                    d.typeRaw = state.getRawParameterValue (typeId);
                    d.k = p->name.substring (6).getIntValue() - 1;
                    d.baseName = p->name;
                    dynamics.push_back (d);
                }
            }
        }

        addAndMakeVisible (*cell.comp);
        items.push_back (std::move (cell));
    }

    if (lastGroup.isNotEmpty())
        addGraphFor (lastGroup);

    // Les parametres d'effet restent dans la plage du type d'effet choisi
    for (size_t i = 0; i < dynamics.size(); ++i)
    {
        if (dynamics[i].kind != 0)
            continue;

        dynamics[i].slider->onValueChange = [this, i]
        {
            auto& d = dynamics[i];
            const double v = d.slider->getValue();
            const double clamped = juce::jlimit (d.lo, d.hi, v);

            if (clamped != v)
                d.slider->setValue (clamped, juce::sendNotificationSync);
        };
    }

    if (! dynamics.empty())
        startTimerHz (5);
}

void ParamPage::timerCallback()
{
    for (auto& d : dynamics)
    {
        if (d.kind == 1)
        {
            const int v = juce::jlimit (0, 429, juce::roundToInt (d.valueRaw->load()));

            if (v != d.lastKey)
            {
                d.lastKey = v;
                d.label->setText (juce::String (v) + " " + juce::String (n364factory::multisound[v]),
                                  juce::dontSendNotification);
            }

            continue;
        }

        const int type = juce::roundToInt (d.typeRaw->load());
        const int key = type * 8 + d.k;

        if (key == d.lastKey)
            continue;

        d.lastKey = key;
        const auto& info = getEffectInfo (type);

        if (d.k < info.count)
        {
            const auto& pi = info.params[d.k];
            d.lo = pi.lo;
            d.hi = pi.hi;

            juce::String tip = juce::String (pi.lo) + " a " + juce::String (pi.hi);
            if (pi.note[0] != 0)
                tip += " : " + juce::String (pi.note);

            d.label->setText (pi.name, juce::dontSendNotification);
            d.label->setTooltip (tip);
            d.slider->setTooltip (tip);
            d.slider->setEnabled (true);
            d.slider->setAlpha (1.0f);

            const double v = d.slider->getValue();
            const double clamped = juce::jlimit (d.lo, d.hi, v);

            if (clamped != v)
                d.slider->setValue (clamped, juce::sendNotificationSync);
        }
        else
        {
            d.lo = d.hi = 0.0;
            d.label->setText ("(inutilise)", juce::dontSendNotification);
            d.label->setTooltip ("Ce parametre n'existe pas pour cet effet.");
            d.slider->setEnabled (false);
            d.slider->setAlpha (0.3f);
        }
    }
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

            if (params[index]->nameSet == 2)
            {
                // valeur 0 = OFF, valeur v = son de batterie n° v-1 (liste du chapitre 14)
                slider->textFromValueFunction = [] (double v)
                {
                    const int i = juce::roundToInt (v);
                    if (i <= 0)
                        return juce::String ("OFF");

                    return i <= 215 ? juce::String (n364factory::drumsound[i - 1]) : juce::String ("CARD " + juce::String (i));
                };
                slider->valueFromTextFunction = [] (const juce::String& t) { return (double) t.getIntValue(); };
                slider->updateText();
            }

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
// Petites fenetres de dialogue
namespace
{
    void askSlot (const juce::String& title, const juce::String& message, const juce::String& okText,
                  std::function<void (int, int)> onChosen)
    {
        auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon);
        w->addComboBox ("bank", juce::StringArray { "Banque A", "Banque B" }, "Banque");

        juce::StringArray numbers;
        for (int i = 0; i < 100; ++i)
            numbers.add ("No " + juce::String (i).paddedLeft ('0', 2));

        w->addComboBox ("number", numbers, "Numero");
        w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onChosen] (int result)
        {
            if (result == 1)
                onChosen (w->getComboBoxComponent ("bank")->getSelectedItemIndex(),
                          w->getComboBoxComponent ("number")->getSelectedItemIndex());
        }), true);
    }

    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  const juce::String& okText, std::function<void (juce::String)> onChosen)
    {
        auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon);
        w->addTextEditor ("text", initial, "Nom");
        w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onChosen] (int result)
        {
            if (result == 1)
                onChosen (w->getTextEditorContents ("text"));
        }), true);
    }

    void saveSyxDialog (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title,
                        const juce::String& defaultName, std::function<void (juce::File)> onChosen)
    {
        holder = std::make_unique<juce::FileChooser> (
            title, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (defaultName), "*.syx");

        holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [onChosen] (const juce::FileChooser& fc)
                             {
                                 const auto f = fc.getResult();

                                 if (f != juce::File())
                                     onChosen (f);
                             });
    }

    void openSyxDialog (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title,
                        std::function<void (juce::File)> onChosen)
    {
        holder = std::make_unique<juce::FileChooser> (
            title, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.syx");

        holder->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [onChosen] (const juce::FileChooser& fc)
                             {
                                 const auto f = fc.getResult();

                                 if (f != juce::File())
                                     onChosen (f);
                             });
    }

    void confirmDialog (const juce::String& title, const juce::String& message, const juce::String& okText,
                        std::function<void()> onOk)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton (okText)
                                          .withButton ("Annuler"),
                                      [onOk] (int result)
                                      {
                                          if (result == 1)
                                              onOk();
                                      });
    }
}

//==============================================================================
namespace
{
    juce::String bankSlotLabel (int bank, int number)
    {
        if (bank <= 3)
            return juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (number).paddedLeft ('0', 2);

        return "G" + juce::String (number).paddedLeft ('0', 3);
    }
}

BankPage::BankPage (N364Processor& p)
    : processor (p), listBox ("slots", this)
{
    kindLabel.setText ("Type", juce::dontSendNotification);
    addAndMakeVisible (kindLabel);

    kindBox.addItem ("Programmes", 1);
    kindBox.addItem ("Combinations", 2);
    kindBox.setSelectedId (1, juce::dontSendNotification);
    kindBox.onChange = [this] { bankChanged(); };
    addAndMakeVisible (kindBox);

    const char* names[5] = { "A", "B", "C", "D", "GM" };

    for (int i = 0; i < 5; ++i)
    {
        auto& b = bankButtons[(size_t) i];
        b.setButtonText (names[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4242);
        b.onClick = [this, i] { selectedBank = i; bankChanged(); };
        addAndMakeVisible (b);
    }

    bankButtons[0].setToggleState (true, juce::dontSendNotification);

    followAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "follow", followToggle);
    addAndMakeVisible (followToggle);

    currentLabel.setFont (currentLabel.getFont().boldened());
    addAndMakeVisible (currentLabel);

    listBox.setRowHeight (22);
    addAndMakeVisible (listBox);

    readButton.onClick = [this]
    {
        processor.requestFromSynth (isCombination() ? N364Processor::Request::combination
                                                    : N364Processor::Request::program);
    };

    importButton.onClick = [this] { processor.startCrawl (isCombination(), selectedBank); };
    cancelButton.onClick = [this] { processor.cancelCrawl(); processor.cancelTask(); };

    renameButton.onClick = [this]
    {
        const int row = listBox.getSelectedRow();

        if (row < 0)
        {
            statusLabel.setText ("Selectionne d'abord un son dans la liste.", juce::dontSendNotification);
            return;
        }

        const bool combination = isCombination();
        const int bank = selectedBank;
        const int number = numberForRow (row);
        const auto current = processor.getSlotName (combination, bank, number);
        juce::Component::SafePointer<BankPage> safe (this);

        askText ("Renommer le son sur le N364",
                 "Nouveau nom (10 caracteres maximum). Le son sera reecrit dans la memoire du N364 (" + bankSlotLabel (bank, number)
                     + "). Seules les banques A et B sont modifiables.",
                 current, "Renommer",
                 [safe, combination, bank, number] (juce::String name)
                 {
                     if (safe != nullptr)
                         safe->processor.renameOnSynth (combination, bank, number, name);
                 });
    };

    copyButton.onClick = [this]
    {
        const int row = listBox.getSelectedRow();

        if (row < 0)
        {
            statusLabel.setText ("Selectionne d'abord un son dans la liste.", juce::dontSendNotification);
            return;
        }

        const bool combination = isCombination();
        const int bank = selectedBank;
        const int number = numberForRow (row);
        juce::Component::SafePointer<BankPage> safe (this);

        askSlot ("Copier le son " + bankSlotLabel (bank, number),
                 "Destination : un emplacement des banques A ou B. Son contenu actuel sera ECRASE.", "Copier",
                 [safe, combination, bank, number] (int dstBank, int dstNumber)
                 {
                     if (safe != nullptr)
                         safe->processor.copySlot (combination, bank, number, dstBank, dstNumber);
                 });
    };

    swapButton.onClick = [this]
    {
        const int row = listBox.getSelectedRow();

        if (row < 0)
        {
            statusLabel.setText ("Selectionne d'abord un son dans la liste.", juce::dontSendNotification);
            return;
        }

        const bool combination = isCombination();
        const int bank = selectedBank;
        const int number = numberForRow (row);
        juce::Component::SafePointer<BankPage> safe (this);

        askSlot ("Echanger le son " + bankSlotLabel (bank, number),
                 "Choisis l'autre emplacement (banques A ou B). Les deux sons seront echanges dans la memoire du N364.", "Echanger",
                 [safe, combination, bank, number] (int otherBank, int otherNumber)
                 {
                     if (safe != nullptr)
                         safe->processor.swapSlots (combination, bank, number, otherBank, otherNumber);
                 });
    };

    addAndMakeVisible (readButton);
    addAndMakeVisible (importButton);
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (renameButton);
    addAndMakeVisible (copyButton);
    addAndMakeVisible (swapButton);

    statusLabel.setFont (statusLabel.getFont().withHeight (13.0f));
    addAndMakeVisible (statusLabel);

    noteLabel.setText ("Clique sur un son : le N364 le selectionne, puis le plugin le lit et met ses potards a jour. Quand tu changes de son sur le N364, "
                       "le plugin suit. Noms en bleu clair = lus sur le N364 ; les autres viennent du manuel. Renommer, copier et echanger "
                       "modifient la memoire du N364 (banques A et B seulement). Les banques C, D et GM sont des sons d'usine : on peut les "
                       "lire et les copier vers A ou B.",
                       juce::dontSendNotification);
    noteLabel.setFont (noteLabel.getFont().withHeight (12.0f));
    addAndMakeVisible (noteLabel);

    bankChanged();
    startTimer (200);
}

bool BankPage::isCombination() const
{
    return kindBox.getSelectedId() == 2;
}

int BankPage::currentBank() const
{
    return selectedBank;
}

int BankPage::numberForRow (int row) const
{
    return (! isCombination() && selectedBank == 4) ? row + 1 : row;
}

int BankPage::rowForNumber (int number) const
{
    return (! isCombination() && selectedBank == 4) ? number - 1 : number;
}

int BankPage::getNumRows()
{
    return (! isCombination() && selectedBank == 4) ? 136 : 100;
}

void BankPage::bankChanged()
{
    if (isCombination() && selectedBank == 4)
    {
        selectedBank = 0;
        bankButtons[0].setToggleState (true, juce::dontSendNotification);
    }

    bankButtons[4].setEnabled (! isCombination());
    listBox.updateContent();
    listBox.repaint();

    const auto cur = processor.getCurrentSlot();

    if (cur.bank == selectedBank && cur.combination == isCombination() && cur.number >= 0)
        listBox.scrollToEnsureRowIsOnscreen (rowForNumber (cur.number));
}

void BankPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const bool combination = isCombination();
    const int number = numberForRow (row);
    const auto cur = processor.getCurrentSlot();
    const bool isCurrent = cur.bank == selectedBank && cur.combination == combination && cur.number == number;

    if (isCurrent)
        g.fillAll (juce::Colour (0xff42a2c8).withAlpha (0.45f));
    else if (selected)
        g.fillAll (juce::Colour (0xff42a2c8).withAlpha (0.2f));

    bool live = false;
    const auto name = processor.getSlotName (combination, selectedBank, number, &live);

    g.setColour (juce::Colour (0xffaab6bb));
    g.drawText (bankSlotLabel (selectedBank, number), 8, 0, 64, height, juce::Justification::centredLeft);

    g.setColour (live ? juce::Colour (0xff9fe3ff) : juce::Colour (0xffe6eef2));
    g.drawText (name, 76, 0, width - 190, height, juce::Justification::centredLeft, true);

    juce::String tag;

    if (! combination)
    {
        const char kind = selectedBank <= 3 ? n364factory::programs[selectedBank][number].kind
                                            : n364factory::gm[number - 1].kind;
        tag = kind == 'D' ? "Double" : kind == 'R' ? "Drums" : "";
    }
    else if (selectedBank <= 3 && number < 100)
    {
        tag = n364factory::combinations[selectedBank][number].type;
    }

    g.setColour (juce::Colour (0xff8a979d));
    g.drawText (tag, width - 110, 0, 100, height, juce::Justification::centredRight);
}

void BankPage::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < getNumRows())
        processor.selectSlot (isCombination(), selectedBank, numberForRow (row));
}

void BankPage::timerCallback()
{
    statusLabel.setText (processor.getStatus(), juce::dontSendNotification);

    const int v = processor.getSlotVersion();
    const int pv = processor.getPresetVersion();

    if (v == lastSlotVersion && pv == lastPresetVersion)
        return;

    lastSlotVersion = v;
    lastPresetVersion = pv;

    const auto cur = processor.getCurrentSlot();

    if (cur.bank >= 0)
    {
        currentLabel.setText ("N364 : " + juce::String (cur.combination ? "Combination " : "Programme ")
                              + bankSlotLabel (cur.bank, cur.number) + "  " + processor.getSlotName (cur.combination, cur.bank, cur.number),
                              juce::dontSendNotification);

        // suivi : la liste se place sur le son courant du N364
        if (followToggle.getToggleState() && (cur.combination != isCombination() || cur.bank != selectedBank))
        {
            kindBox.setSelectedId (cur.combination ? 2 : 1, juce::dontSendNotification);
            selectedBank = cur.bank;
            bankButtons[(size_t) cur.bank].setToggleState (true, juce::dontSendNotification);
            bankChanged();
        }
        else if (followToggle.getToggleState())
        {
            listBox.scrollToEnsureRowIsOnscreen (rowForNumber (cur.number));
        }
    }

    listBox.repaint();
}

void BankPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto row1 = area.removeFromTop (28);
    kindLabel.setBounds (row1.removeFromLeft (40));
    kindBox.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (16);

    for (auto& b : bankButtons)
    {
        b.setBounds (row1.removeFromLeft (56));
        row1.removeFromLeft (4);
    }

    row1.removeFromLeft (16);
    followToggle.setBounds (row1.removeFromLeft (240));

    area.removeFromTop (6);
    currentLabel.setBounds (area.removeFromTop (24));

    auto bottom = area.removeFromBottom (150);
    area.removeFromBottom (6);
    listBox.setBounds (area);

    auto buttons = bottom.removeFromTop (28);
    readButton.setBounds (buttons.removeFromLeft (170));
    buttons.removeFromLeft (10);
    importButton.setBounds (buttons.removeFromLeft (260));
    buttons.removeFromLeft (10);
    cancelButton.setBounds (buttons.removeFromLeft (150));

    bottom.removeFromTop (6);
    auto buttons2 = bottom.removeFromTop (28);
    renameButton.setBounds (buttons2.removeFromLeft (200));
    buttons2.removeFromLeft (10);
    copyButton.setBounds (buttons2.removeFromLeft (180));
    buttons2.removeFromLeft (10);
    swapButton.setBounds (buttons2.removeFromLeft (150));

    bottom.removeFromTop (6);
    statusLabel.setBounds (bottom.removeFromTop (22));
    noteLabel.setBounds (bottom);
}

//==============================================================================
ExchangePage::ExchangePage (N364Processor& p)
    : processor (p)
{
    titleBackup.setText ("Banques A+B du N364", juce::dontSendNotification);
    titleBackup.setFont (titleBackup.getFont().boldened());
    addAndMakeVisible (titleBackup);

    titleFiles.setText ("Bibliotheque et fichiers .syx", juce::dontSendNotification);
    titleFiles.setFont (titleFiles.getFont().boldened());
    addAndMakeVisible (titleFiles);

    juce::Component::SafePointer<ExchangePage> safe (this);

    backupProgramsButton.onClick = [this, safe]
    {
        saveSyxDialog (chooser, "Sauvegarder les Programs A+B", "N364_Programs_AB.syx",
                       [safe] (juce::File f)
                       {
                           if (safe != nullptr)
                               safe->processor.backupBanksToFile (false, f);
                       });
    };

    backupCombisButton.onClick = [this, safe]
    {
        saveSyxDialog (chooser, "Sauvegarder les Combinations A+B", "N364_Combinations_AB.syx",
                       [safe] (juce::File f)
                       {
                           if (safe != nullptr)
                               safe->processor.backupBanksToFile (true, f);
                       });
    };

    restoreButton.onClick = [this, safe]
    {
        openSyxDialog (chooser, "Choisis le fichier .syx a restaurer",
                       [safe] (juce::File f)
                       {
                           if (safe == nullptr)
                               return;

                           confirmDialog ("Restaurer des banques",
                                          "Les banques A et B du N364 (Programs ou Combinations selon le fichier) vont etre REMPLACEES par le contenu de "
                                          + f.getFileName() + ". Le plugin sauvegarde d'abord l'etat actuel du N364 dans son dossier de sauvegardes. "
                                          "Continuer ?",
                                          "Restaurer",
                                          [safe, f]
                                          {
                                              if (safe != nullptr)
                                                  safe->processor.restoreBanksFromFile (f);
                                          });
                       });
    };

    openBackupsButton.onClick = [this]
    {
        const auto folder = processor.getBackupFolder();
        folder.createDirectory();
        folder.revealToUser();
    };

    importButton.onClick = [this, safe]
    {
        openSyxDialog (chooser, "Choisis un fichier .syx a importer",
                       [safe] (juce::File f)
                       {
                           if (safe != nullptr)
                               safe->processor.importSyxFile (f);
                       });
    };

    exportProgramsButton.onClick = [this, safe]
    {
        saveSyxDialog (chooser, "Exporter la bibliotheque Programs A+B", "Bibliotheque_Programs_AB.syx",
                       [safe] (juce::File f)
                       {
                           if (safe != nullptr)
                               safe->processor.exportLibraryBanksSyx (false, f);
                       });
    };

    exportCombisButton.onClick = [this, safe]
    {
        saveSyxDialog (chooser, "Exporter la bibliotheque Combinations A+B", "Bibliotheque_Combinations_AB.syx",
                       [safe] (juce::File f)
                       {
                           if (safe != nullptr)
                               safe->processor.exportLibraryBanksSyx (true, f);
                       });
    };

    cancelButton.onClick = [this] { processor.cancelCrawl(); processor.cancelTask(); };

    for (auto* b : { &backupProgramsButton, &backupCombisButton, &restoreButton, &openBackupsButton, &importButton,
                     &exportProgramsButton, &exportCombisButton, &cancelButton })
        addAndMakeVisible (*b);

    statusLabel.setFont (statusLabel.getFont().withHeight (13.0f));
    addAndMakeVisible (statusLabel);

    noteLabel.setText ("Sauvegarder : le plugin demande au N364 ses 200 Programs (ou Combinations) A+B et les enregistre tels quels dans un fichier .syx "
                       "(a conserver precieusement). Restaurer : le fichier est renvoye au N364 apres une sauvegarde automatique de l'etat actuel ; "
                       "il faut le mode \"Port MIDI direct\". Importer : les sons d'un .syx (un son ou une banque) deviennent des presets de la bibliotheque. "
                       "Exporter la bibliotheque : les presets A00 a B99 (avec donnees completes) sont regroupes en une banque .syx.",
                       juce::dontSendNotification);
    noteLabel.setFont (noteLabel.getFont().withHeight (12.0f));
    addAndMakeVisible (noteLabel);

    startTimer (250);
}

void ExchangePage::timerCallback()
{
    statusLabel.setText (processor.getStatus(), juce::dontSendNotification);
}

void ExchangePage::resized()
{
    auto area = getLocalBounds().reduced (14);

    titleBackup.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    auto row = area.removeFromTop (30);
    backupProgramsButton.setBounds (row.removeFromLeft (330));
    row.removeFromLeft (10);
    backupCombisButton.setBounds (row.removeFromLeft (350));

    area.removeFromTop (8);
    row = area.removeFromTop (30);
    restoreButton.setBounds (row.removeFromLeft (330));
    row.removeFromLeft (10);
    openBackupsButton.setBounds (row.removeFromLeft (350));

    area.removeFromTop (22);
    titleFiles.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    row = area.removeFromTop (30);
    importButton.setBounds (row.removeFromLeft (330));

    area.removeFromTop (8);
    row = area.removeFromTop (30);
    exportProgramsButton.setBounds (row.removeFromLeft (330));
    row.removeFromLeft (10);
    exportCombisButton.setBounds (row.removeFromLeft (350));

    area.removeFromTop (22);
    row = area.removeFromTop (30);
    cancelButton.setBounds (row.removeFromLeft (200));

    area.removeFromTop (10);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);
    noteLabel.setBounds (area.removeFromTop (90));
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

    renameButton.onClick = [this]
    {
        const auto f = selectedFile();
        const auto name = nameEditor.getText().trim();

        if (! f.existsAsFile() || name.isEmpty())
            return;

        if (processor.renamePreset (f, name))
            refresh();
    };

    sendDumpButton.onClick = [this]
    {
        const auto f = selectedFile();

        if (f.existsAsFile())
            processor.sendPresetToSynth (f, -1, 0);
    };

    writeSlotButton.onClick = [this]
    {
        const auto f = selectedFile();

        if (! f.existsAsFile())
            return;

        juce::Component::SafePointer<LibraryPage> safe (this);

        askSlot ("Ecrire ce preset dans le N364",
                 "Emplacement des banques A ou B. Son contenu actuel sera ECRASE.", "Ecrire",
                 [safe, f] (int bank, int number)
                 {
                     if (safe != nullptr)
                         safe->processor.sendPresetToSynth (f, bank, number);
                 });
    };

    exportButton.onClick = [this]
    {
        const auto f = selectedFile();

        if (! f.existsAsFile())
            return;

        juce::Component::SafePointer<LibraryPage> safe (this);

        saveSyxDialog (chooser, "Exporter le preset en .syx", f.getFileNameWithoutExtension() + ".syx",
                       [safe, f] (juce::File dest)
                       {
                           if (safe != nullptr)
                               safe->processor.exportPresetSyx (f, dest);
                       });
    };

    for (auto* b : { &loadButton, &loadSendButton, &saveButton, &deleteButton, &folderButton,
                     &getButton, &importButton, &writeButton, &renameButton, &sendDumpButton,
                     &writeSlotButton, &exportButton })
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
    sendDumpButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    writeSlotButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    exportButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (14);
    nameLabel.setBounds (right.removeFromTop (20));
    nameEditor.setBounds (right.removeFromTop (26));
    right.removeFromTop (6);
    saveButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    renameButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (14);
    deleteButton.setBounds (right.removeFromTop (28));
    right.removeFromTop (6);
    folderButton.setBounds (right.removeFromTop (28));

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
    tabs.addTab ("Banques", tabColour, new BankPage (processor), true);
    tabs.addTab ("Bibliotheque", tabColour, new LibraryPage (processor), true);
    tabs.addTab ("SysEx", tabColour, new ExchangePage (processor), true);

    tabs.onTabChanged = [this] (const juce::String& name)
    {
        if (name == "Bibliotheque" || name == "Banques" || name == "SysEx")
            return;   // ces onglets ne changent pas la cible

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
