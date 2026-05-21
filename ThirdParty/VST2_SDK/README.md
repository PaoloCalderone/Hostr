# VST2 SDK

Hostr can host VST2 plugins on macOS, Windows, and Linux, but JUCE requires the
legacy Steinberg VST2 headers at build time.

This repository does not include those headers. Copy them from a VST2-capable
Steinberg SDK that you are licensed to use.

Expected layout:

```text
ThirdParty/VST2_SDK/
  pluginterfaces/
    vst2.x/
      aeffect.h
      aeffectx.h
      vstfxstore.h
```

You can prepare the folder with:

```bash
python3 tools/prepare_vst2_sdk.py /path/to/SteinbergSDK
```

After the files are present, re-save `Hostr.jucer` with Projucer or build from
the generated Xcode, Visual Studio, or Linux Makefile projects.
