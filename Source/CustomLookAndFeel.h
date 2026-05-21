#pragma once

#include <JuceHeader.h>

// Lightweight custom Look&Feel used for some project controls.

class CustomLookAndFeel : public LookAndFeel_V4
{
public:
    CustomLookAndFeel() {}
    ~CustomLookAndFeel() override {}
    
    void drawToggleButton(Graphics& g, ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        
        // Draw the toggle button background
        g.setColour(button.getToggleState() ? Colours::lightblue : Colours::darkgrey);
        g.fillRoundedRectangle(bounds, 4.0f);
        
        // Draw the toggle button outline
        g.setColour(Colours::black);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
        
        // Center and draw the text
        g.setColour(Colours::white);
        g.setFont(Font(14.0f, Font::bold));
        g.drawText(button.getButtonText(), bounds, Justification::centred, true);
    }
};
