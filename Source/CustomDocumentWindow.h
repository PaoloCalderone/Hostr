#pragma once

#include <JuceHeader.h>
#include <functional>

// Minimal custom window with optional callback when the user closes it.

class CustomDocumentWindow : public DocumentWindow
{
    public:
    CustomDocumentWindow(const String& name,
                         Colour backgroundColour,
                         int buttonsNeeded)
    : DocumentWindow(name, backgroundColour, buttonsNeeded)
    {
    }
    
    void setOnCloseCallback(std::function<void()> callback)
    {
        onCloseCallback = std::move(callback);
    }
    
    void closeButtonPressed() override
    {
        if (onCloseCallback)
            onCloseCallback();
        else
            DocumentWindow::closeButtonPressed();
    }
    
private:
    std::function<void()> onCloseCallback;
};
