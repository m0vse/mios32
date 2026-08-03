function(patch_juce_winmm_sysex_wait juce_root)
    set(midi_source
        "${juce_root}/modules/juce_audio_devices/native/juce_Midi_windows.cpp")

    if(NOT EXISTS "${midi_source}")
        message(FATAL_ERROR "JUCE Windows MIDI source not found: ${midi_source}")
    endif()

    file(READ "${midi_source}" source_text)

    set(patch_marker "int completionTimeout = 2000; // MIOS Studio device-loss guard")
    string(FIND "${source_text}" "${patch_marker}" patch_marker_position)
    if(NOT patch_marker_position EQUAL -1)
        return()
    endif()

    string(REPLACE "\r\n" "\n" normalized_source "${source_text}")
    string(CONCAT original_wait
        "while ((h.dwFlags & MHDR_DONE) == 0)\n"
        "                                Sleep (1);")
    string(CONCAT bounded_wait
        "int completionTimeout = 2000; // MIOS Studio device-loss guard\n"
        "                            while ((h.dwFlags & MHDR_DONE) == 0 && --completionTimeout >= 0)\n"
        "                                Sleep (1);\n\n"
        "                            if ((h.dwFlags & MHDR_DONE) == 0)\n"
        "                                midiOutReset (handle);")

    string(REPLACE "${original_wait}" "${bounded_wait}"
           patched_source "${normalized_source}")

    if(patched_source STREQUAL normalized_source)
        message(FATAL_ERROR
            "JUCE WinMM SysEx wait did not match the expected JUCE 9.0.0 source. "
            "Review the local timeout patch before updating JUCE.")
    endif()

    file(WRITE "${midi_source}" "${patched_source}")
    message(STATUS "Applied MIOS Studio WinMM SysEx device-loss guard")
endfunction()
