#pragma once

#include <JuceHeader.h>
#include <functional>

class PluginProcessor;

// ==============================================================================
// Manages saving / loading / default presets for Hostr.
//
// File format: .hostrpreset (XML)
// Path: JUCE user data folder / Hostr / Presets
// macOS: ~/Library/Application Support/Hostr/Presets
// Windows: %APPDATA%\Hostr\Presets
// Linux: ~/.config/Hostr/Presets
// Default: JUCE user data folder / Hostr / default.hostrpreset
// Philosophy: Local, portable, transparent; no cloud/account/ecosystem.
//
// XML structure:
//
//   <HostrPreset name="MyPreset" version="2" product="Hostr">
//     <Product slogan="Crea rack paralleli complessi dentro un solo insert..."
//              cloud="none" accountRequired="0" ecosystemLockIn="0"/>
//     <MasterSection inputGainDb="0.0" masterVolumeDb="0.0"/>
//     <MacroControls/>
//     <MacroMappings/>
//     <PluginManifest/>
//     <Slots>
//       <Slot index="0" type="Plugin" bypassed="0"
//             pluginName="EQ Match" format="VST3"
//             fileOrIdentifier="/Library/.../EQ.vst3" uid="12345"
//             manufacturer="Acme">
//         <PluginState>[base64 binary state]</PluginState>
//       </Slot>
//       <Slot index="2" type="ParallelSplit" bypassed="0">
//         <Chain id="chain0" name="Chain 1" muted="0" solo="0"
//                inputGainDb="0.0" outputVolDb="0.0">
//           <Slot index="0" pluginName="Comp" format="VST3"
//                 fileOrIdentifier="..." uid="..." bypassed="0">
//             <PluginState>[base64]</PluginState>
//           </Slot>
//         </Chain>
//         <Chain id="chain1" name="Chain 2" muted="1" solo="0"
//                inputGainDb="0.0" outputVolDb="0.0"/>
//       </Slot>
//     </Slots>
//   </HostrPreset>
//
// ==============================================================================

class PresetManager
{
public:
    explicit PresetManager(PluginProcessor& processor);
    ~PresetManager() = default;

    // Callback invoked BEFORE applyPresetXml destroys processors.
    // The editor must register a lambda here that destroys the MultiParallelPanel
    // (and all ParallelSplitComponents with their raw pointers).
    // This prevents EXC_BAD_ACCESS from dangling pointers 
    // when loading presets into existing ParallelSplit sessions.
    std::function<void()> onPrepareForPresetLoad;

    // After a successfully applied preset, to realign the UI to the restored state.
    std::function<void()> onPresetApplied;

    // Save the current state to a .hostrpreset file in the presets folder
    bool savePresetAs(const juce::String& presetName);

    // Opens a native FileChooser and loads the selected preset.
    //      The callback receives: (success, presetName, cancelled)
    //      cancelled=true means the user pressed Cancel (no error to display).
    //      cancelled=false + success=false means a real parsing/application error.
    void loadPresetFromFile(std::function<void(bool success,
                                               const juce::String& presetName,
                                               bool cancelled)> onComplete);

    // Save the current preset as "default" (loaded on startup)
    bool saveAsDefault();

    // Clears the default preset and clears the current session.
    // After this call, the plugin has all its slots empty, both in memory
    // and on disk (the default.hostrpreset file is deleted).
    bool resetDefaultPreset();

    bool loadDefaultPreset();

    // Serialization / Deserialization 
    // createPresetXml takes a snapshot of the current state; apply PresetXml restores it.
    juce::XmlElement* createPresetXml(const juce::String& presetName) const;
    bool              applyPresetXml (const juce::XmlElement& xml);

    static juce::File getPresetsFolder();
    static juce::File getDefaultPresetFile();

    // Current preset name
    const juce::String& getCurrentPresetName() const { return currentPresetName; }
    void setCurrentPresetName(const juce::String& name) { currentPresetName = name; }

    // Re-entrancy guard — read from PluginProcessor::setStateInformation
    bool isApplyingPreset = false;

private:
    PluginProcessor& processor;
    juce::String     currentPresetName;

    // Helper to serialize / restore the binary state of hosted plugins.
    static juce::String serializePluginState (juce::AudioProcessor* proc);
    static bool         applyPluginState     (juce::AudioProcessor* proc,
                                              const juce::String&   base64State);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
