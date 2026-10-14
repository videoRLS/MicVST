# Applies MicVST's small realtime fixes to JUCE's WASAPI backend (run by FetchContent's
# PATCH_COMMAND inside the JUCE source tree). Line-ending independent and idempotent: every
# snippet must match exactly once, otherwise the configure step fails loudly instead of
# silently building an unpatched JUCE.
#
#   1. input-only devices: process a backlog immediately after a stall (no permanent extra
#      latency, no later capture overflow) and keep waiting on a 1 s timeout instead of ending
#      the audio thread for good;
#   2. no busy-spinning at MMCSS priority while the WASAPI session is (re)starting/inactive;
#   3. MMCSS "Pro Audio" thread priority HIGH instead of NORMAL.

set(file "modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")
set(marker "MicVST patch")

if(NOT EXISTS "${file}")
    message(FATAL_ERROR "PatchJuce: ${file} not found (working directory must be the JUCE root)")
endif()

file(READ "${file}" content)
string(REPLACE "\r\n" "\n" content "${content}")

string(FIND "${content}" "${marker}" alreadyPatched)
if(NOT alreadyPatched EQUAL -1)
    message(STATUS "PatchJuce: WASAPI realtime fixes already applied")
    return()
endif()

function(replace_once oldText newText)
    string(FIND "${content}" "${oldText}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "PatchJuce: snippet not found - JUCE version changed? Snippet:\n${oldText}")
    endif()
    string(LENGTH "${oldText}" len)
    math(EXPR afterFirst "${first} + ${len}")
    string(SUBSTRING "${content}" ${afterFirst} -1 rest)
    string(FIND "${rest}" "${oldText}" second)
    if(NOT second EQUAL -1)
        message(FATAL_ERROR "PatchJuce: snippet is not unique:\n${oldText}")
    endif()
    string(REPLACE "${oldText}" "${newText}" result "${content}")
    set(content "${result}" PARENT_SCOPE)
endfunction()

# --- 3. MMCSS priority ------------------------------------------------------------------------
replace_once([=[        if (auto h = avSetMmThreadCharacteristics (L"Pro Audio", &dummy))
            avSetMmThreadPriority (h, AVRT_PRIORITY_NORMAL);]=]
[=[        // MicVST patch: HIGH instead of NORMAL inside the "Pro Audio" MMCSS task, so the audio
        // thread keeps its deadline against busy game/render threads (not CRITICAL, which would
        // compete with the Windows audio engine itself).
        if (auto h = avSetMmThreadCharacteristics (L"Pro Audio", &dummy))
            avSetMmThreadPriority (h, AVRT_PRIORITY_HIGH);]=])

# --- 2. no busy spin while the session is inactive ---------------------------------------------
replace_once([=[            if (! inputDeviceActive && ! outputDeviceActive)
                continue;
]=]
[=[            if (! inputDeviceActive && ! outputDeviceActive)
            {
                // MicVST patch: the session is (re)starting or inactive - sleep briefly instead of
                // busy-spinning a whole core at MMCSS priority.
                Thread::sleep (1);
                continue;
            }
]=])

# --- 1. input-only devices: drain backlogs, survive timeouts -----------------------------------
replace_once([=[                if (outputDevice == nullptr)
                {
                    if (WaitForSingleObject (inputDevice->clientEvent, 1000) == WAIT_TIMEOUT)
                        break;

                    inputDevice->handleDeviceBuffer();
]=]
[=[                if (outputDevice == nullptr)
                {
                    // MicVST patch (input-only device):
                    //  * only wait when the reservoir does not already hold a full block, so a
                    //    backlog after a scheduling stall is processed at once instead of turning
                    //    into permanent extra latency (and later into capture-buffer overflows);
                    //  * a 1 s timeout keeps waiting instead of ending the audio thread for good
                    //    (the stream silently died; now it resumes when data flows again).
                    if (inputDevice->getNumSamplesInReservoir() < bufferSize
                         && WaitForSingleObject (inputDevice->clientEvent, 1000) == WAIT_TIMEOUT)
                        continue;

                    inputDevice->handleDeviceBuffer();
]=])

file(WRITE "${file}" "${content}")
message(STATUS "PatchJuce: applied MicVST WASAPI realtime fixes to ${file}")
