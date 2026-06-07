#include "PluginEditor.h"
#include "BinaryData.h"
#include <functional>
#include <ghostmoon/ui/synthwave/Controls.h>
#include <ghostmoon/ui/synthwave/Chrome.h>
#include <ghostmoon/ui/synthwave/Theme.h>
#include <ghostmoon/ui/synthwave/Scope.h>
#include "melatonin_blur/melatonin_blur.h"

namespace {
// Shared synthwave glass gradient (keeps the main panel + popups in sync).
inline juce::ColourGradient bbGlassGradient(juce::Rectangle<float> fb) {
  return juce::ColourGradient::vertical(juce::Colour(0x70141622),
                                        juce::Colour(0x9907080E), fb);
}
// Rounded glass panel fill (main editor panels).
inline void drawGlassFill(juce::Graphics &g, juce::Rectangle<float> fb, float radius) {
  g.setGradientFill(bbGlassGradient(fb));
  g.fillRoundedRectangle(fb, radius);
  g.setColour(gm::ui::theme::line);
  g.drawRoundedRectangle(fb, radius, 1.0f);
  g.setColour(juce::Colour(0x0AFFFFFF));
  g.drawLine(fb.getX() + radius * 0.5f, fb.getY() + 1.0f,
             fb.getRight() - radius * 0.5f, fb.getY() + 1.0f, 1.0f);
}
// Full-rect popup chrome: grid backdrop -> glass -> scanline -> accent glow edge.
inline void drawPopupGlass(juce::Graphics &g, juce::Rectangle<float> fb, juce::Colour accent,
                           const juce::Image &gridCache, const juce::Image &scanCache) {
  if (gridCache.isValid()) g.drawImageAt(gridCache, 0, 0);
  else { g.setColour(gm::ui::theme::bg0); g.fillRect(fb); }
  g.setGradientFill(bbGlassGradient(fb));
  g.fillRect(fb);
  if (scanCache.isValid()) g.drawImageAt(scanCache, 0, 0);
  const float alpha[] = {0.85f, 0.22f, 0.10f};
  const float inset[] = {0.5f, 2.0f, 4.0f};
  for (int i = 0; i < 3; ++i) {
    g.setColour(accent.withAlpha(alpha[i]));
    g.drawRect(fb.reduced(inset[i]), i == 0 ? 1.5f : 1.0f);
  }
}
// Build the dimmed grid backdrop + scanline caches for a popup (call from resized()).
// The grid is drawn faint over near-black so it reads as a subtle texture, not a focal point.
inline void buildPopupCaches(juce::Component &c, juce::Image &gridCache,
                             juce::Image &scanCache) {
  const int w = c.getWidth(), h = c.getHeight();
  if (w <= 0 || h <= 0) return;
  gridCache = juce::Image(juce::Image::ARGB, w, h, true);
  {
    juce::Graphics gc(gridCache);
    gc.setColour(gm::ui::theme::bg0);
    gc.fillAll();
    auto src = juce::ImageCache::getFromMemory(BinaryData::popup_grid_png,
                                               BinaryData::popup_grid_pngSize);
    if (src.isValid()) {
      gc.setOpacity(0.30f); // faint backdrop
      gc.drawImage(src, 0, 0, w, h, 0, 0, src.getWidth(), src.getHeight());
    }
  }
  // Frosted glass: blur the static grid backdrop once (melatonin cached stack blur).
  melatonin::CachedBlur blur(12);
  gridCache = blur.render(gridCache).createCopy();
  scanCache = gm::ui::makeScanlineOverlay(w, h);
}
// Small floppy-disk glyph (C64 save nostalgia), stroked in the given colour.
inline void drawFloppyIcon(juce::Graphics &g, juce::Rectangle<float> r,
                           juce::Colour c) {
  g.setColour(c);
  g.drawRoundedRectangle(r, 1.5f, 1.0f);
  g.fillRect(r.getX() + r.getWidth() * 0.55f, r.getY() + r.getHeight() * 0.14f,
             r.getWidth() * 0.22f, r.getHeight() * 0.26f); // shutter
  g.fillRect(r.getX() + r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.55f,
             r.getWidth() * 0.56f, r.getHeight() * 0.30f); // label
}
// Small cassette-tape glyph (digi load nostalgia).
inline void drawTapeIcon(juce::Graphics &g, juce::Rectangle<float> r,
                         juce::Colour c) {
  g.setColour(c);
  g.drawRoundedRectangle(r, 1.5f, 1.0f);
  float hr = r.getHeight() * 0.15f;
  float cy = r.getCentreY();
  g.drawEllipse(r.getX() + r.getWidth() * 0.32f - hr, cy - hr, hr * 2, hr * 2,
                1.0f);
  g.drawEllipse(r.getX() + r.getWidth() * 0.68f - hr, cy - hr, hr * 2, hr * 2,
                1.0f);
}
// Translucent recessed inset card (popup sub-panel) — the grid shows faintly through.
inline void drawInsetCard(juce::Graphics &g, juce::Rectangle<float> r) {
  g.setColour(juce::Colour(0x990C0D15));
  g.fillRoundedRectangle(r, 5.0f);
  g.setColour(juce::Colours::black.withAlpha(0.85f));
  g.drawRoundedRectangle(r, 5.0f, 1.0f);
  g.setColour(juce::Colour(0x44000000)); // inner top shadow
  g.fillRect(r.getX() + 2.0f, r.getY() + 1.0f, r.getWidth() - 4.0f, 2.0f);
}
// Mini waveform glyph (0=Triangle, 1=Saw, 2=Pulse, 3=Noise) stroked in the given colour.
inline void drawWaveGlyph(juce::Graphics &g, juce::Rectangle<float> box, int wave,
                          juce::Colour c) {
  auto a = box.reduced(box.getWidth() * 0.20f, box.getHeight() * 0.30f);
  const float x0 = a.getX(), x1 = a.getRight(), yT = a.getY(), yB = a.getBottom(),
              yM = a.getCentreY(), xM = a.getCentreX();
  juce::Path p;
  if (wave == 0) { // triangle
    p.startNewSubPath(x0, yB); p.lineTo(xM, yT); p.lineTo(x1, yB);
  } else if (wave == 1) { // saw
    p.startNewSubPath(x0, yB); p.lineTo(x1, yT); p.lineTo(x1, yB);
  } else if (wave == 2) { // pulse
    p.startNewSubPath(x0, yB); p.lineTo(x0, yT); p.lineTo(xM, yT);
    p.lineTo(xM, yB); p.lineTo(x1, yB); p.lineTo(x1, yT);
  } else { // noise — zigzag
    p.startNewSubPath(x0, yM);
    for (int i = 1; i <= 6; ++i)
      p.lineTo(x0 + (x1 - x0) * (float)i / 6.0f, (i % 2) ? yT : yB);
  }
  g.setColour(c);
  g.strokePath(p, juce::PathStrokeType(1.4f));
}
} // namespace

// ========== CUSTOM LOOKANDFEEL ==========

BreadbinLookAndFeel::BreadbinLookAndFeel() {
  setColour(juce::Slider::backgroundColourId, juce::Colour(20, 20, 25));
  setColour(juce::Slider::trackColourId, juce::Colours::cyan);
  setColour(juce::Slider::thumbColourId, juce::Colour(180, 180, 190));

  setColour(juce::TextButton::buttonColourId, juce::Colour(50, 50, 60));
  setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
  setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);

  setColour(juce::ComboBox::backgroundColourId, juce::Colour(25, 25, 30));
  setColour(juce::ComboBox::outlineColourId,
            juce::Colours::cyan.withAlpha(0.5f));
  setColour(juce::ComboBox::textColourId, juce::Colours::white);
  setColour(juce::ComboBox::arrowColourId, juce::Colours::cyan);

  setColour(juce::PopupMenu::backgroundColourId, juce::Colour(25, 25, 30));
  setColour(juce::PopupMenu::highlightedBackgroundColourId,
            juce::Colours::cyan.withAlpha(0.25f));
  setColour(juce::PopupMenu::textColourId, juce::Colours::lightgrey);
  setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

  // Document window (popup panels) title bar colours
  setColour(juce::DocumentWindow::textColourId, juce::Colours::cyan);
  setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(30, 30, 35));
}

void BreadbinLookAndFeel::drawRotarySlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float /*rotaryStartAngle*/, float /*rotaryEndAngle*/, juce::Slider &slider) {
  gm::ui::drawKnob(g,
                   juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height),
                   sliderPos,
                   accentOf(slider),
                   slider.getMinimum() < 0.0);
}

void BreadbinLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float /*minSliderPos*/, float /*maxSliderPos*/,
    juce::Slider::SliderStyle style, juce::Slider &slider) {

  const auto bounds = juce::Rectangle<float>((float)x, (float)y,
                                             (float)width, (float)height);
  const bool isVertical = (style == juce::Slider::LinearVertical ||
                           style == juce::Slider::LinearBarVertical);
  const bool bipolar = (slider.getMinimum() < 0.0);
  const juce::Colour accent = accentOf(slider);

  if (isVertical) {
    const float fh = (float)height;
    const float v01 = juce::jlimit(0.0f, 1.0f,
        (fh > 0.0f) ? ((float)(y + height) - sliderPos) / fh : 0.0f);
    gm::ui::drawVSlider(g, bounds, v01, accent, bipolar);
  } else {
    const float fw = (float)width;
    const float v01 = juce::jlimit(0.0f, 1.0f,
        (fw > 0.0f) ? (sliderPos - (float)x) / fw : 0.0f);
    gm::ui::drawHSlider(g, bounds, v01, accent, bipolar);
  }
}

void BreadbinLookAndFeel::drawToggleButton(juce::Graphics &g,
                                           juce::ToggleButton &button,
                                           bool /*highlighted*/,
                                           bool /*down*/) {
  const auto bounds = button.getLocalBounds().toFloat();
  const bool isOn = button.getToggleState();
  const juce::Colour accent = accentOf(button);

  // Dot indicator: 10x10 square at left edge, vertically centred
  const float dotSize = 10.0f;
  const float dotX = bounds.getX() + 2.0f;
  const float dotY = bounds.getCentreY() - dotSize * 0.5f;
  const auto dotRect = juce::Rectangle<float>(dotX, dotY, dotSize, dotSize);
  gm::ui::drawToggleDot(g, dotRect, accent, isOn);

  // Label text to the right of the dot
  const float textX = dotX + dotSize + 4.0f;
  const float textW = bounds.getRight() - textX;
  g.setColour(isOn ? gm::ui::theme::txt : gm::ui::theme::txt2);
  g.setFont(boldFont.withHeight(10.0f));
  g.drawText(button.getButtonText(),
             juce::Rectangle<float>(textX, bounds.getY(), textW, bounds.getHeight()),
             juce::Justification::centredLeft);
}

void BreadbinLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                               juce::Button &button,
                                               const juce::Colour & /*bgColour*/,
                                               bool /*highlighted*/, bool down) {
  gm::ui::drawButtonBackground(g, button.getLocalBounds().toFloat(), down);
}

void BreadbinLookAndFeel::drawButtonText(juce::Graphics &g,
                                         juce::TextButton &button,
                                         bool /*shouldDrawButtonAsHighlighted*/,
                                         bool /*shouldDrawButtonAsDown*/) {
  auto text = button.getButtonText();
  auto leftArrow = juce::String::charToString(0x25C0);
  auto rightArrow = juce::String::charToString(0x25B6);

  if (text == leftArrow || text == rightArrow) {
    auto bounds = button.getLocalBounds().toFloat();
    float h = bounds.getHeight();
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    float s = h * 0.25f; // Arrow size

    juce::Path p;
    if (text == leftArrow) {
      // Left pointing triangle
      p.addTriangle(cx + s * 0.5f, cy - s, cx + s * 0.5f, cy + s, cx - s * 0.5f,
                    cy);
    } else {
      // Right pointing triangle
      p.addTriangle(cx - s * 0.5f, cy - s, cx - s * 0.5f, cy + s, cx + s * 0.5f,
                    cy);
    }

    g.setColour(button.findColour(juce::TextButton::textColourOffId)
                    .withAlpha(button.isEnabled() ? 1.0f : 0.5f));
    if (button.getToggleState() || button.isDown())
      g.setColour(button.findColour(juce::TextButton::textColourOnId));

    g.fillPath(p);
  } else if (button.getProperties().contains("btnIcon")) {
    auto which = button.getProperties()["btnIcon"].toString();
    if (which == "play" || which == "pause" || which == "stop") {
      // Centered transport glyph in the button's accent colour, no text.
      auto b = button.getLocalBounds().toFloat();
      auto r =
          b.withSizeKeepingCentre(b.getHeight() * 0.42f, b.getHeight() * 0.42f);
      g.setColour(accentOf(button).withAlpha(button.isEnabled() ? 1.0f : 0.5f));
      if (which == "play") {
        juce::Path p;
        p.addTriangle(r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(),
                      r.getCentreY());
        g.fillPath(p);
      } else if (which == "stop") {
        g.fillRect(r);
      } else {
        float bw = r.getWidth() * 0.34f;
        g.fillRect(r.getX(), r.getY(), bw, r.getHeight());
        g.fillRect(r.getRight() - bw, r.getY(), bw, r.getHeight());
      }
    } else {
      auto bounds = button.getLocalBounds().toFloat().reduced(7.0f, 3.0f);
      juce::Colour col =
          button.findColour(button.getToggleState()
                                ? juce::TextButton::textColourOnId
                                : juce::TextButton::textColourOffId)
              .withAlpha(button.isEnabled() ? 1.0f : 0.5f);
      auto icon = bounds.removeFromLeft(bounds.getHeight()).reduced(1.0f);
      bounds.removeFromLeft(4.0f);
      if (which == "tape")
        drawTapeIcon(g, icon, col);
      else
        drawFloppyIcon(g, icon, col);
      g.setColour(col);
      g.setFont(boldFont.withHeight(juce::jmin(11.0f, bounds.getHeight() * 0.9f)));
      g.drawText(button.getButtonText(), bounds,
                 juce::Justification::centredLeft, true);
    }
  } else {
    auto bounds = button.getLocalBounds().toFloat().reduced(4.0f, 2.0f);
    g.setFont(boldFont.withHeight(juce::jmin(11.0f, bounds.getHeight() * 0.7f)));
    g.setColour(button.findColour(button.getToggleState()
                                      ? juce::TextButton::textColourOnId
                                      : juce::TextButton::textColourOffId)
                    .withAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred,
               true);
  }
}

void BreadbinLookAndFeel::drawComboBox(juce::Graphics &g, int width, int height,
                                       bool /*isButtonDown*/, int buttonX,
                                       int /*buttonY*/, int buttonW,
                                       int /*buttonH*/, juce::ComboBox &box) {
  gm::ui::drawComboBackground(g, juce::Rectangle<float>(0.0f, 0.0f,
                                                        (float)width, (float)height));

  // Chevron arrow near the right edge
  const float arrowX = (float)buttonX + (float)buttonW * 0.5f;
  const float arrowY = (float)height * 0.5f;
  const float arrowSize = 4.5f;
  juce::Path arrow;
  arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.4f,
                    arrowX + arrowSize, arrowY - arrowSize * 0.4f,
                    arrowX,             arrowY + arrowSize * 0.6f);
  g.setColour(gm::ui::theme::txt3);
  g.fillPath(arrow);

  // Apply accent colour to the combo text label
  box.setColour(juce::ComboBox::textColourId, accentOf(box));
}

void BreadbinLookAndFeel::drawPopupMenuBackground(juce::Graphics &g, int width,
                                                  int height) {
  g.fillAll(juce::Colour(25, 25, 30));
  g.setColour(juce::Colours::cyan.withAlpha(0.2f));
  g.drawRect(0, 0, width, height, 1);
}

void BreadbinLookAndFeel::drawPopupMenuItem(
    juce::Graphics &g, const juce::Rectangle<int> &area, bool isSeparator,
    bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
    const juce::String &text, const juce::String & /*shortcut*/,
    const juce::Drawable * /*icon*/, const juce::Colour * /*textColour*/) {

  if (isSeparator) {
    g.setColour(juce::Colour(50, 50, 60));
    g.fillRect(area.reduced(5, 0).withHeight(1));
    return;
  }

  auto r = area.reduced(1);

  if (isHighlighted && isActive) {
    g.setColour(juce::Colours::cyan.withAlpha(0.2f));
    g.fillRect(r);
  }

  g.setColour(isActive ? (isHighlighted ? juce::Colours::white
                                        : juce::Colours::lightgrey)
                       : juce::Colours::grey);
  g.setFont(boldFont.withHeight(11.0f));

  auto textArea = r.reduced(10, 0);
  if (isTicked) {
    auto tickArea = textArea.removeFromLeft(16);
    g.setColour(juce::Colours::cyan);
    g.setFont(boldFont.withHeight(11.0f));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x93")), tickArea,
               juce::Justification::centred);
    g.setColour(isActive ? juce::Colours::white : juce::Colours::grey);
    g.setFont(boldFont.withHeight(11.0f));
  }

  g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

  if (hasSubMenu) {
    float arrowH = 8.0f;
    float arrowXPos = static_cast<float>(r.getRight() - 12);
    float arrowYPos = static_cast<float>(r.getCentreY());
    juce::Path arrow;
    arrow.addTriangle(arrowXPos, arrowYPos - arrowH * 0.5f, arrowXPos,
                      arrowYPos + arrowH * 0.5f, arrowXPos + 5.0f, arrowYPos);
    g.setColour(juce::Colours::cyan.withAlpha(0.5f));
    g.fillPath(arrow);
  }
}

void BreadbinLookAndFeel::drawPopupMenuSectionHeaderWithOptions(
    juce::Graphics &g, const juce::Rectangle<int> &area,
    const juce::String &sectionName, const juce::PopupMenu::Options &) {
  g.setColour(juce::Colours::cyan);
  g.setFont(boldFont.withHeight(11.0f));
  g.drawFittedText(sectionName, area.reduced(10, 0),
                   juce::Justification::centredLeft, 1);
  // Subtle underline
  g.setColour(juce::Colours::cyan.withAlpha(0.3f));
  g.drawHorizontalLine(area.getBottom() - 1,
                       static_cast<float>(area.getX() + 8),
                       static_cast<float>(area.getRight() - 8));
}

void BreadbinLookAndFeel::drawDocumentWindowTitleBar(
    juce::DocumentWindow &window, juce::Graphics &g, int w, int h,
    int titleSpaceX, int titleSpaceW, const juce::Image *, bool) {
  if (w * h == 0) return;

  const juce::Colour accent = accentOf(window);
  // Dark title bar
  g.setColour(juce::Colour(0xFF101016));
  g.fillRect(0, 0, w, h);
  // Accent bottom border
  g.setColour(accent.withAlpha(0.55f));
  g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));
  // Glow title in the popup's accent (Press Start 2P)
  gm::ui::drawGlowText(g, window.getName(), retroFont.withHeight(10.0f),
                       juce::Rectangle<int>(titleSpaceX, 0, titleSpaceW, h).toFloat(),
                       accent, juce::Justification::centred);
}

// ========== END CUSTOM LOOKANDFEEL ==========

// ========== SID FILE PLAYER PANEL ==========

SidPlayerPanel::SidPlayerPanel(BreadbinProcessor &proc) : processor(proc) {
  setSize(panelWidth, panelHeight);

  // Load button
  loadButton.setColour(juce::TextButton::buttonColourId,
                       juce::Colour(60, 60, 70));
  loadButton.setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
  loadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::cyan);
  loadButton.getProperties().set("btnIcon", "floppy");
  loadButton.setTooltip("Load a .sid / .psid / .mus / .prg file for playback");
  loadButton.onClick = [this]() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load SID File", juce::File{}, "*.sid;*.psid;*.mus;*.prg");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser &fc) {
          auto file = fc.getResult();
          if (file.existsAsFile()) {
            auto &player = processor.getSidFilePlayer();
            if (player.loadFile(file.getFullPathName().toStdString())) {
              tuneInfoLabel.setText("Loaded: " + file.getFileName(),
                                    juce::dontSendNotification);
              titleLabel.setText(juce::String(player.getTitle()),
                                 juce::dontSendNotification);
              authorLabel.setText(juce::String(player.getAuthor()),
                                  juce::dontSendNotification);
              releasedLabel.setText(juce::String(player.getReleased()),
                                    juce::dontSendNotification);
              // Populate subtune selector
              subtuneSelector.clear();
              int numSubs = player.getNumSubtunes();
              for (int i = 1; i <= numSubs; ++i)
                subtuneSelector.addItem("Sub-tune " + juce::String(i), i);
              subtuneSelector.setSelectedId(player.getCurrentSubtune(),
                                            juce::dontSendNotification);
              loadedFileName = file.getFileNameWithoutExtension().toUpperCase();
              loadLineLabel.setText(
                  "LOAD\"" + loadedFileName +
                      "\",8,1   READY.   \xE2\x96\x88   DEVICE 8 \xC2\xB7 1541",
                  juce::dontSendNotification);
              loadLineLabel.setVisible(true);
            } else {
              tuneInfoLabel.setText("Failed to load file",
                                    juce::dontSendNotification);
            }
          }
        });
  };
  addAndMakeVisible(loadButton);

  // Transport buttons
  auto setupTransport = [this](juce::TextButton &btn, juce::Colour col,
                               const char *icon) {
    btn.setButtonText("");
    btn.setColour(juce::TextButton::buttonColourId, juce::Colour(50, 50, 55));
    btn.getProperties().set("accent", (int)col.getARGB());
    btn.getProperties().set("btnIcon", icon); // glyph drawn by drawButtonText
    addAndMakeVisible(btn);
  };
  setupTransport(playButton, juce::Colours::lime, "play");
  setupTransport(pauseButton, juce::Colours::yellow, "pause");
  setupTransport(stopButton, juce::Colours::red, "stop");

  playButton.setTooltip("Play the loaded SID file");
  pauseButton.setTooltip("Pause playback");
  stopButton.setTooltip("Stop playback and reset to start");
  playButton.onClick = [this]() { processor.getSidFilePlayer().play(); };
  pauseButton.onClick = [this]() { processor.getSidFilePlayer().pause(); };
  stopButton.onClick = [this]() { processor.getSidFilePlayer().stop(); };

  // Snapshot button (accent red)
  snapshotButton.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(120, 40, 40));
  snapshotButton.setColour(juce::TextButton::textColourOnId,
                           juce::Colours::white);
  snapshotButton.setColour(juce::TextButton::textColourOffId,
                           juce::Colours::white);
  snapshotButton.setTooltip("Copy current SID register state into synth voice parameters");
  snapshotButton.onClick = [this]() { processor.snapshotSidPlayerToAPVTS(); };
  addAndMakeVisible(snapshotButton);

  // Tune info labels
  auto setupLabel = [this](juce::Label &lbl, const juce::String &text) {
    lbl.setText(text, juce::dontSendNotification);
    lbl.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(lbl);
  };
  setupLabel(tuneInfoLabel, "No file loaded");
  setupLabel(titleLabel, "No SID loaded");
  setupLabel(authorLabel, "");
  setupLabel(releasedLabel, "");
  titleLabel.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
  authorLabel.setColour(juce::Label::textColourId, gm::ui::theme::cyan);
  authorLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
  releasedLabel.setColour(juce::Label::textColourId, juce::Colour(150, 150, 165));
  releasedLabel.setFont(juce::Font(juce::FontOptions(11.0f)));

  // Subtune selector
  subtuneLabel.setText("Sub-tune:", juce::dontSendNotification);
  subtuneLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  addAndMakeVisible(subtuneLabel);
  subtuneSelector.setTooltip("Select which sub-tune to play from a multi-song SID file");
  subtuneSelector.onChange = [this]() {
    int sel = subtuneSelector.getSelectedId();
    if (sel > 0)
      processor.getSidFilePlayer().selectSubtune(sel);
  };
  addAndMakeVisible(subtuneSelector);

  // Volume slider
  volumeLabel.setText("Vol:", juce::dontSendNotification);
  volumeLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  addAndMakeVisible(volumeLabel);
  volumeSlider.setRange(0.0, 1.0, 0.01);
  volumeSlider.setValue(0.7);
  volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
  volumeSlider.setTooltip("SID player playback volume");
  volumeSlider.onValueChange = [this]() {
    processor.getSidFilePlayer().setVolume(
        static_cast<float>(volumeSlider.getValue()));
  };
  addAndMakeVisible(volumeSlider);

  // Register display (read-only text editor with monospace font)
  registerDisplay.setMultiLine(true);
  registerDisplay.setReadOnly(true);
  registerDisplay.setColour(juce::TextEditor::backgroundColourId,
                            juce::Colour(20, 20, 25));
  registerDisplay.setColour(juce::TextEditor::textColourId,
                            juce::Colours::cyan);
  registerDisplay.setFont(panelMonoFont.withHeight(12.0f));
  registerDisplay.setText("Registers will appear during playback...");
  addAndMakeVisible(registerDisplay);

  // REG / BASIC view toggle for the register dump
  auto setupSeg = [this](juce::TextButton &b, const juce::String &txt, bool on) {
    b.setButtonText(txt);
    b.setClickingTogglesState(true);
    b.setRadioGroupId(7001);
    b.setToggleState(on, juce::dontSendNotification);
    b.getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
    addAndMakeVisible(b);
  };
  setupSeg(regButton, "REG", true);
  setupSeg(basicButton, "BASIC", false);
  regButton.onClick = [this]() { basicView = false; updateRegisterDisplay(); };
  basicButton.onClick = [this]() { basicView = true; updateRegisterDisplay(); };

  // C64 LOAD line (shown once a file is loaded)
  loadLineLabel.setColour(juce::Label::textColourId, gm::ui::theme::cyan);
  loadLineLabel.setJustificationType(juce::Justification::centredLeft);
  loadLineLabel.setVisible(false);
  addAndMakeVisible(loadLineLabel);

  // ========== PER-ROLE ACCENT COLOURS ==========
  // Transport/playback = cyan; snapshot = orange. accentOf() reads "accent".
  {
    const int cy = (int)gm::ui::theme::cyan.getARGB(),
              orr = (int)gm::ui::theme::orange.getARGB();
    auto acc = [](juce::Component &c, int a) {
      c.getProperties().set("accent", a);
    };
    acc(loadButton, cy);
    acc(subtuneSelector, cy);
    acc(volumeSlider, cy);
    acc(snapshotButton, orr);
  }

  startTimerHz(30); // 30Hz register display updates
}

void SidPlayerPanel::resized() {
  // Row 1: Load + filename + Sub-tune
  loadButton.setBounds(10, 8, 110, 26);
  subtuneSelector.setBounds(panelWidth - 120, 8, 110, 26);
  subtuneLabel.setBounds(panelWidth - 186, 8, 62, 26);
  tuneInfoLabel.setBounds(128, 8, panelWidth - 320, 26);

  // C64 LOAD line
  loadLineLabel.setBounds(12, 40, panelWidth - 24, 18);

  // Info block: title / author / released (left) + transport (right)
  titleLabel.setBounds(20, 66, panelWidth - 200, 22);
  authorLabel.setBounds(20, 88, panelWidth - 200, 18);
  releasedLabel.setBounds(20, 106, panelWidth - 200, 16);
  const int ty = 78, tw = 50, th = 30;
  playButton.setBounds(panelWidth - 178, ty, tw, th);
  pauseButton.setBounds(panelWidth - 124, ty, tw, th);
  stopButton.setBounds(panelWidth - 70, ty, tw, th);

  // Volume
  volumeLabel.setBounds(10, 140, 36, 24);
  volumeSlider.setBounds(50, 140, panelWidth - 60, 24);

  // REG / BASIC register-view toggle
  regButton.setBounds(10, 170, 54, 24);
  basicButton.setBounds(66, 170, 60, 24);

  // Register display fills the middle
  registerDisplay.setBounds(10, 200, panelWidth - 20, panelHeight - 244);

  // Snapshot (bottom-right)
  snapshotButton.setBounds(panelWidth - 170, panelHeight - 38, 160, 26);

  buildPopupCaches(*this, gridCache, scanCache);
}

void SidPlayerPanel::paint(juce::Graphics &g) {
  drawPopupGlass(g, getLocalBounds().toFloat(),
                 BreadbinLookAndFeel::accentOf(*this), gridCache, scanCache);
  // Info block ground (title / author / transport)
  g.setColour(juce::Colour(0x99101018));
  g.fillRoundedRectangle(8.0f, 60.0f, static_cast<float>(panelWidth - 16), 76.0f,
                         5.0f);
}

void SidPlayerPanel::refreshFonts(const juce::Font &mono) {
  panelMonoFont = mono;
  registerDisplay.setFont(panelMonoFont.withHeight(12.0f));
  loadLineLabel.setFont(panelMonoFont.withHeight(11.0f));
}

void SidPlayerPanel::timerCallback() { updateRegisterDisplay(); }

void SidPlayerPanel::updateRegisterDisplay() {
  auto &player = processor.getSidFilePlayer();
  if (!player.isPlaying() && !player.isPaused())
    return;

  auto snapshot = player.getRegisterSnapshot();
  if (!snapshot.valid)
    return;

  if (basicView) {
    // Same register state rendered as a C64 BASIC POKE listing.
    juce::String t;
    t << "10 SID=54272 : REM $D400\n";
    t << "20 POKE SID+24," << (int)(snapshot.regs[0x18] & 0x0F)
      << " : REM VOLUME\n";
    for (int v = 0; v < 3; ++v) {
      int b = v * 7;
      t << (30 + v * 20) << " POKE SID+" << b << "," << (int)snapshot.regs[b]
        << " : POKE SID+" << (b + 1) << "," << (int)snapshot.regs[b + 1]
        << " : REM V" << (v + 1) << " FREQ\n";
      t << (40 + v * 20) << " POKE SID+" << (b + 5) << ","
        << (int)snapshot.regs[b + 5] << " : POKE SID+" << (b + 6) << ","
        << (int)snapshot.regs[b + 6] << " : REM V" << (v + 1) << " ADSR\n";
    }
    t << "90 POKE SID+4," << (int)snapshot.regs[0x04] << " : REM V1 CTRL\n";
    registerDisplay.setText(t);
    return;
  }

  juce::String text;

  // Voice registers
  const char *waveNames[] = {"---", "TRI", "SAW", "T+S", "PUL", "T+P",
                             "S+P", "TSP", "NOI", "T+N", "S+N", "TSN",
                             "P+N", "TPN", "SPN", "ALL"};
  for (int v = 0; v < 3; ++v) {
    int base = v * 7;
    int freq = snapshot.regs[base] | (snapshot.regs[base + 1] << 8);
    int pw = snapshot.regs[base + 2] | ((snapshot.regs[base + 3] & 0x0F) << 8);
    uint8_t ctrl = snapshot.regs[base + 4];
    int waveIdx = (ctrl >> 4) & 0x0F;
    int attack = (snapshot.regs[base + 5] >> 4) & 0x0F;
    int decay = snapshot.regs[base + 5] & 0x0F;
    int sustain = (snapshot.regs[base + 6] >> 4) & 0x0F;
    int release = snapshot.regs[base + 6] & 0x0F;
    bool gate = ctrl & 0x01;
    bool sync = ctrl & 0x02;
    bool ring = ctrl & 0x04;

    text += "V" + juce::String(v + 1) + ": " + waveNames[waveIdx];
    text += " F:" +
            juce::String::toHexString(freq).paddedLeft('0', 4).toUpperCase();
    text += " PW:" + juce::String(pw).paddedLeft(' ', 4);
    text += " A:" + juce::String(attack);
    text += " D:" + juce::String(decay);
    text += " S:" + juce::String(sustain);
    text += " R:" + juce::String(release);
    if (gate)
      text += " GATE";
    if (sync)
      text += " SYNC";
    if (ring)
      text += " RING";
    text += "\n";
  }

  // Filter registers
  int cutoff = (snapshot.regs[0x15] & 0x07) | (snapshot.regs[0x16] << 3);
  int resonance = (snapshot.regs[0x17] >> 4) & 0x0F;
  uint8_t routing = snapshot.regs[0x17] & 0x07;
  uint8_t mode = snapshot.regs[0x18];
  int volume = mode & 0x0F;

  text += "Filter: Cut=" + juce::String(cutoff);
  text += " Res=" + juce::String(resonance);
  if (mode & 0x10)
    text += " LP";
  if (mode & 0x20)
    text += " BP";
  if (mode & 0x40)
    text += " HP";
  text += " Route=" + juce::String(static_cast<int>(routing));
  text += " Vol=" + juce::String(volume);

  // Time display
  text += "\nTime: " + juce::String(player.getPlayTimeMs() / 1000) + "s";

  registerDisplay.setText(text);
}

// ========== END SID FILE PLAYER PANEL ==========

// ========== DIGI SAMPLER PANEL ==========

DigiSamplerPanel::DigiSamplerPanel(BreadbinProcessor &proc) : processor(proc) {
  setSize(panelWidth, panelHeight);

  loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(50, 50, 60));
  loadButton.setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
  loadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::cyan);
  loadButton.getProperties().set("btnIcon", "tape");
  loadButton.onClick = [this]() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Sample", juce::File{}, "*.wav;*.aiff;*.aif");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser &fc) {
      auto result = fc.getResult();
      if (result == juce::File{})
        return;
      auto &digi = processor.getDigiSampler();
      if (digi.loadFromFile(result.getFullPathName().toStdString())) {
        // Sync root note from APVTS
        auto *rootParam = processor.apvts.getRawParameterValue("digiRootNote");
        if (rootParam)
          digi.setRootNote(static_cast<int>(rootParam->load()));
        updateInfoLabels();
        repaint();
      }
    });
  };
  addAndMakeVisible(loadButton);

  fileNameLabel.setText("No sample loaded", juce::dontSendNotification);
  fileNameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(fileNameLabel);

  sampleInfoLabel.setText("", juce::dontSendNotification);
  sampleInfoLabel.setColour(juce::Label::textColourId,
                            juce::Colours::grey);
  addAndMakeVisible(sampleInfoLabel);

  rootNoteLabel.setText("ROOT NOTE", juce::dontSendNotification);
  rootNoteLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF6F6F82));
  addAndMakeVisible(rootNoteLabel);

  // Populate root note selector: C1 (24) to C7 (96)
  const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F",
                              "F#", "G", "G#", "A", "A#", "B"};
  for (int midi = 24; midi <= 96; ++midi) {
    int octave = (midi / 12) - 1;
    juce::String name = juce::String(noteNames[midi % 12]) + juce::String(octave);
    rootNoteSelector.addItem(name, midi);
  }
  rootNoteSelector.setSelectedId(60, juce::dontSendNotification); // C4
  rootNoteSelector.onChange = [this]() {
    int note = rootNoteSelector.getSelectedId();
    if (note > 0) {
      auto *p = processor.apvts.getParameter("digiRootNote");
      if (p) p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(note)));
      processor.getDigiSampler().setRootNote(note);
    }
  };
  addAndMakeVisible(rootNoteSelector);

  loopButton.setButtonText("LOOP");
  loopButton.setColour(juce::ToggleButton::textColourId, juce::Colours::cyan);
  loopButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  addAndMakeVisible(loopButton);
  loopAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "digiLoop", loopButton);

  bitDepthLabel.setText("BIT DEPTH", juce::dontSendNotification);
  bitDepthLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF6F6F82));
  addAndMakeVisible(bitDepthLabel);

  bitDepthSelector.addItem("4-bit", 1);
  bitDepthSelector.addItem("8-bit", 2);
  bitDepthSelector.setSelectedId(1, juce::dontSendNotification);
  bitDepthSelector.onChange = [this]() {
    auto *p = processor.apvts.getParameter("digiBitDepth");
    if (p) p->setValueNotifyingHost(
        p->convertTo0to1(static_cast<float>(bitDepthSelector.getSelectedId() - 1)));
  };
  addAndMakeVisible(bitDepthSelector);

  // ========== PER-ROLE ACCENT COLOURS ==========
  // All controls = cyan; loop toggle = green. accentOf() reads "accent".
  {
    const int cy = (int)gm::ui::theme::cyan.getARGB(),
              gr = (int)gm::ui::theme::grn.getARGB();
    auto acc = [](juce::Component &c, int a) {
      c.getProperties().set("accent", a);
    };
    acc(loadButton, cy);
    acc(rootNoteSelector, cy);
    acc(bitDepthSelector, cy);
    acc(loopButton, gr);
  }

  updateInfoLabels();
}

void DigiSamplerPanel::resized() {
  // Header: tape-icon Load + filename / sample info
  loadButton.setBounds(14, 16, 120, 28);
  fileNameLabel.setBounds(146, 13, panelWidth - 160, 18);
  sampleInfoLabel.setBounds(146, 32, panelWidth - 160, 13);

  // Controls card (paint draws the card at y=140, h=64): 3 labeled columns
  rootNoteLabel.setBounds(26, 148, 120, 12);
  rootNoteSelector.setBounds(26, 164, 120, 28);
  bitDepthLabel.setBounds(160, 148, 130, 12);
  bitDepthSelector.setBounds(160, 164, 130, 28);
  loopButton.setBounds(306, 160, 110, 28);

  buildPopupCaches(*this, gridCache, scanCache);
}

void DigiSamplerPanel::paint(juce::Graphics &g) {
  drawPopupGlass(g, getLocalBounds().toFloat(),
                 BreadbinLookAndFeel::accentOf(*this), gridCache, scanCache);

  const juce::Colour acc = BreadbinLookAndFeel::accentOf(*this);

  // Waveform card
  auto waveRect = juce::Rectangle<float>(14.0f, 54.0f,
                                         static_cast<float>(panelWidth - 28), 78.0f);
  drawInsetCard(g, waveRect);

  auto &digi = processor.getDigiSampler();
  if (digi.isLoaded() && digi.getNumSamples() > 0) {
    const auto &packed = digi.getPackedData();
    int numSamples = digi.getNumSamples();
    float padX = 6.0f, padY = 6.0f;
    float w = waveRect.getWidth() - padX * 2.0f;
    float h = waveRect.getHeight() - padY * 2.0f;
    float x0 = waveRect.getX() + padX;
    float y0 = waveRect.getY() + padY;
    juce::Path path;
    int numPts = juce::jmin(static_cast<int>(w), numSamples);
    for (int i = 0; i < numPts; ++i) {
      float t = static_cast<float>(i) / static_cast<float>(juce::jmax(1, numPts - 1));
      int sampleIdx = static_cast<int>(t * static_cast<float>(numSamples - 1));
      int byteIdx = sampleIdx / 2;
      uint8_t val = (sampleIdx % 2 == 0)
                        ? ((packed[static_cast<size_t>(byteIdx)] >> 4) & 0x0F)
                        : (packed[static_cast<size_t>(byteIdx)] & 0x0F);
      float norm = static_cast<float>(val) / 15.0f;
      float px = x0 + static_cast<float>(i);
      float py = y0 + h * (1.0f - norm);
      if (i == 0)
        path.startNewSubPath(px, py);
      else
        path.lineTo(px, py);
    }
    g.setColour(acc.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(2.5f));
    g.setColour(acc);
    g.strokePath(path, juce::PathStrokeType(1.0f));
  } else {
    g.setColour(juce::Colour(0xFF6F6F82));
    g.setFont(panelProFont.withHeight(11.0f));
    g.drawText("Load a WAV sample for 4-bit $D418 digi playback", waveRect,
               juce::Justification::centred);
  }

  // Controls card
  drawInsetCard(g, juce::Rectangle<float>(
                       14.0f, 140.0f, static_cast<float>(panelWidth - 28), 64.0f));

  // Hint card
  auto hintRect = juce::Rectangle<int>(14, 212, panelWidth - 28, 44);
  drawInsetCard(g, hintRect.toFloat());
  g.setColour(juce::Colour(0xFF8A8A9A));
  g.setFont(panelMonoFont.withHeight(9.5f));
  g.drawFittedText("4-bit mode = authentic C64 $D418 volume-register crunch. "
                   "Pitch tracks the MIDI note relative to root.",
                   hintRect.reduced(10, 6), juce::Justification::centredLeft, 3);
}

void DigiSamplerPanel::refreshFonts(const juce::Font &pro,
                                     const juce::Font &bold,
                                     const juce::Font &mono) {
  panelProFont = pro;
  panelBoldFont = bold;
  panelMonoFont = mono;
  fileNameLabel.setFont(bold.withHeight(14.0f));
  sampleInfoLabel.setFont(mono.withHeight(10.0f));
  rootNoteLabel.setFont(pro.withHeight(9.5f));
  bitDepthLabel.setFont(pro.withHeight(9.5f));
}

void DigiSamplerPanel::updateInfoLabels() {
  auto &digi = processor.getDigiSampler();
  if (digi.isLoaded()) {
    juce::File f(digi.getFilePath());
    fileNameLabel.setText(f.getFileName(), juce::dontSendNotification);
    double durSec = static_cast<double>(digi.getNumSamples()) / digi.getSourceSampleRate();
    int packedBytes = static_cast<int>(digi.getPackedData().size());
    sampleInfoLabel.setText(
        juce::String(durSec, 2) + "s  " +
            juce::String(digi.getNumSamples()) + " samples  " +
            juce::String(packedBytes) + " bytes packed",
        juce::dontSendNotification);
  } else {
    fileNameLabel.setText("No sample loaded", juce::dontSendNotification);
    sampleInfoLabel.setText("", juce::dontSendNotification);
  }
}

// ========== END DIGI SAMPLER PANEL ==========

BreadbinEditor::BreadbinEditor(BreadbinProcessor &p)
    : gm::ui::ScaledEditor(p, 1000, 800), processor(p),
      keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

  backgroundImage = juce::ImageFileFormat::loadFrom(
      BinaryData::background_jpg, BinaryData::background_jpgSize);

  // Load retro font (Press Start 2P) from binary assets
  auto retroTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::PressStart2PRegular_ttf,
      BinaryData::PressStart2PRegular_ttfSize);
  retroFont = juce::Font(juce::FontOptions(retroTypeface).withHeight(12.0f));

  // Load professional fonts (Lato) and monospaced font (JetBrains Mono)
  auto latoTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::LatoRegular_ttf, BinaryData::LatoRegular_ttfSize);
  proFont = juce::Font(juce::FontOptions(latoTypeface).withHeight(14.0f));

  auto latoBoldTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::LatoBold_ttf, BinaryData::LatoBold_ttfSize);
  boldFont = juce::Font(juce::FontOptions(latoBoldTypeface).withHeight(14.0f));

  auto monoTypeface = juce::Typeface::createSystemTypefaceFor(
      BinaryData::JetBrainsMonoRegular_ttf,
      BinaryData::JetBrainsMonoRegular_ttfSize);
  monoFont = juce::Font(juce::FontOptions(monoTypeface).withHeight(12.0f));

  customLookAndFeel.setFonts(proFont, boldFont, monoFont, retroFont);
  setLookAndFeel(&customLookAndFeel);
  midiLearnOverlay.refreshFonts(proFont, boldFont);

  keyboardState.addListener(this);
  processor.getMidiMessageCollector().reset(p.getSampleRate());

  setupControls();
  setupSidPanel(true);
  setupSidPanel(false);
  setupVoiceEditor();

  // On first-ever launch (no saved state AND editor never opened), apply init
  // preset. On subsequent editor opens (close/reopen in DAW) or state restore,
  // sync non-APVTS controls from processor state.
  if (!processor.wasStateRestored() && !processor.wasEditorOpened()) {
    applyGlobalPreset(1);
  } else {
    for (int v = 0; v < 6; ++v)
      processor.applyVoiceSettings(v);

    // Sync non-APVTS filter sliders from processor state
    leftCutoffSlider.setValue(processor.getBaseFilterCutoff(true),
                              juce::dontSendNotification);
    leftResonanceSlider.setValue(processor.getBaseFilterResonance(true),
                                 juce::dontSendNotification);
    rightCutoffSlider.setValue(processor.getBaseFilterCutoff(false),
                               juce::dontSendNotification);
    rightResonanceSlider.setValue(processor.getBaseFilterResonance(false),
                                  juce::dontSendNotification);

    // Sync global preset selector from persisted ID
    globalPresetSelector.setSelectedId(processor.getGlobalPresetId(),
                                       juce::dontSendNotification);
  }
  processor.markEditorOpened();
  selectVoice(processor.getSelectedVoice());
  addAndMakeVisible(midiLearnOverlay);
  midiLearnOverlay.setAlwaysOnTop(true);

  filterDisplay_L.setFont(proFont);
  filterDisplay_R.setFont(proFont);
  filterDisplay_L.setMonoFont(monoFont);
  filterDisplay_R.setMonoFont(monoFont);
  addAndMakeVisible(filterDisplay_L);
  addAndMakeVisible(filterDisplay_R);

  startTimerHz(30);

  // Initialize Global Attachments
  masterVolAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "masterVol", masterVolSlider);
  noiseGateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "noiseGateThreshold", noiseGateSlider);
  dualModeAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "dualMode", dualModeSelector);
  chipLeftAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "chipLeft", leftChipSelector);
  chipRightAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "chipRight", rightChipSelector);
  leftDetuneAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "leftDetune", leftDetuneSlider);
  rightDetuneAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "rightDetune", rightDetuneSlider);
  leftPanAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "leftPan", leftPanSlider);
  rightPanAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "rightPan", rightPanSlider);
  glideAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "glide", glideTimeSlider);
  clockModeAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "clockMode", clockModeSelector);
  extInputEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "extInputEnable", extInputEnableButton);
  extInputLevelAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "extInputLevel", extInputLevelSlider);

  // FX: Chorus
  chorusEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "chorusEnable", chorusEnableButton);
  chorusRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "chorusRate", chorusRateSlider);
  chorusDepthAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "chorusDepth", chorusDepthSlider);
  chorusMixAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "chorusMix", chorusMixSlider);

  // FX: Delay
  delayEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "delayEnable", delayEnableButton);
  delayTimeLAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayTimeL", delayTimeLSlider);
  delayTimeRAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayTimeR", delayTimeRSlider);
  delayFBAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayFeedback", delayFeedbackSlider);
  delayMixAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "delayMix", delayMixSlider);

  // FX: Reverb
  reverbEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "reverbEnable", reverbEnableButton);
  reverbDecayAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "reverbDecay", reverbDecaySlider);
  reverbDampingAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "reverbDamping", reverbDampingSlider);
  reverbMixAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "reverbMix", reverbMixSlider);

  // Wavetable attachments are now in WavetablePanel popup

  // Filter Envelope
  filterEnvEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "filterEnvEnable", filterEnvEnableButton);
  filterEnvAttackAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvAttack", filterEnvAttackSlider);
  filterEnvDecayAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvDecay", filterEnvDecaySlider);
  filterEnvSustainAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvSustain", filterEnvSustainSlider);
  filterEnvReleaseAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvRelease", filterEnvReleaseSlider);
  filterEnvAmountAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "filterEnvAmount", filterEnvAmountSlider);

  arpEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "arpEnable", arpEnableButton);
  arpPatternAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "arpPattern", arpPatternSelector);
  arpRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "arpRate", arpRateSlider);
  arpOctaveAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "arpOctaves", arpOctaveSelector);

  refreshVoiceEditorAttachments();

  // Snapshot initial preset state for dirty detection
  processor.snapshotPresetState();

  // UI scale selector — persisted per-machine via PropertiesFile
  juce::PropertiesFile::Options propOpts;
  propOpts.applicationName = "Breadbin";
  propOpts.filenameSuffix = "settings";
  propOpts.folderName = "Breadbin";
  propOpts.osxLibrarySubFolder = "Application Support";
  appProperties.setStorageParameters(propOpts);

  scaleSelector.addItem("75%", 1);
  scaleSelector.addItem("100%", 2);
  scaleSelector.addItem("125%", 3);
  scaleSelector.addItem("150%", 4);
  scaleSelector.setTooltip("UI scale (rescales the entire window for low-res displays)");

  auto *settings = appProperties.getUserSettings();
  const float savedScale =
      settings != nullptr
          ? (float)settings->getDoubleValue("uiScale", 1.0)
          : 1.0f;
  const auto idForScale = [](float s) {
    if (s < 0.875f) return 1;
    if (s < 1.125f) return 2;
    if (s < 1.375f) return 3;
    return 4;
  };
  scaleSelector.setSelectedId(idForScale(savedScale), juce::dontSendNotification);

  scaleSelector.onChange = [this]() {
    const float scales[] = {0.75f, 1.0f, 1.25f, 1.5f};
    const int idx = scaleSelector.getSelectedId() - 1;
    if (idx < 0 || idx >= 4) return;
    setScale(scales[idx]);
    if (auto *s = appProperties.getUserSettings()) {
      s->setValue("uiScale", (double)scales[idx]);
      s->saveIfNeeded();
    }
  };
  addAndMakeVisible(scaleSelector);

  setScale(savedScale);
}

BreadbinEditor::~BreadbinEditor() {
  stopTimer();
  setLookAndFeel(nullptr); // Must reset before customLookAndFeel is destroyed
  keyboardState.removeListener(this);
}

void BreadbinEditor::handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                                  int midiNoteNumber, float velocity) {
  auto msg = juce::MidiMessage::noteOn(midiChannel, midiNoteNumber, velocity);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                                   int midiNoteNumber, float velocity) {
  auto msg = juce::MidiMessage::noteOff(midiChannel, midiNoteNumber, velocity);
  processor.getMidiMessageCollector().addMessageToQueue(msg);
}

void BreadbinEditor::timerCallback() {
  midiLearnOverlay.tick();
  if (midiLearnOverlay.isShowingAnything())
    repaint();

  updateModulationMeters();
  updateVoiceCountDisplay();
  updateFxBypassVisuals();
  updateSidPlayerOverlay();
}

void BreadbinEditor::updateModulationMeters() {
  if (cutoffMeterL.setValues(
          static_cast<float>(processor.getBaseFilterCutoff(true)),
          static_cast<float>(processor.getLastAppliedCutoffLeft())))
    cutoffMeterL.repaint();

  if (cutoffMeterR.setValues(
          static_cast<float>(processor.getBaseFilterCutoff(false)),
          static_cast<float>(processor.getLastAppliedCutoffRight())))
    cutoffMeterR.repaint();

  if (pwMeter.setValues(
          static_cast<float>(processor.getVoiceSettings(selectedVoice).pulseWidth),
          static_cast<float>(processor.getLastAppliedPW())))
    pwMeter.repaint();

  if (pitchMeter.setValues(0.0f, processor.getLastAppliedPitchOffset()))
    pitchMeter.repaint();

  if (resMeterL.setValues(
          static_cast<float>(processor.getBaseFilterResonance(true)),
          static_cast<float>(processor.getLastAppliedResLeft())))
    resMeterL.repaint();

  if (resMeterR.setValues(
          static_cast<float>(processor.getBaseFilterResonance(false)),
          static_cast<float>(processor.getLastAppliedResRight())))
    resMeterR.repaint();

  // Filter response displays (setters have internal dirty checks)
  filterDisplay_L.setCutoff(processor.getBaseFilterCutoff(true));
  filterDisplay_L.setResonance(processor.getBaseFilterResonance(true));
  filterDisplay_L.setModes(leftLPButton.getToggleState(),
                            leftBPButton.getToggleState(),
                            leftHPButton.getToggleState());

  filterDisplay_R.setCutoff(processor.getBaseFilterCutoff(false));
  filterDisplay_R.setResonance(processor.getBaseFilterResonance(false));
  filterDisplay_R.setModes(rightLPButton.getToggleState(),
                            rightBPButton.getToggleState(),
                            rightHPButton.getToggleState());

  // Preset dirty indicator
  presetDirtyLabel.setText(processor.isPresetDirty() ? "*" : "",
                           juce::dontSendNotification);

  // CPU load
  float cpu = processor.getCpuLoad();
  juce::String txt = "CPU: " + juce::String(static_cast<int>(cpu)) + "%";
  if (cpu > 80.0f)
    cpuLoadLabel.setColour(juce::Label::textColourId, juce::Colours::red);
  else if (cpu > 50.0f)
    cpuLoadLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
  else
    cpuLoadLabel.setColour(juce::Label::textColourId, gm::ui::theme::grn);
  cpuLoadLabel.setText(txt, juce::dontSendNotification);
}

void BreadbinEditor::updateVoiceCountDisplay() {
  auto vm = processor.getVoiceMode();
  bool showMaxNotes = (vm == BreadbinProcessor::VoiceMode::Polyphonic ||
                       vm == BreadbinProcessor::VoiceMode::PolyPara);
  polyMaxNotesSelector.setVisible(showMaxNotes);

  switch (vm) {
  case BreadbinProcessor::VoiceMode::Mono:
    polyVoiceCountLabel.setVisible(false);
    break;
  case BreadbinProcessor::VoiceMode::Paraphonic: {
    int active = processor.getActiveParaVoiceCount();
    polyVoiceCountLabel.setText(
        juce::String(active) + "/6", juce::dontSendNotification);
    polyVoiceCountLabel.setVisible(true);
    break;
  }
  case BreadbinProcessor::VoiceMode::Polyphonic: {
    int active = processor.getActivePolyVoiceCount();
    int maxN = processor.getPolyMaxNotes();
    polyVoiceCountLabel.setText(
        juce::String(active) + "/" + juce::String(maxN),
        juce::dontSendNotification);
    polyVoiceCountLabel.setVisible(true);
    break;
  }
  case BreadbinProcessor::VoiceMode::PolyPara: {
    int total = processor.getTotalActiveNoteCount();
    int maxTotal = processor.getPolyMaxNotes() * 3;
    polyVoiceCountLabel.setText(
        juce::String(total) + "/" + juce::String(maxTotal),
        juce::dontSendNotification);
    polyVoiceCountLabel.setVisible(true);
    break;
  }
  }

  // Para mode controls visibility and voice button dimming
  bool paraActive = (vm == BreadbinProcessor::VoiceMode::Paraphonic ||
                     vm == BreadbinProcessor::VoiceMode::PolyPara);
  paraSpreadSlider.setVisible(paraActive);
  paraSpreadLabel.setVisible(paraActive);
  paraRetrigButton.setVisible(paraActive);

  ringModButton.setEnabled(!paraActive);
  syncButton.setEnabled(!paraActive);
  modOffsetSlider.setEnabled(!paraActive);
  if (paraActive) {
    ringModButton.setAlpha(0.4f);
    syncButton.setAlpha(0.4f);
    modOffsetSlider.setAlpha(0.4f);
  } else {
    ringModButton.setAlpha(1.0f);
    syncButton.setAlpha(1.0f);
    modOffsetSlider.setAlpha(1.0f);
  }

  // Grey out voice 1-5 buttons in para mode (all voices share V1 settings)
  if (paraActive) {
    for (int i = 1; i < 3; ++i)
      leftVoiceButtons[i].setAlpha(0.35f);
    for (int i = 0; i < 3; ++i)
      rightVoiceButtons[i].setAlpha(0.35f);
    leftVoiceButtons[0].setAlpha(1.0f);
    if (selectedVoice > 0) {
      juce::String sidName = selectedVoice < 3 ? "L" : "R";
      voiceEditorLabel.setText(
          "VOICE " + juce::String(selectedVoice + 1) + " (" + sidName +
              ") SHARED",
          juce::dontSendNotification);
    }
  } else {
    for (int i = 0; i < 3; ++i) {
      leftVoiceButtons[i].setAlpha(1.0f);
      rightVoiceButtons[i].setAlpha(1.0f);
    }
  }

  // Mod matrix activity indicators
  int activeSlots = 0;
  for (int i = 0; i < 4; ++i) {
    auto *enableParam = processor.apvts.getRawParameterValue(
        "mod" + juce::String(i) + "_enable");
    if (enableParam && enableParam->load() >= 0.5f) {
      auto *srcParam = processor.apvts.getRawParameterValue(
          "mod" + juce::String(i) + "_src");
      if (srcParam && static_cast<int>(srcParam->load()) > 0)
        ++activeSlots;
    }
  }
  if (activeSlots > 0)
    modMatrixButton.setButtonText(
        "Modulation [" + juce::String(activeSlots) + "]");
  else
    modMatrixButton.setButtonText("Modulation");
}

void BreadbinEditor::updateFxBypassVisuals() {
  float chorusAlpha = chorusEnableButton.getToggleState() ? 1.0f : 0.35f;
  chorusRateSlider.setAlpha(chorusAlpha);
  chorusDepthSlider.setAlpha(chorusAlpha);
  chorusMixSlider.setAlpha(chorusAlpha);
  chorusRateLabel.setAlpha(chorusAlpha);
  chorusDepthLabel.setAlpha(chorusAlpha);
  chorusMixLabel.setAlpha(chorusAlpha);

  float delayAlpha = delayEnableButton.getToggleState() ? 1.0f : 0.35f;
  delayTimeLSlider.setAlpha(delayAlpha);
  delayTimeRSlider.setAlpha(delayAlpha);
  delayFeedbackSlider.setAlpha(delayAlpha);
  delayMixSlider.setAlpha(delayAlpha);
  delayTimeLLabel.setAlpha(delayAlpha);
  delayTimeRLabel.setAlpha(delayAlpha);
  delayFBLabel.setAlpha(delayAlpha);
  delayMixLabel.setAlpha(delayAlpha);

  float reverbAlpha = reverbEnableButton.getToggleState() ? 1.0f : 0.35f;
  reverbDecaySlider.setAlpha(reverbAlpha);
  reverbDampingSlider.setAlpha(reverbAlpha);
  reverbMixSlider.setAlpha(reverbAlpha);
  reverbDecayLabel.setAlpha(reverbAlpha);
  reverbDampingLabel.setAlpha(reverbAlpha);
  reverbMixLabel.setAlpha(reverbAlpha);
}

void BreadbinEditor::updateSidPlayerOverlay() {
  if (processor.sidPlayerActive.load(std::memory_order_relaxed)) {
    auto snapshot = processor.getSidFilePlayer().getRegisterSnapshot();
    if (snapshot.valid) {
      const char *waveNames[] = {"---", "TRI", "SAW", "T+S", "PUL", "T+P",
                                 "S+P", "TSP", "NOI", "T+N", "S+N", "TSN",
                                 "P+N", "TPN", "SPN", "ALL"};
      int v = selectedVoice % 3;
      int base = v * 7;
      uint8_t ctrl = snapshot.regs[base + 4];
      int waveIdx = (ctrl >> 4) & 0x0F;
      int pw =
          snapshot.regs[base + 2] | ((snapshot.regs[base + 3] & 0x0F) << 8);

      sidOverlayWave.setText(waveNames[waveIdx], juce::dontSendNotification);
      sidOverlayPW.setText("PW:" + juce::String(pw),
                           juce::dontSendNotification);
      sidOverlayAttack.setText(
          "A:" + juce::String((snapshot.regs[base + 5] >> 4) & 0x0F),
          juce::dontSendNotification);
      sidOverlayDecay.setText("D:" +
                                  juce::String(snapshot.regs[base + 5] & 0x0F),
                              juce::dontSendNotification);
      sidOverlaySustain.setText(
          "S:" + juce::String((snapshot.regs[base + 6] >> 4) & 0x0F),
          juce::dontSendNotification);
      sidOverlayRelease.setText(
          "R:" + juce::String(snapshot.regs[base + 6] & 0x0F),
          juce::dontSendNotification);

      int cutoff = (snapshot.regs[0x15] & 0x07) | (snapshot.regs[0x16] << 3);
      int res = (snapshot.regs[0x17] >> 4) & 0x0F;
      sidOverlayCutoff.setText("Cut:" + juce::String(cutoff),
                               juce::dontSendNotification);
      sidOverlayRes.setText("Res:" + juce::String(res),
                            juce::dontSendNotification);

      sidOverlayWave.setVisible(true);
      sidOverlayPW.setVisible(true);
      sidOverlayAttack.setVisible(true);
      sidOverlayDecay.setVisible(true);
      sidOverlaySustain.setVisible(true);
      sidOverlayRelease.setVisible(true);
      sidOverlayCutoff.setVisible(true);
      sidOverlayRes.setVisible(true);
    }
  } else {
    sidOverlayWave.setVisible(false);
    sidOverlayPW.setVisible(false);
    sidOverlayAttack.setVisible(false);
    sidOverlayDecay.setVisible(false);
    sidOverlaySustain.setVisible(false);
    sidOverlayRelease.setVisible(false);
    sidOverlayCutoff.setVisible(false);
    sidOverlayRes.setVisible(false);
  }
}

// ========== ModMatrixPanel Implementation ==========

ModMatrixPanel::ModMatrixPanel(BreadbinProcessor &proc) : processor(proc) {
  // ========== LFO1 SETUP ==========
  lfoEnableButton.setTooltip("LFO 1: Low-frequency oscillator for modulation");
  lfoEnableButton.setColour(juce::ToggleButton::tickColourId,
                            juce::Colours::cyan);
  addAndMakeVisible(lfoEnableButton);

  lfoWaveformSelector.addItem("Tri", 1);
  lfoWaveformSelector.addItem("Saw", 2);
  lfoWaveformSelector.addItem("Sq", 3);
  lfoWaveformSelector.addItem("S&H", 4);
  lfoWaveformSelector.addItem("Sin", 5);
  lfoWaveformSelector.setTooltip("LFO1 waveform shape");
  addAndMakeVisible(lfoWaveformSelector);

  auto setupLfoSlider = [this](std::unique_ptr<MappableSlider> &slider,
                               juce::Label &label, const juce::String &name,
                               float minVal, float maxVal, float defaultVal,
                               juce::Slider::SliderStyle style,
                               juce::Slider::TextEntryBoxPosition textPos) {
    slider = std::make_unique<MappableSlider>(
        processor, BreadbinProcessor::ControlParam::None);
    slider->setRange(minVal, maxVal, 0.01);
    slider->setValue(defaultVal);
    slider->setDoubleClickReturnValue(true, static_cast<double>(defaultVal));
    slider->setSliderStyle(style);
    slider->setTextBoxStyle(textPos, false, 40, 16); // 16px: default LAF 15px font fits
    slider->setColour(juce::Slider::textBoxTextColourId, juce::Colours::cyan);
    slider->setColour(juce::Slider::textBoxOutlineColourId,
                      juce::Colours::transparentBlack);
    addAndMakeVisible(*slider);
    slider->setTooltip(name);
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(panelProFont.withHeight(9.0f));
    addAndMakeVisible(label);
  };

  setupLfoSlider(lfoRateSlider, lfoRateLabel, "Rate", 0.1f, 10.0f, 2.0f,
                 juce::Slider::LinearHorizontal, juce::Slider::TextBoxBelow);
  lfoRateSlider->setTextValueSuffix(" Hz");
  lfoRateSlider->setTooltip("LFO1 rate in Hz (0.1 - 10 Hz)");
  setupLfoSlider(lfoDepthFilterSlider, lfoDepthFilterLabel, "Flt", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  lfoDepthFilterSlider->setTooltip("LFO1 filter cutoff modulation depth");
  setupLfoSlider(lfoDepthPWSlider, lfoDepthPWLabel, "PW", 0.0f, 1.0f, 0.0f,
                 juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  lfoDepthPWSlider->setTooltip("LFO1 pulse width modulation depth");
  setupLfoSlider(lfoDepthPitchSlider, lfoDepthPitchLabel, "Vib", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  lfoDepthPitchSlider->setTooltip("LFO1 vibrato (pitch modulation) depth");

  // ========== LFO2 SETUP ==========
  lfo2EnableButton.setTooltip("LFO 2: Second LFO for additional modulation");
  lfo2EnableButton.setColour(juce::ToggleButton::tickColourId,
                             juce::Colours::orange);
  addAndMakeVisible(lfo2EnableButton);

  lfo2WaveformSelector.addItem("Tri", 1);
  lfo2WaveformSelector.addItem("Saw", 2);
  lfo2WaveformSelector.addItem("Sq", 3);
  lfo2WaveformSelector.addItem("S&H", 4);
  lfo2WaveformSelector.addItem("Sin", 5);
  lfo2WaveformSelector.setTooltip("LFO2 waveform shape");
  addAndMakeVisible(lfo2WaveformSelector);

  setupLfoSlider(lfo2RateSlider, lfo2RateLabel, "Rate", 0.1f, 10.0f, 3.0f,
                 juce::Slider::LinearHorizontal, juce::Slider::TextBoxBelow);
  lfo2RateSlider->setTextValueSuffix(" Hz");
  lfo2RateSlider->setTooltip("LFO2 rate in Hz (0.1 - 10 Hz)");
  setupLfoSlider(lfo2DepthFilterSlider, lfo2DepthFilterLabel, "Flt", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  lfo2DepthFilterSlider->setTooltip("LFO2 filter cutoff modulation depth");
  setupLfoSlider(lfo2DepthPWSlider, lfo2DepthPWLabel, "PW", 0.0f, 1.0f, 0.0f,
                 juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  lfo2DepthPWSlider->setTooltip("LFO2 pulse width modulation depth");
  setupLfoSlider(lfo2DepthPitchSlider, lfo2DepthPitchLabel, "Vib", 0.0f, 1.0f,
                 0.0f, juce::Slider::RotaryHorizontalVerticalDrag,
                 juce::Slider::NoTextBox);
  lfo2DepthPitchSlider->setTooltip("LFO2 vibrato (pitch modulation) depth");

  // LFO2 accent colour: orange
  lfo2DepthFilterSlider->setColour(juce::Slider::trackColourId,
                                   juce::Colours::orange);
  lfo2DepthPWSlider->setColour(juce::Slider::trackColourId,
                               juce::Colours::orange);
  lfo2DepthPitchSlider->setColour(juce::Slider::trackColourId,
                                  juce::Colours::orange);

  // ========== PITCH BEND RANGE ==========
  pitchBendRangeLabel.setText("PB Range", juce::dontSendNotification);
  pitchBendRangeLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
  pitchBendRangeLabel.setFont(panelProFont.withHeight(11.0f));
  addAndMakeVisible(pitchBendRangeLabel);

  pitchBendRangeSelector.addItem("+/- 2", 1);
  pitchBendRangeSelector.addItem("+/- 3", 2);
  pitchBendRangeSelector.addItem("+/- 5", 3);
  pitchBendRangeSelector.addItem("+/- 7", 4);
  pitchBendRangeSelector.addItem("+/- 12", 5);
  pitchBendRangeSelector.setTooltip("Pitch bend range in semitones");
  addAndMakeVisible(pitchBendRangeSelector);

  // Sync PB ComboBox with APVTS
  auto *pbParam = processor.apvts.getParameter("pitchBendRange");
  if (pbParam) {
    int pbVal = static_cast<int>(pbParam->convertFrom0to1(pbParam->getValue()));
    int selId = 1;
    if (pbVal == 3)
      selId = 2;
    else if (pbVal == 5)
      selId = 3;
    else if (pbVal == 7)
      selId = 4;
    else if (pbVal >= 12)
      selId = 5;
    pitchBendRangeSelector.setSelectedId(selId, juce::dontSendNotification);
  }
  pitchBendRangeSelector.onChange = [this]() {
    static constexpr int vals[] = {2, 3, 5, 7, 12};
    int idx = pitchBendRangeSelector.getSelectedId() - 1;
    if (idx >= 0 && idx < 5) {
      auto *p = processor.apvts.getParameter("pitchBendRange");
      if (p)
        p->setValueNotifyingHost(
            p->convertTo0to1(static_cast<float>(vals[idx])));
    }
  };

  // ========== MOD MATRIX SLOTS ==========
  for (int i = 0; i < BreadbinProcessor::kModSlots; ++i) {
    auto &s = slots[i];
    auto prefix = "mod" + juce::String(i) + "_";

    s.slotLabel.setText("S" + juce::String(i + 1), juce::dontSendNotification);
    s.slotLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    s.slotLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(s.slotLabel);

    s.enableButton.setButtonText("On");
    s.enableButton.setColour(juce::ToggleButton::textColourId,
                             juce::Colours::lightgrey);
    s.enableButton.setColour(juce::ToggleButton::tickColourId,
                             juce::Colours::greenyellow);
    s.enableButton.setTooltip("Enable or bypass this modulation slot");
    addAndMakeVisible(s.enableButton);

    s.srcBox.addItem("None", 1);
    s.srcBox.addItem("LFO1", 2);
    s.srcBox.addItem("LFO2", 3);
    s.srcBox.addItem("FiltEnv", 4);
    s.srcBox.addItem("ModWheel", 5);
    s.srcBox.addItem("Velocity", 6);
    s.srcBox.setTooltip("Modulation source");
    addAndMakeVisible(s.srcBox);

    s.dstBox.addItem("None", 1);
    s.dstBox.addItem("Filter", 2);
    s.dstBox.addItem("PW", 3);
    s.dstBox.addItem("Pitch", 4);
    s.dstBox.addItem("Resonance", 5);
    s.dstBox.setTooltip("Modulation destination");
    addAndMakeVisible(s.dstBox);

    s.amtSlider.setRange(-1.0, 1.0, 0.01);
    s.amtSlider.setValue(0.0);
    s.amtSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    s.amtSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    s.amtSlider.setColour(juce::Slider::textBoxTextColourId,
                          juce::Colours::cyan);
    s.amtSlider.setColour(juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);
    s.amtSlider.setTooltip("Modulation amount (-1 to +1)");
    addAndMakeVisible(s.amtSlider);

    s.sourceValueLabel.setColour(juce::Label::textColourId,
                                 juce::Colours::cyan);
    s.sourceValueLabel.setFont(panelMonoFont.withHeight(10.0f));
    s.sourceValueLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.sourceValueLabel);

    s.contributionLabel.setColour(juce::Label::textColourId,
                                  juce::Colours::orange);
    s.contributionLabel.setFont(panelMonoFont.withHeight(10.0f));
    s.contributionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(s.contributionLabel);

    s.enableAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.apvts, prefix + "enable", s.enableButton);
    s.srcAttach = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, prefix + "src", s.srcBox);
    s.dstAttach = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, prefix + "dst", s.dstBox);
    s.amtAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, prefix + "amt", s.amtSlider);
  }

  auto setupTotalLabel = [this](juce::Label &label, juce::Colour colour) {
    label.setColour(juce::Label::textColourId, colour);
    label.setFont(panelMonoFont.withHeight(10.0f));
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
  };
  setupTotalLabel(totalFilterLabel, juce::Colours::cyan);
  setupTotalLabel(totalPWLabel, juce::Colours::orange);
  setupTotalLabel(totalPitchLabel, juce::Colours::greenyellow);
  setupTotalLabel(totalResLabel, juce::Colours::white);

  // ========== PWM SWEEP SETUP ==========
  pwmSweepEnableButton.setColour(juce::ToggleButton::tickColourId,
                                 juce::Colours::greenyellow);
  pwmSweepEnableButton.setTooltip("Enable dedicated PWM sweep oscillator");
  addAndMakeVisible(pwmSweepEnableButton);

  pwmSweepRateSlider.setTooltip("PWM sweep speed in Hz");
  pwmSweepRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pwmSweepRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
  pwmSweepRateSlider.setRange(0.05, 10.0, 0.01);
  pwmSweepRateSlider.setColour(juce::Slider::textBoxTextColourId,
                               juce::Colours::greenyellow);
  pwmSweepRateSlider.setColour(juce::Slider::textBoxOutlineColourId,
                               juce::Colours::transparentBlack);
  addAndMakeVisible(pwmSweepRateSlider);

  pwmSweepRateLabel.setText("Rate", juce::dontSendNotification);
  pwmSweepRateLabel.setColour(juce::Label::textColourId,
                              juce::Colours::greenyellow);
  pwmSweepRateLabel.setFont(panelProFont.withHeight(10.0f));
  addAndMakeVisible(pwmSweepRateLabel);

  pwmSweepDepthSlider.setTooltip("PWM sweep depth (0 = none, 1 = full range)");
  pwmSweepDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pwmSweepDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40,
                                      18);
  pwmSweepDepthSlider.setRange(0.0, 1.0, 0.01);
  pwmSweepDepthSlider.setColour(juce::Slider::textBoxTextColourId,
                                juce::Colours::greenyellow);
  pwmSweepDepthSlider.setColour(juce::Slider::textBoxOutlineColourId,
                                juce::Colours::transparentBlack);
  addAndMakeVisible(pwmSweepDepthSlider);

  pwmSweepDepthLabel.setText("Depth", juce::dontSendNotification);
  pwmSweepDepthLabel.setColour(juce::Label::textColourId,
                               juce::Colours::greenyellow);
  pwmSweepDepthLabel.setFont(panelProFont.withHeight(10.0f));
  addAndMakeVisible(pwmSweepDepthLabel);

  // ========== APVTS ATTACHMENTS (LFO1/LFO2) ==========
  lfoEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "lfoEnable", lfoEnableButton);
  lfoWaveAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "lfoWave", lfoWaveformSelector);
  lfoRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoRate", *lfoRateSlider);
  lfoDepthFiltAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoDepthFilt", *lfoDepthFilterSlider);
  lfoDepthPWAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoDepthPW", *lfoDepthPWSlider);
  lfoDepthPitchAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfoDepthPitch", *lfoDepthPitchSlider);

  lfo2EnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "lfo2Enable", lfo2EnableButton);
  lfo2WaveAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, "lfo2Wave", lfo2WaveformSelector);
  lfo2RateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2Rate", *lfo2RateSlider);
  lfo2DepthFiltAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2DepthFilt", *lfo2DepthFilterSlider);
  lfo2DepthPWAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2DepthPW", *lfo2DepthPWSlider);
  lfo2DepthPitchAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "lfo2DepthPitch", *lfo2DepthPitchSlider);

  // LFO1 Sync controls
  {
    int id = 1;
    for (const char *s : {"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/4D","1/8D","1/4T","1/8T"})
      lfoSyncDivCombo.addItem(s, id++);
  }
  lfoSyncDivCombo.setTooltip("LFO sync note division (relative to quarter note)");
  lfoSyncDivCombo.setVisible(false);
  addAndMakeVisible(lfoSyncDivCombo);
  lfoSyncModeBtn.setButtonText("Free");
  lfoSyncModeBtn.setClickingTogglesState(true);
  lfoSyncModeBtn.setColour(juce::TextButton::buttonOnColourId,
                            juce::Colours::cyan.darker(0.3f));
  lfoSyncModeBtn.setTooltip("LFO rate mode: Free = manual Hz, Sync = lock to DAW tempo");
  addAndMakeVisible(lfoSyncModeBtn);

  // LFO2 Sync controls
  {
    int id = 1;
    for (const char *s : {"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/4D","1/8D","1/4T","1/8T"})
      lfo2SyncDivCombo.addItem(s, id++);
  }
  lfo2SyncDivCombo.setTooltip("LFO2 sync note division (relative to quarter note)");
  lfo2SyncDivCombo.setVisible(false);
  addAndMakeVisible(lfo2SyncDivCombo);
  lfo2SyncModeBtn.setButtonText("Free");
  lfo2SyncModeBtn.setClickingTogglesState(true);
  lfo2SyncModeBtn.setColour(juce::TextButton::buttonOnColourId,
                             juce::Colours::orange.darker(0.3f));
  lfo2SyncModeBtn.setTooltip("LFO2 rate mode: Free = manual Hz, Sync = lock to DAW tempo");
  addAndMakeVisible(lfo2SyncModeBtn);

  using APVTS = juce::AudioProcessorValueTreeState;
  lfoSyncAttach     = std::make_unique<APVTS::ButtonAttachment>(
      processor.apvts, "lfoSync", lfoSyncModeBtn);
  lfoSyncDivAttach  = std::make_unique<APVTS::ComboBoxAttachment>(
      processor.apvts, "lfoSyncDiv", lfoSyncDivCombo);
  lfo2SyncAttach    = std::make_unique<APVTS::ButtonAttachment>(
      processor.apvts, "lfo2Sync", lfo2SyncModeBtn);
  lfo2SyncDivAttach = std::make_unique<APVTS::ComboBoxAttachment>(
      processor.apvts, "lfo2SyncDiv", lfo2SyncDivCombo);

  // PWM Sweep attachments
  pwmSweepEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "pwmSweepEnable", pwmSweepEnableButton);
  pwmSweepRateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "pwmSweepRate", pwmSweepRateSlider);
  pwmSweepDepthAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "pwmSweepDepth", pwmSweepDepthSlider);

  addAndMakeVisible(lfoDisplay1);
  addAndMakeVisible(lfoDisplay2);

  // ========== PER-ROLE ACCENT COLOURS ==========
  // LFO1 = cyan, LFO2 = orange, PWM = green, pitch-bend = cyan, mod slots = mag.
  // accentOf() reads this "accent" property; renderers honour it (per-section colours).
  {
    const int cy = (int)gm::ui::theme::cyan.getARGB(),
              orr = (int)gm::ui::theme::orange.getARGB(),
              gr = (int)gm::ui::theme::grn.getARGB(),
              mg = (int)gm::ui::theme::mag.getARGB();
    auto acc = [](juce::Component &c, int a) {
      c.getProperties().set("accent", a);
    };
    // LFO1 = CYAN
    acc(lfoEnableButton, cy);
    acc(lfoWaveformSelector, cy);
    if (lfoRateSlider) acc(*lfoRateSlider, cy);
    if (lfoDepthFilterSlider) acc(*lfoDepthFilterSlider, cy);
    if (lfoDepthPWSlider) acc(*lfoDepthPWSlider, cy);
    if (lfoDepthPitchSlider) acc(*lfoDepthPitchSlider, cy);
    acc(lfoSyncModeBtn, cy);
    acc(lfoSyncDivCombo, cy);
    // LFO2 = ORANGE
    acc(lfo2EnableButton, orr);
    acc(lfo2WaveformSelector, orr);
    if (lfo2RateSlider) acc(*lfo2RateSlider, orr);
    if (lfo2DepthFilterSlider) acc(*lfo2DepthFilterSlider, orr);
    if (lfo2DepthPWSlider) acc(*lfo2DepthPWSlider, orr);
    if (lfo2DepthPitchSlider) acc(*lfo2DepthPitchSlider, orr);
    acc(lfo2SyncModeBtn, orr);
    acc(lfo2SyncDivCombo, orr);
    // PWM Sweep = GREEN
    acc(pwmSweepEnableButton, gr);
    acc(pwmSweepRateSlider, gr);
    acc(pwmSweepDepthSlider, gr);
    // Pitch-bend range = CYAN
    acc(pitchBendRangeSelector, cy);
    // Mod-matrix slots = MAGENTA
    for (auto &s : slots) {
      acc(s.enableButton, mg);
      acc(s.srcBox, mg);
      acc(s.dstBox, mg);
      acc(s.amtSlider, mg);
    }
  }

  startTimerHz(30);
  setSize(panelWidth, panelHeight);
}

void ModMatrixPanel::paint(juce::Graphics &g) {
  drawPopupGlass(g, getLocalBounds().toFloat(),
                 BreadbinLookAndFeel::accentOf(*this), gridCache, scanCache);

  // Translucent recessed section grounds (let the grid show faintly through)
  auto drawSectionBg = [&](int y, int h) {
    g.setColour(juce::Colour(0x99101018));
    g.fillRoundedRectangle(2.0f, static_cast<float>(y),
                           static_cast<float>(panelWidth - 4),
                           static_cast<float>(h), 5.0f);
  };
  drawSectionBg(0, 54);    // LFO1
  drawSectionBg(55, 54);   // LFO2
  drawSectionBg(112, 56);  // PWM Sweep + pitch-bend range
  drawSectionBg(172, 204); // Mod matrix

  // Section glow pills (theme tokens for consistent neon)
  auto drawGlowLabel = [&](const juce::String &text, int x, int y,
                           juce::Colour colour) {
    int pillW = text.length() * 7 + 10;
    g.setColour(colour.withAlpha(0.15f));
    g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(pillW), 14.0f, 3.0f);
    g.setColour(colour);
    g.setFont(panelProFont.withHeight(11.0f));
    g.drawText(text, x + 4, y, pillW, 14, juce::Justification::centredLeft);
  };
  drawGlowLabel("LFO 1", 4, 2, gm::ui::theme::cyan);
  drawGlowLabel("LFO 2", 4, 57, gm::ui::theme::orange);
  drawGlowLabel("PWM SWEEP", 4, 114, gm::ui::theme::grn);

  // Mod matrix column headers (magenta — the section accent)
  g.setColour(gm::ui::theme::mag.withAlpha(0.85f));
  g.setFont(panelProFont.withHeight(9.0f));
  g.drawText("ON", 30, 178, 40, 12, juce::Justification::centred);
  g.drawText("SOURCE", 70, 178, 90, 12, juce::Justification::centred);
  g.drawText("DEST", 170, 178, 90, 12, juce::Justification::centred);
  g.drawText("AMOUNT", 270, 178, 120, 12, juce::Justification::centred);
  g.drawText("VAL", 400, 178, 45, 12, juce::Justification::centred);
  g.drawText("OUT", 447, 178, 45, 12, juce::Justification::centred);

  // Destination totals
  g.setColour(gm::ui::theme::mag.withAlpha(0.5f));
  g.drawHorizontalLine(342, 8.0f, static_cast<float>(panelWidth - 8));
  g.setColour(juce::Colour(130, 130, 145));
  g.setFont(panelProFont.withHeight(10.0f));
  g.drawText("Destination Totals", 8, 346, 160, 14,
             juce::Justification::centredLeft);
}

void ModMatrixPanel::resized() {
  // LFO row layout helper
  auto layoutLfoRow = [this](int y, juce::ToggleButton &enableBtn,
                             juce::ComboBox &waveBox, MappableSlider &rateSldr,
                             juce::Label &rateLbl, juce::TextButton &syncBtn,
                             juce::ComboBox &syncDivCombo,
                             MappableSlider &fltSldr,
                             juce::Label &fltLbl, MappableSlider &pwSldr,
                             juce::Label &pwLbl, MappableSlider &vibSldr,
                             juce::Label &vibLbl) {
    int x = 4;
    enableBtn.setBounds(x, y + 14, 50, 20);
    x += 54;
    waveBox.setBounds(x, y + 14, 68, 20);
    x += 76;
    rateLbl.setBounds(x, y + 2, 40, 12);
    rateSldr.setBounds(x, y + 14, 80, 34); // h=34: 20px track + 14px text box
    syncDivCombo.setBounds(x, y + 14, 80, 20);
    x += 88;
    const int dW = 42;
    fltLbl.setBounds(x, y + 2, dW, 12);
    fltSldr.setBounds(x, y + 14, dW, 36);
    x += dW + 4;
    pwLbl.setBounds(x, y + 2, dW, 12);
    pwSldr.setBounds(x, y + 14, dW, 36);
    x += dW + 4;
    vibLbl.setBounds(x, y + 2, dW, 12);
    vibSldr.setBounds(x, y + 14, dW, 36);
    x += dW + 4;
    syncBtn.setBounds(x, y + 2, 28, 50); // full-height toggle, right of depth sliders
  };

  // LFO1 row: y=0..54
  layoutLfoRow(0, lfoEnableButton, lfoWaveformSelector, *lfoRateSlider,
               lfoRateLabel, lfoSyncModeBtn, lfoSyncDivCombo,
               *lfoDepthFilterSlider, lfoDepthFilterLabel,
               *lfoDepthPWSlider, lfoDepthPWLabel, *lfoDepthPitchSlider,
               lfoDepthPitchLabel);
  lfoDisplay1.setBounds(394, 14, 120, 36);

  // LFO2 row: y=55..109
  layoutLfoRow(55, lfo2EnableButton, lfo2WaveformSelector, *lfo2RateSlider,
               lfo2RateLabel, lfo2SyncModeBtn, lfo2SyncDivCombo,
               *lfo2DepthFilterSlider, lfo2DepthFilterLabel,
               *lfo2DepthPWSlider, lfo2DepthPWLabel, *lfo2DepthPitchSlider,
               lfo2DepthPitchLabel);
  lfoDisplay2.setBounds(394, 69, 120, 36);

  // PWM Sweep row: y=112..139
  {
    int x = 4;
    pwmSweepEnableButton.setBounds(x, 126, 60, 20);
    x += 64;
    pwmSweepRateLabel.setBounds(x, 114, 35, 12);
    pwmSweepRateSlider.setBounds(x, 126, 170, 20);
    x += 178;
    pwmSweepDepthLabel.setBounds(x, 114, 40, 12);
    pwmSweepDepthSlider.setBounds(x, 126, 170, 20);
  }

  // PB Range row: y=144..166
  pitchBendRangeLabel.setBounds(4, 150, 60, 18);
  pitchBendRangeSelector.setBounds(66, 150, 90, 20);

  // Mod matrix slots: y=194 onward (header at 176)
  const int mmTop = 194;
  const int rowH = 35;
  for (int i = 0; i < BreadbinProcessor::kModSlots; ++i) {
    auto &s = slots[i];
    int y = mmTop + i * rowH;

    s.slotLabel.setBounds(4, y + 8, 24, 18);
    s.enableButton.setBounds(30, y + 7, 38, 20);
    s.srcBox.setBounds(70, y + 6, 90, 22);
    s.dstBox.setBounds(170, y + 6, 90, 22);
    s.amtSlider.setBounds(270, y + 6, 120, 22);
    s.sourceValueLabel.setBounds(400, y + 6, 45, 22);
    s.contributionLabel.setBounds(447, y + 6, 45, 22);
  }

  totalFilterLabel.setBounds(8, 360, 120, 16);
  totalPWLabel.setBounds(132, 360, 120, 16);
  totalPitchLabel.setBounds(256, 360, 120, 16);
  totalResLabel.setBounds(380, 360, 132, 16);

  buildPopupCaches(*this, gridCache, scanCache);
}

void ModMatrixPanel::refreshFonts(const juce::Font &pro, const juce::Font &bold,
                                  const juce::Font &mono) {
  panelProFont  = pro;
  panelBoldFont = bold;
  panelMonoFont = mono;

  // Numeric value labels → JetBrains Mono
  for (auto &s : slots) {
    s.sourceValueLabel.setFont(mono.withHeight(10.0f));
    s.contributionLabel.setFont(mono.withHeight(10.0f));
  }
  totalFilterLabel.setFont(mono.withHeight(10.0f));
  totalPWLabel.setFont(mono.withHeight(10.0f));
  totalPitchLabel.setFont(mono.withHeight(10.0f));
  totalResLabel.setFont(mono.withHeight(10.0f));

  // Control labels → Lato Regular
  lfoRateLabel.setFont(pro.withHeight(9.0f));
  lfoDepthFilterLabel.setFont(pro.withHeight(9.0f));
  lfoDepthPWLabel.setFont(pro.withHeight(9.0f));
  lfoDepthPitchLabel.setFont(pro.withHeight(9.0f));
  lfo2RateLabel.setFont(pro.withHeight(9.0f));
  lfo2DepthFilterLabel.setFont(pro.withHeight(9.0f));
  lfo2DepthPWLabel.setFont(pro.withHeight(9.0f));
  lfo2DepthPitchLabel.setFont(pro.withHeight(9.0f));
  pitchBendRangeLabel.setFont(pro.withHeight(11.0f));
  pwmSweepRateLabel.setFont(pro.withHeight(10.0f));
  pwmSweepDepthLabel.setFont(pro.withHeight(10.0f));

  // LFO rate sliders: initial text box used h=16 (default LAF).
  // Pass h=14 so JUCE detects a change and recreates via createSliderTextBox
  // with BreadbinLookAndFeel active → Lato 11px font.
  // Slider is h=34 total: 20px track + 14px text box.
  lfoRateSlider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 14);
  lfo2RateSlider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 14);

  repaint();
}

void ModMatrixPanel::timerCallback() {
  for (int i = 0; i < BreadbinProcessor::kModSlots; ++i) {
    bool rowEnabled = slots[i].enableButton.getToggleState();
    float alpha = rowEnabled ? 1.0f : 0.35f;
    slots[i].srcBox.setAlpha(alpha);
    slots[i].dstBox.setAlpha(alpha);
    slots[i].amtSlider.setAlpha(alpha);
    slots[i].sourceValueLabel.setAlpha(alpha);
    slots[i].contributionLabel.setAlpha(alpha);

    float srcVal = processor.getModSlotSourceValue(i);
    float contrib = processor.getModSlotContribution(i);
    slots[i].sourceValueLabel.setText(juce::String(srcVal, 2),
                                      juce::dontSendNotification);
    slots[i].contributionLabel.setText(juce::String(contrib, 2),
                                       juce::dontSendNotification);
  }

  totalFilterLabel.setText(
      "Filter " + juce::String(processor.getModTotalFilterCutoff(), 2),
      juce::dontSendNotification);
  totalPWLabel.setText("PW " +
                           juce::String(processor.getModTotalPulseWidth(), 2),
                       juce::dontSendNotification);
  totalPitchLabel.setText("Pitch " +
                              juce::String(processor.getModTotalPitch(), 2),
                          juce::dontSendNotification);
  totalResLabel.setText("Res " +
                            juce::String(processor.getModTotalResonance(), 2),
                        juce::dontSendNotification);

  lfoDisplay1.setWaveType(lfoWaveformSelector.getSelectedId());
  lfoDisplay1.setPhase(static_cast<float>(processor.getLFO().phase));
  lfoDisplay2.setWaveType(lfo2WaveformSelector.getSelectedId());
  lfoDisplay2.setPhase(static_cast<float>(processor.getLFO2().phase));

  bool s1 = processor.isLfoSynced();
  lfoSyncModeBtn.setButtonText(s1 ? "Sync" : "Free");
  lfoRateSlider->setVisible(!s1);
  lfoSyncDivCombo.setVisible(s1);

  bool s2 = processor.isLfo2Synced();
  lfo2SyncModeBtn.setButtonText(s2 ? "Sync" : "Free");
  lfo2RateSlider->setVisible(!s2);
  lfo2SyncDivCombo.setVisible(s2);
}

// ========== CHORD MEMORY PANEL ==========

ChordMemoryPanel::ChordMemoryPanel(BreadbinProcessor &proc) : processor(proc) {
  // Enable toggle
  enableButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  enableButton.setTooltip(
      "Enable chord memory (mutually exclusive with arpeggiator)");
  addAndMakeVisible(enableButton);
  enableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "chordEnable", enableButton);
  enableButton.onClick = [this]() {
    if (!enableButton.getToggleState())
      return;
    if (auto *arpParam = processor.apvts.getParameter("arpEnable"))
      arpParam->setValueNotifyingHost(0.0f);
  };

  // 4 slot radio buttons
  int currentSlot = static_cast<int>(
      processor.apvts.getRawParameterValue("chordSlot")->load());
  for (int s = 0; s < 4; ++s) {
    slotButtons[s].setButtonText("Slot " + juce::String(s + 1));
    slotButtons[s].setColour(juce::TextButton::buttonColourId,
                             s == currentSlot
                                 ? gm::ui::theme::mag.withAlpha(0.35f)
                                 : juce::Colour(50, 50, 60));
    slotButtons[s].setColour(juce::TextButton::textColourOnId,
                             gm::ui::theme::mag);
    slotButtons[s].setColour(juce::TextButton::textColourOffId,
                             juce::Colours::lightgrey);
    slotButtons[s].setTooltip("Select chord slot " + juce::String(s + 1));
    slotButtons[s].onClick = [this, s]() {
      if (auto *param = processor.apvts.getParameter("chordSlot"))
        param->setValueNotifyingHost(
            param->convertTo0to1(static_cast<float>(s)));
      for (int j = 0; j < 4; ++j)
        slotButtons[j].setColour(juce::TextButton::buttonColourId,
                                 j == s ? gm::ui::theme::mag.withAlpha(0.35f)
                                        : juce::Colour(50, 50, 60));
    };
    addAndMakeVisible(slotButtons[s]);

    // Learn button per slot
    learnButtons[s].setButtonText("Learn");
    learnButtons[s].setColour(juce::TextButton::buttonColourId,
                              juce::Colour(50, 50, 60));
    learnButtons[s].setColour(juce::TextButton::textColourOnId,
                              juce::Colours::white);
    learnButtons[s].setColour(juce::TextButton::textColourOffId,
                              juce::Colours::lightgrey);
    learnButtons[s].setTooltip("Play a chord, then click to capture intervals");
    learnButtons[s].onClick = [this, s]() {
      if (processor.isChordLearning() && processor.getChordLearnSlot() == s) {
        // Finish learning — apply captured notes
        auto notes = processor.getChordLearnNotes();
        processor.stopChordLearn();
        if (notes.size() >= 2)
          applyLearnedChord(s, notes);
      } else {
        // Start learning for this slot
        processor.startChordLearn(s);
      }
    };
    addAndMakeVisible(learnButtons[s]);
  }

  // 4 rows x 5 interval sliders
  for (int s = 0; s < 4; ++s) {
    slots[s].label.setText("Slot " + juce::String(s + 1),
                           juce::dontSendNotification);
    slots[s].label.setColour(juce::Label::textColourId, juce::Colours::cyan);
    slots[s].label.setFont(panelProFont.withHeight(11.0f));
    addAndMakeVisible(slots[s].label);

    for (int i = 0; i < 5; ++i) {
      auto &slider = slots[s].sliders[i];
      slider.setSliderStyle(juce::Slider::LinearVertical);
      slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 36, 14);
      slider.setRange(-24, 24, 1);
      slider.setColour(juce::Slider::textBoxTextColourId, gm::ui::theme::mag);
      slider.setColour(juce::Slider::textBoxOutlineColourId,
                       juce::Colours::transparentBlack);
      slider.setTextValueSuffix(" st");
      slider.setTooltip("Semitone offset from root (0 = unused)");
      addAndMakeVisible(slider);

      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      slots[s].attachments[i] = std::make_unique<
          juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,
                                                                id, slider);
    }
  }

  // Chord preset browser
  presetSelector.addItem("Major Triad", 1);
  presetSelector.addItem("Minor Triad", 2);
  presetSelector.addItem("7th Chord", 3);
  presetSelector.addItem("Sus4", 4);
  presetSelector.addItem("Power Chord", 5);
  presetSelector.addItem("Octaves", 6);
  presetSelector.setTooltip("Factory chord presets");
  presetSelector.setTextWhenNothingSelected("Presets...");
  presetSelector.onChange = [this]() {
    int idx = presetSelector.getSelectedId();
    if (idx > 0)
      applyChordPresetByIndex(idx);
  };
  addAndMakeVisible(presetSelector);

  presetPrevButton.setButtonText("<");
  presetPrevButton.setTooltip("Previous chord preset");
  presetPrevButton.onClick = [this]() {
    int id = presetSelector.getSelectedId();
    if (id <= 1)
      id = presetSelector.getNumItems() + 1;
    presetSelector.setSelectedId(id - 1);
  };
  addAndMakeVisible(presetPrevButton);

  presetNextButton.setButtonText(">");
  presetNextButton.setTooltip("Next chord preset");
  presetNextButton.onClick = [this]() {
    int id = presetSelector.getSelectedId();
    if (id <= 0 || id >= presetSelector.getNumItems())
      id = 0;
    presetSelector.setSelectedId(id + 1);
  };
  addAndMakeVisible(presetNextButton);
  // Chord preset save/load
  saveButton.setTooltip("Save chord memory to file");
  saveButton.onClick = [this]() { saveChordPreset(); };
  addAndMakeVisible(saveButton);
  loadButton.setTooltip("Load chord memory from file");
  loadButton.onClick = [this]() { loadChordPreset(); };
  addAndMakeVisible(loadButton);

  // ========== PER-ROLE ACCENT COLOURS ==========
  // All chord-memory controls = magenta. accentOf() reads "accent".
  {
    const int mg = (int)gm::ui::theme::mag.getARGB();
    auto acc = [](juce::Component &c, int a) {
      c.getProperties().set("accent", a);
    };
    acc(enableButton, mg);
    for (auto &b : slotButtons) acc(b, mg);
    for (auto &b : learnButtons) acc(b, mg);
    for (auto &s : slots)
      for (auto &sl : s.sliders) acc(sl, mg);
    acc(presetSelector, mg);
    acc(presetPrevButton, mg);
    acc(presetNextButton, mg);
    acc(saveButton, mg);
    acc(loadButton, mg);
  }

  startTimer(33);
  setSize(panelWidth, panelHeight);
}

void ChordMemoryPanel::applyLearnedChord(int slot,
                                         const std::vector<int> &notes) {
  // Sort notes ascending, root = lowest
  auto sorted = notes;
  std::sort(sorted.begin(), sorted.end());
  int root = sorted[0];

  // Compute intervals from root, write to APVTS
  for (int i = 0; i < 5; ++i) {
    int interval = 0;
    if (i + 1 < static_cast<int>(sorted.size()))
      interval = sorted[i + 1] - root;

    auto id = "chord_s" + juce::String(slot) + "_i" + juce::String(i);
    if (auto *param = processor.apvts.getParameter(id))
      param->setValueNotifyingHost(
          param->convertTo0to1(static_cast<float>(interval)));
  }
}

void ChordMemoryPanel::refreshFonts(const juce::Font &pro,
                                    const juce::Font &bold) {
  panelProFont = pro;
  panelBoldFont = bold;
  for (auto &s : slots)
    s.label.setFont(pro.withHeight(11.0f));
  repaint();
}

void ChordMemoryPanel::timerCallback() {
  bool learning = processor.isChordLearning();
  int learnSlot = processor.getChordLearnSlot();

  for (int s = 0; s < 4; ++s) {
    bool isLearning = learning && learnSlot == s;
    learnButtons[s].setColour(juce::TextButton::buttonColourId,
                              isLearning ? juce::Colours::gold.withAlpha(0.4f)
                                         : juce::Colour(50, 50, 60));
    if (isLearning) {
      auto notes = processor.getChordLearnNotes();
      learnButtons[s].setButtonText("Done (" + juce::String(notes.size()) +
                                    ")");
    } else {
      learnButtons[s].setButtonText("Learn");
    }
  }
}

void ChordMemoryPanel::paint(juce::Graphics &g) {
  drawPopupGlass(g, getLocalBounds().toFloat(),
                 BreadbinLookAndFeel::accentOf(*this), gridCache, scanCache);

  const juce::Colour acc = BreadbinLookAndFeel::accentOf(*this);

  // Hint line under the top controls
  g.setColour(juce::Colour(160, 160, 175));
  g.setFont(panelProFont.withHeight(11.0f));
  g.drawText("Trigger one key produces a full chord.  Play a chord then Learn, "
             "or set intervals manually.",
             10, 34, panelWidth - 20, 16, juce::Justification::centredLeft);

  // Recessed ground behind the interval table
  g.setColour(juce::Colour(0x99101018));
  g.fillRoundedRectangle(6.0f, 56.0f, static_cast<float>(panelWidth - 12),
                         static_cast<float>(panelHeight - 62), 5.0f);

  // Column headers aligned with the slider columns
  const int slidersX = 88, sliderW = 72;
  g.setColour(acc.withAlpha(0.8f));
  g.setFont(panelProFont.withHeight(9.0f));
  for (int i = 0; i < 5; ++i)
    g.drawText("Note " + juce::String(i + 2), slidersX + i * sliderW, 60,
               sliderW - 6, 12, juce::Justification::centred);
}

void ChordMemoryPanel::resized() {
  // Top row: Enable + preset stepper + Save/Load
  enableButton.setBounds(10, 8, 80, 24);
  presetPrevButton.setBounds(panelWidth - 278, 9, 20, 22);
  presetSelector.setBounds(panelWidth - 256, 9, 120, 22);
  presetNextButton.setBounds(panelWidth - 134, 9, 20, 22);
  saveButton.setBounds(panelWidth - 104, 9, 46, 22);
  loadButton.setBounds(panelWidth - 54, 9, 46, 22);

  // Interval table: 4 rows, each [Slot button | 5 interval sliders | Learn]
  const int slotW = 70, learnW = 54, slidersX = 88, sliderW = 72;
  const int tableTop = 74;
  const int rowH = (panelHeight - 12 - tableTop) / 4;
  for (int s = 0; s < 4; ++s) {
    int y = tableTop + s * rowH;
    int cy = y + rowH / 2;
    slots[s].label.setVisible(false); // redundant with the Slot button
    slotButtons[s].setBounds(10, cy - 12, slotW, 24);
    for (int i = 0; i < 5; ++i)
      slots[s].sliders[i].setBounds(slidersX + i * sliderW, y + 2, sliderW - 6,
                                    rowH - 8);
    learnButtons[s].setBounds(panelWidth - 10 - learnW, cy - 12, learnW, 24);
  }

  buildPopupCaches(*this, gridCache, scanCache);
}

// ========== WAVETABLE PANEL ==========

WavetablePanel::WavetablePanel(BreadbinProcessor &proc) : processor(proc) {
  // Enable toggle
  enableButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  enableButton.setTooltip("Enable wavetable sequencer");
  addAndMakeVisible(enableButton);
  enableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "wtEnable", enableButton);

  // Steps slider
  stepsLabel.setText("Steps", juce::dontSendNotification);
  stepsLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  stepsLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(stepsLabel);

  numStepsSlider.setRange(1.0, 16.0, 1.0);
  numStepsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  numStepsSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 14);
  numStepsSlider.setColour(juce::Slider::textBoxTextColourId,
                           juce::Colours::white);
  numStepsSlider.setTooltip("Number of active steps (1-16)");
  addAndMakeVisible(numStepsSlider);
  stepsAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "wtNumSteps", numStepsSlider);

  // Step rate slider
  rateLabel.setText("Step Rate", juce::dontSendNotification);
  rateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  rateLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(rateLabel);

  rateSlider.setRange(1.0, 200.0, 1.0);
  rateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  rateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 14);
  rateSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
  rateSlider.setTextValueSuffix(" Hz");
  rateSlider.setTooltip("How fast the sequencer advances (steps per second)");
  addAndMakeVisible(rateSlider);
  rateAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, "wtRate", rateSlider);

  // Loop toggle
  loopButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
  loopButton.setTooltip("Loop sequence or play once and stop");
  addAndMakeVisible(loopButton);
  loopAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "wtLoop", loopButton);

  // Per-step controls
  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    auto &step = steps[i];

    // Waveform ComboBox
    step.waveBox.addItem("Tri", 1);
    step.waveBox.addItem("Saw", 2);
    step.waveBox.addItem("Pls", 3);
    step.waveBox.addItem("Noi", 4);
    step.waveBox.setTooltip("Waveform: Triangle, Sawtooth, Pulse, or Noise");
    addAndMakeVisible(step.waveBox);
    step.waveBox.setVisible(false); // replaced by a click-to-cycle glyph box (paint + mouseDown)
    step.waveAttach = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, prefix + "wave", step.waveBox);

    // Pitch slider (semitones relative to note)
    step.pitchSlider.setRange(-24.0, 24.0, 1.0);
    step.pitchSlider.setSliderStyle(juce::Slider::LinearVertical);
    step.pitchSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 36, 14);
    step.pitchSlider.setColour(juce::Slider::textBoxTextColourId,
                               juce::Colours::white);
    step.pitchSlider.setColour(juce::Slider::thumbColourId,
                               juce::Colours::cyan);
    step.pitchSlider.setTextValueSuffix(" st");
    step.pitchSlider.setTooltip("Pitch offset in semitones (-24 to +24)");
    addAndMakeVisible(step.pitchSlider);
    step.pitchAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, prefix + "pitch", step.pitchSlider);

    // Pulse Width slider (SID register 0-4095, 50% = 2048)
    step.pwSlider.setRange(0.0, 4095.0, 1.0);
    step.pwSlider.setSliderStyle(juce::Slider::LinearVertical);
    step.pwSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 36, 14);
    step.pwSlider.setColour(juce::Slider::textBoxTextColourId,
                            juce::Colours::white);
    step.pwSlider.setColour(juce::Slider::thumbColourId, juce::Colours::orange);
    step.pwSlider.setTooltip("Pulse Width (0-4095, 2048 = 50% duty cycle)");
    addAndMakeVisible(step.pwSlider);
    step.pwAttach =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, prefix + "pw", step.pwSlider);
  }

  auto setupActionButton = [this](juce::TextButton &button,
                                  const juce::String &tooltip,
                                  std::function<void()> onClick) {
    button.setColour(juce::TextButton::buttonColourId,
                     juce::Colour(50, 50, 60));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
    button.setColour(juce::TextButton::textColourOffId,
                     juce::Colours::lightgrey);
    button.setTooltip(tooltip);
    button.onClick = std::move(onClick);
    addAndMakeVisible(button);
  };
  setupActionButton(shiftLeftButton,
                    "Rotate active steps left (step 1 takes step 2, etc.)",
                    [this]() { shiftActiveSteps(false); });
  setupActionButton(
      shiftRightButton,
      "Rotate active steps right (last active step wraps to step 1)",
      [this]() { shiftActiveSteps(true); });
  setupActionButton(
      randomizeButton,
      "Randomize waveform, pitch, and pulse width for active steps",
      [this]() { randomizeActiveSteps(); });
  setupActionButton(clearButton, "Reset active steps to Pulse / 0 st / PW 2048",
                    [this]() { clearActiveSteps(); });

  // Wavetable preset browser
  presetSelector.addItem("Classic Sweep", 1);
  presetSelector.addItem("Arp Up", 2);
  presetSelector.addItem("Pluck Sequence", 3);
  presetSelector.addItem("PWM Cycle", 4);
  presetSelector.addItem("Octave Bounce", 5);
  presetSelector.addItem("Waveform Morph", 6);
  presetSelector.addItem("Noise Rhythm", 7);
  presetSelector.addItem("Chip Drum", 8);
  presetSelector.setTooltip("Factory wavetable presets");
  presetSelector.setTextWhenNothingSelected("Presets...");
  presetSelector.onChange = [this]() {
    int idx = presetSelector.getSelectedId();
    if (idx > 0)
      applyWavetablePresetByIndex(idx);
  };
  addAndMakeVisible(presetSelector);

  presetPrevButton.setButtonText("<");
  presetPrevButton.setTooltip("Previous wavetable preset");
  presetPrevButton.onClick = [this]() {
    int id = presetSelector.getSelectedId();
    if (id <= 1)
      id = presetSelector.getNumItems() + 1;
    presetSelector.setSelectedId(id - 1);
  };
  addAndMakeVisible(presetPrevButton);

  presetNextButton.setButtonText(">");
  presetNextButton.setTooltip("Next wavetable preset");
  presetNextButton.onClick = [this]() {
    int id = presetSelector.getSelectedId();
    if (id <= 0 || id >= presetSelector.getNumItems())
      id = 0;
    presetSelector.setSelectedId(id + 1);
  };
  addAndMakeVisible(presetNextButton);
  // Wavetable preset save/load
  saveButton.setTooltip("Save wavetable to file");
  saveButton.onClick = [this]() { saveWavetablePreset(); };
  addAndMakeVisible(saveButton);
  loadButton.setTooltip("Load wavetable from file");
  loadButton.onClick = [this]() { loadWavetablePreset(); };
  addAndMakeVisible(loadButton);

  // ========== PER-ROLE ACCENT COLOURS ==========
  // Most controls = cyan; loop + per-step PW = green. accentOf() reads "accent".
  {
    const int cy = (int)gm::ui::theme::cyan.getARGB(),
              gr = (int)gm::ui::theme::grn.getARGB();
    auto acc = [](juce::Component &c, int a) {
      c.getProperties().set("accent", a);
    };
    // CYAN controls
    acc(enableButton, cy);
    acc(numStepsSlider, cy);
    acc(rateSlider, cy);
    acc(shiftLeftButton, cy);
    acc(shiftRightButton, cy);
    acc(randomizeButton, cy);
    acc(clearButton, cy);
    acc(presetSelector, cy);
    acc(presetPrevButton, cy);
    acc(presetNextButton, cy);
    acc(saveButton, cy);
    acc(loadButton, cy);
    // GREEN
    acc(loopButton, gr);
    // Per-step: wave + pitch = CYAN, pulse-width = GREEN
    for (auto &step : steps) {
      acc(step.waveBox, cy);
      acc(step.pitchSlider, cy);
      acc(step.pwSlider, gr);
    }
  }

  startTimer(33);
  setSize(panelWidth, panelHeight);
}

void WavetablePanel::paint(juce::Graphics &g) {
  drawPopupGlass(g, getLocalBounds().toFloat(),
                 BreadbinLookAndFeel::accentOf(*this), gridCache, scanCache);

  const juce::Colour acc = BreadbinLookAndFeel::accentOf(*this);

  // Left-margin row labels (aligned with the control rows)
  g.setColour(juce::Colour(150, 150, 165));
  g.setFont(panelProFont.withHeight(9.0f));
  g.drawText("WAVE", 4, 80, 48, 12, juce::Justification::centredRight);
  g.drawText("PITCH", 4, 156, 48, 12, juce::Justification::centredRight);
  g.drawText("PW", 4, 280, 48, 12, juce::Justification::centredRight);

  int numActiveSteps = static_cast<int>(numStepsSlider.getValue());
  auto &wt = processor.getWavetable();
  int currentStep = wt.enabled ? wt.currentStep : -1;

  const int leftMargin = 55;
  const int colW = 47;

  for (int i = 0; i < 16; ++i) {
    int x = leftMargin + i * colW;
    bool isActive = i < numActiveSteps;
    bool isCurrent = (i == currentStep) && wt.enabled;

    // Per-step card
    auto card = juce::Rectangle<float>(static_cast<float>(x - 1), 58.0f,
                                       static_cast<float>(colW - 2), 290.0f);
    g.setColour(isActive ? acc.withAlpha(isCurrent ? 0.22f : 0.10f)
                         : juce::Colour(0x40000000));
    g.fillRoundedRectangle(card, 5.0f);
    g.setColour(isCurrent ? acc
                          : (isActive ? acc.withAlpha(0.5f)
                                      : juce::Colour(0x30FFFFFF)));
    g.drawRoundedRectangle(card, 5.0f, isCurrent ? 1.6f : 1.0f);

    // Step number
    g.setFont(panelProFont.withHeight(10.0f));
    g.setColour(isCurrent ? acc
                          : (isActive ? juce::Colours::white
                                      : juce::Colour(90, 90, 100)));
    g.drawText(juce::String(i + 1).paddedLeft('0', 2), x, 62, colW - 3, 12,
               juce::Justification::centred);

    // Waveform glyph box (click to cycle Tri/Saw/Pulse/Noise)
    int wave = (int)processor.apvts
                   .getRawParameterValue("wt_s" + juce::String(i) + "_wave")
                   ->load();
    auto gbox = juce::Rectangle<float>((float)x, 76.0f, (float)(colW - 3), 18.0f);
    g.setColour(juce::Colour(0x66000000));
    g.fillRoundedRectangle(gbox, 3.0f);
    g.setColour(isActive ? acc.withAlpha(0.6f) : juce::Colour(0x33FFFFFF));
    g.drawRoundedRectangle(gbox, 3.0f, 1.0f);
    drawWaveGlyph(g, gbox, wave, isActive ? acc : juce::Colour(0xFF8A8A9A));
    // Wave-name label under the glyph
    static const char *wn[] = {"TRI", "SAW", "PLS", "NOI"};
    g.setColour(isActive ? acc.withAlpha(0.9f) : juce::Colour(0xFF6F6F82));
    g.setFont(panelProFont.withHeight(8.5f));
    g.drawText(wn[wave], x, 95, colW - 3, 11, juce::Justification::centred);
  }
}

void WavetablePanel::resized() {
  // Header row 1 (y=24..42): Enable + Preset browser + Save/Load
  enableButton.setBounds(10, 24, 65, 20);
  presetPrevButton.setBounds(78, 25, 20, 20);
  presetSelector.setBounds(100, 25, 120, 20);
  presetNextButton.setBounds(222, 25, 20, 20);
  saveButton.setBounds(250, 25, 46, 20);
  loadButton.setBounds(300, 25, 46, 20);

  // Header row 2 (y=22..56): Steps/Rate/Loop + utility buttons
  stepsLabel.setBounds(360, 24, 40, 18);
  numStepsSlider.setBounds(402, 22, 80, 34);
  rateLabel.setBounds(488, 24, 35, 18);
  rateSlider.setBounds(525, 22, 80, 34);
  loopButton.setBounds(610, 24, 55, 20);
  shiftLeftButton.setBounds(668, 24, 35, 20);
  shiftRightButton.setBounds(705, 24, 35, 20);
  randomizeButton.setBounds(743, 24, 40, 20);
  clearButton.setBounds(785, 24, 30, 20);

  // Per-step columns
  const int leftMargin = 55;
  const int colW = 47;
  const int ctrlW = 44;
  for (int i = 0; i < 16; ++i) {
    int x = leftMargin + i * colW;
    auto &step = steps[i];
    step.waveBox.setBounds(x, 76, ctrlW, 22);
    step.pitchSlider.setBounds(x, 110, ctrlW, 110);
    step.pwSlider.setBounds(x, 224, ctrlW, 120);
  }

  buildPopupCaches(*this, gridCache, scanCache);
}

void WavetablePanel::mouseDown(const juce::MouseEvent &e) {
  // Click a step's waveform glyph box to cycle Tri -> Saw -> Pulse -> Noise.
  const int leftMargin = 55, colW = 47;
  for (int i = 0; i < 16; ++i) {
    int x = leftMargin + i * colW;
    if (juce::Rectangle<int>(x, 76, colW - 3, 30).contains(e.getPosition())) {
      auto id = "wt_s" + juce::String(i) + "_wave";
      if (auto *p = processor.apvts.getParameter(id)) {
        int cur = (int)processor.apvts.getRawParameterValue(id)->load();
        p->setValueNotifyingHost(p->convertTo0to1((float)((cur + 1) % 4)));
      }
      repaint();
      return;
    }
  }
}

void WavetablePanel::refreshFonts(const juce::Font &pro,
                                  const juce::Font &bold) {
  panelProFont  = pro;
  panelBoldFont = bold;
  stepsLabel.setFont(pro.withHeight(10.0f));
  rateLabel.setFont(pro.withHeight(10.0f));
  repaint();
}

void WavetablePanel::timerCallback() {
  auto &wt = processor.getWavetable();
  int current = wt.enabled ? wt.currentStep : -1;
  if (current != lastHighlightedStep) {
    lastHighlightedStep = current;
    repaint();
  }

  // Dim inactive step columns
  int numActive = static_cast<int>(numStepsSlider.getValue());
  for (int i = 0; i < 16; ++i) {
    float alpha = (i < numActive) ? 1.0f : 0.3f;
    steps[i].waveBox.setAlpha(alpha);
    steps[i].pitchSlider.setAlpha(alpha);
    steps[i].pwSlider.setAlpha(alpha);
  }
}

void WavetablePanel::shiftActiveSteps(bool right) {
  const int numActive =
      juce::jlimit(1, 16, juce::roundToInt(numStepsSlider.getValue()));
  if (numActive <= 1)
    return;

  struct StepValues {
    int wave = 2;
    int pitch = 0;
    int pw = 2048;
  };
  std::array<StepValues, 16> before;

  auto readParamInt = [this](const juce::String &id, int fallback) {
    if (auto *param = processor.apvts.getParameter(id))
      return juce::roundToInt(param->convertFrom0to1(param->getValue()));
    return fallback;
  };
  auto writeParamInt = [this](const juce::String &id, int value) {
    if (auto *param = processor.apvts.getParameter(id))
      param->setValueNotifyingHost(
          param->convertTo0to1(static_cast<float>(value)));
  };

  for (int i = 0; i < numActive; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    before[i].wave = readParamInt(prefix + "wave", 2);
    before[i].pitch = readParamInt(prefix + "pitch", 0);
    before[i].pw = readParamInt(prefix + "pw", 2048);
  }

  for (int i = 0; i < numActive; ++i) {
    int src = right ? (i - 1 + numActive) % numActive : (i + 1) % numActive;
    auto prefix = "wt_s" + juce::String(i) + "_";
    writeParamInt(prefix + "wave", before[src].wave);
    writeParamInt(prefix + "pitch", before[src].pitch);
    writeParamInt(prefix + "pw", before[src].pw);
  }
}

void WavetablePanel::randomizeActiveSteps() {
  const int numActive =
      juce::jlimit(1, 16, juce::roundToInt(numStepsSlider.getValue()));
  juce::Random rng(static_cast<juce::int64>(juce::Time::currentTimeMillis()));

  auto writeParamInt = [this](const juce::String &id, int value) {
    if (auto *param = processor.apvts.getParameter(id))
      param->setValueNotifyingHost(
          param->convertTo0to1(static_cast<float>(value)));
  };

  for (int i = 0; i < numActive; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    writeParamInt(prefix + "wave", rng.nextInt(4));
    writeParamInt(prefix + "pitch", rng.nextInt(25) - 12);
    writeParamInt(prefix + "pw", 256 + rng.nextInt(3585));
  }
}

void WavetablePanel::clearActiveSteps() {
  const int numActive =
      juce::jlimit(1, 16, juce::roundToInt(numStepsSlider.getValue()));

  auto writeParamInt = [this](const juce::String &id, int value) {
    if (auto *param = processor.apvts.getParameter(id))
      param->setValueNotifyingHost(
          param->convertTo0to1(static_cast<float>(value)));
  };

  for (int i = 0; i < numActive; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    writeParamInt(prefix + "wave", 2);
    writeParamInt(prefix + "pitch", 0);
    writeParamInt(prefix + "pw", 2048);
  }
}

void BreadbinEditor::showChordMemoryPopup() {
  if (chordMemoryWindow != nullptr) {
    if (!chordMemoryWindow->isVisible()) {
      chordMemoryWindow.deleteAndZero();
    } else {
      chordMemoryWindow->toFront(true);
      return;
    }
  }

  auto *panel = new ChordMemoryPanel(processor);
  panel->setLookAndFeel(&customLookAndFeel);
  panel->getProperties().set("accent", (int)gm::ui::theme::mag.getARGB());
  panel->refreshFonts(proFont, boldFont);

  auto *window =
      new NonModalPopup("Chord Memory", juce::Colour(30, 30, 35), true);
  window->getProperties().set("accent", (int)gm::ui::theme::mag.getARGB());
  window->setContentOwned(panel, true);
  window->setUsingNativeTitleBar(false);
  window->setDropShadowEnabled(true);
  window->setResizable(false, false);
  window->setLookAndFeel(&customLookAndFeel);
  window->centreAroundComponent(this, window->getWidth(), window->getHeight());
  window->setVisible(true);
  window->addToDesktop();
  chordMemoryWindow = window;
}

void BreadbinEditor::showSidPlayerPopup() {
  if (sidPlayerWindow != nullptr) {
    if (!sidPlayerWindow->isVisible()) {
      sidPlayerWindow.deleteAndZero();
    } else {
      sidPlayerWindow->toFront(true);
      return;
    }
  }

  auto *panel = new SidPlayerPanel(processor);
  panel->setLookAndFeel(&customLookAndFeel);
  panel->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  panel->refreshFonts(monoFont);

  auto *window =
      new NonModalPopup("SID File Player", juce::Colour(30, 30, 35), true);
  window->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  window->setContentOwned(panel, true);
  window->setUsingNativeTitleBar(false);
  window->setDropShadowEnabled(true);
  window->setResizable(false, false);
  window->setLookAndFeel(&customLookAndFeel);
  window->centreAroundComponent(this, window->getWidth(), window->getHeight());
  window->setVisible(true);
  window->addToDesktop();
  sidPlayerWindow = window;
}

void BreadbinEditor::showModMatrixPopup() {
  if (modMatrixWindow != nullptr) {
    if (!modMatrixWindow->isVisible()) {
      modMatrixWindow.deleteAndZero();
    } else {
      modMatrixWindow->toFront(true);
      return;
    }
  }

  auto *panel = new ModMatrixPanel(processor);
  panel->setLookAndFeel(&customLookAndFeel);
  panel->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  panel->refreshFonts(proFont, boldFont, monoFont);

  auto *window =
      new NonModalPopup("Modulation", juce::Colour(30, 30, 35), true);
  window->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  window->setContentOwned(panel, true);
  window->setUsingNativeTitleBar(false);
  window->setDropShadowEnabled(true);
  window->setResizable(false, false);
  window->setLookAndFeel(&customLookAndFeel);
  window->centreAroundComponent(this, window->getWidth(), window->getHeight());
  window->setVisible(true);
  window->addToDesktop();
  modMatrixWindow = window;
}

void BreadbinEditor::showWavetablePopup() {
  if (wavetableWindow != nullptr) {
    if (!wavetableWindow->isVisible()) {
      wavetableWindow.deleteAndZero();
    } else {
      wavetableWindow->toFront(true);
      return;
    }
  }

  auto *panel = new WavetablePanel(processor);
  panel->setLookAndFeel(&customLookAndFeel);
  panel->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  panel->refreshFonts(proFont, boldFont);

  auto *window = new NonModalPopup("Wavetable Step Sequencer",
                                   juce::Colour(30, 30, 35), true);
  window->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  window->setContentOwned(panel, true);
  window->setUsingNativeTitleBar(false);
  window->setDropShadowEnabled(true);
  window->setResizable(false, false);
  window->setLookAndFeel(&customLookAndFeel);
  window->centreAroundComponent(this, window->getWidth(), window->getHeight());
  window->setVisible(true);
  window->addToDesktop();
  wavetableWindow = window;
}

void BreadbinEditor::showDigiPopup() {
  if (digiWindow != nullptr) {
    if (!digiWindow->isVisible()) {
      digiWindow.deleteAndZero();
    } else {
      digiWindow->toFront(true);
      return;
    }
  }

  auto *panel = new DigiSamplerPanel(processor);
  panel->setLookAndFeel(&customLookAndFeel);
  panel->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  panel->refreshFonts(proFont, boldFont, monoFont);

  auto *window =
      new NonModalPopup("Digi Sampler ($D418)", juce::Colour(30, 30, 35), true);
  window->getProperties().set("accent", (int)gm::ui::theme::cyan.getARGB());
  window->setContentOwned(panel, true);
  window->setUsingNativeTitleBar(false);
  window->setDropShadowEnabled(true);
  window->setResizable(false, false);
  window->setLookAndFeel(&customLookAndFeel);
  window->centreAroundComponent(this, window->getWidth(), window->getHeight());
  window->setVisible(true);
  window->addToDesktop();
  digiWindow = window;
}

void BreadbinEditor::setupControls() {
  setupGlobalControls();
  setupFXControls();
  setupPopupButtons();
}

void BreadbinEditor::setupGlobalControls() {
  // Mode
  modeLabel.setText("Mode:", juce::dontSendNotification);
  modeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(modeLabel);

  dualModeSelector.addItem("Stereo Split", 1);
  dualModeSelector.addItem("Unison", 2);
  // Multitimbral (ID 3) kept in engine but hidden from UI — redundant with
  // per-voice settings. Re-add here if needed for niche use.
  dualModeSelector.setSelectedId(1);
  dualModeSelector.setTooltip("Stereo: L/R SID split\nUnison: Both SIDs "
                              "together");
  dualModeSelector.onChange = [this]() {
    processor.setDualMode(static_cast<BreadbinProcessor::DualMode>(
        dualModeSelector.getSelectedId() - 1));
  };
  addAndMakeVisible(dualModeSelector);

  // Voice mode selector + max notes
  voiceModeSelector.addItem("Mono", 1);
  voiceModeSelector.addItem("Para", 2);
  voiceModeSelector.addItem("Poly", 3);
  voiceModeSelector.addItem("P+P", 4);
  voiceModeSelector.setSelectedId(1, juce::dontSendNotification);
  voiceModeSelector.setTooltip(
      "Mono: All voices same note\n"
      "Para: Each voice different note (shared filter)\n"
      "Poly: Each note gets own SID pair\n"
      "P+P: Poly voices with paraphonic sub-allocation");
  addAndMakeVisible(voiceModeSelector);
  voiceModeAttachment = std::make_unique<
      juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processor.apvts, "voiceMode", voiceModeSelector);

  for (int i = 1; i <= 8; ++i)
    polyMaxNotesSelector.addItem(juce::String(i), i);
  polyMaxNotesSelector.setSelectedId(4, juce::dontSendNotification);
  polyMaxNotesSelector.setTooltip("Maximum polyphonic voices (1-8)");
  addAndMakeVisible(polyMaxNotesSelector);
  polyMaxNotesAttachment = std::make_unique<
      juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
      processor.apvts, "polyMaxNotes", polyMaxNotesSelector);

  polyVoiceCountLabel.setText("0/6", juce::dontSendNotification);
  polyVoiceCountLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
  polyVoiceCountLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(polyVoiceCountLabel);

  // Paraphonic stacking controls
  paraSpreadSlider.setRange(0.0, 50.0, 0.1);
  paraSpreadSlider.setTextValueSuffix(" ct");
  paraSpreadSlider.setTooltip("Per-voice detune spread in cents (voice stacking thickness)");
  addAndMakeVisible(paraSpreadSlider);
  paraSpreadAttachment = std::make_unique<
      juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor.apvts, "paraSpread", paraSpreadSlider);
  paraSpreadLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  paraSpreadLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(paraSpreadLabel);

  paraRetrigButton.setTooltip("Multi-trigger: retrigger filter envelope on each new note");
  addAndMakeVisible(paraRetrigButton);
  paraRetrigAttachment = std::make_unique<
      juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor.apvts, "paraFilterRetrig", paraRetrigButton);

  // Global Factory Presets
  globalPresetLabel.setText("Patch:", juce::dontSendNotification);
  globalPresetLabel.setColour(juce::Label::textColourId,
                              juce::Colours::lightgrey);
  addAndMakeVisible(globalPresetLabel);

  // Factory + user presets are populated by refreshUserPresets()
  refreshUserPresets();

  // Initial ID set by constructor restore path or applyGlobalPreset
  globalPresetSelector.setTooltip("Factory presets - applies to entire plugin");
  globalPresetSelector.onChange = [this]() {
    int id = globalPresetSelector.getSelectedId();
    // Remap favorite sentinel IDs to real preset IDs
    constexpr int favMap[][2] = {
        {500, 1}, {501, 10}, {502, 38}, {503, 22}, {504, 25}};
    for (auto &f : favMap)
      if (id == f[0]) {
        id = f[1];
        break;
      }
    if (id >= 1000) {
      // User preset: load from file
      auto idx = static_cast<size_t>(id - 1000);
      if (idx < userPresetFiles.size() && userPresetFiles[idx].existsAsFile()) {
        auto xml = juce::XmlDocument::parse(userPresetFiles[idx]);
        if (xml != nullptr) {
          auto state = juce::ValueTree::fromXml(*xml);
          if (state.isValid()) {
            juce::MemoryBlock data;
            juce::MemoryOutputStream stream(data, false);
            state.writeToStream(stream);
            processor.setStateInformation(data.getData(),
                                          static_cast<int>(data.getSize()));
            loadVoiceToUI(selectedVoice);
            updateVoiceButtonStates();
            dualModeSelector.setSelectedId(
                static_cast<int>(processor.getDualMode()) + 1,
                juce::dontSendNotification);
            clockModeSelector.setSelectedId(
                static_cast<int>(processor.getClockMode()) + 1,
                juce::dontSendNotification);
            extInputEnableButton.setToggleState(processor.isExtInputEnabled(),
                                                juce::dontSendNotification);
            extInputLevelSlider.setValue(processor.getExtInputLevel(),
                                         juce::dontSendNotification);
            processor.snapshotPresetState();
          }
        }
      }
    } else if (id > 0) {
      applyGlobalPreset(id);
      processor.snapshotPresetState();
    }
    // Persist selection for editor close/reopen
    if (id > 0)
      processor.setGlobalPresetId(id);
  };
  addAndMakeVisible(globalPresetSelector);

  // CPU load display
  cpuLoadLabel.setText("CPU: 0%", juce::dontSendNotification);
  cpuLoadLabel.setFont(monoFont.withHeight(10.0f));
  cpuLoadLabel.setColour(juce::Label::textColourId, gm::ui::theme::grn);
  cpuLoadLabel.setJustificationType(juce::Justification::centredRight);
  cpuLoadLabel.setTooltip("DSP CPU usage (% of audio buffer time budget)");
  addAndMakeVisible(cpuLoadLabel);

  // Preset prev/next navigation
  presetPrevButton.setButtonText(juce::String::charToString(0x25C0));
  presetPrevButton.setTooltip("Previous preset");
  // Shared helper: build full navigation list (factory in menu order + user presets)
  auto buildNavIds = [this]() {
    // Factory presets in submenu display order (alphabetical within category)
    static const int factory[] = {
        // Leads
        20, 62, 56, 1, 46, 59, 5, 51, 15, 19, 47, 66,
        // Bass
        45, 32, 50, 22, 60, 21, 40,
        // Pads & Keys
        54, 67, 6, 38, 53, 68, 23, 2, 55, 42, 24, 64, 63, 48,
        // Arps & Sequences
        39, 3, 25, 13, 49, 41, 52, 61, 8, 9,
        // FX & Modulation
        57, 4, 14, 7, 69, 27, 65, 26, 58,
        // Showcase
        75, 76, 77, 70, 73, 72, 71, 74,
        // Classic C64
        10, 16, 37, 31, 43, 36, 33, 30, 28, 11, 44, 12, 29, 18, 35, 34, 17};
    std::vector<int> ids(std::begin(factory), std::end(factory));
    // Append user presets (already sorted alphabetically by refreshUserPresets)
    for (int i = 0; i < static_cast<int>(userPresetFiles.size()); ++i)
      ids.push_back(1000 + i);
    return ids;
  };

  presetPrevButton.onClick = [this, buildNavIds]() {
    auto ids = buildNavIds();
    int count = static_cast<int>(ids.size());
    if (count == 0)
      return;
    int cur = globalPresetSelector.getSelectedId();
    // Remap favorites to real IDs
    constexpr int favMap[][2] = {
        {500, 1}, {501, 10}, {502, 38}, {503, 22}, {504, 25}};
    for (auto &f : favMap)
      if (cur == f[0]) {
        cur = f[1];
        break;
      }
    int idx = 0;
    for (int i = 0; i < count; ++i)
      if (ids[i] == cur) {
        idx = i;
        break;
      }
    idx = (idx - 1 + count) % count;
    globalPresetSelector.setSelectedId(ids[idx]);
  };
  addAndMakeVisible(presetPrevButton);

  presetNextButton.setButtonText(juce::String::charToString(0x25B6));
  presetNextButton.setTooltip("Next preset");
  presetNextButton.onClick = [this, buildNavIds]() {
    auto ids = buildNavIds();
    int count = static_cast<int>(ids.size());
    if (count == 0)
      return;
    int cur = globalPresetSelector.getSelectedId();
    constexpr int favMap[][2] = {
        {500, 1}, {501, 10}, {502, 38}, {503, 22}, {504, 25}};
    for (auto &f : favMap)
      if (cur == f[0]) {
        cur = f[1];
        break;
      }
    int idx = 0;
    for (int i = 0; i < count; ++i)
      if (ids[i] == cur) {
        idx = i;
        break;
      }
    idx = (idx + 1) % count;
    globalPresetSelector.setSelectedId(ids[idx]);
  };
  addAndMakeVisible(presetNextButton);

  // Preset dirty indicator
  presetDirtyLabel.setTooltip("* indicates unsaved changes to the current preset");
  presetDirtyLabel.setColour(juce::Label::textColourId, juce::Colours::gold);
  presetDirtyLabel.setFont(boldFont.withHeight(16.0f));
  presetDirtyLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(presetDirtyLabel);

  // Voice Preset (for selected voice)
  presetLabel.setText("Voice:", juce::dontSendNotification);
  presetLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(presetLabel);

  presetSelector.addItem("-- Select --", 1);
  // Build categorized voice preset menu using PopupMenu submenus
  auto *vpRoot = presetSelector.getRootMenu();
  // -- Leads --
  {
    juce::PopupMenu sub;
    sub.addItem(2, "Classic Lead (Monty)");
    sub.addItem(7, "Thin Pulse");
    sub.addItem(8, "Wide Pulse");
    sub.addItem(11, "Saw Lead");
    sub.addItem(25, "Wide Lead");
    sub.addItem(21, "Commando Pluck");
    sub.addItem(30, "Delta Sustain");
    sub.addItem(31, "Brass Saw");
    vpRoot->addSubMenu("Leads", sub);
  }
  // -- Bass --
  {
    juce::PopupMenu sub;
    sub.addItem(3, "Fat Bass (Ocean)");
    sub.addItem(9, "Pluck Bass");
    sub.addItem(10, "Sub Bass");
    sub.addItem(22, "Punchy Saw");
    sub.addItem(28, "Punch Bass");
    sub.addItem(23, "Buzz Saw");
    sub.addItem(24, "Driving Saw");
    vpRoot->addSubMenu("Bass", sub);
  }
  // -- Pads & Keys --
  {
    juce::PopupMenu sub;
    sub.addItem(4, "PWM Pad (Hubbard)");
    sub.addItem(13, "Soft Pad");
    sub.addItem(14, "Bright Pad");
    sub.addItem(26, "Gentle Triangle");
    sub.addItem(37, "Ambient Swell");
    sub.addItem(32, "String Ensemble");
    sub.addItem(15, "Organ");
    sub.addItem(16, "Clavinet");
    sub.addItem(33, "Electric Piano");
    sub.addItem(34, "Harpsichord");
    vpRoot->addSubMenu("Pads & Keys", sub);
  }
  // -- Percussion --
  {
    juce::PopupMenu sub;
    sub.addItem(5, "Noise Snare");
    sub.addItem(17, "Hi-Hat");
    sub.addItem(18, "Kick Thump");
    sub.addItem(35, "Snare Roll");
    sub.addItem(36, "Tom");
    vpRoot->addSubMenu("Percussion", sub);
  }
  // -- FX & Utility --
  {
    juce::PopupMenu sub;
    sub.addItem(6, "Retro Triangle");
    sub.addItem(19, "White Noise");
    sub.addItem(20, "Zap");
    sub.addItem(29, "Bell Triangle");
    sub.addItem(27, "Short Pluck");
    sub.addItem(12, "Staccato Saw");
    sub.addItem(38, "Rising Noise");
    vpRoot->addSubMenu("FX & Utility", sub);
  }
  presetSelector.setSelectedId(1);
  presetSelector.setTooltip("Applies preset to currently selected voice");
  presetSelector.onChange = [this]() {
    if (presetSelector.getSelectedId() > 1) {
      applyPreset(presetSelector.getSelectedId());
      processor.snapshotPresetState();
    }
  };
  addAndMakeVisible(presetSelector);

  // Save/Load Patch buttons (full state) - Now Icon-based
  auto diskPath = makeDiskPath();
  auto folderPath = makeFolderPath();

  savePatchButton.setShape(diskPath, true, true, false);
  savePatchButton.setTooltip("Save all settings to a file or preset menu");
  savePatchButton.onClick = [this]() {
    juce::PopupMenu menu;
    menu.addItem(1, "Save to File...");
    menu.addItem(2, "Save to Preset Menu...");
    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int choice) {
      if (choice == 1)
        savePresetToFile();
      else if (choice == 2)
        savePresetToMenu();
    });
  };
  addAndMakeVisible(savePatchButton);

  loadPatchButton.setShape(folderPath, true, true, false);
  loadPatchButton.setTooltip("Load settings from a .breadbin patch file");
  loadPatchButton.onClick = [this]() { loadPresetFromFile(); };
  addAndMakeVisible(loadPatchButton);

  // Save/Load Voice buttons (selected oscillator)
  saveVoiceButton.setShape(diskPath, true, true, false);
  saveVoiceButton.setTooltip("Save this voice's settings to a .voice file");
  saveVoiceButton.onClick = [this]() { saveVoicePresetToFile(); };
  addAndMakeVisible(saveVoiceButton);

  loadVoiceButton.setShape(folderPath, true, true, false);
  loadVoiceButton.setTooltip("Load settings for this voice from a .voice file");
  loadVoiceButton.onClick = [this]() { loadVoicePresetFromFile(); };
  addAndMakeVisible(loadVoiceButton);

  // ========== ARPEGGIATOR ==========
  arpEnableButton.setTooltip(
      "Enable the arpeggiator (automatically disables chord memory)");
  arpEnableButton.onClick = [this]() {
    if (!arpEnableButton.getToggleState())
      return;
    if (auto *chordParam = processor.apvts.getParameter("chordEnable"))
      chordParam->setValueNotifyingHost(0.0f);
  };
  addAndMakeVisible(arpEnableButton);
  arpEnableButton.getProperties().set("accent",
                                      (int)gm::ui::theme::grn.getARGB());

  arpPatternSelector.addItem("Up", 1);
  arpPatternSelector.addItem("Down", 2);
  arpPatternSelector.addItem("Up/Down", 3);
  arpPatternSelector.addItem("Random", 4);
  arpPatternSelector.setSelectedId(static_cast<int>(processor.getArpPattern()) +
                                       1,
                                   juce::dontSendNotification);
  arpPatternSelector.onChange = [this]() {
    processor.setArpPattern(static_cast<BreadbinProcessor::ArpPattern>(
        arpPatternSelector.getSelectedId() - 1));
  };
  arpPatternSelector.setTooltip("Arp pattern: Up, Down, Up/Down, or Random");
  addAndMakeVisible(arpPatternSelector);

  arpRateSlider.setRange(1.0, 100.0, 1.0);
  arpRateSlider.setDoubleClickReturnValue(true, 5.0);
  arpRateSlider.setValue(processor.getArpRate());
  arpRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  arpRateSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 35, 18);
  arpRateSlider.setTooltip("Arp Rate (Hz) - PAL=50, NTSC=60");
  arpRateSlider.onValueChange = [this]() {
    processor.setArpRate(static_cast<float>(arpRateSlider.getValue()));
  };
  addAndMakeVisible(arpRateSlider);

  arpRateLabel.setText("Hz", juce::dontSendNotification);
  arpRateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(arpRateLabel);

  arpOctaveSelector.addItem("1 Octave", 1);
  arpOctaveSelector.addItem("2 Octaves", 2);
  arpOctaveSelector.addItem("3 Octaves", 3);
  arpOctaveSelector.addItem("4 Octaves", 4);
  arpOctaveSelector.setSelectedId(processor.getArpOctaves(),
                                  juce::dontSendNotification);
  arpOctaveSelector.onChange = [this]() {
    processor.setArpOctaves(arpOctaveSelector.getSelectedId());
  };
  arpOctaveSelector.setTooltip("Arpeggiator range: 1-4 octaves");
  addAndMakeVisible(arpOctaveSelector);

  // Glide/Portamento
  glideTimeLabel.setText("Glide", juce::dontSendNotification);
  glideTimeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  glideTimeLabel.setFont(proFont.withHeight(10.0f));
  addAndMakeVisible(glideTimeLabel);

  glideTimeSlider.setRange(0.0, 2000.0, 1.0);
  glideTimeSlider.setDoubleClickReturnValue(true, 0.0);
  glideTimeSlider.setValue(processor.getGlideTimeMs());
  glideTimeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  glideTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  glideTimeSlider.setTooltip("Portamento time (0 = off, up to 2000ms)");
  glideTimeSlider.onValueChange = [this]() {
    processor.setGlideTimeMs(static_cast<float>(glideTimeSlider.getValue()));
  };
  addAndMakeVisible(glideTimeSlider);

  // Master Volume
  masterVolLabel.setText("Master", juce::dontSendNotification);
  masterVolLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  masterVolLabel.setFont(proFont.withHeight(12.0f));
  addAndMakeVisible(masterVolLabel);

  masterVolSlider.setRange(0.0, 1.0, 0.01);
  masterVolSlider.setDoubleClickReturnValue(true, 0.8);
  masterVolSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  masterVolSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  masterVolSlider.setTooltip("Master output volume (affects both SID chips)");
  // No onValueChange needed — APVTS attachment + processBlock sync handles it
  addAndMakeVisible(masterVolSlider);

  noiseGateLabel.setText("Gate", juce::dontSendNotification);
  noiseGateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  noiseGateLabel.setFont(proFont.withHeight(12.0f));
  addAndMakeVisible(noiseGateLabel);

  noiseGateSlider.setRange(0.0, 0.1, 0.001);
  noiseGateSlider.setDoubleClickReturnValue(true, 0.01);
  noiseGateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  noiseGateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  noiseGateSlider.setTooltip(
      "Noise gate threshold. Silences residual SID drone below this level.\n"
      "Uses envelope following with smooth attack/release transitions.\n"
      "0 = gate off, higher = more aggressive gating.");
  addAndMakeVisible(noiseGateSlider);

  // External Audio Input
  extInputEnableButton.setToggleState(processor.isExtInputEnabled(),
                                      juce::dontSendNotification);
  extInputEnableButton.setTooltip("Route external audio through SID filters");
  addAndMakeVisible(extInputEnableButton);

  extInputLabel.setText("Level", juce::dontSendNotification);
  extInputLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  extInputLabel.setFont(proFont.withHeight(10.0f));
  addAndMakeVisible(extInputLabel);

  extInputLevelSlider.setRange(0.0, 2.0, 0.01);
  extInputLevelSlider.setDoubleClickReturnValue(true, 1.0);
  extInputLevelSlider.setValue(processor.getExtInputLevel());
  extInputLevelSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  extInputLevelSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40,
                                      18);
  extInputLevelSlider.setTooltip("External input level (0-200%)");
  extInputLevelSlider.onValueChange = [this]() {
    processor.setExtInputLevel(
        static_cast<float>(extInputLevelSlider.getValue()));
  };
  addAndMakeVisible(extInputLevelSlider);

  // Clock mode (PAL/NTSC)
  clockModeLabel.setText("Clock:", juce::dontSendNotification);
  clockModeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  clockModeLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(clockModeLabel);

  clockModeSelector.addItem("PAL", 1);
  clockModeSelector.addItem("NTSC", 2);
  clockModeSelector.setSelectedId(
      processor.getClockMode() == SIDEngine::ClockMode::NTSC ? 2 : 1);
  clockModeSelector.setTooltip(
      "PAL (985 kHz) or NTSC (1023 kHz) clock frequency");
  clockModeSelector.onChange = [this]() {
    processor.setClockMode(clockModeSelector.getSelectedId() == 2
                               ? SIDEngine::ClockMode::NTSC
                               : SIDEngine::ClockMode::PAL);
  };
  addAndMakeVisible(clockModeSelector);
}

void BreadbinEditor::setupFXControls() {
  // ===== FX: CHORUS =====
  chorusEnableButton.setTooltip("Chorus: Dimension D-style stereo widening");
  addAndMakeVisible(chorusEnableButton);

  auto setupFXSlider = [this](juce::Slider &slider, juce::Label &label,
                              const juce::String &name, float minVal,
                              float maxVal, float defaultVal, float step,
                              const juce::String &tooltip) {
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(retroFont.withHeight(7.0f));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    slider.setRange(minVal, maxVal, step);
    slider.setValue(defaultVal);
    slider.setDoubleClickReturnValue(true, static_cast<double>(defaultVal));
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    addAndMakeVisible(slider);
  };

  setupFXSlider(chorusRateSlider, chorusRateLabel, "Rate", 0.1f, 10.0f, 1.5f,
                0.1f, "Chorus Rate (Hz)");
  chorusRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 18);
  chorusRateSlider.setNumDecimalPlacesToDisplay(1);
  setupFXSlider(chorusDepthSlider, chorusDepthLabel, "Depth", 0.0f, 1.0f, 0.3f,
                0.01f, "Chorus Depth");
  setupFXSlider(chorusMixSlider, chorusMixLabel, "Mix", 0.0f, 1.0f, 0.5f, 0.01f,
                "Chorus Wet/Dry Mix");

  // ===== FX: DELAY =====
  delayEnableButton.setTooltip("Stereo Delay with independent L/R times");
  addAndMakeVisible(delayEnableButton);

  setupFXSlider(delayTimeLSlider, delayTimeLLabel, "L ms", 1.0f, 1000.0f,
                375.0f, 1.0f, "Delay Time Left (ms)");
  delayTimeLSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 18);
  delayTimeLSlider.setNumDecimalPlacesToDisplay(0);
  setupFXSlider(delayTimeRSlider, delayTimeRLabel, "R ms", 1.0f, 1000.0f,
                500.0f, 1.0f, "Delay Time Right (ms)");
  delayTimeRSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 18);
  delayTimeRSlider.setNumDecimalPlacesToDisplay(0);
  setupFXSlider(delayFeedbackSlider, delayFBLabel, "FB", 0.0f, 0.95f, 0.3f,
                0.01f, "Delay Feedback");
  setupFXSlider(delayMixSlider, delayMixLabel, "Mix", 0.0f, 1.0f, 0.3f, 0.01f,
                "Delay Wet/Dry Mix");

  // ===== FX: REVERB =====
  reverbEnableButton.setTooltip(
      "Reverb: Algorithmic stereo reverb (Costello FDN)");
  addAndMakeVisible(reverbEnableButton);

  setupFXSlider(reverbDecaySlider, reverbDecayLabel, "Decay", 0.1f, 0.95f,
                0.7f, 0.01f, "Reverb Decay Time");
  setupFXSlider(reverbDampingSlider, reverbDampingLabel, "Damp", 1000.0f,
                16000.0f, 10000.0f, 100.0f, "Reverb Damping (LP Cutoff Hz)");
  reverbDampingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50,
                                      18);
  reverbDampingSlider.setNumDecimalPlacesToDisplay(0);
  setupFXSlider(reverbMixSlider, reverbMixLabel, "Mix", 0.0f, 1.0f, 0.3f,
                0.01f, "Reverb Wet/Dry Mix");

  // ===== FILTER ENVELOPE =====
  filterEnvEnableButton.setTooltip(
      "Filter Envelope: Dedicated ADSR for filter cutoff");
  filterEnvEnableButton.setButtonText("Filt Env");
  addAndMakeVisible(filterEnvEnableButton);

  auto setupFilterEnvSlider = [this](juce::Slider &slider, juce::Label &label,
                                     const juce::String &name, float minVal,
                                     float maxVal, float defaultVal,
                                     const juce::String &tooltip) {
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(retroFont.withHeight(7.0f));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    slider.setRange(minVal, maxVal, 0.001);
    slider.setValue(defaultVal);
    slider.setDoubleClickReturnValue(true, static_cast<double>(defaultVal));
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    addAndMakeVisible(slider);
  };

  setupFilterEnvSlider(filterEnvAttackSlider, filterEnvAttackLabel, "A", 0.001f,
                       10.0f, 0.01f, "Filter Env Attack (seconds)");
  setupFilterEnvSlider(filterEnvDecaySlider, filterEnvDecayLabel, "D", 0.001f,
                       10.0f, 0.3f, "Filter Env Decay (seconds)");
  setupFilterEnvSlider(filterEnvSustainSlider, filterEnvSustainLabel, "S", 0.0f,
                       1.0f, 0.5f, "Filter Env Sustain level");
  setupFilterEnvSlider(filterEnvReleaseSlider, filterEnvReleaseLabel, "R",
                       0.001f, 10.0f, 0.5f, "Filter Env Release (seconds)");

  filterEnvAmountLabel.setText("Amt", juce::dontSendNotification);
  filterEnvAmountLabel.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgrey);
  filterEnvAmountLabel.setFont(retroFont.withHeight(7.0f));
  filterEnvAmountLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(filterEnvAmountLabel);

  filterEnvAmountSlider.setRange(-1.0, 1.0, 0.01);
  filterEnvAmountSlider.setDoubleClickReturnValue(true, 0.5);
  filterEnvAmountSlider.setValue(0.5);
  filterEnvAmountSlider.setSliderStyle(
      juce::Slider::RotaryHorizontalVerticalDrag);
  filterEnvAmountSlider.setColour(juce::Slider::trackColourId,
                                  gm::ui::theme::grn);
  filterEnvAmountSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40,
                                        12);
  filterEnvAmountSlider.setColour(juce::Slider::textBoxTextColourId,
                                  gm::ui::theme::grn);
  filterEnvAmountSlider.setColour(juce::Slider::textBoxOutlineColourId,
                                  juce::Colours::transparentBlack);
  filterEnvAmountSlider.setTooltip("Filter Env Amount: Bipolar (-1 to +1). "
                                   "Positive opens filter on attack.");
  addAndMakeVisible(filterEnvAmountSlider);

  // Per-section accent (Phase B2): Filter Envelope group = greenyellow.
  {
    const int grnA = (int)gm::ui::theme::grn.getARGB();
    filterEnvEnableButton.getProperties().set("accent", grnA);
    filterEnvAttackSlider.getProperties().set("accent", grnA);
    filterEnvDecaySlider.getProperties().set("accent", grnA);
    filterEnvSustainSlider.getProperties().set("accent", grnA);
    filterEnvReleaseSlider.getProperties().set("accent", grnA);
    filterEnvAmountSlider.getProperties().set("accent", grnA);
  }
}

void BreadbinEditor::setupPopupButtons() {
  // Wavetable
  wavetableButton.setTooltip("Wavetable: C64-style step sequencer editor");
  wavetableButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(60, 60, 70));
  wavetableButton.setColour(juce::TextButton::textColourOnId,
                            juce::Colours::cyan);
  wavetableButton.setColour(juce::TextButton::textColourOffId,
                            juce::Colours::cyan);
  wavetableButton.onClick = [this]() { showWavetablePopup(); };
  addAndMakeVisible(wavetableButton);

  // Enable toggles
  wtEnableToggle.setTooltip("Enable/disable wavetable step sequencer");
  wtEnableToggle.setColour(juce::ToggleButton::textColourId,
                           juce::Colours::cyan);
  wtEnableToggle.setColour(juce::ToggleButton::tickColourId,
                           juce::Colours::cyan);
  addAndMakeVisible(wtEnableToggle);
  wtEnableToggleAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "wtEnable", wtEnableToggle);

  lfo1EnableToggle.setTooltip("Enable/disable LFO 1");
  lfo1EnableToggle.setColour(juce::ToggleButton::textColourId,
                             juce::Colours::cyan);
  lfo1EnableToggle.setColour(juce::ToggleButton::tickColourId,
                             juce::Colours::cyan);
  addAndMakeVisible(lfo1EnableToggle);
  lfo1EnableToggleAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "lfoEnable", lfo1EnableToggle);

  lfo2EnableToggle.setTooltip("Enable/disable LFO 2");
  lfo2EnableToggle.setColour(juce::ToggleButton::textColourId,
                             juce::Colours::cyan);
  lfo2EnableToggle.setColour(juce::ToggleButton::tickColourId,
                             juce::Colours::cyan);
  addAndMakeVisible(lfo2EnableToggle);
  lfo2EnableToggleAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "lfo2Enable", lfo2EnableToggle);

  // Mod Matrix
  modMatrixButton.setTooltip("Modulation: LFO, pitch bend range, mod matrix");
  modMatrixButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(60, 60, 70));
  modMatrixButton.setColour(juce::TextButton::textColourOnId,
                            juce::Colours::cyan);
  modMatrixButton.setColour(juce::TextButton::textColourOffId,
                            juce::Colours::cyan);
  modMatrixButton.onClick = [this]() { showModMatrixPopup(); };
  addAndMakeVisible(modMatrixButton);

  // Chord Memory
  chordMemoryButton.setTooltip("Chord Memory: Trigger chords from single keys");
  chordMemoryButton.setColour(juce::TextButton::buttonColourId,
                              juce::Colour(60, 60, 70));
  chordMemoryButton.setColour(juce::TextButton::textColourOnId,
                              juce::Colours::cyan);
  chordMemoryButton.setColour(juce::TextButton::textColourOffId,
                              juce::Colours::cyan);
  chordMemoryButton.onClick = [this]() { showChordMemoryPopup(); };
  addAndMakeVisible(chordMemoryButton);

  // SID Player
  sidPlayerButton.setTooltip(
      "SID Player: Load and play .SID files, snapshot registers to synth");
  sidPlayerButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(70, 50, 50));
  sidPlayerButton.setColour(juce::TextButton::textColourOnId,
                            juce::Colours::orange);
  sidPlayerButton.setColour(juce::TextButton::textColourOffId,
                            juce::Colours::orange);
  sidPlayerButton.onClick = [this]() { showSidPlayerPopup(); };
  addAndMakeVisible(sidPlayerButton);

  // Digi Sampler
  digiButton.setTooltip("Digi Sampler: Authentic 4-bit $D418 sample playback");
  digiButton.setColour(juce::TextButton::buttonColourId,
                       juce::Colour(60, 50, 70));
  digiButton.setColour(juce::TextButton::textColourOnId,
                       juce::Colours::cyan);
  digiButton.setColour(juce::TextButton::textColourOffId,
                       juce::Colours::cyan);
  digiButton.onClick = [this]() { showDigiPopup(); };
  addAndMakeVisible(digiButton);

  digiEnableToggle.setTooltip("Enable/disable digi sampler");
  digiEnableToggle.setColour(juce::ToggleButton::textColourId,
                             juce::Colours::cyan);
  digiEnableToggle.setColour(juce::ToggleButton::tickColourId,
                             juce::Colours::cyan);
  addAndMakeVisible(digiEnableToggle);
  digiEnableAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, "digiEnable", digiEnableToggle);

  // Per-section accent (Phase B2): aux enable toggles = greenyellow.
  {
    const int grnA = (int)gm::ui::theme::grn.getARGB();
    lfo1EnableToggle.getProperties().set("accent", grnA);
    lfo2EnableToggle.getProperties().set("accent", grnA);
    wtEnableToggle.getProperties().set("accent", grnA);
    digiEnableToggle.getProperties().set("accent", grnA);
  }

  // SID Player register overlay labels (hidden by default)
  auto setupOverlay = [this](juce::Label &lbl) {
    lbl.setColour(juce::Label::textColourId, juce::Colours::cyan);
    lbl.setColour(juce::Label::backgroundColourId,
                  juce::Colours::black.withAlpha(0.7f));
    lbl.setFont(proFont.withHeight(9.0f));
    lbl.setJustificationType(juce::Justification::centred);
    lbl.setVisible(false);
    addAndMakeVisible(lbl);
  };
  setupOverlay(sidOverlayWave);
  setupOverlay(sidOverlayPW);
  setupOverlay(sidOverlayAttack);
  setupOverlay(sidOverlayDecay);
  setupOverlay(sidOverlaySustain);
  setupOverlay(sidOverlayRelease);
  setupOverlay(sidOverlayCutoff);
  setupOverlay(sidOverlayRes);

  // Keyboard
  keyboard.setKeyWidth(16.0f);
  keyboard.setAvailableRange(36, 84); // C2 to C6 - reasonable SID range
  addAndMakeVisible(keyboard);
}

void BreadbinEditor::setupSidPanel(bool isLeft) {
  auto colour = isLeft ? juce::Colours::cyan : juce::Colours::orange;
  int voiceOffset = isLeft ? 0 : 3;
  int defaultChip = isLeft ? 1 : 5;
  float defaultPan = isLeft ? -1.0f : 1.0f;
  juce::String sidName = isLeft ? "Left" : "Right";

  // References to left or right member widgets
  auto &sidLabel = isLeft ? leftSIDLabel : rightSIDLabel;
  auto &chipSelector = isLeft ? leftChipSelector : rightChipSelector;
  auto &voiceButtons = isLeft ? leftVoiceButtons : rightVoiceButtons;
  auto &voiceEnables = isLeft ? leftVoiceEnables : rightVoiceEnables;
  auto &cutoffLabel = isLeft ? leftCutoffLabel : rightCutoffLabel;
  auto &cutoffSlider = isLeft ? leftCutoffSlider : rightCutoffSlider;
  auto &resLabel = isLeft ? leftResonanceLabel : rightResonanceLabel;
  auto &resSlider = isLeft ? leftResonanceSlider : rightResonanceSlider;
  auto &cutoffMeter = isLeft ? cutoffMeterL : cutoffMeterR;
  auto &resMeter = isLeft ? resMeterL : resMeterR;
  auto &lpBtn = isLeft ? leftLPButton : rightLPButton;
  auto &bpBtn = isLeft ? leftBPButton : rightBPButton;
  auto &hpBtn = isLeft ? leftHPButton : rightHPButton;
  auto &filterEnBtn = isLeft ? leftFilterEnableButton : rightFilterEnableButton;
  auto &detuneLabel = isLeft ? leftDetuneLabel : rightDetuneLabel;
  auto &detuneSlider = isLeft ? leftDetuneSlider : rightDetuneSlider;
  auto &panLabel = isLeft ? leftPanLabel : rightPanLabel;
  auto &panSlider = isLeft ? leftPanSlider : rightPanSlider;
  auto &sid = isLeft ? processor.getLeftSID() : processor.getRightSID();

  // SID label — text is drawn by drawGlowText in paint(); suppress Label's own text draw.
  sidLabel.setText(sidName.toUpperCase() + " SID", juce::dontSendNotification);
  sidLabel.setFont(retroFont.withHeight(10.0f));
  sidLabel.setColour(juce::Label::textColourId, juce::Colours::transparentBlack);
  sidLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(sidLabel);

  // Chip selector
  chipSelector.addItem("MOS 6581", 1);
  chipSelector.addItem("MOS 6581 R2", 2);
  chipSelector.addItem("MOS 6581 R3", 3);
  chipSelector.addItem("MOS 6581 R4", 4);
  chipSelector.addItem("MOS 8580", 5);
  chipSelector.addItem("MOS 8580 R5", 6);
  chipSelector.addItem("CSG 9580", 7);
  chipSelector.addItem("MOS 8580D", 8);
  chipSelector.setSelectedId(defaultChip);
  chipSelector.onChange = [this, isLeft, &chipSelector]() {
    static constexpr SIDEngine::ChipModel models[] = {
        SIDEngine::ChipModel::MOS6581,  SIDEngine::ChipModel::MOS6581R2,
        SIDEngine::ChipModel::MOS6581R3, SIDEngine::ChipModel::MOS6581R4,
        SIDEngine::ChipModel::MOS8580,  SIDEngine::ChipModel::MOS8580R5,
        SIDEngine::ChipModel::CSG9580,  SIDEngine::ChipModel::MOS8580D};
    int idx = chipSelector.getSelectedId() - 1;
    if (idx >= 0 && idx < 8) {
      if (isLeft)
        processor.setLeftChipModel(models[idx]);
      else
        processor.setRightChipModel(models[idx]);
    }
  };
  chipSelector.setTooltip(
      "6581: Warm, R2: Bright, R3: Classic, R4: Bright/Strong, "
      "8580: Clean, R5: Dark, 9580: Bright, 8580D: Mellow");
  addAndMakeVisible(chipSelector);

  // Voice buttons and enables
  for (int i = 0; i < 3; ++i) {
    int vi = i + voiceOffset;
    voiceButtons[i].setButtonText(juce::String(vi + 1));
    voiceButtons[i].onClick = [this, vi]() { selectVoice(vi); };
    voiceButtons[i].setTooltip("Select Voice " + juce::String(vi + 1) +
                               " for editing");
    addAndMakeVisible(voiceButtons[i]);

    voiceEnables[i].setButtonText("");
    voiceEnables[i].setToggleState(true, juce::dontSendNotification);
    voiceEnables[i].onClick = [this, vi, &voiceEnables, i]() {
      auto *p = processor.apvts.getParameter("v" + juce::String(vi) + "_enable");
      if (p) p->setValueNotifyingHost(voiceEnables[i].getToggleState() ? 1.0f : 0.0f);
    };
    voiceEnables[i].setTooltip("Enable/disable Voice " + juce::String(vi + 1));
    addAndMakeVisible(voiceEnables[i]);
  }

  // Filter cutoff
  cutoffLabel.setText("Cutoff", juce::dontSendNotification);
  cutoffLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  cutoffLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(cutoffLabel);

  cutoffSlider.setRange(0, 2047, 1);
  cutoffSlider.setDoubleClickReturnValue(true, 1024.0);
  cutoffSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  cutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  cutoffSlider.setColour(juce::Slider::trackColourId, colour);
  cutoffSlider.setTooltip("Filter Cutoff Frequency (0-2047)");
  cutoffSlider.onValueChange = [this, isLeft, &cutoffSlider, &sid]() {
    int val = static_cast<int>(cutoffSlider.getValue());
    processor.setBaseFilterCutoff(isLeft, val);
    sid.setFilterCutoff(val);
  };
  addAndMakeVisible(cutoffSlider);

  // Filter resonance
  resLabel.setText("Res", juce::dontSendNotification);
  resLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  resLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(resLabel);

  resSlider.setRange(0, 15, 1);
  resSlider.setDoubleClickReturnValue(true, 0.0);
  resSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  resSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  resSlider.setColour(juce::Slider::trackColourId, colour);
  resSlider.setTooltip("Filter Resonance (0-15)");
  resSlider.onValueChange = [this, isLeft, &resSlider, &sid]() {
    int val = static_cast<int>(resSlider.getValue());
    processor.setBaseFilterResonance(isLeft, val);
    sid.setFilterResonance(val);
  };
  addAndMakeVisible(resSlider);

  // Modulation meters
  cutoffMeter.setRange(0.0f, 2047.0f);
  addAndMakeVisible(cutoffMeter);
  resMeter.setRange(0.0f, 15.0f);
  addAndMakeVisible(resMeter);

  // Filter mode buttons
  auto setupButton = [this, &colour](juce::ToggleButton &btn) {
    btn.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    btn.setColour(juce::ToggleButton::tickColourId, colour);
    btn.onClick = [this]() { updateFiltersFromUI(); };
    addAndMakeVisible(btn);
  };
  setupButton(lpBtn);
  setupButton(bpBtn);
  setupButton(hpBtn);
  setupButton(filterEnBtn);
  lpBtn.setButtonText("LP");
  lpBtn.setToggleState(true, juce::dontSendNotification);
  filterEnBtn.setToggleState(true, juce::dontSendNotification);
  filterEnBtn.setTooltip("Enable filter routing for all voices");
  lpBtn.setTooltip("Low-pass filter - cuts high frequencies");
  bpBtn.setTooltip("Band-pass filter - cuts lows and highs");
  hpBtn.setTooltip("High-pass filter - cuts low frequencies");

  sid.setFilterVoices(true, true, true);
  sid.setFilterMode(true, false, false);
  sid.setFilterCutoff(1024);

  // Detune slider
  detuneLabel.setText("Detune", juce::dontSendNotification);
  detuneLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  detuneLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(detuneLabel);

  detuneSlider.setRange(-50.0, 50.0, 1.0);
  detuneSlider.setDoubleClickReturnValue(true, 0.0);
  detuneSlider.setValue(0.0);
  detuneSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  detuneSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);
  detuneSlider.setTooltip("Detune: -50 to +50 cents");
  detuneSlider.onValueChange = [this, isLeft, &detuneSlider]() {
    float val = static_cast<float>(detuneSlider.getValue());
    if (isLeft)
      processor.setLeftDetune(val);
    else
      processor.setRightDetune(val);
  };
  addAndMakeVisible(detuneSlider);

  // Pan slider
  panLabel.setText("Pan", juce::dontSendNotification);
  panLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  panLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(panLabel);

  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setDoubleClickReturnValue(true, static_cast<double>(defaultPan));
  panSlider.setValue(static_cast<double>(defaultPan));
  panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  panSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 18);
  panSlider.setTooltip(sidName + " SID Pan: -1 (left) to +1 (right)");
  addAndMakeVisible(panSlider);

  // Per-section accent (Phase B2): SID I = cyan, SID II = orange.
  const int accentARGB =
      (int)(isLeft ? gm::ui::theme::cyan : gm::ui::theme::orange).getARGB();
  auto setAccent = [accentARGB](juce::Component &c) {
    c.getProperties().set("accent", accentARGB);
  };
  setAccent(chipSelector);
  setAccent(cutoffSlider);
  setAccent(resSlider);
  setAccent(lpBtn);
  setAccent(bpBtn);
  setAccent(hpBtn);
  setAccent(filterEnBtn);
  setAccent(detuneSlider);
  setAccent(panSlider);
  for (int i = 0; i < 3; ++i) {
    setAccent(voiceButtons[i]);
    setAccent(voiceEnables[i]);
  }
}

void BreadbinEditor::setupVoiceEditor() {
  // voiceEditorLabel text is drawn by drawGlowText in paint(); suppress Label's own text draw.
  voiceEditorLabel.setText("VOICE EDITOR", juce::dontSendNotification);
  voiceEditorLabel.setFont(retroFont.withHeight(8.0f));
  voiceEditorLabel.setColour(juce::Label::textColourId, juce::Colours::transparentBlack);
  addAndMakeVisible(voiceEditorLabel);

  waveformLabel.setText("Wave:", juce::dontSendNotification);
  waveformLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  waveformLabel.setFont(retroFont.withHeight(7.0f));
  addAndMakeVisible(waveformLabel);

  waveformSelector.addItem("Triangle", 1);
  waveformSelector.addItem("Sawtooth", 2);
  waveformSelector.addItem("Pulse", 3);
  waveformSelector.addItem("Noise", 4);
  waveformSelector.setSelectedId(1);
  waveformSelector.setTooltip("Oscillator Waveform");
  // onChange set below (ring mod logic); APVTS attachment handles sync
  addAndMakeVisible(waveformSelector);

  pwLabel.setText("Pulse:", juce::dontSendNotification);
  pwLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(pwLabel);

  pulseWidthSlider.setRange(0, 4095, 1);
  pulseWidthSlider.setDoubleClickReturnValue(true, 2048.0);
  pulseWidthSlider.setValue(2048);
  pulseWidthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  pulseWidthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 18);
  pulseWidthSlider.setTooltip(
      "Pulse Width (0-4095): Controls the square wave duty cycle");
  // APVTS attachment handles sync — no manual callback needed
  addAndMakeVisible(pulseWidthSlider);

  // Modulation meters for voice editor
  pwMeter.setRange(0.0f, 4095.0f);
  addAndMakeVisible(pwMeter);
  pitchMeter.setRange(-24.0f, 24.0f);
  addAndMakeVisible(pitchMeter);

  auto setupADSR = [this](juce::Slider &slider, juce::Label &label,
                          const juce::String &text, const juce::String &tooltip,
                          int defaultVal) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label.setFont(proFont.withHeight(10.0f));
    addAndMakeVisible(label);

    slider.setRange(0, 15, 1);
    slider.setValue(defaultVal);
    slider.setDoubleClickReturnValue(true, static_cast<double>(defaultVal));
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setTooltip(tooltip);
    // APVTS attachment handles sync — no manual callback needed
    addAndMakeVisible(slider);
  };

  setupADSR(attackSlider, attackLabel, "Atk",
            "Attack: Time for volume to reach maximum (0-15)", 0);
  setupADSR(decaySlider, decayLabel, "Dec",
            "Decay: Time to fall to sustain level (0-15)", 0);
  setupADSR(sustainSlider, sustainLabel, "Sus",
            "Sustain: Volume level while key held (0-15)", 15);
  setupADSR(releaseSlider, releaseLabel, "Rel",
            "Release: Time to fade after key release (0-15)", 0);

  // Per-voice pan removed (now per-SID via leftPan/rightPan APVTS)

  // Ring Modulation button
  ringModButton.setTooltip(
      "Ring Modulation: Multiplies triangle wave with previous voice. "
      "Only works with Triangle waveform.");
  // APVTS attachment handles sync — no manual callback needed
  addAndMakeVisible(ringModButton);

  // Hard Sync button
  syncButton.setTooltip(
      "Hard Sync: Resets oscillator phase when previous voice completes a "
      "cycle. Creates harmonic overtones.");
  // APVTS attachment handles sync — no manual callback needed
  addAndMakeVisible(syncButton);

  // Per-voice filter routing button
  voiceFilterButton.setTooltip("Route this voice through the SID filter.");
  voiceFilterButton.setToggleState(true, juce::dontSendNotification);
  // APVTS attachment handles sync — no manual callback needed
  addAndMakeVisible(voiceFilterButton);

  // Mod Offset slider (semitones for sync/ring mod modulator voice)
  modOffsetSlider.setDoubleClickReturnValue(true, 7.0);
  modOffsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  modOffsetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 35, 20);
  modOffsetSlider.setTooltip(
      "Modulator voice offset in semitones. Controls the pitch difference \n"
      "between carrier and modulator voices for Sync/Ring Mod effects.\n"
      "Default: 7 (perfect fifth). Set to 0 for no offset.");
  addAndMakeVisible(modOffsetSlider);
  modOffsetLabel.setJustificationType(juce::Justification::centredRight);
  modOffsetLabel.setFont(proFont.withHeight(11.0f));
  addAndMakeVisible(modOffsetLabel);

  // Update Ring Mod enable state when waveform changes
  waveformSelector.onChange = [this]() {
    // Ring mod only works with Triangle waveform
    bool isTriangle = (waveformSelector.getSelectedId() == 1);
    ringModButton.setEnabled(isTriangle);
    if (!isTriangle) {
      ringModButton.setToggleState(false, juce::dontSendNotification);
    }
  };

  // Per-section accent (Phase B2): voice editor = magenta; ADSR + Ring/Sync/Flt = greenyellow.
  const int vMag = (int)gm::ui::theme::mag.getARGB();
  const int vGrn = (int)gm::ui::theme::grn.getARGB();
  auto setAcc = [](juce::Component &c, int argb) {
    c.getProperties().set("accent", argb);
  };
  setAcc(waveformSelector, vMag);
  setAcc(pulseWidthSlider, vMag);
  setAcc(modOffsetSlider, vMag);
  setAcc(attackSlider, vGrn);
  setAcc(decaySlider, vGrn);
  setAcc(sustainSlider, vGrn);
  setAcc(releaseSlider, vGrn);
  setAcc(ringModButton, vGrn);
  setAcc(syncButton, vGrn);
  setAcc(voiceFilterButton, vGrn);
}

void BreadbinEditor::selectVoice(int voice) {
  if (voice < 0 || voice > 5)
    return;
  if (selectedVoice != voice) {
    saveUIToVoice(selectedVoice);
  }
  selectedVoice = voice;
  updateVoiceButtonStates();
  loadVoiceToUI(voice);
}

void BreadbinEditor::updateVoiceButtonStates() {
  for (int i = 0; i < 3; ++i) {
    leftVoiceButtons[i].setColour(juce::TextButton::buttonColourId,
                                  i == selectedVoice ? juce::Colours::cyan
                                                     : juce::Colours::darkgrey);
    rightVoiceButtons[i].setColour(juce::TextButton::buttonColourId,
                                   (i + 3) == selectedVoice
                                       ? juce::Colours::orange
                                       : juce::Colours::darkgrey);
  }

  // Update voice editor label to show which voice
  juce::String sidName = selectedVoice < 3 ? "L" : "R";

  voiceEditorLabel.setText("VOICE " + juce::String(selectedVoice + 1) + " (" +
                               sidName + ")",
                           juce::dontSendNotification);
}

void BreadbinEditor::loadVoiceToUI(int voice) {
  const auto &settings = processor.getVoiceSettings(voice);

  int waveformId = 1;
  switch (settings.waveform) {
  case SIDEngine::Waveform::Triangle:
    waveformId = 1;
    break;
  case SIDEngine::Waveform::Sawtooth:
    waveformId = 2;
    break;
  case SIDEngine::Waveform::Pulse:
    waveformId = 3;
    break;
  case SIDEngine::Waveform::Noise:
    waveformId = 4;
    break;
  }
  waveformSelector.setSelectedId(waveformId, juce::dontSendNotification);
  pulseWidthSlider.setValue(settings.pulseWidth, juce::dontSendNotification);
  attackSlider.setValue(settings.attack, juce::dontSendNotification);
  decaySlider.setValue(settings.decay, juce::dontSendNotification);
  sustainSlider.setValue(settings.sustain, juce::dontSendNotification);
  releaseSlider.setValue(settings.release, juce::dontSendNotification);
  // per-voice pan removed (now per-SID)
  presetSelector.setSelectedId(settings.presetId, juce::dontSendNotification);

  // Ring mod and sync
  ringModButton.setToggleState(settings.ringMod, juce::dontSendNotification);
  syncButton.setToggleState(settings.sync, juce::dontSendNotification);
  voiceFilterButton.setToggleState(settings.filterEnabled,
                                   juce::dontSendNotification);

  // Ring mod only works with Triangle waveform
  bool isTriangle = (waveformId == 1);
  ringModButton.setEnabled(isTriangle);
}

void BreadbinEditor::saveUIToVoice(int voice) {
  auto &settings = processor.getVoiceSettings(voice);

  switch (waveformSelector.getSelectedId()) {
  case 1:
    settings.waveform = SIDEngine::Waveform::Triangle;
    break;
  case 2:
    settings.waveform = SIDEngine::Waveform::Sawtooth;
    break;
  case 3:
    settings.waveform = SIDEngine::Waveform::Pulse;
    break;
  case 4:
    settings.waveform = SIDEngine::Waveform::Noise;
    break;
  }

  settings.pulseWidth = static_cast<int>(pulseWidthSlider.getValue());
  settings.attack = static_cast<int>(attackSlider.getValue());
  settings.decay = static_cast<int>(decaySlider.getValue());
  settings.sustain = static_cast<int>(sustainSlider.getValue());
  settings.release = static_cast<int>(releaseSlider.getValue());
  // per-voice pan removed (now per-SID)

  // Ring mod and sync
  settings.ringMod = ringModButton.getToggleState();
  settings.sync = syncButton.getToggleState();
  settings.filterEnabled = voiceFilterButton.getToggleState();

  processor.applyVoiceSettings(voice);
}

void BreadbinEditor::updateFiltersFromUI() {
  // Master filter enable (SID panel) combined with per-voice routing
  bool leftMaster = leftFilterEnableButton.getToggleState();
  processor.getLeftSID().setFilterVoices(
      leftMaster && processor.getVoiceSettings(0).filterEnabled,
      leftMaster && processor.getVoiceSettings(1).filterEnabled,
      leftMaster && processor.getVoiceSettings(2).filterEnabled);
  processor.getLeftSID().setFilterMode(leftLPButton.getToggleState(),
                                       leftBPButton.getToggleState(),
                                       leftHPButton.getToggleState());

  bool rightMaster = rightFilterEnableButton.getToggleState();
  processor.getRightSID().setFilterVoices(
      rightMaster && processor.getVoiceSettings(3).filterEnabled,
      rightMaster && processor.getVoiceSettings(4).filterEnabled,
      rightMaster && processor.getVoiceSettings(5).filterEnabled);
  processor.getRightSID().setFilterMode(rightLPButton.getToggleState(),
                                        rightBPButton.getToggleState(),
                                        rightHPButton.getToggleState());

  // Cache filter mode for poly voice replication
  processor.cacheFilterMode(
      leftLPButton.getToggleState(), leftBPButton.getToggleState(),
      leftHPButton.getToggleState(), rightLPButton.getToggleState(),
      rightBPButton.getToggleState(), rightHPButton.getToggleState());
}

void BreadbinEditor::paint(juce::Graphics &g) {
  // Cached background (bg_clean.png + radial vignette, built once in resizedContent)
  if (bgCache.isValid()) {
    g.drawImageAt(bgCache, 0, 0);
  } else {
    g.fillAll(gm::ui::theme::bg0);
  }

  // Glass panel backgrounds: semi-transparent dark gradient over the real backdrop.
  // The fill is translucent so the background image reads through.
  auto drawGlassPanel = [&](juce::Rectangle<int> bounds) {
    if (bounds.isEmpty()) return;
    auto fb = bounds.toFloat();
    // Frosted glass: blurred backdrop clipped to the panel, then the glass tint over it.
    if (bgBlurCache.isValid()) {
      juce::Graphics::ScopedSaveState ss(g);
      juce::Path clip;
      clip.addRoundedRectangle(fb, 8.0f);
      g.reduceClipRegion(clip);
      g.drawImageAt(bgBlurCache, 0, 0);
    }
    drawGlassFill(g, fb, 8.0f);
  };

  drawGlassPanel(topBarPanelBounds);
  drawGlassPanel(leftSidPanelBounds);
  drawGlassPanel(rightSidPanelBounds);
  drawGlassPanel(voiceEditorPanelBounds);
  drawGlassPanel(fxPanelBounds);
  drawGlassPanel(dockPanelBounds);

  // Glow text headers for SID and voice editor sections
  // drawGlowText draws bloom passes then accent; the Label child renders sharp text on top.
  if (!leftSIDLabel.getBounds().isEmpty())
    gm::ui::drawGlowText(g, leftSIDLabel.getText(),
                         retroFont.withHeight(10.0f),
                         leftSIDLabel.getBounds().toFloat(),
                         gm::ui::theme::cyan,
                         juce::Justification::centred);

  if (!rightSIDLabel.getBounds().isEmpty())
    gm::ui::drawGlowText(g, rightSIDLabel.getText(),
                         retroFont.withHeight(10.0f),
                         rightSIDLabel.getBounds().toFloat(),
                         gm::ui::theme::orange,
                         juce::Justification::centred);

  if (!voiceEditorLabel.getBounds().isEmpty())
    gm::ui::drawGlowText(g, voiceEditorLabel.getText(),
                         retroFont.withHeight(8.0f),
                         voiceEditorLabel.getBounds().toFloat(),
                         gm::ui::theme::cyan,
                         juce::Justification::centredLeft);
}

void BreadbinEditor::resizedContent() {
  // OptionD 6-region layout — positioning only, no behavior changes.
  // Content area: ~984 wide × 727 tall (1000x743 minus 8px padding each side).
  // Region heights (sum = 687): topBar=46, towers=240, voiceEd=175, fx=104, dock=34, keyboard=88.
  // 5 gaps × 8px = 40px. Total = 727px. ✓
  static constexpr int kGap      = 8;  // vertical gap between regions
  static constexpr int kTopBarH  = 46;
  static constexpr int kTowersH  = 240;
  static constexpr int kVoiceH   = 175;
  static constexpr int kFxH      = 104;
  static constexpr int kDockH    = 48;
  static constexpr int kKbH      = 74;

  midiLearnOverlay.setBounds(getLocalBounds());
  auto bounds = getLocalBounds().reduced(8);

  // --- Region 1: Top bar ---
  topBarPanelBounds = bounds.removeFromTop(kTopBarH);
  layoutTopRow(topBarPanelBounds); // passes a copy so it doesn't consume bounds
  bounds.removeFromTop(kGap);

  // --- Region 2: SID towers ---
  auto towersRow = bounds.removeFromTop(kTowersH);
  {
    const int pad = 6;
    const int sidWidth = (towersRow.getWidth() - pad) / 2;
    leftSidPanelBounds  = towersRow.withWidth(sidWidth);
    rightSidPanelBounds = towersRow.withX(towersRow.getRight() - sidWidth).withWidth(sidWidth);
  }
  layoutSidPanels(towersRow);
  bounds.removeFromTop(kGap);

  // --- Region 3: Voice editor ---
  voiceEditorPanelBounds = bounds.removeFromTop(kVoiceH);
  layoutVoiceEditor(voiceEditorPanelBounds);
  bounds.removeFromTop(kGap);

  // --- Region 4: FX chain ---
  fxPanelBounds = bounds.removeFromTop(kFxH);
  bounds.removeFromTop(kGap);

  // --- Region 5: Dock ---
  dockPanelBounds = bounds.removeFromTop(kDockH);
  bounds.removeFromTop(kGap);

  // --- Region 6: Keyboard ---
  keyboard.setBounds(bounds.removeFromTop(kKbH));

  // Now lay out FX and dock controls using their stored rects
  {
    auto fxArea   = fxPanelBounds;
    auto dockArea = dockPanelBounds;
    layoutBottomControls(fxArea, dockArea);
  }

  // Build background cache (background_clean.png + vignette)
  if (getWidth() > 0 && getHeight() > 0) {
    const int w = getWidth();
    const int h = getHeight();
    auto srcBg = juce::ImageCache::getFromMemory(
        BinaryData::background_clean_png, BinaryData::background_clean_pngSize);

    bgCache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics gc(bgCache);

    if (srcBg.isValid()) {
      // Scale-to-cover: fill the cache rect, preserving aspect ratio
      gc.drawImage(srcBg, 0, 0, w, h,
                   0, 0, srcBg.getWidth(), srcBg.getHeight());
    } else {
      gc.setColour(gm::ui::theme::bg0);
      gc.fillAll();
    }

    // Light radial vignette: centre rgba(6,6,12,.14) -> edge rgba(4,4,9,.52) at ~85%
    juce::ColourGradient vignette(
        juce::Colour(0x2406060C),   // centre: rgba(6,6,12,.14)
        static_cast<float>(w) * 0.5f,
        static_cast<float>(h) * 0.4f,
        juce::Colour(0x85040409),   // edge: rgba(4,4,9,.52)
        0.0f, 0.0f,
        true);                        // radial
    vignette.addColour(0.85, juce::Colour(0x85040409));
    gc.setGradientFill(vignette);
    gc.fillRect(0, 0, w, h);
  }

  // Frosted glass: pre-blur the backdrop once for the glass panels (melatonin, cached).
  if (bgCache.isValid()) {
    melatonin::CachedBlur bgBlur(20);
    bgBlurCache = bgBlur.render(bgCache).createCopy();
  }
}

void BreadbinEditor::layoutTopRow(juce::Rectangle<int> &bounds) {
  // OptionD top bar: logo | divider | Engine seg | Voicing seg + fold-ins | spacer |
  //   Master slider+val | divider | preset stepper | CPU label | scaleSelector
  const int pad = 6;
  auto row = bounds.reduced(6, 1); // inner margin inside the glass panel

  // Right side: scale selector + CPU
  scaleSelector.setBounds(row.removeFromRight(60).withHeight(22).withY(row.getCentreY() - 11));
  row.removeFromRight(pad);
  cpuLoadLabel.setBounds(row.removeFromRight(52).withHeight(16).withY(row.getCentreY() - 8));
  row.removeFromRight(pad);

  // Left: logo area (drawn by paint(), reserve space)
  auto logoBounds = row.removeFromLeft(92);
  titleLabel.setBounds(logoBounds); // titleLabel occupies logo area
  row.removeFromLeft(pad);

  // Vertical divider (just a spacer — drawn in paint if needed)
  row.removeFromLeft(1);
  row.removeFromLeft(pad);

  // Engine segmented (dualModeSelector)
  modeLabel.setBounds(row.removeFromLeft(44));
  dualModeSelector.setBounds(row.removeFromLeft(110).withHeight(22).withY(row.getCentreY() - 11));
  row.removeFromLeft(pad);

  // Voicing segmented (voiceModeSelector) + poly count + spread + retrig
  voiceModeSelector.setBounds(row.removeFromLeft(80).withHeight(22).withY(row.getCentreY() - 11));
  polyMaxNotesSelector.setBounds(row.removeFromLeft(42).withHeight(22).withY(row.getCentreY() - 11));
  polyVoiceCountLabel.setBounds(row.removeFromLeft(28).withHeight(16).withY(row.getCentreY() - 8));
  paraSpreadLabel.setBounds(row.removeFromLeft(36).withHeight(16).withY(row.getCentreY() - 8));
  paraSpreadSlider.setBounds(row.removeFromLeft(64).withHeight(22).withY(row.getCentreY() - 11));
  paraRetrigButton.setBounds(row.removeFromLeft(50).withHeight(20).withY(row.getCentreY() - 10));
  row.removeFromLeft(pad);

  // Spacer (flex:1 in JSX) — skip to master slider
  // Place master vol in centre-right area
  const int masterW = 130;
  masterVolLabel.setBounds(row.removeFromLeft(46).withHeight(16).withY(row.getCentreY() - 8));
  masterVolSlider.setBounds(row.removeFromLeft(masterW).withHeight(22).withY(row.getCentreY() - 11));
  noiseGateLabel.setBounds(row.removeFromLeft(34).withHeight(16).withY(row.getCentreY() - 8));
  noiseGateSlider.setBounds(row.removeFromLeft(100).withHeight(22).withY(row.getCentreY() - 11));
  row.removeFromLeft(pad);

  // Divider (spacer)
  row.removeFromLeft(1);
  row.removeFromLeft(pad);

  // Preset stepper: prev | selector | dirty | next | save | load
  globalPresetLabel.setBounds(row.removeFromLeft(40).withHeight(16).withY(row.getCentreY() - 8));
  presetPrevButton.setBounds(row.removeFromLeft(20).reduced(0, 3));
  globalPresetSelector.setBounds(row.removeFromLeft(110).withHeight(22).withY(row.getCentreY() - 11));
  presetDirtyLabel.setBounds(row.removeFromLeft(14).withHeight(16).withY(row.getCentreY() - 8));
  presetNextButton.setBounds(row.removeFromLeft(20).reduced(0, 3));
  row.removeFromLeft(pad);
  savePatchButton.setBounds(row.removeFromLeft(28).reduced(0, 3));
  row.removeFromLeft(pad / 2);
  loadPatchButton.setBounds(row.removeFromLeft(28).reduced(0, 3));
  row.removeFromLeft(pad);

  // Ext input (tucked before right edge)
  extInputEnableButton.setBounds(row.removeFromLeft(50).withHeight(20).withY(row.getCentreY() - 10));
  row.removeFromLeft(pad / 2);
  extInputLabel.setBounds(row.removeFromLeft(30).withHeight(16).withY(row.getCentreY() - 8));
  extInputLevelSlider.setBounds(row.removeFromLeft(70).withHeight(22).withY(row.getCentreY() - 11));
}

void BreadbinEditor::layoutSidPanels(juce::Rectangle<int> &bounds) {
  // OptionD SID towers: two equal columns with a 6px gap.
  // Each column top→bottom: header (SID label + chip combo), voice-enable row,
  //   FilterDisplay (~74px), Cutoff row, Reso row, bottom row (LP/BP/HP + Flt + Pan + Detune).
  const int pad = 4;
  const int gap = 6;
  const int sidWidth = (bounds.getWidth() - gap) / 2;

  // ----- LEFT SID -----
  auto leftPanel = bounds.removeFromLeft(sidWidth).reduced(8, 6);

  // Header row: SID label + chip selector
  auto leftHeader = leftPanel.removeFromTop(20);
  leftSIDLabel.setBounds(leftHeader.removeFromLeft(120));
  leftChipSelector.setBounds(leftHeader.removeFromLeft(150));

  leftPanel.removeFromTop(pad);

  // Voice enable + button row (3 voice pairs)
  auto leftVoicesRow = leftPanel.removeFromTop(24);
  for (int i = 0; i < 3; ++i) {
    leftVoiceEnables[i].setBounds(leftVoicesRow.removeFromLeft(20).reduced(0, 2));
    leftVoiceButtons[i].setBounds(leftVoicesRow.removeFromLeft(52).reduced(0, 1));
    if (i < 2) leftVoicesRow.removeFromLeft(pad);
  }

  leftPanel.removeFromTop(pad);

  // FilterDisplay — ~85% column width × 74px (centred; small inset keeps it from bleeding edge)
  {
    auto fdRow = leftPanel.removeFromTop(74);
    // Slim: match the cutoff/res controls' width (214) below, left-aligned (reveals the centre)
    filterDisplay_L.setBounds(fdRow.withWidth(214));
  }

  leftPanel.removeFromTop(pad);

  // Cutoff + Res: two usable rotary knobs side by side (label + knob + meter)
  {
    auto filterRow = leftPanel.removeFromTop(52);
    const int knob = 50;
    leftCutoffLabel.setBounds(filterRow.removeFromLeft(46).withHeight(16).withY(filterRow.getCentreY() - 8));
    leftCutoffSlider.setBounds(filterRow.removeFromLeft(knob).withHeight(knob).withY(filterRow.getCentreY() - knob / 2));
    cutoffMeterL.setBounds(filterRow.removeFromLeft(6).reduced(0, 6));
    filterRow.removeFromLeft(pad * 4);
    leftResonanceLabel.setBounds(filterRow.removeFromLeft(40).withHeight(16).withY(filterRow.getCentreY() - 8));
    leftResonanceSlider.setBounds(filterRow.removeFromLeft(knob).withHeight(knob).withY(filterRow.getCentreY() - knob / 2));
    resMeterL.setBounds(filterRow.removeFromLeft(6).reduced(0, 6));
  }

  leftPanel.removeFromTop(pad);

  // Bottom row: LP/BP/HP + Flt toggle | spacer | Pan slider + Detune slider
  auto leftBottomRow = leftPanel.removeFromTop(22);
  leftLPButton.setBounds(leftBottomRow.removeFromLeft(36).reduced(0, 1));
  leftBPButton.setBounds(leftBottomRow.removeFromLeft(36).reduced(0, 1));
  leftHPButton.setBounds(leftBottomRow.removeFromLeft(36).reduced(0, 1));
  leftFilterEnableButton.setBounds(leftBottomRow.removeFromLeft(38).reduced(0, 1));
  leftBottomRow.removeFromLeft(pad);
  leftPanLabel.setBounds(leftBottomRow.removeFromLeft(28).withHeight(14).withY(leftBottomRow.getCentreY() - 7));
  leftPanSlider.setBounds(leftBottomRow.removeFromLeft(60).withHeight(20).withY(leftBottomRow.getCentreY() - 10));
  leftBottomRow.removeFromLeft(pad);
  leftDetuneLabel.setBounds(leftBottomRow.removeFromLeft(36).withHeight(14).withY(leftBottomRow.getCentreY() - 7));
  leftDetuneSlider.setBounds(leftBottomRow.withHeight(20).withY(leftBottomRow.getCentreY() - 10));

  bounds.removeFromLeft(gap);

  // ----- RIGHT SID -----
  auto rightPanel = bounds.removeFromLeft(sidWidth).reduced(8, 6);

  // Header row: SID label + chip selector — right-justified to mirror the left tower
  // (label hugs the outer-right edge, combo just inside it).
  auto rightHeader = rightPanel.removeFromTop(20);
  rightSIDLabel.setBounds(rightHeader.removeFromRight(120));
  rightChipSelector.setBounds(rightHeader.removeFromRight(150));

  rightPanel.removeFromTop(pad);

  // Voice enable + button row (voices 4-6) — right-justified to mirror the left tower
  auto rightVoicesRow = rightPanel.removeFromTop(24);
  auto vrow = rightVoicesRow.removeFromRight(3 * (20 + 52) + 2 * pad);
  for (int i = 0; i < 3; ++i) {
    rightVoiceEnables[i].setBounds(vrow.removeFromLeft(20).reduced(0, 2));
    rightVoiceButtons[i].setBounds(vrow.removeFromLeft(52).reduced(0, 1));
    if (i < 2) vrow.removeFromLeft(pad);
  }

  rightPanel.removeFromTop(pad);

  // FilterDisplay — ~85% column width × 74px (centred; small inset keeps it from bleeding edge)
  {
    auto fdRow = rightPanel.removeFromTop(74);
    // Slim: match the cutoff/res controls' width (214) below, right-aligned (reveals the centre)
    filterDisplay_R.setBounds(fdRow.removeFromRight(214));
  }

  rightPanel.removeFromTop(pad);

  // Cutoff + Res: two usable rotary knobs side by side, right-justified (mirrors left tower)
  {
    auto filterRow = rightPanel.removeFromTop(52);
    auto group = filterRow.removeFromRight(214);
    const int knob = 50;
    rightCutoffLabel.setBounds(group.removeFromLeft(46).withHeight(16).withY(group.getCentreY() - 8));
    rightCutoffSlider.setBounds(group.removeFromLeft(knob).withHeight(knob).withY(group.getCentreY() - knob / 2));
    cutoffMeterR.setBounds(group.removeFromLeft(6).reduced(0, 6));
    group.removeFromLeft(pad * 4);
    rightResonanceLabel.setBounds(group.removeFromLeft(40).withHeight(16).withY(group.getCentreY() - 8));
    rightResonanceSlider.setBounds(group.removeFromLeft(knob).withHeight(knob).withY(group.getCentreY() - knob / 2));
    resMeterR.setBounds(group.removeFromLeft(6).reduced(0, 6));
  }

  rightPanel.removeFromTop(pad);

  // Bottom row: LP/BP/HP + Flt toggle | Pan | Detune
  auto rightBottomRow = rightPanel.removeFromTop(22);
  rightLPButton.setBounds(rightBottomRow.removeFromLeft(36).reduced(0, 1));
  rightBPButton.setBounds(rightBottomRow.removeFromLeft(36).reduced(0, 1));
  rightHPButton.setBounds(rightBottomRow.removeFromLeft(36).reduced(0, 1));
  rightFilterEnableButton.setBounds(rightBottomRow.removeFromLeft(38).reduced(0, 1));
  rightBottomRow.removeFromLeft(pad);
  rightPanLabel.setBounds(rightBottomRow.removeFromLeft(28).withHeight(14).withY(rightBottomRow.getCentreY() - 7));
  rightPanSlider.setBounds(rightBottomRow.removeFromLeft(60).withHeight(20).withY(rightBottomRow.getCentreY() - 10));
  rightBottomRow.removeFromLeft(pad);
  rightDetuneLabel.setBounds(rightBottomRow.removeFromLeft(36).withHeight(14).withY(rightBottomRow.getCentreY() - 7));
  rightDetuneSlider.setBounds(rightBottomRow.withHeight(20).withY(rightBottomRow.getCentreY() - 10));
}

void BreadbinEditor::layoutVoiceEditor(juce::Rectangle<int> &bounds) {
  // OptionD Voice Editor: receives voiceEditorPanelBounds directly.
  // Header: Voice Editor label + voice selector combo + wave selector + Ring/Sync/Flt toggles + voice preset save/load.
  // Body: 3 columns separated by thin dividers:
  //   [Pulse-Width, Glide, Mod-offset sliders] | [Amp ADSR] | [Filter Envelope: toggle + ADSR + Amount]
  const int pad = 4;
  const int colGap = 12;

  auto area = bounds.reduced(8, 6);

  // --- Header row ---
  auto header = area.removeFromTop(22);
  voiceEditorLabel.setBounds(header.removeFromLeft(100));
  header.removeFromLeft(pad);
  presetSelector.setBounds(header.removeFromLeft(120).withHeight(20).withY(header.getCentreY() - 10));
  header.removeFromLeft(pad);
  saveVoiceButton.setBounds(header.removeFromLeft(24).reduced(0, 1));
  header.removeFromLeft(pad / 2);
  loadVoiceButton.setBounds(header.removeFromLeft(24).reduced(0, 1));
  header.removeFromLeft(pad * 2);
  waveformLabel.setBounds(header.removeFromLeft(36).withHeight(14).withY(header.getCentreY() - 7));
  waveformSelector.setBounds(header.removeFromLeft(110).withHeight(20).withY(header.getCentreY() - 10));
  header.removeFromLeft(pad * 3);
  ringModButton.setBounds(header.removeFromLeft(48).reduced(0, 1));
  header.removeFromLeft(pad / 2);
  syncButton.setBounds(header.removeFromLeft(48).reduced(0, 1));
  header.removeFromLeft(pad / 2);
  voiceFilterButton.setBounds(header.removeFromLeft(40).reduced(0, 1));
  header.removeFromLeft(pad);
  presetLabel.setBounds(header.removeFromLeft(28).withHeight(14).withY(header.getCentreY() - 7));

  area.removeFromTop(pad);

  // --- Body: 3 columns ---
  // Divide remaining area into 3 columns
  const int bodyH = area.getHeight();
  const int totalColW = area.getWidth() - colGap * 2;
  // Col1 ~flex:1.4, col2 and col3 each ~flex:1
  const int col1W = (totalColW * 42) / 100; // ~42%
  const int col23W = (totalColW * 29) / 100; // ~29% each (remainder)

  auto col1 = area.removeFromLeft(col1W);
  area.removeFromLeft(colGap);
  auto col2 = area.removeFromLeft(col23W);
  area.removeFromLeft(colGap);
  auto col3 = area; // remainder

  // --- Col1: Pulse Width, Glide, Mod-offset sliders ---
  const int sliderRowH = 22;
  const int labelW = 54;

  auto pwRow = col1.removeFromTop(sliderRowH);
  pwLabel.setBounds(pwRow.removeFromLeft(labelW).withHeight(14).withY(pwRow.getCentreY() - 7));
  pwMeter.setBounds(pwRow.removeFromRight(6).reduced(0, 4));
  pitchMeter.setBounds(pwRow.removeFromRight(6).reduced(0, 4));
  pulseWidthSlider.setBounds(pwRow.withHeight(20).withY(pwRow.getCentreY() - 10));
  col1.removeFromTop(pad);

  auto glideRow = col1.removeFromTop(sliderRowH);
  glideTimeLabel.setBounds(glideRow.removeFromLeft(labelW).withHeight(14).withY(glideRow.getCentreY() - 7));
  glideTimeSlider.setBounds(glideRow.withHeight(20).withY(glideRow.getCentreY() - 10));
  col1.removeFromTop(pad);

  auto modRow = col1.removeFromTop(sliderRowH);
  modOffsetLabel.setBounds(modRow.removeFromLeft(labelW).withHeight(14).withY(modRow.getCentreY() - 7));
  modOffsetSlider.setBounds(modRow.withHeight(20).withY(modRow.getCentreY() - 10));

  // --- Col2: Amp ADSR (label + 4 vertical sliders) ---
  const int adsrLabelH = 14;
  const int adsrH = col2.getHeight() - adsrLabelH - pad;
  const int adsrW = col2.getWidth() / 4;
  int ax = col2.getX();
  int ay = col2.getY();

  attackLabel.setBounds(ax,             ay, adsrW, adsrLabelH);
  attackSlider.setBounds(ax,            ay + adsrLabelH + pad, adsrW, adsrH);
  decayLabel.setBounds(ax + adsrW,      ay, adsrW, adsrLabelH);
  decaySlider.setBounds(ax + adsrW,     ay + adsrLabelH + pad, adsrW, adsrH);
  sustainLabel.setBounds(ax + adsrW*2,  ay, adsrW, adsrLabelH);
  sustainSlider.setBounds(ax + adsrW*2, ay + adsrLabelH + pad, adsrW, adsrH);
  releaseLabel.setBounds(ax + adsrW*3,  ay, adsrW, adsrLabelH);
  releaseSlider.setBounds(ax + adsrW*3, ay + adsrLabelH + pad, adsrW, adsrH);

  // --- Col3: Filter Envelope (toggle + ADSR + Amount knob) ---
  // Toggle
  filterEnvEnableButton.setBounds(col3.removeFromTop(20));
  col3.removeFromTop(pad);

  // 4 ADSR sliders + amount knob side by side
  const int feH = col3.getHeight();
  const int feAdsrW = col3.getWidth() / 5; // 4 sliders + amount
  int fx = col3.getX();
  int fy = col3.getY();

  filterEnvAttackLabel.setBounds(fx,              fy, feAdsrW, adsrLabelH);
  filterEnvAttackSlider.setBounds(fx,             fy + adsrLabelH, feAdsrW, feH - adsrLabelH);
  filterEnvDecayLabel.setBounds(fx + feAdsrW,     fy, feAdsrW, adsrLabelH);
  filterEnvDecaySlider.setBounds(fx + feAdsrW,    fy + adsrLabelH, feAdsrW, feH - adsrLabelH);
  filterEnvSustainLabel.setBounds(fx + feAdsrW*2, fy, feAdsrW, adsrLabelH);
  filterEnvSustainSlider.setBounds(fx + feAdsrW*2,fy + adsrLabelH, feAdsrW, feH - adsrLabelH);
  filterEnvReleaseLabel.setBounds(fx + feAdsrW*3, fy, feAdsrW, adsrLabelH);
  filterEnvReleaseSlider.setBounds(fx + feAdsrW*3,fy + adsrLabelH, feAdsrW, feH - adsrLabelH);
  filterEnvAmountLabel.setBounds(fx + feAdsrW*4,  fy, feAdsrW, adsrLabelH);
  filterEnvAmountSlider.setBounds(fx + feAdsrW*4, fy + adsrLabelH, feAdsrW, feH - adsrLabelH);
}

void BreadbinEditor::layoutBottomControls(juce::Rectangle<int> fxArea,
                                          juce::Rectangle<int> dockArea) {
  // OptionD region 4 (FX chain) + region 5 (Dock).
  // Filter envelope controls are now in layoutVoiceEditor (col3).
  // SID overlay labels are positioned relative to their target controls.
  const int pad = 4;

  // SID Player register overlay positions (overlapping bottom of each control)
  auto overlayAt = [](juce::Label &lbl, juce::Component &target) {
    auto b = target.getBounds();
    lbl.setBounds(b.getX(), b.getBottom() - 14, b.getWidth(), 14);
  };
  overlayAt(sidOverlayWave, waveformSelector);
  overlayAt(sidOverlayPW, pulseWidthSlider);
  overlayAt(sidOverlayAttack, attackSlider);
  overlayAt(sidOverlayDecay, decaySlider);
  overlayAt(sidOverlaySustain, sustainSlider);
  overlayAt(sidOverlayRelease, releaseSlider);
  overlayAt(sidOverlayCutoff, leftCutoffSlider);
  overlayAt(sidOverlayRes, leftResonanceSlider);

  // ---- FX Chain panel ----
  // 3 rows: Chorus, Delay, Reverb — each a full-width horizontal strip
  auto fx = fxArea.reduced(8, 4);
  const int fxRowH = (fx.getHeight() - pad * 2) / 3;

  auto layoutFxRow = [&](juce::Rectangle<int> row,
                          juce::ToggleButton &enableBtn,
                          juce::Label &l1, juce::Slider &s1,
                          juce::Label &l2, juce::Slider &s2,
                          juce::Label &l3, juce::Slider &s3,
                          juce::Label *l4 = nullptr, juce::Slider *s4 = nullptr) {
    const int cH = row.getHeight();
    enableBtn.setBounds(row.removeFromLeft(68).withHeight(20).withY(row.getCentreY() - 10));
    row.removeFromLeft(pad);
    auto placeSlider = [&](juce::Label &lbl, juce::Slider &sl, int lw, int sw) {
      lbl.setBounds(row.removeFromLeft(lw).withHeight(13).withY(row.getCentreY() - 7));
      sl.setBounds(row.removeFromLeft(sw).withHeight(std::min(cH, 20)).withY(row.getCentreY() - 10));
      row.removeFromLeft(pad);
    };
    const int flexW = (row.getWidth() - (l4 ? 4 : 3) * (pad + 30)) / (l4 ? 4 : 3);
    placeSlider(l1, s1, 36, flexW);
    placeSlider(l2, s2, 36, flexW);
    placeSlider(l3, s3, 32, flexW);
    if (l4 && s4)
      placeSlider(*l4, *s4, 28, flexW);
  };

  auto chorusRow = fx.removeFromTop(fxRowH);
  layoutFxRow(chorusRow, chorusEnableButton,
              chorusRateLabel, chorusRateSlider,
              chorusDepthLabel, chorusDepthSlider,
              chorusMixLabel, chorusMixSlider);
  fx.removeFromTop(pad);

  auto delayRow = fx.removeFromTop(fxRowH);
  layoutFxRow(delayRow, delayEnableButton,
              delayTimeLLabel, delayTimeLSlider,
              delayTimeRLabel, delayTimeRSlider,
              delayFBLabel, delayFeedbackSlider,
              &delayMixLabel, &delayMixSlider);
  fx.removeFromTop(pad);

  auto reverbRow = fx;
  layoutFxRow(reverbRow, reverbEnableButton,
              reverbDecayLabel, reverbDecaySlider,
              reverbDampingLabel, reverbDampingSlider,
              reverbMixLabel, reverbMixSlider);

  // ---- Dock (2 tiers: enable toggles on top, arp/buttons/clock below) ----
  auto dock = dockArea.reduced(4, 2);
  auto togRow  = dock.removeFromTop(15);
  dock.removeFromTop(2);
  auto mainRow = dock; // ~25px
  auto centreV = [](juce::Rectangle<int> r, int h) {
    return r.withHeight(h).withY(r.getCentreY() - h / 2);
  };

  // -- Bottom tier: Arp (left) | popup buttons (centre) | Clock (right) --
  arpEnableButton.setBounds(centreV(mainRow.removeFromLeft(46), 20));
  mainRow.removeFromLeft(pad);
  arpPatternLabel.setText("Arp", juce::dontSendNotification);
  arpPatternLabel.setBounds(mainRow.removeFromLeft(28).withHeight(13).withY(mainRow.getCentreY() - 7));
  arpPatternSelector.setBounds(centreV(mainRow.removeFromLeft(86), 20));
  mainRow.removeFromLeft(pad);
  arpRateLabel.setBounds(mainRow.removeFromLeft(30).withHeight(13).withY(mainRow.getCentreY() - 7));
  arpRateSlider.setBounds(centreV(mainRow.removeFromLeft(84), 20));
  mainRow.removeFromLeft(pad);
  arpOctaveLabel.setText("Oct", juce::dontSendNotification);
  arpOctaveLabel.setBounds(mainRow.removeFromLeft(26).withHeight(13).withY(mainRow.getCentreY() - 7));
  arpOctaveSelector.setBounds(centreV(mainRow.removeFromLeft(56), 20));

  clockModeSelector.setBounds(centreV(mainRow.removeFromRight(65), 20));
  clockModeLabel.setBounds(mainRow.removeFromRight(36).withHeight(13).withY(mainRow.getCentreY() - 7));
  mainRow.removeFromRight(pad);

  const int btnH = 20;
  auto digiBtnBounds  = mainRow.removeFromRight(56); mainRow.removeFromRight(pad);
  auto sidBtnBounds   = mainRow.removeFromRight(75); mainRow.removeFromRight(pad);
  auto chordBtnBounds = mainRow.removeFromRight(62); mainRow.removeFromRight(pad);
  auto wtBtnBounds    = mainRow.removeFromRight(80); mainRow.removeFromRight(pad);
  auto modBtnBounds   = mainRow.removeFromRight(86);
  digiButton.setBounds(centreV(digiBtnBounds, btnH));
  sidPlayerButton.setBounds(centreV(sidBtnBounds, btnH));
  chordMemoryButton.setBounds(centreV(chordBtnBounds, btnH));
  wavetableButton.setBounds(centreV(wtBtnBounds, btnH));
  modMatrixButton.setBounds(centreV(modBtnBounds, btnH));

  // -- Top tier: enable toggles, evenly spaced across the right portion --
  {
    auto t = togRow.removeFromRight(juce::jmin(380, togRow.getWidth()));
    const int tw = t.getWidth() / 4;
    lfo1EnableToggle.setBounds(t.removeFromLeft(tw).withHeight(14));
    lfo2EnableToggle.setBounds(t.removeFromLeft(tw).withHeight(14));
    wtEnableToggle.setBounds(t.removeFromLeft(tw).withHeight(14));
    digiEnableToggle.setBounds(t.removeFromLeft(tw).withHeight(14));
  }
}

void BreadbinEditor::applyPreset(int presetId) {
  // Convert SIDEngine::Waveform to APVTS choice index
  auto waveToIndex = [](SIDEngine::Waveform w) -> float {
    switch (w) {
    case SIDEngine::Waveform::Triangle:
      return 0.0f;
    case SIDEngine::Waveform::Sawtooth:
      return 1.0f;
    case SIDEngine::Waveform::Pulse:
      return 2.0f;
    case SIDEngine::Waveform::Noise:
      return 3.0f;
    default:
      return 0.0f;
    }
  };

  auto configureVoice = [this, presetId,
                         &waveToIndex](int voice, SIDEngine::Waveform wave,
                                       int pw, int a, int d, int s, int r) {
    juce::String prefix = "v" + juce::String(voice) + "_";
    auto setParam = [&](const juce::String &id, float val) {
      auto *p = processor.apvts.getParameter(id);
      p->setValueNotifyingHost(p->convertTo0to1(val));
    };
    setParam(prefix + "waveform", waveToIndex(wave));
    setParam(prefix + "pw", static_cast<float>(pw));
    setParam(prefix + "attack", static_cast<float>(a));
    setParam(prefix + "decay", static_cast<float>(d));
    setParam(prefix + "sustain", static_cast<float>(s));
    setParam(prefix + "release", static_cast<float>(r));
    processor.getVoiceSettings(voice).presetId = presetId;
    processor.applyVoiceSettings(voice);
  };

  // Apply preset to selected voice only

  switch (presetId) {
  case 2: // Classic Lead (Monty) - Pulse with medium PWM
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4);
    break;
  case 3: // Fat Bass (Ocean) - Sawtooth with slow attack
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12,
                   3);
    break;
  case 4: // PWM Pad (Hubbard) - Pulse with slow attack/release
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1024, 8, 6, 12,
                   8);
    break;
  case 5: // Noise Snare - Noise with fast decay
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 0, 8, 0, 4);
    break;
  case 6: // Retro Triangle - Soft triangle with slow attack
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 2, 4, 10,
                   6);
    break;
  case 7: // Thin Pulse - Narrow PW, bright nasal tone
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 512, 0, 0, 15, 2);
    break;
  case 8: // Wide Pulse - Wide PW, hollow/woody
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 3584, 0, 3, 12,
                   4);
    break;
  case 9: // Pluck Bass - Fast decay, no sustain
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 2048, 0, 8, 0, 2);
    break;
  case 10: // Sub Bass - Pure triangle, organ-like
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 0, 0, 15,
                   3);
    break;
  case 11: // Saw Lead - Raw sawtooth, full sustain
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 0, 0, 0, 15,
                   3);
    break;
  case 12: // Staccato Saw - Short punchy saw stab
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 0, 0, 6, 0, 0);
    break;
  case 13: // Soft Pad - Slow swell triangle
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 10, 4, 12,
                   10);
    break;
  case 14: // Bright Pad - Slow attack sawtooth
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 0, 6, 4, 10,
                   8);
    break;
  case 15: // Organ - Square wave, instant on/off
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15,
                   0);
    break;
  case 16: // Clavinet - Percussive pluck
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1536, 0, 10, 4,
                   2);
    break;
  case 17: // Hi-Hat - Short noise burst
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 0, 4, 0, 0);
    break;
  case 18: // Kick Thump - Low triangle thump
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 0, 6, 0, 0);
    break;
  case 19: // White Noise - Sustained noise
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 0, 0, 15, 4);
    break;
  case 20: // Zap - Fast saw decay
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 0, 0, 12, 0,
                   0);
    break;
  case 21: // Commando Pluck - Hubbard's iconic plucky pulse
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1536, 0, 6, 0, 6);
    break;
  case 22: // Punchy Saw - Fast decay saw bass hit
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 9, 0,
                   0);
    break;
  case 23: // Buzz Saw - Ultra-short buzzy saw
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 2, 6,
                   1);
    break;
  case 24: // Driving Saw - Medium decay driving saw
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 10,
                   2);
    break;
  case 25: // Wide Lead - Hubbard's wide clean pulse
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 2400, 0, 4, 12,
                   3);
    break;
  case 26: // Gentle Triangle - Galway's soft triangle
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 4, 4, 14,
                   8);
    break;
  case 27: // Short Pluck - Tel's short pulse pluck
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1600, 0, 4, 0, 3);
    break;
  case 28: // Punch Bass - Daglish's punchy saw bass
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 2048, 0, 6, 0,
                   0);
    break;
  case 29: // Bell Triangle - Daglish's metallic ring mod bell
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 0, 6, 0, 8);
    {
      juce::String rp = "v" + juce::String(selectedVoice) + "_ringMod";
      auto *ringParam = processor.apvts.getParameter(rp);
      if (ringParam)
        ringParam->setValueNotifyingHost(ringParam->convertTo0to1(1.0f));
    }
    break;
  case 30: // Delta Sustain - Hubbard's sustained narrow pulse
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1200, 0, 0, 15,
                   6);
    break;
  case 31: // Brass Saw - Medium attack saw for brass stabs
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 0, 2, 4, 12,
                   4);
    break;
  case 32: // String Ensemble - Slow attack saw for string sections
    configureVoice(selectedVoice, SIDEngine::Waveform::Sawtooth, 0, 8, 4, 12,
                   8);
    break;
  case 33: // Electric Piano - Bell-like pulse decay
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 1800, 0, 8, 6, 4);
    break;
  case 34: // Harpsichord - Very fast thin pulse pluck
    configureVoice(selectedVoice, SIDEngine::Waveform::Pulse, 768, 0, 3, 0, 2);
    break;
  case 35: // Snare Roll - Medium noise decay
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 0, 6, 2, 3);
    break;
  case 36: // Tom - Melodic triangle percussion
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 0, 8, 0, 3);
    break;
  case 37: // Ambient Swell - Max attack triangle drone
    configureVoice(selectedVoice, SIDEngine::Waveform::Triangle, 0, 15, 0, 15,
                   15);
    break;
  case 38: // Rising Noise - Slow attack noise for FX risers
    configureVoice(selectedVoice, SIDEngine::Waveform::Noise, 0, 12, 4, 6, 8);
    break;
  }

  loadVoiceToUI(selectedVoice);
}

void BreadbinEditor::applyGlobalPreset(int presetId) {
  // APVTS helper: set any parameter by ID and denormalized value
  auto setParam = [this](const juce::String &id, float val) {
    auto *p = processor.apvts.getParameter(id);
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(val));
  };

  // Convert SIDEngine::Waveform to APVTS choice index
  auto waveToIndex = [](SIDEngine::Waveform w) -> float {
    switch (w) {
    case SIDEngine::Waveform::Triangle:
      return 0.0f;
    case SIDEngine::Waveform::Sawtooth:
      return 1.0f;
    case SIDEngine::Waveform::Pulse:
      return 2.0f;
    case SIDEngine::Waveform::Noise:
      return 3.0f;
    default:
      return 0.0f;
    }
  };

  // ---- FULL STATE RESET ----
  // Every global preset defines the complete plugin state.
  // Reset all params to factory defaults first, then each preset overrides.

  // Per-voice defaults
  for (int v = 0; v < 6; ++v) {
    juce::String vp = "v" + juce::String(v) + "_";
    setParam(vp + "enable", 1.0f);
    setParam(vp + "waveform", 2.0f); // Pulse
    setParam(vp + "pw", 2048.0f);
    setParam(vp + "attack", 0.0f);
    setParam(vp + "decay", 0.0f);
    setParam(vp + "sustain", 15.0f);
    setParam(vp + "release", 0.0f);
    setParam(vp + "ringMod", 0.0f);
    setParam(vp + "sync", 0.0f);
    setParam(vp + "modOffset", 7.0f); // Default: perfect fifth
    setParam(vp + "filter", 1.0f);
    processor.getVoiceSettings(v).presetId = 1; // "-- Select --"
  }

  setParam("noiseGateThreshold", 0.02f);
  setParam("gateAttack", 1.0f);
  setParam("gateRelease", 50.0f);
  setParam("gateHold", 10.0f);

  // Dual mode: StereoSplit (0)
  setParam("dualMode", 0.0f);

  // Detune: centered
  setParam("leftDetune", 0.0f);
  setParam("rightDetune", 0.0f);

  // Glide: off
  setParam("glide", 0.0f);

  // Clock: PAL
  setParam("clockMode", 0.0f);

  // Pan: hard left/right
  setParam("leftPan", -1.0f);
  setParam("rightPan", 1.0f);

  // Filter cutoff/resonance (non-APVTS, set on SID directly)
  processor.getLeftSID().setFilterCutoff(1024);
  processor.getLeftSID().setFilterResonance(0);
  processor.getRightSID().setFilterCutoff(1024);
  processor.getRightSID().setFilterResonance(0);
  processor.setBaseFilterCutoff(true, 1024);
  processor.setBaseFilterCutoff(false, 1024);
  processor.setBaseFilterResonance(true, 0);
  processor.setBaseFilterResonance(false, 0);
  leftCutoffSlider.setValue(1024.0, juce::dontSendNotification);
  leftResonanceSlider.setValue(0.0, juce::dontSendNotification);
  rightCutoffSlider.setValue(1024.0, juce::dontSendNotification);
  rightResonanceSlider.setValue(0.0, juce::dontSendNotification);

  // Arpeggiator: off, defaults
  setParam("arpEnable", 0.0f);
  setParam("arpPattern", 0.0f); // Up
  setParam("arpRate", 50.0f);
  setParam("arpOctaves", 1.0f);

  // LFO1: off, defaults
  setParam("lfoEnable", 0.0f);
  setParam("lfoWave", 0.0f); // Triangle
  setParam("lfoRate", 1.0f);
  setParam("lfoDepthFilt", 0.0f);
  setParam("lfoDepthPW", 0.0f);
  setParam("lfoDepthPitch", 0.0f);

  // LFO2: off, defaults
  setParam("lfo2Enable", 0.0f);
  setParam("lfo2Wave", 0.0f); // Triangle
  setParam("lfo2Rate", 3.0f);
  setParam("lfo2DepthFilt", 0.0f);
  setParam("lfo2DepthPW", 0.0f);
  setParam("lfo2DepthPitch", 0.0f);

  // Filter Envelope: off, defaults
  setParam("filterEnvEnable", 0.0f);
  setParam("filterEnvAttack", 0.01f);
  setParam("filterEnvDecay", 0.3f);
  setParam("filterEnvSustain", 0.5f);
  setParam("filterEnvRelease", 0.5f);
  setParam("filterEnvAmount", 0.5f);

  // Chorus: off, defaults
  setParam("chorusEnable", 0.0f);
  setParam("chorusRate", 1.5f);
  setParam("chorusDepth", 0.3f);
  setParam("chorusMix", 0.5f);

  // Delay: off, defaults
  setParam("delayEnable", 0.0f);
  setParam("delayTimeL", 375.0f);
  setParam("delayTimeR", 500.0f);
  setParam("delayFeedback", 0.3f);
  setParam("delayMix", 0.3f);

  // Reverb: off, defaults
  setParam("reverbEnable", 0.0f);
  setParam("reverbDecay", 0.7f);
  setParam("reverbDamping", 10000.0f);
  setParam("reverbMix", 0.3f);

  // Wavetable: off, defaults
  setParam("wtEnable", 0.0f);
  setParam("wtNumSteps", 4.0f);
  setParam("wtRate", 50.0f);
  setParam("wtLoop", 1.0f);
  for (int i = 0; i < 16; ++i) {
    auto wp = "wt_s" + juce::String(i) + "_";
    setParam(wp + "wave", 2.0f); // Pulse
    setParam(wp + "pitch", 0.0f);
    setParam(wp + "pw", 2048.0f);
  }

  // Mod Matrix: all slots cleared
  for (int i = 0; i < 4; ++i) {
    auto mp = "mod" + juce::String(i) + "_";
    setParam(mp + "enable", 1.0f);
    setParam(mp + "src", 0.0f); // None
    setParam(mp + "dst", 0.0f); // None
    setParam(mp + "amt", 0.0f);
  }

  // Pitch Bend Range: reset to default
  setParam("pitchBendRange", 2.0f);

  // PWM Sweep: reset to default
  setParam("pwmSweepEnable", 0.0f);
  setParam("pwmSweepRate", 0.5f);
  setParam("pwmSweepDepth", 0.0f);

  // Chord Memory: off, all intervals = 0
  setParam("chordEnable", 0.0f);
  setParam("chordSlot", 0.0f);
  for (int s = 0; s < 4; ++s)
    for (int i = 0; i < 5; ++i)
      setParam("chord_s" + juce::String(s) + "_i" + juce::String(i), 0.0f);

  // Digi Sampler: off, defaults
  setParam("digiEnable", 0.0f);
  setParam("digiRootNote", 60.0f);
  setParam("digiLoop", 0.0f);
  setParam("digiBitDepth", 0.0f);
  processor.getDigiSampler().unload();

  // Poly mode: off, default max notes
  setParam("voiceMode", 0.0f);
  setParam("polyMaxNotes", 4.0f);
  setParam("paraSpread", 0.0f);
  setParam("paraFilterRetrig", 1.0f);

  // Note: masterVol, chipLeft/Right, extInput left unchanged (user preference)

  // ---- VOICE CONFIGURATION HELPER ----
  // Sets voice waveform/ADSR via APVTS, assigns voice preset ID, syncs engine
  auto configVoice = [this, &setParam,
                      &waveToIndex](int v, SIDEngine::Waveform wave, int pw,
                                    int a, int d, int s, int r, int vpId = 1) {
    juce::String vp = "v" + juce::String(v) + "_";
    setParam(vp + "enable", 1.0f);
    setParam(vp + "waveform", waveToIndex(wave));
    setParam(vp + "pw", static_cast<float>(pw));
    setParam(vp + "attack", static_cast<float>(a));
    setParam(vp + "decay", static_cast<float>(d));
    setParam(vp + "sustain", static_cast<float>(s));
    setParam(vp + "release", static_cast<float>(r));
    processor.getVoiceSettings(v).presetId = vpId;
    processor.applyVoiceSettings(v);
  };

  // Helper: set filter cutoff/resonance on both SIDs + base values + sliders
  auto setFilters = [this](int cutoff, int resonance) {
    processor.getLeftSID().setFilterCutoff(cutoff);
    processor.getLeftSID().setFilterResonance(resonance);
    processor.getRightSID().setFilterCutoff(cutoff);
    processor.getRightSID().setFilterResonance(resonance);
    processor.setBaseFilterCutoff(true, cutoff);
    processor.setBaseFilterCutoff(false, cutoff);
    processor.setBaseFilterResonance(true, resonance);
    processor.setBaseFilterResonance(false, resonance);
    leftCutoffSlider.setValue(static_cast<double>(cutoff),
                              juce::dontSendNotification);
    leftResonanceSlider.setValue(static_cast<double>(resonance),
                                 juce::dontSendNotification);
    rightCutoffSlider.setValue(static_cast<double>(cutoff),
                               juce::dontSendNotification);
    rightResonanceSlider.setValue(static_cast<double>(resonance),
                                  juce::dontSendNotification);
  };

  // ---- PER-PRESET OVERRIDES ----
  // Only set what differs from the defaults above.

  switch (presetId) {
  case 1: // Dual Lead - Fat Bass saw + Classic Lead pulse, detuned
    setParam("voiceMode", 2.0f);
    configVoice(0, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    configVoice(1, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(2, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(3, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    configVoice(4, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(5, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    setParam("leftDetune", -5.0f);
    setParam("rightDetune", 5.0f);
    setFilters(1800, 4);
    // Light chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.15f);
    setParam("chorusMix", 0.25f);
    // Gentle LFO1 on filter for movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoRate", 0.4f);
    setParam("lfoDepthFilt", 0.15f);
    break;

  case 2: // Pad Stack - PWM Pad on all voices, lush FX
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1024, 8, 6, 12, 8, 4);
    setParam("leftDetune", -8.0f);
    setParam("rightDetune", 8.0f);
    setFilters(800, 6);
    // Chorus for lushness
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.35f);
    setParam("chorusMix", 0.4f);
    // Slow PWM sweep for evolving texture
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.3f);
    setParam("pwmSweepDepth", 0.4f);
    // Filter envelope: slow swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 2.0f);
    setParam("filterEnvDecay", 1.5f);
    setParam("filterEnvSustain", 0.7f);
    setParam("filterEnvRelease", 3.0f);
    setParam("filterEnvAmount", 0.4f);
    // Reverb for lush depth
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.75f);
    setParam("reverbDamping", 8000.0f);
    setParam("reverbMix", 0.25f);
    break;

  case 3: // Arpeggiated - Classic Lead with arp + echo
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    setParam("arpEnable", 1.0f);
    setParam("arpRate", 8.0f);
    setParam("arpOctaves", 2.0f);
    // Stereo delay for rhythmic echo
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 250.0f);
    setParam("delayTimeR", 375.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.3f);
    // Filter envelope for plucky attack
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.15f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.3f);
    setParam("filterEnvAmount", 0.6f);
    setFilters(600, 4);
    break;

  case 4: // Fat Unison - Fat Bass, PWM sweep + mod matrix
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    setParam("dualMode", 1.0f); // Unison
    setParam("leftDetune", -12.0f);
    setParam("rightDetune", 12.0f);
    setFilters(1200, 3);
    // PWM sweep for subtle movement
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.6f);
    setParam("pwmSweepDepth", 0.25f);
    // Mod matrix: LFO1 -> Resonance for growl
    setParam("lfoEnable", 1.0f);
    setParam("lfoRate", 0.3f);
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(1.0f)); // LFO1
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(4.0f)); // Resonance
      setParam("mod0_amt", 0.3f);
    }
    break;

  case 5: // Retro Synth - Mixed voices, vibrato + chorus
    setParam("voiceMode", 2.0f);
    configVoice(0, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6, 6);
    configVoice(1, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(2, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    configVoice(3, SIDEngine::Waveform::Triangle, 0, 2, 4, 10, 6, 6);
    configVoice(4, SIDEngine::Waveform::Pulse, 2048, 0, 6, 8, 4, 2);
    configVoice(5, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 12, 3, 3);
    // LFO2 for subtle pitch vibrato
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 5.0f);
    setParam("lfo2DepthPitch", 0.08f);
    // Light chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.3f);
    setParam("pitchBendRange", 7.0f);
    break;

  case 6: { // Chord Stab - Chord memory + delay + filter env stab
    setParam("voiceMode", 3.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1800, 0, 3, 10, 2, 2);
    setFilters(1400, 5);
    // Chord memory: major 7th voicing (+4, +7, +11)
    setParam("chordEnable", 1.0f);
    setParam("chordSlot", 0.0f);
    setParam("chord_s0_i0", 4.0f); // major 3rd
    setParam("chord_s0_i1", 7.0f); // perfect 5th
    // Slot 1: minor chord (-3, -7)
    setParam("chord_s1_i0", 3.0f); // minor 3rd
    setParam("chord_s1_i1", 7.0f); // perfect 5th
    // Slot 2: sus4 (+5, +7)
    setParam("chord_s2_i0", 5.0f); // perfect 4th
    setParam("chord_s2_i1", 7.0f); // perfect 5th
    // Slot 3: power chord (+7, +12)
    setParam("chord_s3_i0", 7.0f);  // perfect 5th
    setParam("chord_s3_i1", 12.0f); // octave
    // Punchy filter envelope
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.12f);
    setParam("filterEnvSustain", 0.1f);
    setParam("filterEnvRelease", 0.2f);
    setParam("filterEnvAmount", 0.7f);
    // Stereo delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 330.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.35f);
    // Chorus for width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.5f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    break;
  }

  case 7: { // Mod Madness - Everything cranked: both LFOs, mod matrix, WT, FX
    setParam("voiceMode", 3.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 6, 10, 6, 2);
    setParam("leftDetune", -10.0f);
    setParam("rightDetune", 10.0f);
    setFilters(1000, 6);
    // LFO1: S&H on filter for glitchy movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 3.0f); // S&H
    setParam("lfoRate", 6.0f);
    setParam("lfoDepthFilt", 0.5f);
    // LFO2: Triangle on pitch for wobble
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f); // Triangle
    setParam("lfo2Rate", 1.5f);
    setParam("lfo2DepthPitch", 0.15f);
    // PWM sweep: fast and deep
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 2.5f);
    setParam("pwmSweepDepth", 0.7f);
    // Filter envelope: sharp attack, slow release
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.5f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 2.0f);
    setParam("filterEnvAmount", 0.6f);
    // Mod matrix: LFO2->PW, FilterEnv->Resonance
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(2.0f)); // LFO2
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(2.0f)); // PW
      setParam("mod0_amt", 0.6f);
      auto *s1src = processor.apvts.getParameter("mod1_src");
      auto *s1dst = processor.apvts.getParameter("mod1_dst");
      if (s1src)
        s1src->setValueNotifyingHost(s1src->convertTo0to1(3.0f)); // FilterEnv
      if (s1dst)
        s1dst->setValueNotifyingHost(s1dst->convertTo0to1(4.0f)); // Resonance
      setParam("mod1_amt", 0.5f);
    }
    // Wavetable: 4-step cycling through waveforms
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 4.0f);
    setParam("wtRate", 12.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);  // Pulse, root
      setWTStep(1, 1.0f, 0.0f, 2048.0f);  // Saw, root
      setWTStep(2, 2.0f, 12.0f, 1024.0f); // Pulse, +octave, narrow PW
      setWTStep(3, 0.0f, 7.0f, 2048.0f);  // Triangle, +fifth
    }
    // Both FX
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 2.0f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.35f);
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 200.0f);
    setParam("delayTimeR", 300.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.3f);
    setParam("pitchBendRange", 12.0f);
    break;
  }

  case 8: { // WT Arpeggio - Classic C64 wavetable arpeggio (Rob Hubbard
            // technique)
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 4, 2);
    setFilters(1200, 4);
    // Wavetable: 3-step major chord arpeggio at 50Hz
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 3.0f);
    setParam("wtRate", 50.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f); // Pulse, root
      setWTStep(1, 2.0f, 4.0f, 2048.0f); // Pulse, major 3rd
      setWTStep(2, 2.0f, 7.0f, 2048.0f); // Pulse, 5th
    }
    // Light chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    // Subtle filter envelope pluck
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.2f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 0.4f);
    setParam("filterEnvAmount", 0.5f);
    break;
  }

  case 9: { // WT Morph - Timbral morphing through waveforms and PW
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 2, 4, 12, 6, 2);
    setFilters(1400, 3);
    // Wavetable: 8-step timbral sequence
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 8.0f);
    setParam("wtRate", 8.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 3500.0f);   // Pulse, thin
      setWTStep(1, 1.0f, 0.0f, 2048.0f);   // Saw
      setWTStep(2, 2.0f, 0.0f, 1024.0f);   // Pulse, narrow
      setWTStep(3, 0.0f, 12.0f, 2048.0f);  // Triangle, octave up
      setWTStep(4, 2.0f, 0.0f, 2800.0f);   // Pulse, medium-thin
      setWTStep(5, 1.0f, -12.0f, 2048.0f); // Saw, octave down
      setWTStep(6, 3.0f, 0.0f, 2048.0f);   // Noise, percussive hit
      setWTStep(7, 2.0f, 7.0f, 1500.0f);   // Pulse, 5th, medium PW
    }
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 250.0f);
    setParam("delayTimeR", 375.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.35f);
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.3f);
    // LFO1: slow filter sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f); // Triangle
    setParam("lfoRate", 0.3f);
    setParam("lfoDepthFilt", 0.3f);
    break;
  }

    // ---- ERA-ACCURATE C64 PRESETS ----
    // Pure SID sound, no modern FX. Authentic register values from iconic
    // tunes.

  case 10: { // Commando - Rob Hubbard's iconic pulse lead
    // Hubbard signature: narrow pulse, fast pluck envelope, LP filter
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1536, 0, 6, 0, 6, 21);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(900, 7);
    // No FX, no LFO, no detune - pure C64
    break;
  }

  case 11: { // Ninja Bass - Punchy saw bass (Ben Daglish / Rob Hubbard style)
    // Classic C64 bass: sawtooth, instant attack, fast decay, no sustain
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 9, 0, 0, 22);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(350, 4);
    // No FX - raw SID punch
    break;
  }

  case 12: { // Ocean Loader - Martin Galway's clean sustained pulse lead
    // Galway signature: clean pulse, full sustain, gentle release
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 4, 2);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1200, 2);
    // No FX - authentic Galway clarity
    break;
  }

    // ---- MODERN STEREO PRESETS ----
    // Inspired by C64 techniques but enhanced with our extended features.

  case 13: { // Hubbard Arp - Classic WT arpeggio with stereo treatment
    // Hubbard technique: wavetable chord arpeggiation at frame rate
    // Enhanced with stereo delay, detune, and per-SID pan
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1536, 0, 0, 15, 3, 2);
    setFilters(1000, 5);
    setParam("leftDetune", -4.0f);
    setParam("rightDetune", 4.0f);
    // Wavetable: minor triad arp at 50Hz (frame rate speed)
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 4.0f);
    setParam("wtRate", 50.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 1536.0f);  // Root
      setWTStep(1, 2.0f, 3.0f, 1536.0f);  // Minor 3rd
      setWTStep(2, 2.0f, 7.0f, 1536.0f);  // 5th
      setWTStep(3, 2.0f, 12.0f, 1536.0f); // Octave
    }
    // Stereo delay: ping-pong feel
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 125.0f);
    setParam("delayTimeR", 187.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.25f);
    // Subtle filter envelope for pluck definition
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.1f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.3f);
    setParam("filterEnvAmount", 0.4f);
    break;
  }

  case 14: { // Galway Sweep - Filter sweeps with LFO + filter env
    // Galway was known for elaborate filter sweeps; we add LFO and mod matrix
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 2, 6, 10, 6, 2);
    setFilters(400, 9);
    setParam("leftDetune", -6.0f);
    setParam("rightDetune", 6.0f);
    // PWM sweep for evolving pulse width
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.4f);
    setParam("pwmSweepDepth", 0.35f);
    // Filter envelope: slow dramatic sweep
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 1.5f);
    setParam("filterEnvDecay", 2.0f);
    setParam("filterEnvSustain", 0.4f);
    setParam("filterEnvRelease", 2.5f);
    setParam("filterEnvAmount", 0.7f);
    // LFO1: slow triangle on filter for additional movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.25f);
    setParam("lfoDepthFilt", 0.2f);
    // Mod matrix: Velocity -> Filter amount for expressive playing
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.5f);
    }
    // Chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    break;
  }

  case 15: { // SID Brass - Hard sync brass with chorus and filter env
    setParam("voiceMode", 2.0f);
    // Hard sync gives harmonically rich brass-like timbre
    // Voices 0,3: Brass Saw voice for timbral variety; others: sync lead
    for (int v = 0; v < 6; ++v) {
      int vpId = (v == 0 || v == 3) ? 31 : 3;
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 2, 4, 12, 4, vpId);
      setParam("v" + juce::String(v) + "_sync", 1.0f);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1400, 5);
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    // Filter envelope: brass attack burst
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.01f);
    setParam("filterEnvDecay", 0.3f);
    setParam("filterEnvSustain", 0.5f);
    setParam("filterEnvRelease", 0.6f);
    setParam("filterEnvAmount", 0.5f);
    // LFO2: subtle vibrato (classic brass technique)
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 5.5f);
    setParam("lfo2DepthPitch", 0.06f);
    // Chorus for stereo spread
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.3f);
    // Pitch bend range: wide for expressive brass
    setParam("pitchBendRange", 5.0f);
    break;
  }

    // ---- MORE CLASSIC C64 ----

  case 16: { // Cybernoid - Jeroen Tel aggressive saw lead
    // Tel signature: raw sawtooth, punchy envelope, moderate filter
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 4, 8, 2, 3);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1100, 6);
    // No FX - pure Tel aggression
    break;
  }

  case 17: { // Wizball - Martin Galway triangle + pulse mix
    // Galway mixed waveforms across voices for richer timbre
    // Left SID: triangle voices, Right SID: pulse voices
    configVoice(0, SIDEngine::Waveform::Triangle, 0, 0, 2, 12, 6, 6);
    configVoice(1, SIDEngine::Waveform::Pulse, 2048, 0, 4, 10, 4, 2);
    configVoice(2, SIDEngine::Waveform::Triangle, 0, 0, 2, 12, 6, 6);
    configVoice(3, SIDEngine::Waveform::Pulse, 2048, 0, 4, 10, 4, 2);
    configVoice(4, SIDEngine::Waveform::Triangle, 0, 0, 2, 12, 6, 6);
    configVoice(5, SIDEngine::Waveform::Pulse, 2048, 0, 4, 10, 4, 2);
    for (int v = 0; v < 6; ++v)
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    setFilters(1000, 3);
    // No FX - authentic Galway layering
    break;
  }

  case 18: { // Thing Bounce - Bouncy short pulse (Jeroen Tel / game SFX style)
    // Short punchy notes, great for chiptune melodies
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1800, 0, 3, 0, 2, 2);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1400, 5);
    // No FX - tight C64 pluck
    break;
  }

    // ---- MORE LEADS ----

  case 19: { // Sync Lead - Hard sync sawtooth, biting harmonics
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 10, 3, 3);
      setParam("v" + juce::String(v) + "_sync", 1.0f);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1600, 4);
    setParam("leftDetune", -4.0f);
    setParam("rightDetune", 4.0f);
    // Filter envelope: sharp bite on attack
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.2f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 0.4f);
    setParam("filterEnvAmount", 0.5f);
    // Chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    break;
  }

  case 20: { // Acid Squelch - Resonant saw with filter env pluck
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 0, 15, 2, 3);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(200, 12); // Low cutoff, high resonance for squelch
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    // Filter envelope: fast pluck sweep
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.15f);
    setParam("filterEnvSustain", 0.05f);
    setParam("filterEnvRelease", 0.2f);
    setParam("filterEnvAmount", 0.85f);
    // Mod matrix: Velocity -> Filter for dynamics
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.4f);
    }
    // Stereo delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 166.0f);
    setParam("delayTimeR", 250.0f);
    setParam("delayFeedback", 0.3f);
    setParam("delayMix", 0.2f);
    setParam("glide", 80.0f); // Slight portamento for acid feel
    break;
  }

    // ---- BASS ----

  case 21: { // Sub Bass - Deep triangle, minimal filtering
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 0, 6, 12, 4, 10);
      setParam("v" + juce::String(v) + "_filter",
               0.0f); // No filter for clean sub
    }
    setFilters(600, 0);
    setParam("dualMode", 1.0f); // Unison for mono-compatible sub
    setParam("leftPan", 0.0f);  // Center both SIDs for mono sub
    setParam("rightPan", 0.0f);
    break;
  }

  case 22: { // Growl Bass - Triangle ring mod + filter env attack
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 0, 5, 6, 2, 2);
      setParam("v" + juce::String(v) + "_ringMod", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset",
               3.0f); // Minor 3rd for growl
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(500, 8);
    setParam("leftDetune", -7.0f);
    setParam("rightDetune", 7.0f);
    // Filter envelope: aggressive growl
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.25f);
    setParam("filterEnvSustain", 0.15f);
    setParam("filterEnvRelease", 0.3f);
    setParam("filterEnvAmount", 0.65f);
    // LFO1: slow saw on filter for movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 1.0f); // Sawtooth
    setParam("lfoRate", 0.5f);
    setParam("lfoDepthFilt", 0.15f);
    break;
  }

    // ---- PADS & KEYS ----

  case 23: { // Ice Pad - Triangle voices, slow attack, spacious FX
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 10, 4, 14, 10, 6);
    setFilters(700, 4);
    setParam("leftDetune", -10.0f);
    setParam("rightDetune", 10.0f);
    // Chorus for shimmer
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.6f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.4f);
    // Long stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 400.0f);
    setParam("delayTimeR", 600.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.35f);
    // Slow LFO on filter for glacial sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f); // Triangle
    setParam("lfoRate", 0.15f);
    setParam("lfoDepthFilt", 0.25f);
    // Filter env: very slow swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 3.0f);
    setParam("filterEnvDecay", 2.0f);
    setParam("filterEnvSustain", 0.6f);
    setParam("filterEnvRelease", 4.0f);
    setParam("filterEnvAmount", 0.3f);
    // Reverb for icy spaciousness
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.85f);
    setParam("reverbDamping", 6000.0f);
    setParam("reverbMix", 0.35f);
    break;
  }

  case 24: { // PWM Strings - Slow attack pulse with PWM sweep, lush
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 6, 4, 13, 8, 2);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(900, 3);
    setParam("leftDetune", -8.0f);
    setParam("rightDetune", 8.0f);
    // PWM sweep: the core of the string sound
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.35f);
    setParam("pwmSweepDepth", 0.5f);
    // LFO2: gentle pitch vibrato (string-like)
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 4.5f);
    setParam("lfo2DepthPitch", 0.05f);
    // Chorus for ensemble width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.9f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    // Reverb for string hall
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.8f);
    setParam("reverbDamping", 7000.0f);
    setParam("reverbMix", 0.3f);
    break;
  }

    // ---- ARPS & SEQUENCES ----

  case 25: { // Chip Sequence - 8-step WT with mixed waveforms and pitch jumps
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 3, 2);
    setFilters(1100, 4);
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    // Wavetable: 8-step melodic/timbral sequence
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 8.0f);
    setParam("wtRate", 25.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);  // Pulse, root
      setWTStep(1, 1.0f, 7.0f, 2048.0f);  // Saw, 5th
      setWTStep(2, 2.0f, 12.0f, 1536.0f); // Pulse, octave, narrow
      setWTStep(3, 0.0f, 0.0f, 2048.0f);  // Triangle, root
      setWTStep(4, 2.0f, 4.0f, 2800.0f);  // Pulse, 3rd, thin
      setWTStep(5, 1.0f, -5.0f, 2048.0f); // Saw, 4th below
      setWTStep(6, 2.0f, 7.0f, 1024.0f);  // Pulse, 5th, very narrow
      setWTStep(7, 3.0f, 12.0f, 2048.0f); // Noise hit, octave
    }
    // Stereo delay for rhythmic interest
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 150.0f);
    setParam("delayTimeR", 225.0f);
    setParam("delayFeedback", 0.3f);
    setParam("delayMix", 0.25f);
    // Filter env for pluck on each step
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.08f);
    setParam("filterEnvSustain", 0.15f);
    setParam("filterEnvRelease", 0.2f);
    setParam("filterEnvAmount", 0.45f);
    break;
  }

    // ---- FX & MODULATION ----

  case 26: { // S&H Glitch - Sample & hold chaos on everything
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 2, 8, 4, 2);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(800, 7);
    setParam("leftDetune", -15.0f);
    setParam("rightDetune", 15.0f);
    // LFO1: S&H on filter (random stepping)
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 3.0f); // Sample & Hold
    setParam("lfoRate", 8.0f);
    setParam("lfoDepthFilt", 0.6f);
    // LFO2: S&H on PW (random width changes)
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 3.0f); // Sample & Hold
    setParam("lfo2Rate", 12.0f);
    setParam("lfo2DepthPW", 0.5f);
    // Mod matrix: LFO1->Pitch (random pitch wobble), LFO2->Resonance
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(1.0f)); // LFO1
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(3.0f)); // Pitch
      setParam("mod0_amt", 0.15f);
      auto *s1src = processor.apvts.getParameter("mod1_src");
      auto *s1dst = processor.apvts.getParameter("mod1_dst");
      if (s1src)
        s1src->setValueNotifyingHost(s1src->convertTo0to1(2.0f)); // LFO2
      if (s1dst)
        s1dst->setValueNotifyingHost(s1dst->convertTo0to1(4.0f)); // Resonance
      setParam("mod1_amt", 0.4f);
    }
    // Delay: short slapback for stuttery feel
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 100.0f);
    setParam("delayTimeR", 133.0f);
    setParam("delayFeedback", 0.45f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 27: { // Ring Bell - Ring mod triangle for metallic bell tones
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 0, 8, 0, 10, 6);
      setParam("v" + juce::String(v) + "_ringMod", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset",
               6.0f); // Tritone for inharmonic bell
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1600, 2);
    setParam("leftDetune", -6.0f);
    setParam("rightDetune", 6.0f);
    // Chorus for spatial shimmer
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.5f);
    setParam("chorusDepth", 0.35f);
    setParam("chorusMix", 0.4f);
    // Long stereo delay for bell-like reverberance
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 350.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.55f);
    setParam("delayMix", 0.35f);
    // LFO1: very slow triangle on pitch for slight detuning shimmer
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.2f);
    setParam("lfoDepthPitch", 0.04f);
    // Reverb for bell shimmer
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.7f);
    setParam("reverbDamping", 12000.0f);
    setParam("reverbMix", 0.2f);
    break;
  }

    // ---- CLASSIC C64 PACK ----

  case 28: { // Monty Lead - Rob Hubbard's melodic pulse (Monty on the Run)
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1400, 0, 8, 10, 4, 2);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1100, 5);
    break;
  }

  case 29: { // Sanxion Buzz - Rob Hubbard's bright aggressive saw (Sanxion)
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 2, 6, 1, 23);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1500, 7);
    break;
  }

  case 30: { // Last Ninja - Ben Daglish's layered tri/pulse mix
    configVoice(0, SIDEngine::Waveform::Triangle, 0, 2, 6, 10, 6, 6);
    configVoice(1, SIDEngine::Waveform::Pulse, 2048, 0, 4, 12, 4, 2);
    configVoice(2, SIDEngine::Waveform::Triangle, 0, 2, 6, 10, 6, 6);
    configVoice(3, SIDEngine::Waveform::Pulse, 2048, 0, 4, 12, 4, 2);
    configVoice(4, SIDEngine::Waveform::Triangle, 0, 2, 6, 10, 6, 6);
    configVoice(5, SIDEngine::Waveform::Pulse, 2048, 0, 4, 12, 4, 2);
    for (int v = 0; v < 6; ++v)
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    setFilters(800, 4);
    break;
  }

  case 31: { // Delta Run - Rob Hubbard's sustained resonant pulse (Delta)
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1200, 0, 0, 15, 6, 30);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(900, 6);
    break;
  }

  case 32: { // Cobra Bass - Ben Daglish's punchy resonant saw bass
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 6, 0, 0, 28);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(400, 8);
    break;
  }

  case 33: { // IK Lead - Rob Hubbard's wide clean pulse (International Karate)
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 2400, 0, 4, 12, 3, 25);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1300, 3);
    break;
  }

  case 34: { // Turbo Saw - Jeroen Tel's bright driving saw (Turbo Outrun)
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 10, 2, 24);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1400, 5);
    break;
  }

  case 35: { // Times of Lore - Martin Galway's soft triangle pad
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 4, 4, 14, 8, 26);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(600, 2);
    // Reverb for soft ambience
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.75f);
    setParam("reverbDamping", 5000.0f);
    setParam("reverbMix", 0.3f);
    break;
  }

  case 36: { // Hawkeye Pluck - Jeroen Tel's short pulse pluck (Hawkeye)
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1600, 0, 4, 0, 3, 27);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1200, 6);
    break;
  }

  case 37: { // Deflektor Bell - Ben Daglish's metallic ring mod bell
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 0, 6, 0, 8, 29);
      setParam("v" + juce::String(v) + "_ringMod", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset",
               11.0f); // Maj 7th for bright metallic bell
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1500, 3);
    break;
  }

    // ---- MODERN MODULATION PACK ----

  case 38: { // Drift Pad - Evolving pad with PWM sweep + dual LFOs + FX
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 8, 4, 14, 10, 2);
    setFilters(600, 4);
    setParam("leftDetune", -12.0f);
    setParam("rightDetune", 12.0f);
    // PWM sweep: slow evolving width
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.2f);
    setParam("pwmSweepDepth", 0.45f);
    // LFO1: slow triangle on filter
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.12f);
    setParam("lfoDepthFilt", 0.3f);
    // LFO2: very slow pitch drift
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f);
    setParam("lfo2Rate", 0.08f);
    setParam("lfo2DepthPitch", 0.03f);
    // Filter env: glacial swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 3.5f);
    setParam("filterEnvDecay", 2.0f);
    setParam("filterEnvSustain", 0.7f);
    setParam("filterEnvRelease", 4.0f);
    setParam("filterEnvAmount", 0.35f);
    // Chorus + delay for space
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.5f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.4f);
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 450.0f);
    setParam("delayTimeR", 650.0f);
    setParam("delayFeedback", 0.45f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 39: { // Arp Machine - Arp + filter env pluck + mod matrix vel->filter
    setParam("voiceMode", 3.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1800, 0, 0, 15, 3, 2);
    setFilters(500, 6);
    setParam("leftDetune", -5.0f);
    setParam("rightDetune", 5.0f);
    // Arpeggiator: fast, 3 octaves, up-down
    setParam("arpEnable", 1.0f);
    setParam("arpPattern", 1.0f); // Up-Down
    setParam("arpRate", 12.0f);
    setParam("arpOctaves", 3.0f);
    // Filter envelope: snappy pluck per note
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.1f);
    setParam("filterEnvSustain", 0.1f);
    setParam("filterEnvRelease", 0.15f);
    setParam("filterEnvAmount", 0.75f);
    // Mod matrix: Velocity -> Filter for dynamics
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.5f);
    }
    // Stereo delay: rhythmic
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 188.0f);
    setParam("delayTimeR", 280.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 40: { // Wobble Bass - LFO saw->filter dubstep wobble + ring mod
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 0, 4, 10, 2, 2);
      setParam("v" + juce::String(v) + "_ringMod", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset",
               4.0f); // Maj 3rd for metallic wobble
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(300, 10);
    setParam("dualMode", 1.0f); // Unison for mono bass
    setParam("leftDetune", -8.0f);
    setParam("rightDetune", 8.0f);
    // LFO1: saw on filter for classic wobble
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 1.0f); // Sawtooth
    setParam("lfoRate", 3.0f);
    setParam("lfoDepthFilt", 0.7f);
    // Mod matrix: LFO2 -> Resonance for extra growl
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f); // Triangle
    setParam("lfo2Rate", 1.5f);
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(2.0f)); // LFO2
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(4.0f)); // Resonance
      setParam("mod0_amt", 0.5f);
    }
    // Filter env: aggressive attack
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.3f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.4f);
    setParam("filterEnvAmount", 0.6f);
    setParam("glide", 60.0f);
    break;
  }

  case 41: { // Sequence Morph - 16-step wavetable timbral journey + FX
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 4, 2);
    setFilters(1000, 5);
    setParam("leftDetune", -6.0f);
    setParam("rightDetune", 6.0f);
    // Wavetable: 16-step full timbral sequence
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 16.0f);
    setParam("wtRate", 10.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);   // Pulse, root
      setWTStep(1, 2.0f, 0.0f, 3000.0f);   // Pulse, thin
      setWTStep(2, 1.0f, 0.0f, 2048.0f);   // Saw, root
      setWTStep(3, 2.0f, 7.0f, 2048.0f);   // Pulse, 5th
      setWTStep(4, 0.0f, 0.0f, 2048.0f);   // Triangle, root
      setWTStep(5, 2.0f, -12.0f, 1200.0f); // Pulse, octave down, narrow
      setWTStep(6, 1.0f, 7.0f, 2048.0f);   // Saw, 5th
      setWTStep(7, 2.0f, 12.0f, 800.0f);   // Pulse, octave up, very narrow
      setWTStep(8, 2.0f, 0.0f, 1536.0f);   // Pulse, medium
      setWTStep(9, 3.0f, 0.0f, 2048.0f);   // Noise hit
      setWTStep(10, 1.0f, -5.0f, 2048.0f); // Saw, 4th below
      setWTStep(11, 2.0f, 4.0f, 2800.0f);  // Pulse, 3rd, thin
      setWTStep(12, 0.0f, 12.0f, 2048.0f); // Triangle, octave up
      setWTStep(13, 2.0f, 0.0f, 600.0f);   // Pulse, very narrow
      setWTStep(14, 1.0f, 0.0f, 2048.0f);  // Saw, root
      setWTStep(15, 2.0f, -7.0f, 2048.0f); // Pulse, 5th below
    }
    // LFO1: slow filter sweep overlaid
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.2f);
    setParam("lfoDepthFilt", 0.25f);
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 200.0f);
    setParam("delayTimeR", 300.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.25f);
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    break;
  }

  case 42: { // Poly Chord - Chord memory + PWM sweep + filter env pad
    setParam("voiceMode", 3.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 4, 4, 13, 6, 2);
    setFilters(700, 5);
    setParam("leftDetune", -10.0f);
    setParam("rightDetune", 10.0f);
    // Chord memory: major 7th voicing
    setParam("chordEnable", 1.0f);
    setParam("chordSlot", 0.0f);
    setParam("chord_s0_i0", 4.0f);  // major 3rd
    setParam("chord_s0_i1", 7.0f);  // perfect 5th
    setParam("chord_s0_i2", 11.0f); // major 7th
    // Slot 1: minor 7th
    setParam("chord_s1_i0", 3.0f);
    setParam("chord_s1_i1", 7.0f);
    setParam("chord_s1_i2", 10.0f);
    // PWM sweep
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.3f);
    setParam("pwmSweepDepth", 0.4f);
    // Filter envelope: warm swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 1.0f);
    setParam("filterEnvDecay", 1.0f);
    setParam("filterEnvSustain", 0.6f);
    setParam("filterEnvRelease", 2.0f);
    setParam("filterEnvAmount", 0.4f);
    // LFO2: gentle vibrato
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 4.0f);
    setParam("lfo2DepthPitch", 0.04f);
    // Chorus for width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.7f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    // Reverb for chord depth
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.8f);
    setParam("reverbDamping", 8000.0f);
    setParam("reverbMix", 0.25f);
    break;
  }

    // ---- BONUS DISTINCT PACK ----

  case 43: { // Follin Complex - Tim Follin's multi-waveform melodic lead
    setParam("voiceMode", 2.0f);
    // Follin signature: waveform variety across voices for rich harmonic
    // content
    configVoice(0, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 12, 4, 3);
    configVoice(1, SIDEngine::Waveform::Pulse, 1536, 0, 5, 10, 3, 2);
    configVoice(2, SIDEngine::Waveform::Triangle, 0, 0, 0, 15, 6, 6);
    configVoice(3, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 12, 4, 3);
    configVoice(4, SIDEngine::Waveform::Pulse, 1536, 0, 5, 10, 3, 2);
    configVoice(5, SIDEngine::Waveform::Triangle, 0, 0, 0, 15, 6, 6);
    for (int v = 0; v < 6; ++v)
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    setFilters(1100, 4);
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    // No FX - authentic multi-waveform layering
    break;
  }

  case 44: { // Noise Drums - Noise + fast envelope for percussion channel
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Noise, 2048, 0, 3, 0, 1, 5);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1800, 2);
    // No FX - raw noise percussion
    break;
  }

  case 45: { // Arp Bass - Hubbard-style bass arpeggiation with WT
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1400, 0, 4, 8, 2, 9);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(600, 5);
    // Wavetable: 3-step bass octave arp
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 3.0f);
    setParam("wtRate", 8.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 1400.0f);   // Root
      setWTStep(1, 2.0f, -12.0f, 1400.0f); // Octave down
      setWTStep(2, 2.0f, -12.0f, 1400.0f); // Octave down (double pump)
    }
    break;
  }

  case 46: { // Filter Scream - High-resonance filter sweep lead
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 0, 15, 4, 3);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(200, 14); // Very low cutoff, max resonance for scream
    setParam("leftDetune", -4.0f);
    setParam("rightDetune", 4.0f);
    // Filter envelope: dramatic sweep from bottom to top
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.5f);
    setParam("filterEnvDecay", 0.8f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 1.0f);
    setParam("filterEnvAmount", 0.9f);
    // LFO1: triangle on pitch for vibrato at top of sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 5.0f);
    setParam("lfoDepthPitch", 0.1f);
    // Chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    break;
  }

  case 47: { // Thin Lead - Narrow pulse lead with vibrato + delay
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 512, 0, 0, 15, 2, 7);
    setFilters(1600, 4);
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    // Vibrato
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 5.5f);
    setParam("lfo2DepthPitch", 0.06f);
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 300.0f);
    setParam("delayTimeR", 450.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 48: { // Wide Organ - Hollow wide-pulse organ + chorus
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 3584, 0, 3, 12, 4, 8);
    setFilters(1200, 3);
    setParam("leftDetune", -6.0f);
    setParam("rightDetune", 6.0f);
    // Chorus for warmth
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.9f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    // Slow PWM sweep for subtle movement
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.2f);
    setParam("pwmSweepDepth", 0.15f);
    break;
  }

  case 49: { // Pluck Sequence - Pluck bass WT arp + delay
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 8, 0, 2, 9);
    setFilters(900, 5);
    // Filter envelope for pluck accent
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.1f);
    setParam("filterEnvSustain", 0.1f);
    setParam("filterEnvRelease", 0.15f);
    setParam("filterEnvAmount", 0.7f);
    // Wavetable: 4-step pitch sequence
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 4.0f);
    setParam("wtRate", 12.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);  // Root
      setWTStep(1, 2.0f, 7.0f, 2048.0f);  // 5th
      setWTStep(2, 2.0f, 12.0f, 2048.0f); // Octave
      setWTStep(3, 2.0f, 5.0f, 2048.0f);  // 4th
    }
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 200.0f);
    setParam("delayTimeR", 300.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 50: { // Deep Sub - Sub bass + glide + slow chorus
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 0, 0, 15, 3, 10);
    setFilters(600, 2);
    setParam("glide", 50.0f);
    setParam("dualMode", 1.0f); // Unison for thickness
    setParam("leftDetune", -2.0f);
    setParam("rightDetune", 2.0f);
    // Subtle chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.5f);
    setParam("chorusDepth", 0.15f);
    setParam("chorusMix", 0.2f);
    break;
  }

  case 51: { // Saw Stack - 6-voice saw unison + heavy detune + FX
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 0, 0, 0, 15, 3, 11);
    setParam("dualMode", 1.0f); // Unison
    setParam("leftDetune", -15.0f);
    setParam("rightDetune", 15.0f);
    setFilters(1800, 4);
    // Chorus for super-saw width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.5f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.4f);
    // LFO1 on filter for slow movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoRate", 0.3f);
    setParam("lfoDepthFilt", 0.2f);
    break;
  }

  case 52: { // Stab Machine - Staccato saw + arp + delay
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 0, 0, 6, 0, 0, 12);
    setFilters(1400, 5);
    // Arpeggiator
    setParam("arpEnable", 1.0f);
    setParam("arpRate", 12.0f);
    setParam("arpOctaves", 2.0f);
    setParam("arpPattern", 2.0f); // Up-Down
    // Stereo delay for rhythmic echoes
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 150.0f);
    setParam("delayTimeR", 225.0f);
    setParam("delayFeedback", 0.45f);
    setParam("delayMix", 0.35f);
    // Filter envelope punch
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.08f);
    setParam("filterEnvSustain", 0.05f);
    setParam("filterEnvRelease", 0.1f);
    setParam("filterEnvAmount", 0.8f);
    break;
  }

  case 53: { // Ethereal Pad - Soft triangle pad + dual LFOs + chorus
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 10, 4, 12, 10, 13);
    setFilters(900, 3);
    setParam("leftDetune", -8.0f);
    setParam("rightDetune", 8.0f);
    // LFO1: slow filter sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f); // Triangle
    setParam("lfoRate", 0.15f);
    setParam("lfoDepthFilt", 0.3f);
    // LFO2: gentle pitch drift
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f);
    setParam("lfo2Rate", 0.1f);
    setParam("lfo2DepthPitch", 0.03f);
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.45f);
    // Delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 500.0f);
    setParam("delayTimeR", 750.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.25f);
    // Reverb for ethereal wash
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.85f);
    setParam("reverbDamping", 5000.0f);
    setParam("reverbMix", 0.35f);
    break;
  }

  case 54: { // Bright Wash - Saw pad + PWM sweep + delay
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 0, 6, 4, 10, 8, 14);
    setFilters(1400, 4);
    setParam("leftDetune", -10.0f);
    setParam("rightDetune", 10.0f);
    // PWM sweep (affects pulse fallback on paired SID)
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.4f);
    setParam("pwmSweepDepth", 0.3f);
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    // Delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 400.0f);
    setParam("delayTimeR", 600.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.25f);
    // Filter envelope: gentle swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 1.5f);
    setParam("filterEnvDecay", 1.0f);
    setParam("filterEnvSustain", 0.6f);
    setParam("filterEnvRelease", 2.0f);
    setParam("filterEnvAmount", 0.35f);
    break;
  }

  case 55: { // Pipe Organ - Square wave organ + wide detune
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 0, 15);
    setFilters(1600, 2);
    setParam("leftDetune", -12.0f);
    setParam("rightDetune", 12.0f);
    // Subtle chorus for pipe organ shimmer
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.8f);
    setParam("chorusDepth", 0.15f);
    setParam("chorusMix", 0.2f);
    break;
  }

  case 56: { // Clav Funk - Clavinet pluck + filter env + chorus
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 1536, 0, 10, 4, 2, 16);
    setFilters(800, 6);
    // Sharp filter envelope for clav bite
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.15f);
    setParam("filterEnvSustain", 0.15f);
    setParam("filterEnvRelease", 0.2f);
    setParam("filterEnvAmount", 0.75f);
    // Light chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    // Subtle detune
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    break;
  }

  case 57: { // Drum Kit - Mixed percussion: noise hi-hats + triangle kicks
    // Voices 0-2: Hi-Hat (noise, short)
    configVoice(0, SIDEngine::Waveform::Noise, 0, 0, 4, 0, 0, 17);
    configVoice(1, SIDEngine::Waveform::Noise, 0, 0, 4, 0, 0, 17);
    configVoice(2, SIDEngine::Waveform::Noise, 0, 0, 4, 0, 0, 17);
    // Voices 3-5: Kick Thump (triangle, medium decay)
    configVoice(3, SIDEngine::Waveform::Triangle, 0, 0, 6, 0, 0, 18);
    configVoice(4, SIDEngine::Waveform::Triangle, 0, 0, 6, 0, 0, 18);
    configVoice(5, SIDEngine::Waveform::Triangle, 0, 0, 6, 0, 0, 18);
    setFilters(1800, 2);
    break;
  }

  case 58: { // Wind Noise - White noise + filter LFO sweep
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Noise, 0, 0, 0, 15, 4, 19);
    setFilters(600, 4);
    // LFO1: slow triangle filter sweep for wind effect
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f); // Triangle
    setParam("lfoRate", 0.2f);
    setParam("lfoDepthFilt", 0.6f);
    // LFO2: even slower secondary sweep for variation
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f);
    setParam("lfo2Rate", 0.05f);
    setParam("lfo2DepthFilt", 0.3f);
    // Delay for spaciousness
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 600.0f);
    setParam("delayTimeR", 900.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 59: { // Laser Lead - Zap saw + arp + delay
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 0, 0, 12, 0, 0, 20);
    setFilters(1800, 6);
    // Arpeggiator for rapid-fire zaps
    setParam("arpEnable", 1.0f);
    setParam("arpRate", 18.0f);
    setParam("arpOctaves", 3.0f);
    setParam("arpPattern", 3.0f); // Random
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 125.0f);
    setParam("delayTimeR", 187.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.4f);
    // Filter envelope for extra attack bite
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.05f);
    setParam("filterEnvSustain", 0.0f);
    setParam("filterEnvRelease", 0.05f);
    setParam("filterEnvAmount", 0.9f);
    break;
  }

  case 60: { // Split Layers - Thin Pulse lead + Pluck Bass mix
    setParam("voiceMode", 2.0f);
    // Voices 0-2: Thin Pulse lead
    configVoice(0, SIDEngine::Waveform::Pulse, 512, 0, 0, 15, 2, 7);
    configVoice(1, SIDEngine::Waveform::Pulse, 512, 0, 0, 15, 2, 7);
    configVoice(2, SIDEngine::Waveform::Pulse, 512, 0, 0, 15, 2, 7);
    // Voices 3-5: Pluck Bass
    configVoice(3, SIDEngine::Waveform::Pulse, 2048, 0, 8, 0, 2, 9);
    configVoice(4, SIDEngine::Waveform::Pulse, 2048, 0, 8, 0, 2, 9);
    configVoice(5, SIDEngine::Waveform::Pulse, 2048, 0, 8, 0, 2, 9);
    setFilters(1200, 4);
    setParam("leftDetune", -5.0f);
    setParam("rightDetune", 5.0f);
    // Chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    // Filter envelope for pluck accent on bass voices
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.2f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.3f);
    setParam("filterEnvAmount", 0.5f);
    break;
  }

  case 61: { // Texture Morph - 8-step WT using diverse voice timbres
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 4, 4, 10, 6, 15);
    setFilters(1000, 4);
    setParam("leftDetune", -6.0f);
    setParam("rightDetune", 6.0f);
    // Wavetable: 8-step timbral journey through diverse waveforms/PW
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 8.0f);
    setParam("wtRate", 6.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);   // Square, root
      setWTStep(1, 2.0f, 0.0f, 512.0f);    // Thin pulse
      setWTStep(2, 1.0f, 0.0f, 2048.0f);   // Saw
      setWTStep(3, 0.0f, 12.0f, 2048.0f);  // Triangle, octave up
      setWTStep(4, 2.0f, 0.0f, 3584.0f);   // Wide pulse
      setWTStep(5, 1.0f, -12.0f, 2048.0f); // Saw, octave down
      setWTStep(6, 2.0f, 7.0f, 1536.0f);   // Pulse, 5th, clav PW
      setWTStep(7, 3.0f, 0.0f, 2048.0f);   // Noise hit
    }
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    // Delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 333.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.3f);
    break;
  }

  case 62: { // Brass Section - Brass saw voices + velocity->filter + chorus
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 0, 2, 4, 12, 4, 31);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1000, 5);
    setParam("leftDetune", -4.0f);
    setParam("rightDetune", 4.0f);
    // Filter envelope: brass attack burst
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.02f);
    setParam("filterEnvDecay", 0.3f);
    setParam("filterEnvSustain", 0.5f);
    setParam("filterEnvRelease", 0.5f);
    setParam("filterEnvAmount", 0.6f);
    // Mod matrix: Velocity -> Filter for expressive dynamics
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.6f);
    }
    // Vibrato
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Rate", 5.0f);
    setParam("lfo2DepthPitch", 0.05f);
    // Chorus for section width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    break;
  }

  case 63: { // String Machine - String ensemble + PWM sweep + dual LFOs
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Sawtooth, 0, 8, 4, 12, 8, 32);
    setFilters(1000, 3);
    setParam("leftDetune", -12.0f);
    setParam("rightDetune", 12.0f);
    // PWM sweep for string shimmer
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.3f);
    setParam("pwmSweepDepth", 0.35f);
    // LFO1: slow filter for warmth
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.15f);
    setParam("lfoDepthFilt", 0.2f);
    // LFO2: gentle pitch for ensemble effect
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f);
    setParam("lfo2Rate", 0.1f);
    setParam("lfo2DepthPitch", 0.02f);
    // Chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.35f);
    setParam("chorusMix", 0.4f);
    // Reverb for string hall
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.8f);
    setParam("reverbDamping", 6000.0f);
    setParam("reverbMix", 0.3f);
    break;
  }

  case 64: { // Retro EP - Electric piano + stereo delay + velocity
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1800, 0, 8, 6, 4, 33);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1200, 3);
    setParam("leftDetune", -2.0f);
    setParam("rightDetune", 2.0f);
    // Filter envelope for bell-like attack
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.4f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.5f);
    setParam("filterEnvAmount", 0.5f);
    // Mod matrix: Velocity -> Filter for touch sensitivity
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.5f);
    }
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 250.0f);
    setParam("delayTimeR", 375.0f);
    setParam("delayFeedback", 0.3f);
    setParam("delayMix", 0.25f);
    // Subtle chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.5f);
    setParam("chorusDepth", 0.15f);
    setParam("chorusMix", 0.2f);
    break;
  }

  case 65: { // Ring Mod Pad - Ring modulated triangle pad + filter sweep
    setParam("voiceMode", 2.0f); // Poly: ring mod needs same-note voices
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 6, 4, 12, 8, 26);
      setParam("v" + juce::String(v) + "_ringMod", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset",
               5.0f); // Maj 3rd for shimmery pad
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(800, 5);
    setParam("leftDetune", -8.0f);
    setParam("rightDetune", 8.0f);
    // LFO1: slow filter sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.2f);
    setParam("lfoDepthFilt", 0.35f);
    // Chorus for width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.7f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    // Delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 400.0f);
    setParam("delayTimeR", 600.0f);
    setParam("delayFeedback", 0.45f);
    setParam("delayMix", 0.25f);
    // Reverb for ring mod atmosphere
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.8f);
    setParam("reverbDamping", 7000.0f);
    setParam("reverbMix", 0.3f);
    break;
  }

  case 66: { // Velocity Keys - EP with velocity->filter + velocity->PW
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 1800, 0, 6, 8, 3, 33);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(900, 4);
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    // Filter envelope for attack character
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.3f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 0.4f);
    setParam("filterEnvAmount", 0.5f);
    // Mod matrix: Velocity -> Filter + Velocity -> PW (dual velocity mapping)
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.6f);
      auto *s1src = processor.apvts.getParameter("mod1_src");
      auto *s1dst = processor.apvts.getParameter("mod1_dst");
      if (s1src)
        s1src->setValueNotifyingHost(s1src->convertTo0to1(5.0f)); // Velocity
      if (s1dst)
        s1dst->setValueNotifyingHost(s1dst->convertTo0to1(2.0f)); // PW
      setParam("mod1_amt", 0.4f);
    }
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    break;
  }

  case 67: { // Chord Pad - Chord memory + ambient swell + delay
    setParam("voiceMode", 1.0f);
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 15, 0, 15, 15, 37);
    setFilters(800, 3);
    setParam("leftDetune", -10.0f);
    setParam("rightDetune", 10.0f);
    // Chord memory: major 7th voicing
    setParam("chordEnable", 1.0f);
    setParam("chordSlot", 0.0f);
    setParam("chord_s0_i0", 4.0f);  // major 3rd
    setParam("chord_s0_i1", 7.0f);  // perfect 5th
    setParam("chord_s1_i0", 3.0f);  // minor 3rd
    setParam("chord_s1_i1", 7.0f);  // perfect 5th
    setParam("chord_s2_i0", 5.0f);  // perfect 4th
    setParam("chord_s2_i1", 7.0f);  // perfect 5th
    setParam("chord_s3_i0", 7.0f);  // perfect 5th
    setParam("chord_s3_i1", 12.0f); // octave
    // LFO1: very slow filter
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.08f);
    setParam("lfoDepthFilt", 0.2f);
    // Chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.6f);
    setParam("chorusDepth", 0.4f);
    setParam("chorusMix", 0.45f);
    // Long delay for ambient wash
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 600.0f);
    setParam("delayTimeR", 900.0f);
    setParam("delayFeedback", 0.55f);
    setParam("delayMix", 0.35f);
    // Reverb for ambient pad space
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.85f);
    setParam("reverbDamping", 6000.0f);
    setParam("reverbMix", 0.3f);
    break;
  }

  case 68: { // Harpsichord Suite - Harpsichord pluck + chorus + delay
    setParam("voiceMode", 2.0f);
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Pulse, 768, 0, 3, 0, 2, 34);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setFilters(1600, 4);
    setParam("leftDetune", -2.0f);
    setParam("rightDetune", 2.0f);
    // Filter envelope for bright attack
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.1f);
    setParam("filterEnvSustain", 0.1f);
    setParam("filterEnvRelease", 0.15f);
    setParam("filterEnvAmount", 0.6f);
    // Chorus for doubled harpsichord effect
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 2.0f);
    setParam("chorusDepth", 0.15f);
    setParam("chorusMix", 0.25f);
    // Stereo delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 200.0f);
    setParam("delayTimeR", 300.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.2f);
    break;
  }

  case 69: { // Percussion Ensemble - Tom + Snare + Hi-Hat layered kit
    // Voices 0-1: Tom (melodic percussion)
    configVoice(0, SIDEngine::Waveform::Triangle, 0, 0, 8, 0, 3, 36);
    configVoice(1, SIDEngine::Waveform::Triangle, 0, 0, 8, 0, 3, 36);
    // Voices 2-3: Snare Roll (medium noise)
    configVoice(2, SIDEngine::Waveform::Noise, 0, 0, 6, 2, 3, 35);
    configVoice(3, SIDEngine::Waveform::Noise, 0, 0, 6, 2, 3, 35);
    // Voices 4-5: Hi-Hat (short noise)
    configVoice(4, SIDEngine::Waveform::Noise, 0, 0, 4, 0, 0, 17);
    configVoice(5, SIDEngine::Waveform::Noise, 0, 0, 4, 0, 0, 17);
    for (int v = 0; v < 6; ++v)
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    setFilters(1600, 3);
    break;
  }

    // ---- SHOWCASE ----
    // Presets that demonstrate the full depth of Breadbin's features.
    // Each uses multiple systems simultaneously in musically powerful ways.

  case 70: { // Kitchen Sink - Every feature active simultaneously
    setParam("voiceMode", 3.0f);
    // Both LFOs, mod matrix (all 4 slots), wavetable, filter env, PWM sweep,
    // chord memory, chorus, delay — the ultimate feature demo
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 1, 5, 10, 5, 2);
    setParam("leftDetune", -6.0f);
    setParam("rightDetune", 6.0f);
    setFilters(800, 6);
    // LFO1: tempo-synced 1/4 note triangle on filter
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoSync", 1.0f);
    setParam("lfoSyncDiv", 4.0f); // 1/4 notes
    setParam("lfoDepthFilt", 0.35f);
    // LFO2: free-running sine on pitch (vibrato)
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 4.0f); // Sine
    setParam("lfo2Rate", 5.0f);
    setParam("lfo2DepthPitch", 0.06f);
    // Filter envelope: medium swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.5f);
    setParam("filterEnvDecay", 0.8f);
    setParam("filterEnvSustain", 0.5f);
    setParam("filterEnvRelease", 1.5f);
    setParam("filterEnvAmount", 0.5f);
    // PWM sweep
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.4f);
    setParam("pwmSweepDepth", 0.3f);
    // Wavetable: 4-step timbral cycle
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 4.0f);
    setParam("wtRate", 6.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0, 2.0f, 0.0f, 2048.0f);  // Pulse, root
      setWTStep(1, 1.0f, 0.0f, 2048.0f);  // Saw, root
      setWTStep(2, 2.0f, 7.0f, 1500.0f);  // Pulse, 5th, narrow
      setWTStep(3, 0.0f, 12.0f, 2048.0f); // Triangle, octave
    }
    // Mod matrix: all 4 slots active
    {
      auto setMod = [this, &setParam](int slot, float src, float dst,
                                      float amt) {
        auto mp = "mod" + juce::String(slot) + "_";
        auto *srcP = processor.apvts.getParameter(mp + "src");
        auto *dstP = processor.apvts.getParameter(mp + "dst");
        if (srcP)
          srcP->setValueNotifyingHost(srcP->convertTo0to1(src));
        if (dstP)
          dstP->setValueNotifyingHost(dstP->convertTo0to1(dst));
        setParam(mp + "amt", amt);
      };
      setMod(0, 5.0f, 1.0f, 0.4f);  // Velocity -> Filter
      setMod(1, 4.0f, 2.0f, 0.3f);  // ModWheel -> PW
      setMod(2, 1.0f, 4.0f, 0.25f); // LFO1 -> Resonance
      setMod(3, 3.0f, 3.0f, 0.15f); // FilterEnv -> Pitch
    }
    // Chord memory: major 7th voicing
    setParam("chordEnable", 1.0f);
    setParam("chordSlot", 0.0f);
    setParam("chord_s0_i0", 4.0f);  // major 3rd
    setParam("chord_s0_i1", 7.0f);  // perfect 5th
    setParam("chord_s0_i2", 11.0f); // major 7th
    // Chorus + delay
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 330.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.25f);
    break;
  }

  case 71: { // Sync Sculptor - Hard sync with tempo-synced modulation
    setParam("voiceMode", 2.0f);
    // Hard sync saw voices with musical modulation creating evolving harmonics
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 12, 4, 3);
      setParam("v" + juce::String(v) + "_sync", 1.0f);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset", 5.0f); // 4th for rich sync
    }
    setParam("leftDetune", -5.0f);
    setParam("rightDetune", 5.0f);
    setFilters(1200, 5);
    // LFO1: tempo-synced 1/2 note on filter for sweeping sync harmonics
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f); // Triangle
    setParam("lfoSync", 1.0f);
    setParam("lfoSyncDiv", 3.0f); // 1/2 notes
    setParam("lfoDepthFilt", 0.4f);
    // LFO2: free slow on PW for timbral drift
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 4.0f); // Sine
    setParam("lfo2Rate", 0.5f);
    setParam("lfo2DepthPW", 0.3f);
    // Filter envelope: sharp attack bite
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.25f);
    setParam("filterEnvSustain", 0.3f);
    setParam("filterEnvRelease", 0.5f);
    setParam("filterEnvAmount", 0.55f);
    // Mod matrix: Velocity->Filter, ModWheel->Pitch
    {
      auto setMod = [this, &setParam](int slot, float src, float dst,
                                      float amt) {
        auto mp = "mod" + juce::String(slot) + "_";
        auto *srcP = processor.apvts.getParameter(mp + "src");
        auto *dstP = processor.apvts.getParameter(mp + "dst");
        if (srcP)
          srcP->setValueNotifyingHost(srcP->convertTo0to1(src));
        if (dstP)
          dstP->setValueNotifyingHost(dstP->convertTo0to1(dst));
        setParam(mp + "amt", amt);
      };
      setMod(0, 5.0f, 1.0f, 0.5f); // Velocity -> Filter
      setMod(1, 4.0f, 3.0f, 0.2f); // ModWheel -> Pitch
    }
    // Chorus for stereo width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    setParam("pitchBendRange", 7.0f);
    break;
  }

  case 72: { // Ring Cathedral - Ethereal ring mod bells with space FX
    setParam("voiceMode", 2.0f); // Poly: ring mod needs same-note voices
    // Ring mod triangles with consonant mod offset creating bell harmonics
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Triangle, 0, 3, 2, 12, 6, 6);
      setParam("v" + juce::String(v) + "_ringMod", 1.0f);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset", 7.0f); // 5th: consonant
    }
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    setFilters(900, 4);
    // LFO1: very slow filter sweep
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 4.0f); // Sine
    setParam("lfoRate", 0.15f);
    setParam("lfoDepthFilt", 0.25f);
    // LFO2: slow PW (affects ring mod timbre via harmonics)
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 0.0f); // Triangle
    setParam("lfo2Rate", 0.3f);
    setParam("lfo2DepthPW", 0.2f);
    // Filter envelope: slow cathedral swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 2.5f);
    setParam("filterEnvDecay", 1.5f);
    setParam("filterEnvSustain", 0.6f);
    setParam("filterEnvRelease", 4.0f);
    setParam("filterEnvAmount", 0.45f);
    // Long spacious delay
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 500.0f);
    setParam("delayTimeR", 750.0f);
    setParam("delayFeedback", 0.5f);
    setParam("delayMix", 0.4f);
    // Wide chorus
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.6f);
    setParam("chorusDepth", 0.35f);
    setParam("chorusMix", 0.35f);
    // Reverb for cathedral wash
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.9f);
    setParam("reverbDamping", 5000.0f);
    setParam("reverbMix", 0.4f);
    break;
  }

  case 73: { // Matrix Express - All 4 mod slots for maximum expression
    setParam("voiceMode", 2.0f);
    // Performance-oriented preset: every mod slot mapped for expressive playing
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 1, 4, 10, 4, 2);
    setParam("leftDetune", -4.0f);
    setParam("rightDetune", 4.0f);
    setFilters(900, 5);
    // LFO1: tempo-synced vibrato (subtle)
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 4.0f); // Sine
    setParam("lfoSync", 1.0f);
    setParam("lfoSyncDiv", 5.0f); // 1/8 notes
    setParam("lfoDepthPitch", 0.04f);
    // Filter envelope: medium pluck
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.01f);
    setParam("filterEnvDecay", 0.4f);
    setParam("filterEnvSustain", 0.4f);
    setParam("filterEnvRelease", 0.6f);
    setParam("filterEnvAmount", 0.5f);
    // All 4 mod matrix slots for full expression
    {
      auto setMod = [this, &setParam](int slot, float src, float dst,
                                      float amt) {
        auto mp = "mod" + juce::String(slot) + "_";
        auto *srcP = processor.apvts.getParameter(mp + "src");
        auto *dstP = processor.apvts.getParameter(mp + "dst");
        if (srcP)
          srcP->setValueNotifyingHost(srcP->convertTo0to1(src));
        if (dstP)
          dstP->setValueNotifyingHost(dstP->convertTo0to1(dst));
        setParam(mp + "amt", amt);
      };
      setMod(0, 5.0f, 1.0f, 0.6f);  // Velocity -> Filter (dynamics)
      setMod(1, 4.0f, 2.0f, 0.5f);  // ModWheel -> PW (timbre morph)
      setMod(2, 1.0f, 3.0f, 0.08f); // LFO1 -> Pitch (vibrato)
      setMod(3, 3.0f, 4.0f, 0.4f);  // FilterEnv -> Resonance (attack bite)
    }
    // Chorus for width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.2f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.3f);
    // Delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 375.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.3f);
    setParam("delayMix", 0.2f);
    setParam("pitchBendRange", 5.0f);
    break;
  }

  case 74: { // WT Kaleidoscope - Full 16-step wavetable journey
    // Maximum wavetable depth: 16 steps cycling through all waveforms,
    // pitch jumps, and PW variations creating a melodic pattern
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 0, 0, 15, 3, 2);
    setParam("leftDetune", -3.0f);
    setParam("rightDetune", 3.0f);
    setFilters(1100, 4);
    // 16-step wavetable: diverse timbral and melodic sequence
    setParam("wtEnable", 1.0f);
    setParam("wtNumSteps", 16.0f);
    setParam("wtRate", 10.0f);
    setParam("wtLoop", 1.0f);
    {
      auto setWTStep = [&setParam](int step, float wave, float pitch,
                                   float pw) {
        auto sp = "wt_s" + juce::String(step) + "_";
        setParam(sp + "wave", wave);
        setParam(sp + "pitch", pitch);
        setParam(sp + "pw", pw);
      };
      setWTStep(0,  2.0f,  0.0f, 2048.0f); // Pulse, root
      setWTStep(1,  2.0f,  4.0f, 1800.0f); // Pulse, M3
      setWTStep(2,  1.0f,  7.0f, 2048.0f); // Saw, P5
      setWTStep(3,  2.0f, 12.0f, 2048.0f); // Pulse, oct
      setWTStep(4,  0.0f,  7.0f, 2048.0f); // Tri, P5
      setWTStep(5,  2.0f,  0.0f, 1024.0f); // Pulse, root, narrow
      setWTStep(6,  1.0f, -5.0f, 2048.0f); // Saw, P4 down
      setWTStep(7,  3.0f,  0.0f, 2048.0f); // Noise, hit
      setWTStep(8,  2.0f,  0.0f, 3000.0f); // Pulse, root, thin
      setWTStep(9,  2.0f,  3.0f, 2048.0f); // Pulse, m3
      setWTStep(10, 0.0f,  5.0f, 2048.0f); // Tri, P4
      setWTStep(11, 1.0f, 12.0f, 2048.0f); // Saw, oct
      setWTStep(12, 2.0f, -7.0f, 1500.0f); // Pulse, P5 down
      setWTStep(13, 2.0f,  0.0f, 512.0f);  // Pulse, root, very narrow
      setWTStep(14, 1.0f,  7.0f, 2048.0f); // Saw, P5
      setWTStep(15, 0.0f,  0.0f, 2048.0f); // Tri, root (resolve)
    }
    // LFO1: slow filter movement
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.25f);
    setParam("lfoDepthFilt", 0.2f);
    // Filter envelope for pluck definition
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.15f);
    setParam("filterEnvSustain", 0.2f);
    setParam("filterEnvRelease", 0.3f);
    setParam("filterEnvAmount", 0.5f);
    // Delay for rhythmic depth
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 200.0f);
    setParam("delayTimeR", 300.0f);
    setParam("delayFeedback", 0.4f);
    setParam("delayMix", 0.3f);
    // Chorus for stereo
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.2f);
    setParam("chorusMix", 0.25f);
    break;
  }

  case 75: { // Chord Cathedral - All 4 chord slots with rich voicings
    // Every chord memory slot loaded with a different musical voicing
    for (int v = 0; v < 6; ++v)
      configVoice(v, SIDEngine::Waveform::Pulse, 2048, 4, 4, 12, 6, 2);
    setParam("leftDetune", -5.0f);
    setParam("rightDetune", 5.0f);
    setFilters(700, 5);
    // Chord memory: 4 different voicings
    setParam("chordEnable", 1.0f);
    setParam("chordSlot", 0.0f);
    // Slot 0: Major 9th (+4, +7, +11, +14)
    setParam("chord_s0_i0", 4.0f);
    setParam("chord_s0_i1", 7.0f);
    setParam("chord_s0_i2", 11.0f);
    setParam("chord_s0_i3", 14.0f);
    // Slot 1: Minor 7th (+3, +7, +10)
    setParam("chord_s1_i0", 3.0f);
    setParam("chord_s1_i1", 7.0f);
    setParam("chord_s1_i2", 10.0f);
    // Slot 2: Dominant 7th (+4, +7, +10)
    setParam("chord_s2_i0", 4.0f);
    setParam("chord_s2_i1", 7.0f);
    setParam("chord_s2_i2", 10.0f);
    // Slot 3: Add9 (+2, +7, +12)
    setParam("chord_s3_i0", 2.0f);
    setParam("chord_s3_i1", 7.0f);
    setParam("chord_s3_i2", 12.0f);
    // PWM sweep for evolving texture
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.3f);
    setParam("pwmSweepDepth", 0.35f);
    // Filter envelope: pad swell
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 1.5f);
    setParam("filterEnvDecay", 1.0f);
    setParam("filterEnvSustain", 0.7f);
    setParam("filterEnvRelease", 3.0f);
    setParam("filterEnvAmount", 0.5f);
    // Chorus for width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    // Long delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 500.0f);
    setParam("delayTimeR", 750.0f);
    setParam("delayFeedback", 0.45f);
    setParam("delayMix", 0.3f);
    // Reverb for rich chord depth
    setParam("reverbEnable", 1.0f);
    setParam("reverbDecay", 0.85f);
    setParam("reverbDamping", 7000.0f);
    setParam("reverbMix", 0.3f);
    break;
  }

  case 76: { // Dual Worlds - Different instruments per SID (stereo split)
    // Left SID (v0-2): aggressive sync saw lead
    // Right SID (v3-5): soft triangle pad
    // Two completely different instruments in stereo
    configVoice(0, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 10, 2, 3);
    configVoice(1, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 10, 2, 3);
    configVoice(2, SIDEngine::Waveform::Sawtooth, 2048, 0, 3, 10, 2, 3);
    for (int v = 0; v < 3; ++v) {
      setParam("v" + juce::String(v) + "_sync", 1.0f);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
      setParam("v" + juce::String(v) + "_modOffset", 5.0f);
    }
    configVoice(3, SIDEngine::Waveform::Triangle, 0, 6, 3, 12, 8, 6);
    configVoice(4, SIDEngine::Waveform::Triangle, 0, 6, 3, 12, 8, 6);
    configVoice(5, SIDEngine::Waveform::Triangle, 0, 6, 3, 12, 8, 6);
    for (int v = 3; v < 6; ++v)
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    setParam("dualMode", 0.0f); // StereoSplit
    setParam("leftDetune", -4.0f);
    setParam("rightDetune", 4.0f);
    setFilters(1000, 4);
    // LFO1: filter sweep for left (lead) side
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 0.0f);
    setParam("lfoRate", 0.4f);
    setParam("lfoDepthFilt", 0.3f);
    // LFO2: slow PW drift
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 4.0f); // Sine
    setParam("lfo2Rate", 0.2f);
    setParam("lfo2DepthPW", 0.2f);
    // Chorus for the pad side
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 0.8f);
    setParam("chorusDepth", 0.25f);
    setParam("chorusMix", 0.3f);
    // Delay for stereo depth
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 375.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.3f);
    setParam("delayMix", 0.2f);
    break;
  }

  case 77: { // Glide Machine - Portamento synthwave solo monster
    // Unison mode, long glide, wide detune — modern synthwave lead
    for (int v = 0; v < 6; ++v) {
      configVoice(v, SIDEngine::Waveform::Sawtooth, 2048, 0, 2, 12, 4, 3);
      setParam("v" + juce::String(v) + "_filter", 1.0f);
    }
    setParam("dualMode", 1.0f); // Unison
    setParam("leftDetune", -15.0f);
    setParam("rightDetune", 15.0f);
    setParam("glide", 800.0f); // Long portamento
    setFilters(1400, 5);
    // LFO1: S&H on filter for random filter steps
    setParam("lfoEnable", 1.0f);
    setParam("lfoWave", 3.0f); // S&H
    setParam("lfoRate", 3.0f);
    setParam("lfoDepthFilt", 0.25f);
    // LFO2: vibrato
    setParam("lfo2Enable", 1.0f);
    setParam("lfo2Wave", 4.0f); // Sine
    setParam("lfo2Rate", 5.5f);
    setParam("lfo2DepthPitch", 0.07f);
    // PWM sweep for movement
    setParam("pwmSweepEnable", 1.0f);
    setParam("pwmSweepRate", 0.5f);
    setParam("pwmSweepDepth", 0.3f);
    // Filter envelope: attack pluck
    setParam("filterEnvEnable", 1.0f);
    setParam("filterEnvAttack", 0.001f);
    setParam("filterEnvDecay", 0.3f);
    setParam("filterEnvSustain", 0.4f);
    setParam("filterEnvRelease", 0.8f);
    setParam("filterEnvAmount", 0.45f);
    // Mod matrix: Velocity->Filter for dynamics
    {
      auto *s0src = processor.apvts.getParameter("mod0_src");
      auto *s0dst = processor.apvts.getParameter("mod0_dst");
      if (s0src)
        s0src->setValueNotifyingHost(s0src->convertTo0to1(5.0f)); // Velocity
      if (s0dst)
        s0dst->setValueNotifyingHost(s0dst->convertTo0to1(1.0f)); // Filter
      setParam("mod0_amt", 0.4f);
    }
    // Delay for space
    setParam("delayEnable", 1.0f);
    setParam("delayTimeL", 333.0f);
    setParam("delayTimeR", 500.0f);
    setParam("delayFeedback", 0.35f);
    setParam("delayMix", 0.3f);
    // Chorus for massive width
    setParam("chorusEnable", 1.0f);
    setParam("chorusRate", 1.0f);
    setParam("chorusDepth", 0.3f);
    setParam("chorusMix", 0.35f);
    setParam("pitchBendRange", 12.0f);
    break;
  }
  }

  // Re-apply all voice settings to SID engine after preset configuration.
  // configVoice calls applyVoiceSettings before per-voice params like
  // _sync/_ringMod are set, so this final pass pushes them to the engine.
  for (int v = 0; v < 6; ++v)
    processor.applyVoiceSettings(v);

  // Refresh UI for current voice
  loadVoiceToUI(selectedVoice);
}

void BreadbinEditor::savePresetToFile() {
  // Save all current UI state to selected voice first
  saveUIToVoice(selectedVoice);

  // Get state from processor
  juce::MemoryBlock data;
  processor.getStateInformation(data);

  // Show file dialog
  auto chooser = std::make_unique<juce::FileChooser>(
      "Save Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.breadbin", true);

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  chooser->launchAsync(chooserFlags, [this, data](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      // Ensure correct extension
      if (!file.hasFileExtension(".breadbin"))
        file = file.withFileExtension(".breadbin");

      // Write state as XML for human readability
      auto state =
          juce::ValueTree::readFromData(data.getData(), data.getSize());
      if (state.isValid()) {
        auto xml = state.createXml();
        if (xml != nullptr) {
          xml->writeTo(file);
        }
      }
    }
  });

  // Keep chooser alive
  static std::unique_ptr<juce::FileChooser> savedChooser;
  savedChooser = std::move(chooser);
}

void BreadbinEditor::loadPresetFromFile() {
  auto chooser = std::make_unique<juce::FileChooser>(
      "Load Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.breadbin", true);

  auto chooserFlags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles;

  chooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      auto xml = juce::XmlDocument::parse(file);
      if (xml != nullptr) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
          juce::MemoryBlock data;
          juce::MemoryOutputStream stream(data, false);
          state.writeToStream(stream);
          processor.setStateInformation(data.getData(),
                                        static_cast<int>(data.getSize()));

          // Refresh UI to show loaded state
          loadVoiceToUI(selectedVoice);
          updateVoiceButtonStates();

          // Update global controls
          dualModeSelector.setSelectedId(
              static_cast<int>(processor.getDualMode()) + 1,
              juce::dontSendNotification);
          // pitchBendRange now APVTS-managed, auto-restored
          clockModeSelector.setSelectedId(
              static_cast<int>(processor.getClockMode()) + 1,
              juce::dontSendNotification);
          extInputEnableButton.setToggleState(processor.isExtInputEnabled(),
                                              juce::dontSendNotification);
          extInputLevelSlider.setValue(processor.getExtInputLevel(),
                                       juce::dontSendNotification);

          // LFO controls (lfoEnable, lfoWaveform, lfoRate, depths) are
          // APVTS-attached — setStateInformation auto-syncs them.

          processor.snapshotPresetState();
        }
      }
    }
  });

  static std::unique_ptr<juce::FileChooser> savedChooser;
  savedChooser = std::move(chooser);
}

juce::File BreadbinEditor::getUserPresetsDir() {
  auto appData =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
  return appData.getChildFile("GPLAudio")
      .getChildFile("Breadbin")
      .getChildFile("Presets");
}

void BreadbinEditor::refreshUserPresets() {
  // JUCE ComboBox has no removeItem(), so clear and rebuild everything
  int prevId = globalPresetSelector.getSelectedId();
  globalPresetSelector.clear(juce::dontSendNotification);

  // Build categorized preset menu using PopupMenu submenus
  auto *root = globalPresetSelector.getRootMenu();

  // -- Favorites (top-level, sentinel IDs remapped in onChange) --
  root->addItem(500, "Dual Lead");
  root->addItem(501, "Commando");
  root->addItem(502, "Drift Pad");
  root->addItem(503, "Growl Bass");
  root->addItem(504, "Chip Sequence");
  root->addSeparator();

  // -- Leads (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(20, "Acid Squelch");
    sub.addItem(62, "Brass Section");
    sub.addItem(56, "Clav Funk");
    sub.addItem(1, "Dual Lead");
    sub.addItem(46, "Filter Scream");
    sub.addItem(59, "Laser Lead");
    sub.addItem(5, "Retro Synth");
    sub.addItem(51, "Saw Stack");
    sub.addItem(15, "SID Brass");
    sub.addItem(19, "Sync Lead");
    sub.addItem(47, "Thin Lead");
    sub.addItem(66, "Velocity Keys");
    root->addSubMenu("Leads", sub);
  }

  // -- Bass (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(45, "Arp Bass");
    sub.addItem(32, "Cobra Bass");
    sub.addItem(50, "Deep Sub");
    sub.addItem(22, "Growl Bass");
    sub.addItem(60, "Split Layers");
    sub.addItem(21, "Sub Bass");
    sub.addItem(40, "Wobble Bass");
    root->addSubMenu("Bass", sub);
  }

  // -- Pads & Keys (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(54, "Bright Wash");
    sub.addItem(67, "Chord Pad");
    sub.addItem(6, "Chord Stab");
    sub.addItem(38, "Drift Pad");
    sub.addItem(53, "Ethereal Pad");
    sub.addItem(68, "Harpsichord Suite");
    sub.addItem(23, "Ice Pad");
    sub.addItem(2, "Pad Stack");
    sub.addItem(55, "Pipe Organ");
    sub.addItem(42, "Poly Chord");
    sub.addItem(24, "PWM Strings");
    sub.addItem(64, "Retro EP");
    sub.addItem(63, "String Machine");
    sub.addItem(48, "Wide Organ");
    root->addSubMenu("Pads & Keys", sub);
  }

  // -- Arps & Sequences (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(39, "Arp Machine");
    sub.addItem(3, "Arpeggiated");
    sub.addItem(25, "Chip Sequence");
    sub.addItem(13, "Hubbard Arp");
    sub.addItem(49, "Pluck Sequence");
    sub.addItem(41, "Sequence Morph");
    sub.addItem(52, "Stab Machine");
    sub.addItem(61, "Texture Morph");
    sub.addItem(8, "WT Arpeggio");
    sub.addItem(9, "WT Morph");
    root->addSubMenu("Arps & Sequences", sub);
  }

  // -- FX & Modulation (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(57, "Drum Kit");
    sub.addItem(4, "Fat Unison");
    sub.addItem(14, "Galway Sweep");
    sub.addItem(7, "Mod Madness");
    sub.addItem(69, "Percussion Ensemble");
    sub.addItem(27, "Ring Bell");
    sub.addItem(65, "Ring Mod Pad");
    sub.addItem(26, "S&H Glitch");
    sub.addItem(58, "Wind Noise");
    root->addSubMenu("FX & Modulation", sub);
  }

  // -- Showcase (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(75, "Chord Cathedral");
    sub.addItem(76, "Dual Worlds");
    sub.addItem(77, "Glide Machine");
    sub.addItem(70, "Kitchen Sink");
    sub.addItem(73, "Matrix Express");
    sub.addItem(72, "Ring Cathedral");
    sub.addItem(71, "Sync Sculptor");
    sub.addItem(74, "WT Kaleidoscope");
    root->addSubMenu("Showcase", sub);
  }

  // -- Classic C64 (alphabetical) --
  {
    juce::PopupMenu sub;
    sub.addItem(10, "Commando");
    sub.addItem(16, "Cybernoid");
    sub.addItem(37, "Deflektor Bell");
    sub.addItem(31, "Delta Run");
    sub.addItem(43, "Follin Complex");
    sub.addItem(36, "Hawkeye Pluck");
    sub.addItem(33, "IK Lead");
    sub.addItem(30, "Last Ninja");
    sub.addItem(28, "Monty Lead");
    sub.addItem(11, "Ninja Bass");
    sub.addItem(44, "Noise Drums");
    sub.addItem(12, "Ocean Loader");
    sub.addItem(29, "Sanxion Buzz");
    sub.addItem(18, "Thing Bounce");
    sub.addItem(35, "Times of Lore");
    sub.addItem(34, "Turbo Saw");
    sub.addItem(17, "Wizball");
    root->addSubMenu("Classic C64", sub);
  }

  // -- User Presets --
  userPresetFiles.clear();
  auto dir = getUserPresetsDir();
  if (dir.exists()) {
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.breadbin");
    files.sort();

    if (!files.isEmpty()) {
      root->addSeparator();
      juce::PopupMenu userMenu;
      for (int i = 0; i < files.size(); ++i) {
        userPresetFiles.push_back(files[i]);
        userMenu.addItem(1000 + i, files[i].getFileNameWithoutExtension());
      }
      root->addSubMenu("User Presets", userMenu);
    }
  }

  // Restore previous selection if valid
  if (prevId > 0)
    globalPresetSelector.setSelectedId(prevId, juce::dontSendNotification);
}

void BreadbinEditor::savePresetToMenu() {
  // Save current voice state
  saveUIToVoice(selectedVoice);

  // Prompt for preset name
  auto *aw = new juce::AlertWindow(
      "Save Preset",
      "Enter a name for this preset:", juce::AlertWindow::NoIcon);
  aw->addTextEditor("name", "", "Preset name:");
  aw->addButton("Save", 1);
  aw->addButton("Cancel", 0);

  aw->enterModalState(
      true, juce::ModalCallbackFunction::create([this, aw](int result) {
        if (result == 1) {
          auto name = aw->getTextEditorContents("name").trim();
          if (name.isEmpty())
            return;

          // Sanitize filename
          name = name.replaceCharacters("\\/:*?\"<>|", "_________");

          // Get state data
          juce::MemoryBlock data;
          processor.getStateInformation(data);

          auto state =
              juce::ValueTree::readFromData(data.getData(), data.getSize());
          if (state.isValid()) {
            auto xml = state.createXml();
            if (xml != nullptr) {
              auto dir = getUserPresetsDir();
              dir.createDirectory();
              auto file = dir.getChildFile(name + ".breadbin");
              xml->writeTo(file);

              // Refresh the preset list
              refreshUserPresets();

              // Select the newly saved preset
              for (int i = 0; i < static_cast<int>(userPresetFiles.size());
                   ++i) {
                if (userPresetFiles[static_cast<size_t>(i)] == file) {
                  globalPresetSelector.setSelectedId(
                      1000 + i, juce::dontSendNotification);
                  break;
                }
              }
            }
          }
        }
        delete aw;
      }),
      false);
}

void BreadbinEditor::saveVoicePresetToFile() {
  saveUIToVoice(selectedVoice);
  auto state = processor.getVoiceState(selectedVoice);

  auto chooser = std::make_unique<juce::FileChooser>(
      "Save Voice Settings",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.voice", true);

  auto chooserFlags = juce::FileBrowserComponent::saveMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::warnAboutOverwriting;

  chooser->launchAsync(chooserFlags, [state](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      if (!file.hasFileExtension(".voice"))
        file = file.withFileExtension(".voice");

      auto xml = state.createXml();
      if (xml != nullptr)
        xml->writeTo(file);
    }
  });

  static std::unique_ptr<juce::FileChooser> savedVoiceChooser;
  savedVoiceChooser = std::move(chooser);
}

void BreadbinEditor::loadVoicePresetFromFile() {
  auto chooser = std::make_unique<juce::FileChooser>(
      "Load Voice Settings",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.voice", true);

  auto chooserFlags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles;

  chooser->launchAsync(chooserFlags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      auto xml = juce::XmlDocument::parse(file);
      if (xml != nullptr) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
          processor.setVoiceState(selectedVoice, state);
          loadVoiceToUI(selectedVoice);
        }
      }
    }
  });

  static std::unique_ptr<juce::FileChooser> savedVoiceChooser;
  savedVoiceChooser = std::move(chooser);
}

// ========== CHORD MEMORY FACTORY PRESETS ==========
void ChordMemoryPanel::applyChordPresetByIndex(int presetIndex) {
  // Each preset: 4 slots x 5 intervals (semitones from root, 0=off)
  // Slot pattern: root+intervals for common chord voicings
  struct ChordPresetData {
    const char *name;
    int intervals[4][5]; // 4 slots x 5 notes
  };
  static const ChordPresetData presets[] = {
      {"Major Triad",
       {{4, 7, 0, 0, 0},
        {4, 7, 12, 0, 0},
        {4, 7, 12, 16, 0},
        {4, 7, 12, 16, 19}}},
      {"Minor Triad",
       {{3, 7, 0, 0, 0},
        {3, 7, 12, 0, 0},
        {3, 7, 12, 15, 0},
        {3, 7, 12, 15, 19}}},
      {"7th Chord",
       {{4, 7, 10, 0, 0},
        {4, 7, 11, 0, 0},
        {3, 7, 10, 0, 0},
        {3, 7, 11, 0, 0}}},
      {"Sus4",
       {{5, 7, 0, 0, 0}, {5, 7, 12, 0, 0}, {2, 7, 0, 0, 0}, {2, 7, 12, 0, 0}}},
      {"Power Chord",
       {{7, 0, 0, 0, 0},
        {7, 12, 0, 0, 0},
        {7, 12, 19, 0, 0},
        {7, 12, 19, 24, 0}}},
      {"Octaves",
       {{12, 0, 0, 0, 0},
        {12, 24, 0, 0, 0},
        {-12, 12, 0, 0, 0},
        {-12, 12, 24, 0, 0}}},
  };
  int idx = presetIndex - 1;
  if (idx < 0 || idx >= 6)
    return;
  auto &data = presets[idx];

  for (int s = 0; s < 4; ++s) {
    for (int i = 0; i < 5; ++i) {
      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      auto *p = processor.apvts.getParameter(id);
      if (p)
        p->setValueNotifyingHost(
            p->convertTo0to1(static_cast<float>(data.intervals[s][i])));
    }
  }
}

// ========== WAVETABLE FACTORY PRESETS ==========
void WavetablePanel::applyWavetablePresetByIndex(int presetIndex) {
  // wave: 1=Tri,2=Saw,3=Pulse,4=Noise  pitch: semitones  pw: 0-4095
  struct WTPresetStep {
    int wave;
    int pitch;
    int pw;
  };
  struct WTPresetData {
    const char *name;
    int numSteps;
    float rate;
    bool loop;
    WTPresetStep steps[16];
  };
  static const WTPresetData presets[] = {
      {"Classic Sweep",
       8,
       8.0f,
       true,
       {{1, 0, 2048},
        {1, 2, 2048},
        {1, 4, 2048},
        {1, 7, 2048},
        {1, 12, 2048},
        {1, 7, 2048},
        {1, 4, 2048},
        {1, 2, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"Arp Up",
       4,
       12.0f,
       true,
       {{2, 0, 1024},
        {2, 4, 1024},
        {2, 7, 1024},
        {2, 12, 1024},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"Pluck Sequence",
       8,
       6.0f,
       true,
       {{1, 0, 2048},
        {2, 0, 2048},
        {0, 7, 2048},
        {2, 5, 2048},
        {1, 12, 2048},
        {0, 3, 2048},
        {2, 7, 2048},
        {1, -5, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"PWM Cycle",
       8,
       4.0f,
       true,
       {{2, 0, 512},
        {2, 0, 1024},
        {2, 0, 2048},
        {2, 0, 3072},
        {2, 0, 4095},
        {2, 0, 3072},
        {2, 0, 2048},
        {2, 0, 1024},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"Octave Bounce",
       4,
       10.0f,
       true,
       {{1, 0, 2048},
        {1, 12, 2048},
        {1, 0, 2048},
        {1, -12, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"Waveform Morph",
       6,
       3.0f,
       true,
       {{0, 0, 2048},
        {1, 0, 2048},
        {2, 0, 512},
        {2, 0, 2048},
        {2, 0, 3500},
        {1, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"Noise Rhythm",
       8,
       10.0f,
       true,
       {{2, 0, 2048},
        {2, 0, 2048},
        {3, 0, 2048},
        {2, 0, 2048},
        {2, 0, 2048},
        {3, 0, 2048},
        {2, 0, 2048},
        {2, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048},
        {0, 0, 2048}}},
      {"Chip Drum",
       16,
       16.0f,
       true,
       {{2, 0, 1024},
        {2, 0, 1024},
        {3, 0, 2048},
        {2, 0, 1024},
        {2, -12, 1024},
        {2, 0, 1024},
        {3, 0, 2048},
        {2, 12, 1024},
        {2, 0, 1024},
        {2, 0, 1024},
        {3, 0, 2048},
        {2, 0, 1024},
        {2, 7, 1024},
        {2, 0, 1024},
        {3, 0, 2048},
        {3, 0, 2048}}},
  };

  int idx = presetIndex - 1;
  if (idx < 0 || idx >= 8)
    return;
  auto &data = presets[idx];

  auto setPar = [&](const juce::String &id, float val) {
    auto *p = processor.apvts.getParameter(id);
    if (p)
      p->setValueNotifyingHost(p->convertTo0to1(val));
  };

  setPar("wtNumSteps", static_cast<float>(data.numSteps));
  setPar("wtRate", data.rate);
  setPar("wtLoop", data.loop ? 1.0f : 0.0f);

  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    setPar(prefix + "wave", static_cast<float>(data.steps[i].wave));
    setPar(prefix + "pitch", static_cast<float>(data.steps[i].pitch));
    setPar(prefix + "pw", static_cast<float>(data.steps[i].pw));
  }
}

// ========== CHORD MEMORY PRESET SAVE/LOAD ==========
void ChordMemoryPanel::saveChordPreset() {
  juce::ValueTree state("ChordPreset");
  for (int s = 0; s < 4; ++s) {
    for (int i = 0; i < 5; ++i) {
      auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
      auto *p = processor.apvts.getParameter(id);
      if (p)
        state.setProperty(juce::Identifier(id),
                          p->convertFrom0to1(p->getValue()), nullptr);
    }
  }
  auto *slotP = processor.apvts.getParameter("chordSlot");
  if (slotP)
    state.setProperty("chordSlot", slotP->convertFrom0to1(slotP->getValue()),
                      nullptr);

  auto chooser = std::make_unique<juce::FileChooser>(
      "Save Chord Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.chords", true);
  auto flags = juce::FileBrowserComponent::saveMode |
               juce::FileBrowserComponent::canSelectFiles |
               juce::FileBrowserComponent::warnAboutOverwriting;
  chooser->launchAsync(flags, [state](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      if (!file.hasFileExtension(".chords"))
        file = file.withFileExtension(".chords");
      auto xml = state.createXml();
      if (xml)
        xml->writeTo(file);
    }
  });
  static std::unique_ptr<juce::FileChooser> saved;
  saved = std::move(chooser);
}

void ChordMemoryPanel::loadChordPreset() {
  auto chooser = std::make_unique<juce::FileChooser>(
      "Load Chord Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.chords", true);
  auto flags = juce::FileBrowserComponent::openMode |
               juce::FileBrowserComponent::canSelectFiles;
  chooser->launchAsync(flags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      auto xml = juce::XmlDocument::parse(file);
      if (xml) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
          for (int s = 0; s < 4; ++s) {
            for (int i = 0; i < 5; ++i) {
              auto id = "chord_s" + juce::String(s) + "_i" + juce::String(i);
              auto *p = processor.apvts.getParameter(id);
              if (p && state.hasProperty(juce::Identifier(id)))
                p->setValueNotifyingHost(p->convertTo0to1(
                    static_cast<float>(state[juce::Identifier(id)])));
            }
          }
          auto *slotP = processor.apvts.getParameter("chordSlot");
          if (slotP && state.hasProperty("chordSlot"))
            slotP->setValueNotifyingHost(
                slotP->convertTo0to1(static_cast<float>(state["chordSlot"])));
        }
      }
    }
  });
  static std::unique_ptr<juce::FileChooser> saved;
  saved = std::move(chooser);
}

// ========== WAVETABLE PRESET SAVE/LOAD ==========
void WavetablePanel::saveWavetablePreset() {
  juce::ValueTree state("WavetablePreset");
  auto savePar = [&](const juce::String &id) {
    auto *p = processor.apvts.getParameter(id);
    if (p)
      state.setProperty(juce::Identifier(id), p->convertFrom0to1(p->getValue()),
                        nullptr);
  };
  savePar("wtNumSteps");
  savePar("wtRate");
  savePar("wtLoop");
  for (int i = 0; i < 16; ++i) {
    auto prefix = "wt_s" + juce::String(i) + "_";
    savePar(prefix + "wave");
    savePar(prefix + "pitch");
    savePar(prefix + "pw");
  }

  auto chooser = std::make_unique<juce::FileChooser>(
      "Save Wavetable Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.wtsteps", true);
  auto flags = juce::FileBrowserComponent::saveMode |
               juce::FileBrowserComponent::canSelectFiles |
               juce::FileBrowserComponent::warnAboutOverwriting;
  chooser->launchAsync(flags, [state](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file != juce::File{}) {
      if (!file.hasFileExtension(".wtsteps"))
        file = file.withFileExtension(".wtsteps");
      auto xml = state.createXml();
      if (xml)
        xml->writeTo(file);
    }
  });
  static std::unique_ptr<juce::FileChooser> saved;
  saved = std::move(chooser);
}

void WavetablePanel::loadWavetablePreset() {
  auto chooser = std::make_unique<juce::FileChooser>(
      "Load Wavetable Preset",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.wtsteps", true);
  auto flags = juce::FileBrowserComponent::openMode |
               juce::FileBrowserComponent::canSelectFiles;
  chooser->launchAsync(flags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      auto xml = juce::XmlDocument::parse(file);
      if (xml) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
          auto loadPar = [&](const juce::String &id) {
            auto *p = processor.apvts.getParameter(id);
            if (p && state.hasProperty(juce::Identifier(id)))
              p->setValueNotifyingHost(p->convertTo0to1(
                  static_cast<float>(state[juce::Identifier(id)])));
          };
          loadPar("wtNumSteps");
          loadPar("wtRate");
          loadPar("wtLoop");
          for (int i = 0; i < 16; ++i) {
            auto prefix = "wt_s" + juce::String(i) + "_";
            loadPar(prefix + "wave");
            loadPar(prefix + "pitch");
            loadPar(prefix + "pw");
          }
        }
      }
    }
  });
  static std::unique_ptr<juce::FileChooser> saved;
  saved = std::move(chooser);
}

juce::Path BreadbinEditor::makeDiskPath() {
  return juce::Drawable::parseSVGPath(
      "M3 3h14l4 4v14H3V3zm16 16V7.83L16.17 5H5v14h14zM7 7h7v4H7V7zm0 "
      "10h10v-4H7v4z");
}

juce::Path BreadbinEditor::makeFolderPath() {
  return juce::Drawable::parseSVGPath(
      "M3 3h7l2 2h9v16H3V3zm16 16V7h-8l-2-2H5v14h14z");
}

void BreadbinEditor::refreshVoiceEditorAttachments() {
  // Release existing attachments
  voiceWaveformAttach.reset();
  voicePWAttach.reset();
  voiceAttackAttach.reset();
  voiceDecayAttach.reset();
  voiceSustainAttach.reset();
  voiceReleaseAttach.reset();
  // voicePanAttach removed (per-SID pan now)
  voiceRingModAttach.reset();
  voiceSyncAttach.reset();
  voiceFilterAttach.reset();
  voiceModOffsetAttach.reset();

  // Re-attach to selected voice
  juce::String prefix = "v" + juce::String(processor.getSelectedVoice()) + "_";

  voiceWaveformAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          processor.apvts, prefix + "waveform", waveformSelector);
  voicePWAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "pw", pulseWidthSlider);
  voiceAttackAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "attack", attackSlider);
  voiceDecayAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "decay", decaySlider);
  voiceSustainAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "sustain", sustainSlider);
  voiceReleaseAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "release", releaseSlider);
  // voicePanAttach removed (per-SID pan now)
  voiceRingModAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, prefix + "ringMod", ringModButton);
  voiceSyncAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, prefix + "sync", syncButton);
  voiceFilterAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          processor.apvts, prefix + "filter", voiceFilterButton);
  voiceModOffsetAttach =
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          processor.apvts, prefix + "modOffset", modOffsetSlider);
}
