#pragma once

#include "ui/HaliteController.h"

namespace halite::ui
{

/**
    The status line (PLAN.md, Parts): the name and one sentence of the control under the pointer or in
    focus on the left (or a short message, such as what Learn set), the mode summary on the right.
*/
class StatusLine final : public juce::Component
{
public:
    explicit StatusLine (HaliteController&);

    void update();
    void paint (juce::Graphics&) override;

    const HelpText& shownHelp() const noexcept { return help; }
    const juce::String& shownSummary() const noexcept { return summary; }

private:
    HaliteController& controller;
    juce::SharedResourcePointer<Fonts> fonts;
    HelpText help;
    juce::String summary;
};

/**
    A small card inside the editor (save, rename, delete, help): no extra OS window, so it behaves the
    same in every host. In the workbench style, both themes; Return confirms, Escape cancels.
*/
class Dialog final : public juce::Component
{
public:
    struct Options
    {
        juce::String title, message;
        bool withTextField = false;
        juce::String initialText;
        juce::String confirmLabel = "OK";
        bool withCancel = true;

        /** Return an error message to keep the dialog open, or an empty string to close it. */
        std::function<juce::String (const juce::String& text)> onConfirm;
    };

    Dialog();
    ~Dialog() override;

    void open (Options);
    void close();
    bool isOpen() const noexcept { return isVisible(); }

    /** Types into the field (if any) and confirms, as Return would (the self-test). */
    void confirmForTest (const juce::String& text);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void confirm();
    juce::Rectangle<float> card() const;
    juce::TextLayout messageLayout (float width) const;

    juce::SharedResourcePointer<Fonts> fonts;
    Options options;
    juce::String error;
    int generation = 0;     // bumped by open(): a confirm that opened another dialog leaves it open
    juce::TextEditor field;
    std::unique_ptr<juce::Button> okButton, cancelButton;
};

} // namespace halite::ui
