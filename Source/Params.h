// Parameter ids of TETHER.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline constexpr const char* link = "link", *sync = "sync", *pset = "pset";
    // audio: from norns (both builds), to norns and THRU (TETHER FX)
    inline constexpr const char* audioFrom = "audio_from", *audioTo = "audio_to", *audioBuffer = "audio_buffer",
                                 *audioBits = "audio_bits", *audioFromGain = "audio_fromgain", *audioToGain = "audio_togain",
                                 *audioThru = "audio_thru";
    inline juce::String slot (int s) { return "slot" + juce::String (s + 1).paddedLeft ('0', 2); }   // slot01 .. slot32
}
