#pragma once

#include "ui/Theme.h"

namespace cerulean::ui
{

/**
    A small modal card drawn inside the editor (no extra OS windows, so it behaves the same in
    every host): a question with OK/Cancel, optionally with a text field.
*/
class InlineDialog final : public juce::Component
{
public:
    struct Options
    {
        juce::String title;
        juce::String message;
        bool withTextField = false;
        juce::String initialText;
        juce::String confirmLabel = "OK";
        bool destructive = false;

        /** Return an error message to keep the dialog open, or an empty string to close it. */
        std::function<juce::String (const juce::String& text)> onConfirm;
    };

    InlineDialog();
    ~InlineDialog() override;

    void open (Options options);
    void close();
    bool isOpen() const noexcept { return isVisible(); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class DialogButton;

    void confirm();
    juce::Rectangle<float> cardArea() const;

    juce::SharedResourcePointer<Fonts> fonts;
    Options options;
    juce::String error;
    juce::TextEditor textField;
    std::unique_ptr<DialogButton> okButton, cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InlineDialog)
};

} // namespace cerulean::ui
