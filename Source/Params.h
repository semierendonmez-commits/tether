// Parameter ids of TETHER.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline constexpr const char* link = "link", *sync = "sync", *pset = "pset";
    inline juce::String slot (int s) { return "slot" + juce::String (s + 1).paddedLeft ('0', 2); }   // slot01 .. slot32
}
