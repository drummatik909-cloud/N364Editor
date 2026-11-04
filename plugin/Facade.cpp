#include "Facade.h"
#include <BinaryData.h>
#include "engine/Scale.h"
#include <cmath>
#include <cstdio>

using namespace juce;

namespace maqui {

namespace {

// Positions mesurées sur la photo de la façade (1369 x 502)
constexpr float kKnobX0 = 366.5f;
constexpr float kKnobDX = 57.56f;
constexpr float kKnobY[3] = {141.5f, 282.9f, 424.5f};
constexpr float kCapRadius = 14.5f;

constexpr float kLedX0 = 360.0f;
constexpr float kLedDX = 57.53f;
constexpr int kLedY[3] = {87, 229, 370};

constexpr int kMenuLedX[4] = {138, 174, 210, 245};
constexpr int kMenuLedY[2] = {207, 257};
constexpr int kMenuBtnCx[4] = {145, 181, 217, 252};
constexpr int kMenuBtnCy[2] = {236, 286};

constexpr float kDigitCx[3] = {165.5f, 198.5f, 232.0f};
constexpr float kDigitTop = 78.0f;
constexpr float kDigitW = 20.0f;
constexpr float kDigitH = 37.0f;

constexpr int kEncoderCx = 199;
constexpr int kEncoderCy = 373;

// 0 EVENT, 1 CHN, 2 FIRST/LAST, 3 PRESCALE, 4 MODE, 5 SINGLE STEP, 6 PRESET, 7 START/STOP

uint8 glyphMask(char c) {
    switch (c) {
        case '0': return 0x3F; case '1': return 0x06; case '2': return 0x5B; case '3': return 0x4F;
        case '4': return 0x66; case '5': return 0x6D; case '6': return 0x7D; case '7': return 0x07;
        case '8': return 0x7F; case '9': return 0x6F;
        case 'A': return 0x77; case 'b': return 0x7C; case 'C': return 0x39; case 'c': return 0x58;
        case 'd': return 0x5E; case 'E': return 0x79; case 'F': return 0x71; case 'G': return 0x3D;
        case 'g': return 0x6F; case 'H': return 0x76; case 'I': return 0x30; case 'L': return 0x38;
        case 'n': return 0x54; case 'O': return 0x3F; case 'o': return 0x5C; case 'P': return 0x73;
        case 'r': return 0x50; case 'S': return 0x6D; case 't': return 0x78; case 'U': return 0x3E;
        case 'Y': return 0x6E; case '-': return 0x40;
        default: return 0x00;
    }
}

void paintDigit(Graphics& g, uint8 mask, bool dot, float x, float y, float w, float h) {
    const float t = 3.6f;
    const float half = h * 0.5f;
    const Colour on(0xffff2a1a);
    const Colour off(0x2dff2a1a);
    auto seg = [&](int bit, float sx, float sy, float sw, float sh) {
        const bool lit = (mask & (1 << bit)) != 0;
        if (lit) {
            g.setColour(on.withAlpha(0.28f));
            g.fillRoundedRectangle(sx - 1.2f, sy - 1.2f, sw + 2.4f, sh + 2.4f, 2.0f);
        }
        g.setColour(lit ? on : off);
        g.fillRoundedRectangle(sx, sy, sw, sh, 1.2f);
    };
    seg(0, x + t, y, w - 2 * t, t);                          // a
    seg(1, x + w - t, y + t * 0.8f, t, half - t * 1.4f);      // b
    seg(2, x + w - t, y + half + t * 0.6f, t, half - t * 1.4f);  // c
    seg(3, x + t, y + h - t, w - 2 * t, t);                  // d
    seg(4, x, y + half + t * 0.6f, t, half - t * 1.4f);      // e
    seg(5, x, y + t * 0.8f, t, half - t * 1.4f);             // f
    seg(6, x + t, y + half - t * 0.5f, w - 2 * t, t);        // g

    // point décimal (sert de dièse pour les noms de notes, et d'indicateur « slot utilisé »)
    const float dx = x + w + 3.5f;
    const float dy = y + h - 1.5f;
    if (dot) {
        g.setColour(on.withAlpha(0.28f));
        g.fillEllipse(dx - 3.4f, dy - 3.4f, 6.8f, 6.8f);
    }
    g.setColour(dot ? on : off);
    g.fillEllipse(dx - 2.2f, dy - 2.2f, 4.4f, 4.4f);
}

void paintLed(Graphics& g, Rectangle<int> r, int level) {
    const auto f = r.toFloat();
    if (level >= 2) {
        g.setColour(Colour(0xffff2a14).withAlpha(0.30f));
        g.fillRect(f.expanded(2.0f));
        g.setColour(Colour(0xffff2a14));
    } else if (level == 1) {
        g.setColour(Colour(0xff7a0f0f));
    } else {
        g.setColour(Colour(0xff2b0606));
    }
    g.fillRect(f);
}

// Nombre aligné à droite sur trois caractères
std::string num3(int v) {
    std::string s = std::to_string(v);
    while (s.size() < 3) s = " " + s;
    return s.substr(s.size() - 3);
}

// Nom de note sur trois afficheurs : lettre (+ point = dièse), signe de l'octave, chiffre
std::string segNote(int midi) {
    static const char letters[12] = {'C', 'C', 'd', 'd', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'b'};
    static const bool sharp[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
    const int n = jlimit(0, 127, midi);
    const int octave = n / 12 - 2;
    std::string s(1, letters[n % 12]);
    if (sharp[n % 12]) s += '.';
    s += (octave < 0) ? '-' : ' ';
    s += static_cast<char>('0' + std::abs(octave));
    return s;
}

String fr(const char* utf8) { return String::fromUTF8(utf8); }
String S(const std::string& utf8) { return String::fromUTF8(utf8.c_str()); }

}  // namespace

// ---------------------------------------------------------------- Look and feel

void MaqLookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int width, int height, float pos,
                                      float startAngle, float endAngle, Slider&) {
    const auto c = Rectangle<int>(x, y, width, height).toFloat().getCentre();
    const float r = kCapRadius;

    ColourGradient grad(Colour(0xffeeeeee), c.x - r * 0.5f, c.y - r * 0.6f,
                        Colour(0xff666666), c.x + r * 0.7f, c.y + r * 0.8f, false);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    g.setColour(Colour(0xff151515));
    g.drawEllipse(c.x - r, c.y - r, 2.0f * r, 2.0f * r, 1.0f);

    const float a = startAngle + pos * (endAngle - startAngle);
    g.setColour(Colour(0xff1a1a1a));
    g.drawLine(c.x + std::sin(a) * r * 0.25f, c.y - std::cos(a) * r * 0.25f,
               c.x + std::sin(a) * r * 0.92f, c.y - std::cos(a) * r * 0.92f, 2.6f);
}

// ---------------------------------------------------------------- Hotspot

void Hotspot::mouseDown(const MouseEvent&) {
    down_ = true;
    repaint();
    if (onClick) onClick();
}

void Hotspot::mouseUp(const MouseEvent&) {
    down_ = false;
    repaint();
}

void Hotspot::paint(Graphics& g) {
    if (down_) {
        g.setColour(Colours::white.withAlpha(0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    }
}

// ---------------------------------------------------------------- Encodeur DATA

void DataEncoder::step(int direction) {
    angle_ += 0.25f * static_cast<float>(direction);
    repaint();
    if (onDelta) onDelta(direction);
}

void DataEncoder::paint(Graphics& g) {
    const auto c = getLocalBounds().toFloat().getCentre();
    g.setColour(Colours::white.withAlpha(0.55f));
    g.drawLine(c.x + std::sin(angle_) * 9.0f, c.y - std::cos(angle_) * 9.0f,
               c.x + std::sin(angle_) * 26.0f, c.y - std::cos(angle_) * 26.0f, 2.2f);
}

void DataEncoder::mouseDown(const MouseEvent& e) {
    lastY_ = e.getPosition().y;
    dragAcc_ = 0.0f;
}

void DataEncoder::mouseDrag(const MouseEvent& e) {
    const int y = e.getPosition().y;
    dragAcc_ += static_cast<float>(lastY_ - y);
    lastY_ = y;
    while (dragAcc_ >= 6.0f) { step(+1); dragAcc_ -= 6.0f; }
    while (dragAcc_ <= -6.0f) { step(-1); dragAcc_ += 6.0f; }
}

void DataEncoder::mouseWheelMove(const MouseEvent&, const MouseWheelDetails& wheel) {
    wheelAcc_ += wheel.deltaY * 12.0f;
    while (wheelAcc_ >= 1.0f) { step(+1); wheelAcc_ -= 1.0f; }
    while (wheelAcc_ <= -1.0f) { step(-1); wheelAcc_ += 1.0f; }
}

// ---------------------------------------------------------------- Façade

Rectangle<int> Facade::stepLedRect(int row, int step) {
    return Rectangle<int>(juce::roundToInt(kLedX0 + kLedDX * static_cast<float>(step)), kLedY[row], 15, 8);
}

Rectangle<int> Facade::menuLedRect(int index) {
    return Rectangle<int>(kMenuLedX[index % 4], kMenuLedY[index / 4], 15, 8);
}

Rectangle<int> Facade::displayRect() { return Rectangle<int>(146, 73, 105, 48); }

Facade::Facade(MaqProcessor& p) : proc_(p), apvts_(p.apvts) {
    background_ = ImageCache::getFromMemory(BinaryData::facade_png, BinaryData::facade_pngSize);
    setOpaque(true);
    setSize(kWidth, kHeight);

    for (int r = 0; r < maq::kRows; ++r) {
        for (int s = 0; s < maq::kSteps; ++s) {
            auto k = std::make_unique<Slider>();
            k->setSliderStyle(Slider::RotaryVerticalDrag);
            k->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
            // L'échelle imprimée (0 à 10) couvre 288 degrés : de 216 à 504 degrés depuis le haut
            k->setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
            k->setMouseDragSensitivity(220);
            k->setLookAndFeel(&lnf_);
            const Point<int> centre(roundToInt(kKnobX0 + kKnobDX * static_cast<float>(s)), roundToInt(kKnobY[r]));
            k->setBounds(Rectangle<int>(36, 36).withCentre(centre));
            addAndMakeVisible(*k);
            if (auto* prm = apvts_.getParameter(MaqProcessor::potId(r, s)))
                attachments_.push_back(std::make_unique<SliderParameterAttachment>(*prm, *k, nullptr));
            k->setDoubleClickReturnValue(true, 0.0);

            // Bulle de valeur pendant le réglage : valeur ou nom de note selon la barre
            k->setPopupDisplayEnabled(true, false, this);
            k->textFromValueFunction = [this, r](double v) {
                return fr(maq::valueText(eventOf(r), static_cast<int>(std::lround(v)),
                                         getBool("valnote"), getBool("notefr")).c_str());
            };
            // Le dernier potard touché affiche sa valeur sur l'afficheur
            Slider* raw = k.get();
            k->onValueChange = [this, r, s, raw] {
                if (raw->isMouseOverOrDragging()) {
                    touchRow_ = r;
                    touchStep_ = s;
                    touchUntil_ = Time::getMillisecondCounter() + 1500;
                }
                // Verrouillage de gamme : un potard de note tombe sur une note de la gamme
                if (!snapping_ && raw->isMouseButtonDown() && getInt("scalemode") == maq::kScaleModeLock) {
                    const int pot = static_cast<int>(std::lround(raw->getValue()));
                    const int snapped = maq::snapPot(eventOf(r), pot, getInt("scaleroot"), getInt("scaletype"));
                    if (snapped != pot) {
                        snapping_ = true;
                        raw->setValue(snapped, sendNotificationSync);
                        snapping_ = false;
                    }
                }
            };
            // Apprentissage : cliquer un potard place le curseur sur ce pas
            k->onDragStart = [this, r, s] {
                if (learnOn_) {
                    learnRow_ = r;
                    learnStep_ = s;
                    learnRef_ = -1;
                }
            };
            knobs_[static_cast<size_t>(r * maq::kSteps + s)] = std::move(k);
        }
    }

    for (int i = 0; i < 8; ++i) {
        buttons_[static_cast<size_t>(i)].setBounds(
            Rectangle<int>(28, 22).withCentre(Point<int>(kMenuBtnCx[i % 4], kMenuBtnCy[i / 4])));
        buttons_[static_cast<size_t>(i)].onClick = [this, i] { pressMenuButton(i); };
        addAndMakeVisible(buttons_[static_cast<size_t>(i)]);
    }

    encoder_.setBounds(Rectangle<int>(66, 66).withCentre(Point<int>(kEncoderCx, kEncoderCy)));
    encoder_.onDelta = [this](int d) { dataChanged(d); };
    addAndMakeVisible(encoder_);

    for (int r = 0; r < maq::kRows; ++r)
        for (int s = 0; s < maq::kSteps; ++s)
            shownLed_[static_cast<size_t>(r * maq::kSteps + s)] = ledLevelFor(r, s);
    for (int i = 0; i < 8; ++i) shownMenuLed_[static_cast<size_t>(i)] = menuLedOn(i);
    shownText_ = computeDisplay();

    startTimerHz(25);
}

Facade::~Facade() {
    stopTimer();
    proc_.learnActive.store(false);
    attachments_.clear();
    for (auto& k : knobs_)
        if (k) {
            k->textFromValueFunction = nullptr;
            k->onValueChange = nullptr;
            k->onDragStart = nullptr;
            k->setLookAndFeel(nullptr);
        }
}

void Facade::paint(Graphics& g) {
    g.setImageResamplingQuality(Graphics::highResamplingQuality);
    g.drawImageAt(background_, 0, 0);

    // La photo étiquette les rangées 2 et 2 : on remplace le deuxième « 2 » par « 3 »
    g.setColour(Colour(0xff0d0d0d));
    g.fillRect(793, 343, 12, 16);
    g.setColour(Colours::white);
    g.setFont(g.getCurrentFont().withHeight(18.0f).boldened());
    g.drawText("3", Rectangle<int>(792, 341, 14, 19), Justification::centred, false);

    // Vitre de l'afficheur : on recouvre le « 300 » de la photo
    g.setColour(Colour(0xff4c4c4c));
    g.fillRect(displayRect());

    // Le texte contient jusqu'à trois caractères ; un « . » après un caractère allume son point décimal
    std::array<char, 3> cells{{' ', ' ', ' '}};
    std::array<bool, 3> dots{{false, false, false}};
    int n = 0;
    for (const char ch : shownText_) {
        if (ch == '.') {
            if (n > 0) dots[static_cast<size_t>(n - 1)] = true;
        } else if (n < 3) {
            cells[static_cast<size_t>(n++)] = ch;
        }
    }
    for (int i = 0; i < 3; ++i)
        paintDigit(g, glyphMask(cells[static_cast<size_t>(i)]), dots[static_cast<size_t>(i)],
                   kDigitCx[i] - kDigitW * 0.5f, kDigitTop, kDigitW, kDigitH);

    for (int r = 0; r < maq::kRows; ++r)
        for (int s = 0; s < maq::kSteps; ++s)
            paintLed(g, stepLedRect(r, s), shownLed_[static_cast<size_t>(r * maq::kSteps + s)]);
    for (int i = 0; i < 8; ++i)
        paintLed(g, menuLedRect(i), shownMenuLed_[static_cast<size_t>(i)] ? 2 : 0);
}

int Facade::getInt(const String& id) const {
    if (auto* v = apvts_.getRawParameterValue(id)) return static_cast<int>(std::lround(v->load()));
    return 0;
}

bool Facade::getBool(const String& id) const {
    if (auto* v = apvts_.getRawParameterValue(id)) return v->load() > 0.5f;
    return false;
}

void Facade::bump(const String& id, int delta) {
    if (auto* p = dynamic_cast<AudioParameterInt*>(apvts_.getParameter(id))) {
        const auto range = p->getRange();
        const int v = jlimit(range.getStart(), range.getEnd(), p->get() + delta);
        if (v != p->get()) {
            p->beginChangeGesture();
            *p = v;
            p->endChangeGesture();
        }
    }
}

maq::EventConfig Facade::eventOf(int row) const {
    maq::EventConfig e = maq::eventForIndex(getInt(MaqProcessor::rowId("event", row)));
    if (e.kind == maq::EventKind::FreeCC) e.ccNumber = getInt(MaqProcessor::rowId("cc", row));
    return e;
}

// Valeur ou nom de note d'un potard, sur l'afficheur à sept segments
std::string Facade::knobSegText(int row, int step) const {
    const maq::EventConfig e = eventOf(row);
    if (e.kind == maq::EventKind::Off) return "---";
    const int pot = getInt(MaqProcessor::potId(row, step));
    if (maq::isPauseValue(e, pot)) return "---";
    const int v = maq::displayValue(e, pot);
    if (getBool("valnote") && maq::isAbsoluteNoteEvent(e)) return segNote(v);
    return num3(v);
}

bool Facade::isRowMenu() const {
    return menu_ == Menu::Event || menu_ == Menu::Channel || menu_ == Menu::FirstLast ||
           menu_ == Menu::Prescale || menu_ == Menu::Mode;
}

void Facade::flash(const std::string& threeChars) {
    msgText_ = threeChars;
    msgUntil_ = Time::getMillisecondCounter() + 1200;
}

void Facade::pressEnter() {
    const int sel = getInt("preset");
    if (storeMode_) {
        if (sel >= 1) {
            proc_.storePreset(sel);
            flash("StO");
        } else {
            flash("---");
        }
        storeMode_ = false;
    } else {
        flash((sel >= 1 && proc_.recallPreset(sel)) ? "rEC" : "---");
    }
}

void Facade::pressMenuButton(int index) {
    static const Menu menuOf[8] = {Menu::Event, Menu::Channel, Menu::FirstLast, Menu::Prescale,
                                   Menu::Mode, Menu::SingleStep, Menu::Preset, Menu::None};

    if (index == 7) {  // START/STOP
        if (menu_ == Menu::SingleStep) {
            toggleMuteOfSelectedStep();
        } else if (menu_ == Menu::Preset) {
            pressEnter();  // « Enter » dans le menu PRESET
        } else {
            // Le transport est piloté par Bitwig : ce bouton ramène à l'affichage du tempo
            menu_ = Menu::None;
            ccEdit_ = false;
            singleRow_ = -1;
        }
        return;
    }

    if (index == 6 && menu_ == Menu::Event) {  // PRESET dans EVENT : numéro du contrôleur libre
        if (getInt(MaqProcessor::rowId("event", row_)) == maq::kFreeCcEventIndex) ccEdit_ = !ccEdit_;
        return;
    }

    const Menu m = menuOf[index];
    if (m == menu_) {
        switch (m) {
            case Menu::Event:
            case Menu::Channel:
            case Menu::Mode:
                row_ = (row_ + 1) % maq::kRows;
                ccEdit_ = false;
                break;
            case Menu::FirstLast:
            case Menu::Prescale:
                sub_ = (sub_ + 1) % 6;
                row_ = sub_ % 3;
                break;
            case Menu::SingleStep:
                singleRow_ = (singleRow_ < 0) ? 0 : (singleRow_ + 1) % maq::kRows;
                break;
            case Menu::Preset:
                storeMode_ = !storeMode_;  // second appui : enregistrement
                break;
            default:
                break;
        }
    } else {
        menu_ = m;
        row_ = 0;
        sub_ = 0;
        ccEdit_ = false;
        singleRow_ = -1;
        storeMode_ = false;
    }
}

void Facade::dataChanged(int delta) {
    switch (menu_) {
        case Menu::Event:
            bump(MaqProcessor::rowId(ccEdit_ ? "cc" : "event", row_), delta);
            break;
        case Menu::Channel:
            bump(MaqProcessor::rowId("chan", row_), delta);
            break;
        case Menu::FirstLast:
            bump(MaqProcessor::rowId(sub_ < 3 ? "first" : "last", row_), delta);
            break;
        case Menu::Prescale:
            bump(MaqProcessor::rowId(sub_ < 3 ? "gate" : "notelen", row_), delta);
            break;
        case Menu::Mode:
            bump(MaqProcessor::rowId("mode", row_), delta);
            break;
        case Menu::SingleStep:
            selStep_ = jlimit(0, maq::kSteps - 1, selStep_ + delta);
            break;
        case Menu::Preset:
            bump("preset", delta);
            break;
        default:
            break;
    }
}

void Facade::toggleMuteOfSelectedStep() {
    if (singleRow_ < 0) return;  // le manuel ne mute qu'en mode une rangée
    if (auto* p = dynamic_cast<AudioParameterBool*>(apvts_.getParameter(MaqProcessor::muteId(singleRow_, selStep_)))) {
        p->beginChangeGesture();
        *p = !p->get();
        p->endChangeGesture();
    }
}

int Facade::ledLevelFor(int r, int s) const {
    if (learnOn_ && r == learnRow_ && s == learnStep_) return 2;  // curseur d'apprentissage
    if (getBool(MaqProcessor::muteId(r, s))) return 0;  // un pas muet reste éteint
    if (menu_ == Menu::SingleStep) {
        if (singleRow_ < 0) return s == selStep_ ? 2 : 0;
        if (r == singleRow_) return s == selStep_ ? 2 : 1;
        return 0;
    }
    int level = 0;
    if ((isRowMenu() && r == row_) || (learnOn_ && r == learnRow_)) level = 1;  // la rangée choisie « miroite »
    if (proc_.uiPlaying.load() && proc_.uiStep[static_cast<size_t>(r)].load() == s + 1) level = 2;
    return level;
}

bool Facade::menuLedOn(int index) const {
    if (menu_ == Menu::Preset && storeMode_) return true;  // avertissement d'écriture : les 8 LED
    switch (index) {
        case 0: return menu_ == Menu::Event;
        case 1: return menu_ == Menu::Channel;
        case 2: return menu_ == Menu::FirstLast;
        case 3: return menu_ == Menu::Prescale;
        case 4: return menu_ == Menu::Mode;
        case 5: return menu_ == Menu::SingleStep;
        case 6: return menu_ == Menu::Preset;
        case 7: return menu_ == Menu::Preset || proc_.uiPlaying.load();
        default: return false;
    }
}

std::string Facade::computeDisplay() const {
    const uint32 now = Time::getMillisecondCounter();
    if (!msgText_.empty() && now < msgUntil_) return msgText_;

    auto two = [](char c, int v) {
        char b[16];
        std::snprintf(b, sizeof b, "%c%02d", c, v);
        return std::string(b);
    };
    auto pad = [](std::string s) {
        while (s.size() < 3) s += ' ';
        return s.substr(0, 3);
    };

    switch (menu_) {
        case Menu::None: {
            if (touchRow_ >= 0 && now < touchUntil_) return knobSegText(touchRow_, touchStep_);
            if (learnOn_) return "LEA";
            const double bpm = proc_.uiBpm.load();
            return bpm > 0.0 ? num3(static_cast<int>(std::lround(bpm))) : std::string("---");
        }
        case Menu::Event: {
            const int ev = getInt(MaqProcessor::rowId("event", row_));
            if (ccEdit_ || ev == maq::kFreeCcEventIndex) return num3(getInt(MaqProcessor::rowId("cc", row_)));
            return pad(maq::eventLabel(ev));
        }
        case Menu::Channel:
            return num3(getInt(MaqProcessor::rowId("chan", row_)));
        case Menu::FirstLast:
            return sub_ < 3 ? two('F', getInt(MaqProcessor::rowId("first", row_)))
                            : two('L', getInt(MaqProcessor::rowId("last", row_)));
        case Menu::Prescale:
            return sub_ < 3 ? two('g', getInt(MaqProcessor::rowId("gate", row_)))
                            : two('n', getInt(MaqProcessor::rowId("notelen", row_)));
        case Menu::Mode:
            return std::string(maq::modeLabel(getInt(MaqProcessor::rowId("mode", row_))));
        case Menu::SingleStep:
            if (singleRow_ >= 0) return knobSegText(singleRow_, selStep_);
            return num3(selStep_ + 1);
        case Menu::Preset: {
            const int sel = getInt("preset");
            std::string t = num3(sel);
            if (sel >= 1 && proc_.presetUsed(sel)) t += '.';  // le point indique un slot déjà enregistré
            return t;
        }
    }
    return std::string("   ");
}

void Facade::timerCallback() {
    if (learnOn_) {
        MaqProcessor::NoteIn note;
        while (proc_.popNote(note)) handleLearnNote(note);
    }
    for (int r = 0; r < maq::kRows; ++r) {
        for (int s = 0; s < maq::kSteps; ++s) {
            const size_t idx = static_cast<size_t>(r * maq::kSteps + s);
            const int level = ledLevelFor(r, s);
            if (level != shownLed_[idx]) {
                shownLed_[idx] = level;
                repaint(stepLedRect(r, s).expanded(3));
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        const bool on = menuLedOn(i);
        if (on != shownMenuLed_[static_cast<size_t>(i)]) {
            shownMenuLed_[static_cast<size_t>(i)] = on;
            repaint(menuLedRect(i).expanded(3));
        }
    }
    const std::string text = computeDisplay();
    if (text != shownText_) {
        shownText_ = text;
        repaint(displayRect());
    }
}

// ---------------------------------------------------------------- Apprentissage

void Facade::setPotValue(int row, int step, int value) {
    if (auto* p = apvts_.getParameter(MaqProcessor::potId(row, step))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(value)));
        p->endChangeGesture();
    }
}

void Facade::setMuteValue(int row, int step, bool muted) {
    if (auto* p = apvts_.getParameter(MaqProcessor::muteId(row, step))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(muted ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void Facade::setLearn(bool on) {
    learnOn_ = on;
    learnRef_ = -1;
    if (on) {
        MaqProcessor::NoteIn dummy;
        while (proc_.popNote(dummy)) {}  // on oublie les notes jouées avant
        learnStep_ = jlimit(0, maq::kSteps - 1, getInt(MaqProcessor::rowId("first", learnRow_)) - 1);
    }
    proc_.learnActive.store(on);
}

// Le curseur avance de First à Last, puis revient au premier pas
void Facade::advanceLearn() {
    int first = jlimit(0, maq::kSteps - 1, getInt(MaqProcessor::rowId("first", learnRow_)) - 1);
    int last = jlimit(0, maq::kSteps - 1, getInt(MaqProcessor::rowId("last", learnRow_)) - 1);
    if (last < first) std::swap(first, last);
    if (learnStep_ < first || learnStep_ >= last) learnStep_ = first;
    else ++learnStep_;
}

void Facade::handleLearnNote(const MaqProcessor::NoteIn& n) {
    const maq::EventConfig e = eventOf(learnRow_);
    const int pot = maq::learnPot(e, n.note, learnRef_, getInt("scalemode") == maq::kScaleModeLock,
                                  getInt("scaleroot"), getInt("scaletype"));
    if (pot < 0) {  // la rangée choisie n'est pas une rangée de notes
        flash("---");
        return;
    }
    setPotValue(learnRow_, learnStep_, pot);
    setMuteValue(learnRow_, learnStep_, false);

    // Une rangée de vélocité enregistre la force de frappe sur le même pas
    for (int r = 0; r < maq::kRows; ++r)
        if (r != learnRow_ && eventOf(r).kind == maq::EventKind::Velocity)
            setPotValue(r, learnStep_, jlimit(1, 127, n.velocity));

    touchRow_ = learnRow_;  // la note apprise s'affiche un instant
    touchStep_ = learnStep_;
    touchUntil_ = Time::getMillisecondCounter() + 1500;
    advanceLearn();
}

void Facade::learnPause() {
    if (!learnOn_) return;
    const maq::EventConfig e = eventOf(learnRow_);
    if (e.kind == maq::EventKind::NoteAbsPause || e.kind == maq::EventKind::NoteRelPause)
        setPotValue(learnRow_, learnStep_, 127);  // potard à fond à droite = pause
    else
        setMuteValue(learnRow_, learnStep_, true);
    advanceLearn();
}

void Facade::learnSkip() {
    if (learnOn_) advanceLearn();
}

maq::HelpContext Facade::helpContext() const {
    maq::HelpContext c;
    switch (menu_) {
        case Menu::Event: c.menu = maq::HelpMenu::Event; break;
        case Menu::Channel: c.menu = maq::HelpMenu::Channel; break;
        case Menu::FirstLast: c.menu = maq::HelpMenu::FirstLast; break;
        case Menu::Prescale: c.menu = maq::HelpMenu::Prescale; break;
        case Menu::Mode: c.menu = maq::HelpMenu::Mode; break;
        case Menu::SingleStep: c.menu = maq::HelpMenu::SingleStep; break;
        case Menu::Preset: c.menu = maq::HelpMenu::Preset; break;
        default: c.menu = maq::HelpMenu::None; break;
    }
    c.eventIndex = getInt(MaqProcessor::rowId("event", row_));
    c.modeIndex = getInt(MaqProcessor::rowId("mode", row_));
    c.second = sub_ >= 3;
    c.ccEdit = ccEdit_;
    c.store = storeMode_;
    c.singleRow = singleRow_ >= 0;
    c.learn = learnOn_;
    return c;
}

// ---------------------------------------------------------------- Barre de contrôle

ControlBar::ControlBar(MaqProcessor& p) : proc_(p) {
    lnf_.setColourScheme(LookAndFeel_V4::getMidnightColourScheme());
    setLookAndFeel(&lnf_);

    prevBtn_.setButtonText("<");
    nextBtn_.setButtonText(">");
    panicBtn_.setButtonText("Panic");
    frBtn_.setButtonText(fr("Do R\xC3\xA9 Mi"));

    notesBtn_.setClickingTogglesState(true);
    frBtn_.setClickingTogglesState(true);
    notesBtn_.setColour(TextButton::buttonOnColourId, Colour(0xffb02a1a));
    frBtn_.setColour(TextButton::buttonOnColourId, Colour(0xffb02a1a));
    panicBtn_.setColour(TextButton::buttonColourId, Colour(0xff7a1a12));
    if (auto* prm = proc_.apvts.getParameter("valnote"))
        notesAttach_ = std::make_unique<ButtonParameterAttachment>(*prm, notesBtn_);
    if (auto* prm = proc_.apvts.getParameter("notefr"))
        frAttach_ = std::make_unique<ButtonParameterAttachment>(*prm, frBtn_);

    titleLabel_.setText("PRESET", dontSendNotification);
    titleLabel_.setJustificationType(Justification::centredRight);
    presetLabel_.setJustificationType(Justification::centred);
    statusLabel_.setJustificationType(Justification::centredLeft);
    statusLabel_.setColour(Label::textColourId, Colours::lightgrey);
    helpLabel_.setJustificationType(Justification::topLeft);
    helpLabel_.setFont(helpLabel_.getFont().withHeight(14.0f));
    helpLabel_.setColour(Label::textColourId, Colour(0xffd6d6d6));
    nameEditor_.setMultiLine(false);
    nameEditor_.setReturnKeyStartsNewLine(false);

    keepSameBtn_.setToggleState(proc_.folders.keepSameFolder(), dontSendNotification);

    prevBtn_.onClick = [this] { bumpPreset(-1); };
    nextBtn_.onClick = [this] { bumpPreset(+1); };
    storeBtn_.onClick = [this] {
        const int s = selectedSlot();
        if (s < 1) { setStatus(S(maq::tr(maq::Str::ChooseSlot, lang()))); return; }
        proc_.storePreset(s);
        setStatus(S(maq::fmt(maq::Str::PresetStored, lang(), "", s)));
    };
    recallBtn_.onClick = [this] {
        const int s = selectedSlot();
        if (s < 1) { setStatus(S(maq::tr(maq::Str::ChooseSlot, lang()))); return; }
        setStatus(S(maq::fmt(proc_.recallPreset(s) ? maq::Str::PresetRecalled : maq::Str::PresetEmpty, lang(), "", s)));
    };
    importBtn_.onClick = [this] { doImport(); };
    exportBtn_.onClick = [this] { doExport(); };
    importDirBtn_.onClick = [this] { chooseFolder(maq::FolderPurpose::PresetImport, S(maq::tr(maq::Str::ImportDirTitle, lang()))); };
    exportDirBtn_.onClick = [this] { chooseFolder(maq::FolderPurpose::PresetExport, S(maq::tr(maq::Str::ExportDirTitle, lang()))); };
    keepSameBtn_.onClick = [this] {
        proc_.folders.setKeepSameFolder(keepSameBtn_.getToggleState());
        proc_.saveFolderSettings();
        setStatus(S(maq::tr(keepSameBtn_.getToggleState() ? maq::Str::OneFolder : maq::Str::PerUseFolder, lang())));
    };
    nameEditor_.onTextChange = [this] {
        const int s = selectedSlot();
        if (s >= 1 && proc_.presetUsed(s)) proc_.setPresetName(s, nameEditor_.getText());
    };
    panicBtn_.onClick = [this] {
        proc_.requestPanic();
        setStatus(S(maq::tr(maq::Str::PanicDone, lang())));
    };
    manualBtn_.onClick = [this] { showManualMenu(); };

    learnBtn_.setClickingTogglesState(true);
    learnBtn_.setColour(TextButton::buttonOnColourId, Colour(0xffb02a1a));
    learnBtn_.onClick = [this] {
        const bool on = learnBtn_.getToggleState();
        if (onLearn_) onLearn_(on);
        setStatus(S(maq::tr(on ? maq::Str::LearnOn : maq::Str::LearnOff, lang())));
    };
    pauseBtn_.onClick = [this] { if (onPause_) onPause_(); };
    skipBtn_.onClick = [this] { if (onSkip_) onSkip_(); };
    rootBox_.onChange = [this] { setChoiceParam("scaleroot", rootBox_.getSelectedId() - 1); };
    typeBox_.onChange = [this] { setChoiceParam("scaletype", typeBox_.getSelectedId() - 1); };
    modeBox_.onChange = [this] { setChoiceParam("scalemode", modeBox_.getSelectedId() - 1); };
    scaleLabel_.setJustificationType(Justification::centredRight);
    langBtn_.onClick = [this] {
        proc_.prefs.language = (lang() == maq::Lang::French) ? maq::Lang::English : maq::Lang::French;
        proc_.savePreferences();
        applyLanguage();
    };

    for (auto* b : {&notesBtn_, &frBtn_, &prevBtn_, &nextBtn_, &storeBtn_, &recallBtn_, &importBtn_, &exportBtn_,
                    &importDirBtn_, &exportDirBtn_, &panicBtn_, &manualBtn_, &langBtn_,
                    &learnBtn_, &pauseBtn_, &skipBtn_})
        addAndMakeVisible(*b);
    addAndMakeVisible(scaleLabel_);
    addAndMakeVisible(rootBox_);
    addAndMakeVisible(typeBox_);
    addAndMakeVisible(modeBox_);
    addAndMakeVisible(keepSameBtn_);
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(presetLabel_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(helpLabel_);
    addAndMakeVisible(nameEditor_);

    applyLanguage();
    startTimerHz(8);
}

ControlBar::~ControlBar() {
    stopTimer();
    setLookAndFeel(nullptr);
}

// Applique la langue choisie à tous les textes de la barre
void ControlBar::applyLanguage() {
    const maq::Lang l = lang();
    notesBtn_.setButtonText(S(maq::tr(maq::Str::Notes, l)));
    storeBtn_.setButtonText(S(maq::tr(maq::Str::Store, l)));
    recallBtn_.setButtonText(S(maq::tr(maq::Str::Recall, l)));
    importBtn_.setButtonText(S(maq::tr(maq::Str::Import, l)));
    exportBtn_.setButtonText(S(maq::tr(maq::Str::Export, l)));
    importDirBtn_.setButtonText(S(maq::tr(maq::Str::ImportDir, l)));
    exportDirBtn_.setButtonText(S(maq::tr(maq::Str::ExportDir, l)));
    keepSameBtn_.setButtonText(S(maq::tr(maq::Str::KeepSameFolder, l)));
    manualBtn_.setButtonText(S(maq::tr(maq::Str::Manual, l)));
    langBtn_.setButtonText(l == maq::Lang::French ? "FR" : "EN");
    learnBtn_.setButtonText(S(maq::tr(maq::Str::Learn, l)));
    pauseBtn_.setButtonText(S(maq::tr(maq::Str::Pause, l)));
    skipBtn_.setButtonText(S(maq::tr(maq::Str::Skip, l)));
    scaleLabel_.setText(S(maq::tr(maq::Str::ScaleLabel, l)), dontSendNotification);
    fillScaleBoxes();
    setStatus(String());
    shownSlot_ = -1;       // force la mise à jour du champ de nom
    shownHelp_ = String(); // et de l'aide
}

void ControlBar::paint(Graphics& g) {
    g.fillAll(Colour(0xff161616));
    g.setColour(Colour(0xff3a3a3a));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    g.drawHorizontalLine(108, 20.0f, static_cast<float>(getWidth() - 20));
}

void ControlBar::resized() {
    // Ligne 1 : noms de notes, presets, import/export, dossiers, Panic
    notesBtn_.setBounds(20, 8, 80, 30);
    frBtn_.setBounds(106, 8, 100, 30);
    titleLabel_.setBounds(220, 8, 70, 30);
    prevBtn_.setBounds(296, 8, 30, 30);
    presetLabel_.setBounds(330, 8, 50, 30);
    nextBtn_.setBounds(384, 8, 30, 30);
    nameEditor_.setBounds(422, 8, 210, 30);
    storeBtn_.setBounds(640, 8, 100, 30);
    recallBtn_.setBounds(746, 8, 84, 30);
    importBtn_.setBounds(840, 8, 90, 30);
    exportBtn_.setBounds(936, 8, 90, 30);
    importDirBtn_.setBounds(1036, 8, 112, 30);
    exportDirBtn_.setBounds(1154, 8, 112, 30);
    panicBtn_.setBounds(1274, 8, 76, 30);
    // Ligne 2 : dossiers, message d'état, manuel, langue
    keepSameBtn_.setBounds(20, 42, 270, 28);
    statusLabel_.setBounds(300, 42, 830, 28);
    manualBtn_.setBounds(1150, 42, 100, 28);
    langBtn_.setBounds(1260, 42, 90, 28);
    // Ligne 3 : apprentissage au clavier et gamme
    learnBtn_.setBounds(20, 76, 120, 28);
    pauseBtn_.setBounds(146, 76, 70, 28);
    skipBtn_.setBounds(222, 76, 70, 28);
    scaleLabel_.setBounds(310, 76, 60, 28);
    rootBox_.setBounds(376, 76, 74, 28);
    typeBox_.setBounds(456, 76, 330, 28);
    modeBox_.setBounds(792, 76, 200, 28);
    // Ligne 4 : aide contextuelle (deux lignes)
    helpLabel_.setBounds(20, 112, 1330, 34);
}

int ControlBar::selectedSlot() const {
    if (auto* v = proc_.apvts.getRawParameterValue("preset")) return static_cast<int>(std::lround(v->load()));
    return 0;
}

void ControlBar::bumpPreset(int delta) {
    if (auto* p = dynamic_cast<AudioParameterInt*>(proc_.apvts.getParameter("preset"))) {
        const auto range = p->getRange();
        const int v = jlimit(range.getStart(), range.getEnd(), p->get() + delta);
        if (v != p->get()) {
            p->beginChangeGesture();
            *p = v;
            p->endChangeGesture();
        }
    }
}

void ControlBar::setStatus(const String& text) { statusLabel_.setText(text, dontSendNotification); }

void ControlBar::timerCallback() {
    const int slot = selectedSlot();
    const bool used = proc_.presetUsed(slot);
    const String name = used ? proc_.presetName(slot) : String();

    if (slot != shownSlot_ || used != shownUsed_) {
        shownSlot_ = slot;
        shownUsed_ = used;
        presetLabel_.setText(String(slot), dontSendNotification);
        nameEditor_.setEnabled(used);
        nameEditor_.setTextToShowWhenEmpty(
            S(maq::tr(used ? maq::Str::PresetName : maq::Str::SlotEmpty, lang())), Colours::grey);
        nameEditor_.setText(name, false);
        shownName_ = name;
    } else if (used && name != shownName_ && !nameEditor_.hasKeyboardFocus(true)) {
        nameEditor_.setText(name, false);
        shownName_ = name;
    }

    const bool frNames = proc_.apvts.getRawParameterValue("notefr")->load() > 0.5f;
    if (frNames != shownFrNames_) fillScaleBoxes();  // les noms de notes ont changé de langue
    else syncScaleBoxes();

    if (helpSource_) {
        const String help = S(maq::helpText(helpSource_(), lang()));
        if (help != shownHelp_) {
            shownHelp_ = help;
            helpLabel_.setText(help, dontSendNotification);
        }
    }
}

void ControlBar::setChoiceParam(const String& id, int index) {
    if (auto* p = proc_.apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(index)));
        p->endChangeGesture();
    }
}

void ControlBar::fillScaleBoxes() {
    const bool french = proc_.apvts.getRawParameterValue("notefr")->load() > 0.5f;
    shownFrNames_ = french;
    rootBox_.clear(dontSendNotification);
    for (int pc = 0; pc < 12; ++pc) rootBox_.addItem(S(maq::pitchClassName(pc, french)), pc + 1);
    typeBox_.clear(dontSendNotification);
    for (int c = 0; c < maq::kScaleCategoryCount; ++c) {  // menu groupé par catégories du catalogue
        typeBox_.addSectionHeading(S(maq::scaleCategoryName(c, lang())));
        for (int t = 0; t < maq::kScaleCount; ++t)
            if (maq::scaleCategory(t) == c) typeBox_.addItem(S(maq::scaleName(t, lang())), t + 1);
    }
    modeBox_.clear(dontSendNotification);
    modeBox_.addItem(S(maq::tr(maq::Str::ScaleOff, lang())), 1);
    modeBox_.addItem(S(maq::tr(maq::Str::ScaleLock, lang())), 2);
    modeBox_.addItem(S(maq::tr(maq::Str::ScaleOutput, lang())), 3);
    syncScaleBoxes();
}

void ControlBar::syncScaleBoxes() {
    auto index = [this](const char* id) {
        return static_cast<int>(std::lround(proc_.apvts.getRawParameterValue(id)->load()));
    };
    const int root = index("scaleroot") + 1, type = index("scaletype") + 1, mode = index("scalemode") + 1;
    if (rootBox_.getSelectedId() != root) rootBox_.setSelectedId(root, dontSendNotification);
    if (typeBox_.getSelectedId() != type) typeBox_.setSelectedId(type, dontSendNotification);
    if (modeBox_.getSelectedId() != mode) modeBox_.setSelectedId(mode, dontSendNotification);
}

void ControlBar::doExport() {
    const File dir(S(proc_.folders.folderForUtf8(maq::FolderPurpose::PresetExport)));
    chooser_ = std::make_unique<FileChooser>(S(maq::tr(maq::Str::ExportTitle, lang())),
                                             dir.getChildFile("MAQ16_banque.maqbank"), "*.maqbank");
    chooser_->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles |
                              FileBrowserComponent::warnAboutOverwriting,
                          [this](const FileChooser& fc) {
                              File f = fc.getResult();
                              if (f == File()) return;
                              if (!f.hasFileExtension("maqbank")) f = f.withFileExtension("maqbank");
                              if (proc_.exportBank(f)) {
                                  proc_.folders.setFolderUtf8(maq::FolderPurpose::PresetExport,
                                                              f.getParentDirectory().getFullPathName().toRawUTF8());
                                  proc_.saveFolderSettings();
                                  setStatus(S(maq::fmt(maq::Str::BankExported, lang(), f.getFileName().toStdString(),
                                                       proc_.usedPresetCount())));
                              } else {
                                  setStatus(S(maq::tr(maq::Str::ExportFailed, lang())));
                              }
                          });
}

void ControlBar::doImport() {
    const File dir(S(proc_.folders.folderForUtf8(maq::FolderPurpose::PresetImport)));
    chooser_ = std::make_unique<FileChooser>(S(maq::tr(maq::Str::ImportTitle, lang())), dir, "*.maqbank");
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this](const FileChooser& fc) {
                              const File f = fc.getResult();
                              if (f == File()) return;
                              if (proc_.importBank(f)) {
                                  proc_.folders.setFolderUtf8(maq::FolderPurpose::PresetImport,
                                                              f.getParentDirectory().getFullPathName().toRawUTF8());
                                  proc_.saveFolderSettings();
                                  setStatus(S(maq::fmt(maq::Str::BankImported, lang(), f.getFileName().toStdString(),
                                                       proc_.usedPresetCount())));
                              } else {
                                  setStatus(S(maq::tr(maq::Str::ImportFailed, lang())));
                              }
                          });
}

void ControlBar::chooseFolder(maq::FolderPurpose purpose, const String& title) {
    const File start(S(proc_.folders.folderForUtf8(purpose)));
    chooser_ = std::make_unique<FileChooser>(title, start);
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this, purpose](const FileChooser& fc) {
                              const File d = fc.getResult();
                              if (!d.isDirectory()) return;
                              proc_.folders.setFolderUtf8(purpose, d.getFullPathName().toRawUTF8());
                              proc_.saveFolderSettings();
                              setStatus(S(maq::fmt(maq::Str::FolderSet, lang(), d.getFullPathName().toStdString())));
                          });
}

// Manuel : page officielle de Doepfer, ou copie locale du PDF choisie par l'utilisateur
void ControlBar::showManualMenu() {
    const bool hasLocal = !proc_.prefs.manualPath.empty() && File(S(proc_.prefs.manualPath)).existsAsFile();
    PopupMenu menu;
    menu.addItem(1, S(maq::tr(maq::Str::ManualOnline, lang())));
    menu.addItem(2, S(maq::tr(maq::Str::ManualLocal, lang())), hasLocal);
    menu.addItem(3, S(maq::tr(maq::Str::ManualChoose, lang())));
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&manualBtn_), [this](int result) {
        if (result == 1) {
            URL("https://doepfer.de/maq_m_e.htm").launchInDefaultBrowser();
        } else if (result == 2) {
            const File f(S(proc_.prefs.manualPath));
            if (f.existsAsFile()) f.startAsProcess();
            else setStatus(S(maq::tr(maq::Str::ManualMissing, lang())));
        } else if (result == 3) {
            chooseManual();
        }
    });
}

void ControlBar::chooseManual() {
    chooser_ = std::make_unique<FileChooser>(S(maq::tr(maq::Str::ManualChooseTitle, lang())), File(), "*.pdf");
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this](const FileChooser& fc) {
                              const File f = fc.getResult();
                              if (f == File()) return;
                              proc_.prefs.manualPath = f.getFullPathName().toStdString();
                              proc_.savePreferences();
                              setStatus(S(maq::tr(maq::Str::ManualSaved, lang())));
                              f.startAsProcess();
                          });
}

// ---------------------------------------------------------------- Éditeur

MaqEditor::MaqEditor(MaqProcessor& p) : AudioProcessorEditor(&p), facade_(p), bar_(p) {
    content_.addAndMakeVisible(facade_);
    content_.addAndMakeVisible(bar_);
    addAndMakeVisible(content_);
    bar_.setHelpSource([this] { return facade_.helpContext(); });
    bar_.setLearnCallbacks([this](bool on) { facade_.setLearn(on); },
                           [this] { facade_.learnPause(); },
                           [this] { facade_.learnSkip(); });

    setResizable(true, true);
    setResizeLimits(685, 325, 2738, 1300);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(kWidth) / static_cast<double>(kTotalHeight));
    setSize(1096, 520);
}

void MaqEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    content_.setTransform(AffineTransform::scale(scale));
    content_.setBounds(0, 0, kWidth, kTotalHeight);
    facade_.setBounds(0, 0, kWidth, kHeight);
    bar_.setBounds(0, kHeight, kWidth, kBarHeight);
}

}  // namespace maqui
