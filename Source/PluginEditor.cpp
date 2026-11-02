#include "PluginEditor.h"
#include "Presets.h"

using namespace prisma;

//==============================================================================
juce::Font prisma::labelFont (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font prisma::monoFont (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

static float textWidth (const juce::Font& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth (f, s);
}

// letter-spaced text, the way labels are printed on hardware
static void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                         const juce::Font& font, float spacing, juce::Justification just)
{
    float total = 0.0f;
    for (int i = 0; i < text.length(); ++i)
        total += textWidth (font, text.substring (i, i + 1)) + (i + 1 < text.length() ? spacing : 0.0f);
    float x = area.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - total * 0.5f;
    else if (just.testFlags (juce::Justification::right)) x = area.getRight() - total;
    g.setFont (font);
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text.substring (i, i + 1);
        const float w = textWidth (font, ch);
        g.drawText (ch, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()), juce::Justification::centredLeft, false);
        x += w + spacing;
    }
}

void prisma::drawKeyBody (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool hover)
{
    r = r.reduced (1.0f);
    const float rad = 4.0f;
    g.setColour (juce::Colours::black.withAlpha (down ? 0.10f : 0.18f));
    g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 2.5f), rad);
    auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
    juce::ColourGradient grad (down ? juce::Colour (0xffd6d2c9) : juce::Colour (0xfff8f7f3), body.getX(), body.getY(),
                               down ? juce::Colour (0xffe8e5de) : juce::Colour (0xffd9d5cc), body.getX(), body.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (body, rad);
    g.setColour (juce::Colour (0xffaea99f).withAlpha (hover ? 1.0f : 0.85f));
    g.drawRoundedRectangle (body, rad, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.2f : 0.7f));
    g.drawLine (body.getX() + rad, body.getY() + 1.2f, body.getRight() - rad, body.getY() + 1.2f, 1.0f);
}

void prisma::drawLed (juce::Graphics& g, juce::Point<float> c, bool on)
{
    if (on)
    {
        g.setColour (Theme::accent().withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c));
        g.setColour (Theme::accent());
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre (c.translated (-1.0f, -1.0f)));
    }
    else
    {
        g.setColour (juce::Colour (0xff8f7a70).withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c), 0.8f);
    }
}

//==============================================================================
HardwareLook::HardwareLook()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfff2f0eb));
    setColour (juce::PopupMenu::textColourId, Theme::ink());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent());
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, Theme::ink());
    setColour (juce::ComboBox::arrowColourId, Theme::label());
    setColour (juce::TextButton::textColourOffId, Theme::ink());
    setColour (juce::TextButton::textColourOnId, Theme::ink());
    setColour (juce::Label::textColourId, Theme::label());
    setColour (juce::Slider::textBoxTextColourId, Theme::ink());
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2b2b2d));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffe9e7e2));
    setColour (juce::TooltipWindow::outlineColourId, juce::Colour (0xff2b2b2d));
}

void HardwareLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                     float a0, float a1, juce::Slider&)
{
    const float size = (float) juce::jmin (w, h);
    const float r = size * 0.5f - 7.0f;
    if (r < 6.0f) return;
    const juce::Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float ang = a0 + pos * (a1 - a0);

    // printed scale
    g.setColour (juce::Colour (0xff8b877e));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const float rr = r + 4.5f;
        const float d = (i == 0 || i == 10 || i == 5) ? 2.6f : 1.7f;
        g.fillEllipse (c.x + rr * std::sin (a) - d * 0.5f, c.y - rr * std::cos (a) - d * 0.5f, d, d);
    }

    // shadow
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.3f, r * 2.3f).withCentre (c.translated (0.0f, r * 0.16f)));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.06f, r * 2.06f).withCentre (c.translated (0.0f, r * 0.09f)));

    // dark skirt
    {
        juce::ColourGradient sk (juce::Colour (0xff5b5954), c.x, c.y - r, juce::Colour (0xff2d2c2a), c.x, c.y + r, false);
        g.setGradientFill (sk);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    }
    // cap side
    const float rb = r * 0.86f;
    {
        juce::ColourGradient side (juce::Colour (0xfffbfaf7), c.x, c.y - rb, juce::Colour (0xffc4c0b6), c.x, c.y + rb, false);
        g.setGradientFill (side);
        g.fillEllipse (juce::Rectangle<float> (rb * 2.0f, rb * 2.0f).withCentre (c));
    }
    // knurling
    g.setColour (juce::Colours::black.withAlpha (0.09f));
    for (int i = 0; i < 36; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f + ang;
        g.drawLine (c.x + rb * 0.88f * std::sin (a), c.y - rb * 0.88f * std::cos (a),
                    c.x + rb * 0.99f * std::sin (a), c.y - rb * 0.99f * std::cos (a), 1.0f);
    }
    // top face
    const float rt = r * 0.70f;
    {
        juce::ColourGradient top (juce::Colours::white, c.x - rt * 0.4f, c.y - rt * 0.6f,
                                  juce::Colour (0xffe1ded6), c.x + rt * 0.5f, c.y + rt * 0.8f, true);
        g.setGradientFill (top);
        g.fillEllipse (juce::Rectangle<float> (rt * 2.0f, rt * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.drawEllipse (juce::Rectangle<float> (rt * 2.0f, rt * 2.0f).withCentre (c), 1.0f);
    }
    // indicator
    g.setColour (Theme::accent());
    const float lw = juce::jmax (2.2f, r * 0.08f);
    juce::Path line;
    line.startNewSubPath (c.x + rt * 0.25f * std::sin (ang), c.y - rt * 0.25f * std::cos (ang));
    line.lineTo (c.x + rt * 0.92f * std::sin (ang), c.y - rt * 0.92f * std::cos (ang));
    g.strokePath (line, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void HardwareLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    drawKeyBody (g, b.getLocalBounds().toFloat(), down, over);
}

void HardwareLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setColour (Theme::ink().withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    drawTracked (g, b.getButtonText(), b.getLocalBounds().toFloat().translated (0.0f, down ? 1.0f : 0.0f),
                 labelFont (11.0f), 1.2f, juce::Justification::centred);
}

void HardwareLook::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    drawKeyBody (g, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), down, box.isMouseOver());
    juce::Path p;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Theme::label());
    g.fillPath (p);
    if (! box.isEnabled())
    {
        g.setColour (Theme::bodyBottom().withAlpha (0.55f));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), 4.0f);
    }
}

void HardwareLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 3);
    label.setFont (getComboBoxFont (box));
}

juce::Font HardwareLook::getComboBoxFont (juce::ComboBox&) { return labelFont (12.0f); }
juce::Font HardwareLook::getPopupMenuFont()                { return labelFont (13.0f, false); }

juce::Label* HardwareLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (monoFont ((bool) s.getProperties()["big"] ? 17.0f : 11.5f));
    l->setColour (juce::Label::textColourId, Theme::ink().withAlpha (0.8f));
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textWhenEditingColourId, Theme::ink());
    l->setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    l->setColour (juce::TextEditor::highlightColourId, Theme::accent().withAlpha (0.3f));
    return l;
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& name, bool isBig) : big (isBig)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, big ? 22 : 16);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, Theme::ink());
    slider.setMouseDragSensitivity (big ? 260 : 200);
    addAndMakeVisible (slider);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (labelFont (big ? 13.0f : 10.5f));
    label.setColour (juce::Label::textColourId, Theme::label());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
    slider.getProperties().set ("big", big);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (big ? 20 : 15));
    slider.setBounds (r);
}

//==============================================================================
KeyGroup::KeyGroup (juce::AudioProcessorValueTreeState& st, const juce::String& id, int cols) : columns (cols)
{
    param = st.getParameter (id);
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param)) options = c->choices;
    att = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { value = juce::roundToInt (v); repaint(); }, nullptr);
    att->sendInitialUpdate();
}

juce::Rectangle<float> KeyGroup::cell (int i) const
{
    const int rows = (options.size() + columns - 1) / columns;
    const float gap = 6.0f;
    const float w = ((float) getWidth() - gap * (float) (columns - 1)) / (float) columns;
    const float h = ((float) getHeight() - gap * (float) (rows - 1)) / (float) rows;
    return { (float) (i % columns) * (w + gap), (float) (i / columns) * (h + gap), w, h };
}

void KeyGroup::paint (juce::Graphics& g)
{
    for (int i = 0; i < options.size(); ++i)
    {
        auto r = cell (i);
        const bool down = pressed == i;
        drawKeyBody (g, r, down, false);
        auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
        drawLed (g, { body.getX() + 11.0f, body.getCentreY() }, value == i);
        g.setColour (Theme::ink());
        drawTracked (g, options[i].toUpperCase(), body.withTrimmedLeft (18.0f), labelFont (10.0f), 0.8f, juce::Justification::centred);
    }
}

void KeyGroup::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < options.size(); ++i)
        if (cell (i).contains (e.position))
        {
            pressed = i;
            att->setValueAsCompleteGesture ((float) i);
            repaint();
            return;
        }
}

//==============================================================================
LedToggle::LedToggle (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& t) : text (t)
{
    att = std::make_unique<juce::ParameterAttachment> (*st.getParameter (id), [this] (float v) { on = v > 0.5f; repaint(); }, nullptr);
    att->sendInitialUpdate();
}

void LedToggle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawKeyBody (g, r, down, false);
    auto body = r.translated (0.0f, down ? 1.0f : 0.0f);
    drawLed (g, { body.getX() + 13.0f, body.getCentreY() }, on);
    g.setColour (Theme::ink());
    drawTracked (g, text, body.withTrimmedLeft (20.0f), labelFont (10.5f), 1.0f, juce::Justification::centred);
}

void LedToggle::mouseDown (const juce::MouseEvent&)
{
    down = true;
    att->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
    repaint();
}

void TabKey::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawKeyBody (g, r, down || selected, false);
    auto body = r.translated (0.0f, down || selected ? 1.0f : 0.0f);
    drawLed (g, { body.getX() + 13.0f, body.getCentreY() }, selected);
    g.setColour (Theme::ink());
    drawTracked (g, text, body.withTrimmedLeft (20.0f), labelFont (11.5f), 2.0f, juce::Justification::centred);
}

//==============================================================================
Display::Display (PrismaProcessor& p) : proc (p)
{
    colA.fill (juce::Colours::magenta);
    colB.fill (juce::Colours::cyan);
    readLog = proc.shared.logWrite.load();
}

juce::Rectangle<float> Display::stripArea() const
{
    return getLocalBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (58.0f);
}

juce::Rectangle<float> Display::patternArea() const
{
    auto r = getLocalBounds().toFloat().reduced (18.0f, 0.0f);
    return r.removeFromBottom (48.0f).withTrimmedBottom (12.0f);
}

juce::Rectangle<float> Display::segment (int i, int n) const
{
    auto a = stripArea();
    const float gap = n > 16 ? 3.0f : 6.0f;
    const float w = (a.getWidth() - gap * (float) (n - 1)) / (float) n;
    return { a.getX() + (float) i * (w + gap), a.getY(), w, a.getHeight() };
}

void Display::resized()
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    overlay = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (overlay);
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    for (int y = 0; y < h; y += 3) g.fillRect (0, y, w, 1);
    juce::ColourGradient vig (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.5f,
                              juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
    g.setGradientFill (vig);
    g.fillRect (0, 0, w, h);
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.055f), 0.0f, 0.0f,
                                juce::Colours::transparentWhite, 0.0f, (float) h * 0.45f, false);
    g.setGradientFill (glass);
    g.fillRect (0, 0, w, h / 2);
}

void Display::drawWave (juce::Graphics& g, const Part& p, juce::Rectangle<float> r, juce::Colour c, float thick, float yJitter) const
{
    if (p.overview.empty()) return;
    g.setColour (c);
    const float mid = r.getCentreY() + yJitter;
    const int cols = juce::jmax (1, (int) (r.getWidth() / 2.0f));
    for (int x = 0; x < cols; ++x)
    {
        const auto idx = (size_t) juce::jmin ((int) p.overview.size() - 1, (int) ((float) x / (float) cols * (float) p.overview.size()));
        const float a = juce::jmax (0.6f, p.overview[idx] * r.getHeight() * 0.46f);
        g.fillRect (r.getX() + (float) x * 2.0f, mid - a, thick, a * 2.0f);
    }
}

void Display::tick()
{
    ++frame;
    const int w = proc.shared.logWrite.load (std::memory_order_acquire);
    if (w - readLog > SharedState::logSize) readLog = w - SharedState::logSize;
    static const juce::uint32 palette[] = { 0xffff2e88, 0xff2ee6ff, 0xffe8ff3a, 0xffff7a1a, 0xff9b5cff, 0xff3aff9e, 0xffff3a3a };
    for (; readLog < w; ++readLog)
    {
        const auto i = (size_t) (readLog % SharedState::logSize);
        const int part = proc.shared.logPart[i].load();
        if (part < 0 || part >= maxParts) continue;
        energy[(size_t) part] = 1.0f;
        const int a = rng.nextInt (7);
        int b = rng.nextInt (7);
        if (b == a) b = (a + 3) % 7;
        colA[(size_t) part] = juce::Colour (palette[a]);
        colB[(size_t) part] = juce::Colour (palette[b]);
        reversed[(size_t) part] = proc.shared.logFlags[i].load() != 0;
    }
    for (auto& e : energy) e *= 0.915f;
    if (statusFrames > 0) --statusFrames;
    repaint();
}

void Display::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::screen());
    g.fillRoundedRectangle (bounds, 6.0f);

    auto pool = proc.getPool();
    const int n = (int) pool->parts.size();
    const int rate = (int) proc.getParam ("rate");
    const int order = (int) proc.getParam ("order");
    const bool locked = proc.getParam ("locked") > 0.5f;
    const bool keysMode = order == OrderKeys || order == OrderRandKeys;

    // ---- header line
    g.setColour (Theme::lcd());
    g.setFont (monoFont (12.0f));
    const auto header = bounds.reduced (18.0f, 0.0f).withHeight (30.0f).withY (6.0f);
    juce::String left = "P-1  ";
    if (n == 0) left << "NO SOUNDS";
    else if (pool->multiFile) left << juce::String (n).paddedLeft ('0', 2) << " SOUNDS";
    else left << pool->sources[0]->name.toUpperCase().substring (0, 22) << " / " << n << " PARTS";
    g.drawText (left, header, juce::Justification::centredLeft);

    juce::String right = "RATE " + rateNames()[rate].toUpperCase() + "   " + orderNames()[order].toUpperCase();
    if (locked && ! keysMode) right << "   LOCK";
    g.drawText (right, header, juce::Justification::centredRight);

    if (statusFrames > 0 && (statusFrames > 40 || (frame / 6) % 2 == 0))
    {
        g.setColour (juce::Colour (0xffffb23a));
        g.drawText (status.toUpperCase(), header.reduced (220.0f, 0.0f), juce::Justification::centred);
    }

    if (n == 0)
    {
        g.setColour (Theme::lcd());
        g.setFont (monoFont (20.0f));
        const juce::String blink = (frame / 30) % 2 == 0 ? "_" : " ";
        g.drawText ("DROP SOUNDS HERE" + blink, bounds.withTrimmedBottom (30.0f), juce::Justification::centred);
        g.setColour (Theme::lcd().withAlpha (0.55f));
        g.setFont (monoFont (12.0f));
        g.drawText ("one file = sliced into parts      several files = one sound per part",
                    bounds.withTrimmedTop (60.0f), juce::Justification::centred);
        if (! proc.missingFiles.isEmpty())
        {
            g.setColour (juce::Colour (0xffff6a4a));
            g.drawText ("MISSING: " + proc.missingFiles.joinIntoString (", ").toUpperCase().substring (0, 90),
                        bounds.withTrimmedTop (120.0f), juce::Justification::centred);
        }
    }
    else
    {
        // ---- segments
        for (int i = 0; i < n; ++i)
        {
            const auto& part = pool->parts[(size_t) i];
            auto r = segment (i, n);
            const float e = energy[(size_t) i];

            g.setColour (juce::Colour (0xff111214));
            g.fillRect (r);
            drawWave (g, part, r.reduced (2.0f, 6.0f), juce::Colour (0xff3d4147), 1.0f, 0.0f);

            if (e > 0.02f)
            {
                const auto a = colA[(size_t) i], b = colB[(size_t) i];
                g.setColour (a.withAlpha (0.10f * e));
                g.fillRect (r.expanded (2.0f, 0.0f));

                // glitch bands: each horizontal band slips sideways and splits into colour channels
                const int bands = 7;
                for (int k = 0; k < bands; ++k)
                {
                    auto band = r.withHeight (r.getHeight() / (float) bands).withY (r.getY() + r.getHeight() * (float) k / (float) bands);
                    const float slip = (rng.nextFloat() - 0.5f) * e * juce::jmin (40.0f, r.getWidth() * 0.35f);
                    const float dx = 2.0f + 7.0f * e;
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (band.toNearestInt());
                    auto inner = r.reduced (2.0f, 6.0f);
                    drawWave (g, part, inner.translated (slip - dx, 0.0f), a.withAlpha (0.85f * e), 1.4f, 0.0f);
                    drawWave (g, part, inner.translated (slip + dx, 0.0f), b.withAlpha (0.85f * e), 1.4f, 0.0f);
                    drawWave (g, part, inner.translated (slip, 0.0f), juce::Colours::white.withAlpha (0.55f * e), 1.0f, 0.0f);
                }

                // noise and torn lines
                const int specks = (int) (e * 46.0f);
                for (int k = 0; k < specks; ++k)
                {
                    const float x = r.getX() + rng.nextFloat() * r.getWidth();
                    const float y = r.getY() + rng.nextFloat() * r.getHeight();
                    const float s = 1.0f + rng.nextFloat() * 2.5f;
                    g.setColour ((k % 3 == 0 ? a : k % 3 == 1 ? b : juce::Colours::white).withAlpha (0.8f * e));
                    g.fillRect (x, y, s * (rng.nextFloat() < 0.2f ? 6.0f : 1.0f), s);
                }
                if (rng.nextFloat() < 0.35f * e)
                {
                    g.setColour (b.withAlpha (0.35f * e));
                    g.fillRect (r.getX() - 6.0f, r.getY() + rng.nextFloat() * r.getHeight(), r.getWidth() + 12.0f, 1.0f + rng.nextFloat() * 3.0f);
                }
            }

            g.setColour (juce::Colour (0xff6a6f76).interpolatedWith (juce::Colours::white, e));
            g.setFont (monoFont (10.0f));
            g.drawText (juce::String (i + 1).paddedLeft ('0', 2) + (reversed[(size_t) i] && e > 0.1f ? "<" : ""),
                        r.withY (r.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centred);
        }

        // ---- playheads
        for (int v = 0; v < numVoices; ++v)
        {
            const int part = proc.shared.headPart[(size_t) v].load();
            const float lvl = proc.shared.headLevel[(size_t) v].load();
            if (part < 0 || part >= n || lvl < 0.01f) continue;
            auto r = segment (part, n);
            const float x = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, proc.shared.headPos[(size_t) v].load());
            g.setColour (juce::Colours::white.withAlpha (0.25f * lvl));
            g.fillRect (x - 3.0f, r.getY(), 6.0f, r.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.95f * lvl));
            g.fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());
        }

        // ---- pattern strip
        auto pa = patternArea();
        g.setFont (monoFont (11.0f));
        if (keysMode)
        {
            g.setColour (Theme::lcd().withAlpha (0.8f));
            g.drawText (order == OrderKeys ? "KEYS: C1 = SOUND 01, C#1 = SOUND 02 ..."
                                           : "RANDOM KEYS: EACH KEY PLAYS A SHUFFLED SOUND  (SHUFFLE KEYS TO REMIX)",
                        pa, juce::Justification::centredLeft);
        }
        else
        {
            const int steps = stepsCount ((int) proc.getParam ("steps"));
            const int cur = proc.shared.currentStep.load();
            const float gap = 4.0f;
            const float cw = (pa.getWidth() - 70.0f - gap * (float) (steps - 1)) / (float) steps;
            g.setColour (Theme::lcd().withAlpha (0.7f));
            g.drawText (locked ? "LOCKED" : "LIVE", pa.withWidth (64.0f), juce::Justification::centredLeft);

            // live view: what played at each step recently
            std::array<int, maxPattern> live {};
            live.fill (-1);
            const int hw = proc.shared.histWrite.load();
            for (int k = juce::jmax (0, hw - maxPattern); k < hw; ++k)
            {
                const auto idx = (size_t) (k % maxPattern);
                live[(size_t) (proc.shared.histStep[idx].load() % steps)] = proc.shared.histPart[idx].load();
            }
            for (int s = 0; s < steps; ++s)
            {
                auto c = juce::Rectangle<float> (pa.getX() + 70.0f + (float) s * (cw + gap), pa.getY() + 4.0f, cw, pa.getHeight() - 8.0f);
                const bool isCur = cur >= 0 && (cur % steps) == s;
                const int val = locked ? proc.getPatternCell (s) % juce::jmax (1, n) : live[(size_t) s];
                if (isCur)
                {
                    g.setColour (Theme::lcd());
                    g.fillRect (c);
                    g.setColour (Theme::screen());
                }
                else
                {
                    g.setColour (Theme::lcd().withAlpha (locked ? 0.55f : 0.25f));
                    g.drawRect (c, 1.0f);
                    g.setColour (Theme::lcd().withAlpha (locked ? 0.95f : 0.5f));
                }
                if (cw > 14.0f)
                    g.drawText (val >= 0 ? juce::String (val + 1) : juce::String ("-"), c, juce::Justification::centred);
            }
        }
    }

    // ---- glass, scanlines, vignette
    g.drawImageAt (overlay, 0, 0);

    if (dragOver)
    {
        g.setColour (Theme::accent());
        juce::Path p;
        p.addRoundedRectangle (bounds.reduced (6.0f), 6.0f);
        juce::Path dashed;
        const float dash[] = { 8.0f, 6.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dash, 2);
        g.fillPath (dashed);
    }
}

void Display::mouseDown (const juce::MouseEvent& e)
{
    auto pool = proc.getPool();
    const int n = (int) pool->parts.size();
    if (n == 0) return;

    auto pa = patternArea();
    const int order = (int) proc.getParam ("order");
    if (pa.contains (e.position) && order != OrderKeys && order != OrderRandKeys)
    {
        const int steps = stepsCount ((int) proc.getParam ("steps"));
        const float gap = 4.0f;
        const float cw = (pa.getWidth() - 70.0f - gap * (float) (steps - 1)) / (float) steps;
        const int s = (int) ((e.position.x - pa.getX() - 70.0f) / (cw + gap));
        if (s < 0 || s >= steps) return;
        if (proc.getParam ("locked") < 0.5f)
        {
            proc.capture();   // start editing from what was just playing
            setStatus ("Pattern locked for editing");
        }
        int v = proc.getPatternCell (s) % n;
        v = e.mods.isPopupMenu() ? (v + n - 1) % n : (v + 1) % n;
        proc.setPatternCell (s, v);
        return;
    }

    if (e.mods.isPopupMenu() && pool->multiFile)
        for (int i = 0; i < n; ++i)
            if (segment (i, n).contains (e.position))
            {
                juce::PopupMenu m;
                m.addItem (1, "Remove " + pool->parts[(size_t) i].name);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                 [this, i] (int r) { if (r == 1) { proc.removeSound (i); setStatus ("Sound removed"); } });
                return;
            }
}

bool Display::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void Display::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    juce::Array<juce::File> list;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) list.add (juce::File (f));
    list.sort();
    setStatus (proc.loadFiles (list));
}

//==============================================================================
PrismaEditor::PrismaEditor (PrismaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p),
      playTab ("PLAY", [this] { showTab (0); }), orderTab ("ORDER", [this] { showTab (1); }),
      rate (p.apvts, "rate", "RATE", true), length (p.apvts, "length", "LENGTH"),
      swing (p.apvts, "swing", "SWING"), decay (p.apvts, "decay", "DECAY"),
      modeKeys (p.apvts, "mode", 4),
      gridSync (p.apvts, "gridSync", "GRID SYNC"), choke (p.apvts, "choke", "CHOKE"),
      orderKeys (p.apvts, "order", 4), stepsKeys (p.apvts, "steps", 4), resetKeys (p.apvts, "reset", 4),
      lockToggle (p.apvts, "locked", "LOCK"),
      reverse (p.apvts, "reverse", "REVERSE"),
      attack (p.apvts, "attack", "ATTACK"), decayTime (p.apvts, "decayTime", "DECAY"),
      sustain (p.apvts, "sustain", "SUSTAIN"), release (p.apvts, "release", "RELEASE"),
      lowCut (p.apvts, "lowCut", "LOW CUT"), highCut (p.apvts, "highCut", "HIGH CUT"), reso (p.apvts, "reso", "RESO"),
      crunch (p.apvts, "crunch", "CRUNCH"), wash (p.apvts, "wash", "WASH"), pump (p.apvts, "pump", "PUMP"),
      tune (p.apvts, "tune", "TUNE"), fine (p.apvts, "fine", "FINE"), volume (p.apvts, "volume", "VOLUME")
{
    setLookAndFeel (&look);

    addAndMakeVisible (display);
    display.setTooltip ("Drop one file (it gets sliced) or several files (one sound each). Click the pattern cells to edit.");

    presetBox.addItemList (presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("PRESETS");
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedItemIndex() >= 0)
        {
            proc.applyPreset (presetBox.getSelectedItemIndex());
            display.setStatus ("Preset " + presetBox.getText());
        }
    };
    addAndMakeVisible (presetBox);

    diceButton.setTooltip ("Randomise rate, length, mode and order");
    diceButton.onClick = [this] { proc.dice(); presetBox.setSelectedItemIndex (-1, juce::dontSendNotification); display.setStatus ("Dice"); };
    copyButton.setTooltip ("Copy all settings as text (paste it into the chat)");
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (proc.settingsToText()); display.setStatus ("Settings copied"); };
    pasteButton.setTooltip ("Apply settings text from the clipboard");
    pasteButton.onClick = [this]
    {
        const int k = proc.settingsFromText (juce::SystemClipboard::getTextFromClipboard());
        display.setStatus (k > 0 ? "Applied " + juce::String (k) + " settings" : juce::String ("Nothing to paste"));
    };
    loadButton.setTooltip ("Choose one or more audio files");
    loadButton.onClick = [this] { loadWithChooser(); };
    clearButton.onClick = [this] { proc.clearSounds(); display.setStatus ("Cleared"); };

    for (auto* b : { &diceButton, &copyButton, &pasteButton, &loadButton, &clearButton }) addAndMakeVisible (b);

    splitBox.addItemList (splitNames(), 1);
    partsBox.addItemList (partsNames(), 1);
    splitBox.setTooltip ("How a single file is cut: equal parts or at the hits");
    partsBox.setTooltip ("Number of equal parts for a single file");
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "split", splitBox);
    partsAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "parts", partsBox);
    addAndMakeVisible (splitBox);
    addAndMakeVisible (partsBox);

    addAndMakeVisible (playTab);
    addAndMakeVisible (orderTab);

    captureButton.setTooltip ("Freeze the order you just heard into the pattern");
    captureButton.onClick = [this]
    {
        const int k = proc.capture();
        display.setStatus (k > 0 ? "Captured " + juce::String (k) + " steps" : juce::String ("Play something first"));
    };
    dicePatternButton.setTooltip ("New random pattern (locked)");
    dicePatternButton.onClick = [this] { proc.dicePattern(); display.setStatus ("New pattern"); };
    shuffleButton.setTooltip ("Reshuffle which key plays which sound (Rand Keys)");
    shuffleButton.onClick = [this] { proc.shuffleKeys(); display.setStatus ("Keys shuffled"); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &rate, &length, &swing, &decay, &modeKeys, &gridSync, &choke,
             &orderKeys, &stepsKeys, &resetKeys, &captureButton, &dicePatternButton, &shuffleButton, &lockToggle, &reverse,
             &attack, &decayTime, &sustain, &release, &lowCut, &highCut, &reso, &crunch, &wash, &pump, &tune, &fine, &volume })
        addAndMakeVisible (c);

    showTab (0);
    setSize (1180, 800);
    startTimerHz (60);
}

PrismaEditor::~PrismaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrismaEditor::showTab (int t)
{
    tab = t;
    playTab.selected = t == 0;
    orderTab.selected = t == 1;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &rate, &length, &swing, &decay, &modeKeys, &gridSync, &choke })
        c->setVisible (t == 0);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &orderKeys, &stepsKeys, &resetKeys, &captureButton,
                                                                        &dicePatternButton, &shuffleButton, &lockToggle, &reverse })
        c->setVisible (t == 1);
    playTab.repaint();
    orderTab.repaint();
    repaint();
}

void PrismaEditor::loadWithChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load sounds", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto results = fc.getResults();
                              if (! results.isEmpty()) { results.sort(); display.setStatus (proc.loadFiles (results)); }
                          });
}

void PrismaEditor::timerCallback()
{
    proc.releaseRetired();
    display.tick();
    const bool single = ! proc.getPool()->multiFile;
    if (splitBox.isEnabled() != single) { splitBox.setEnabled (single); partsBox.setEnabled (single); }
}

void PrismaEditor::drawScrew (juce::Graphics& g, juce::Point<float> c) const
{
    g.setColour (juce::Colours::black.withAlpha (0.15f));
    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (c.translated (0.0f, 1.0f)));
    juce::ColourGradient grad (juce::Colour (0xfff4f3ef), c.x, c.y - 6.0f, juce::Colour (0xffa9a49a), c.x, c.y + 6.0f, false);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));
    g.setColour (juce::Colour (0xff77736b));
    g.drawLine (c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y + 1.5f, 1.4f);
}

void PrismaEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> b, const juce::String& title) const
{
    auto r = b.toFloat();
    g.setColour (Theme::panel());
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.drawLine (r.getX() + 8.0f, r.getBottom() + 0.5f, r.getRight() - 8.0f, r.getBottom() + 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.06f));
    g.drawLine (r.getX() + 8.0f, r.getY() + 1.5f, r.getRight() - 8.0f, r.getY() + 1.5f, 1.5f);
    g.setColour (Theme::label());
    drawTracked (g, title, juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 8.0f, 300.0f, 14.0f), labelFont (10.5f), 2.2f,
                 juce::Justification::centredLeft);
}

void PrismaEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    juce::ColourGradient body (Theme::bodyTop(), 0.0f, 0.0f, Theme::bodyBottom(), 0.0f, b.getBottom(), false);
    g.setGradientFill (body);
    g.fillAll();
    if (texture.isValid()) g.drawImageAt (texture, 0, 0);

    // side bevels
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawLine (1.0f, 0.0f, 1.0f, b.getBottom(), 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.drawLine (b.getRight() - 1.0f, 0.0f, b.getRight() - 1.0f, b.getBottom(), 2.0f);

    for (auto p : { juce::Point<float> (14.0f, 14.0f), juce::Point<float> (b.getRight() - 14.0f, 14.0f),
                    juce::Point<float> (14.0f, b.getBottom() - 14.0f), juce::Point<float> (b.getRight() - 14.0f, b.getBottom() - 14.0f) })
        drawScrew (g, p);

    // logo
    g.setColour (Theme::ink());
    drawTracked (g, "PRISMA", { 34.0f, 18.0f, 200.0f, 38.0f }, labelFont (30.0f), 7.0f, juce::Justification::centredLeft);
    auto badge = juce::Rectangle<float> (232.0f, 27.0f, 40.0f, 21.0f);
    g.setColour (Theme::accent());
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (juce::Colours::white);
    g.setFont (labelFont (12.0f));
    g.drawText ("P-1", badge, juce::Justification::centred);
    g.setColour (Theme::label());
    drawTracked (g, "SAMPLE ROULETTE", { 284.0f, 28.0f, 220.0f, 20.0f }, labelFont (10.5f), 2.6f, juce::Justification::centredLeft);
    g.setFont (monoFont (10.0f));
    g.drawText (juce::String ("V") + PRISMA_VERSION, juce::Rectangle<float> (456.0f, 28.0f, 70.0f, 20.0f), juce::Justification::centredLeft);

    // vent slots
    for (int i = 0; i < 9; ++i)
    {
        auto slot = juce::Rectangle<float> (530.0f + (float) i * 14.0f, 26.0f, 5.0f, 24.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (slot, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawLine (slot.getX() + 1.0f, slot.getBottom() + 1.0f, slot.getRight() - 1.0f, slot.getBottom() + 1.0f, 1.0f);
    }

    // screen bezel
    auto bezel = display.getBounds().toFloat().expanded (16.0f);
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRoundedRectangle (bezel.translated (0.0f, 3.0f), 16.0f);
    juce::ColourGradient bz (juce::Colour (0xff3a3a3d), bezel.getX(), bezel.getY(), juce::Colour (0xff1d1d1f), bezel.getX(), bezel.getBottom(), false);
    g.setGradientFill (bz);
    g.fillRoundedRectangle (bezel, 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (bezel.reduced (1.0f), 15.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (display.getBounds().toFloat().expanded (2.0f), 8.0f, 2.0f);

    // small printed labels next to the slicing controls
    g.setColour (Theme::label());
    drawTracked (g, "SPLIT", splitBox.getBounds().toFloat().translated (-58.0f, 0.0f).withWidth (52.0f), labelFont (10.0f), 1.5f, juce::Justification::right);
    drawTracked (g, "PARTS", partsBox.getBounds().toFloat().translated (-58.0f, 0.0f).withWidth (52.0f), labelFont (10.0f), 1.5f, juce::Justification::right);

    // panels
    const juce::Rectangle<int> left (28, 444, 570, 330);
    drawPanel (g, left, tab == 0 ? "PLAY" : "ORDER");
    drawPanel (g, { 612, 444, 302, 160 }, "AMP");
    drawPanel (g, { 926, 444, 226, 160 }, "TONE");
    drawPanel (g, { 612, 614, 302, 160 }, "COLOR");
    drawPanel (g, { 926, 614, 226, 160 }, "MASTER");

    g.setColour (Theme::label());
    if (tab == 0)
    {
        drawTracked (g, "MODE", { 252.0f, 610.0f, 200.0f, 14.0f }, labelFont (10.0f), 2.0f, juce::Justification::centredLeft);
        g.setFont (labelFont (10.5f, false));
        g.drawFittedText ("Hold a key: the sound repeats at this rate.\nOFF = plays once, like a normal sampler.",
                          juce::Rectangle<int> (44, 712, 200, 40), juce::Justification::centredLeft, 2);
    }
    else
    {
        drawTracked (g, "STEPS", { 44.0f, 572.0f, 200.0f, 14.0f }, labelFont (10.0f), 2.0f, juce::Justification::centredLeft);
        drawTracked (g, "RESET", { 300.0f, 572.0f, 200.0f, 14.0f }, labelFont (10.0f), 2.0f, juce::Justification::centredLeft);
        g.setFont (labelFont (10.5f, false));
        g.drawFittedText ("Play in Random, hit CAPTURE when you like it.\nClick the cells on the screen to edit the pattern.",
                          juce::Rectangle<int> (194, 712, 280, 40), juce::Justification::centredLeft, 2);
    }
}

void PrismaEditor::resized()
{
    // header
    presetBox.setBounds (680, 24, 200, 30);
    diceButton.setBounds (890, 24, 74, 30);
    copyButton.setBounds (972, 24, 86, 30);
    pasteButton.setBounds (1066, 24, 86, 30);

    // screen
    display.setBounds (44, 84, 1092, 288);

    // row under the screen
    playTab.setBounds (28, 396, 112, 34);
    orderTab.setBounds (148, 396, 112, 34);
    loadButton.setBounds (560, 396, 92, 34);
    clearButton.setBounds (660, 396, 84, 34);
    splitBox.setBounds (816, 397, 140, 32);
    partsBox.setBounds (1068, 397, 84, 32);

    // play tab
    rate.setBounds (44, 474, 190, 230);
    length.setBounds (252, 474, 104, 124);
    swing.setBounds (366, 474, 104, 124);
    decay.setBounds (480, 474, 104, 124);
    modeKeys.setBounds (252, 628, 332, 36);
    gridSync.setBounds (252, 676, 160, 36);
    choke.setBounds (424, 676, 160, 36);

    // order tab
    orderKeys.setBounds (44, 476, 540, 80);
    stepsKeys.setBounds (44, 590, 240, 36);
    resetKeys.setBounds (300, 590, 284, 36);
    captureButton.setBounds (44, 644, 128, 44);
    lockToggle.setBounds (180, 644, 106, 44);
    dicePatternButton.setBounds (294, 644, 150, 44);
    shuffleButton.setBounds (44, 700, 140, 36);
    reverse.setBounds (470, 636, 114, 124);

    // right panels
    auto row = [] (juce::Rectangle<int> panel, std::initializer_list<juce::Component*> comps)
    {
        auto r = panel.reduced (8, 0).withTrimmedTop (28).withTrimmedBottom (6);
        const int w = r.getWidth() / (int) comps.size();
        for (auto* c : comps)
        {
            auto cell = r.removeFromLeft (w);
            c->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 78), cell.getHeight()));
        }
    };
    row ({ 612, 444, 302, 160 }, { &attack, &decayTime, &sustain, &release });
    row ({ 926, 444, 226, 160 }, { &lowCut, &highCut, &reso });
    row ({ 612, 614, 302, 160 }, { &crunch, &wash, &pump });
    row ({ 926, 614, 226, 160 }, { &tune, &fine, &volume });

    // subtle paper texture
    texture = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics tg (texture);
    juce::Random r (17);
    for (int i = 0; i < getWidth() * getHeight() / 40; ++i)
    {
        tg.setColour ((r.nextBool() ? juce::Colours::black : juce::Colours::white).withAlpha (0.035f));
        tg.fillRect (r.nextInt (getWidth()), r.nextInt (getHeight()), 1, 1);
    }
}
