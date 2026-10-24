# Applies MicVST's small realtime fixes to JUCE (WASAPI backend, processor graph; run by FetchContent's
# PATCH_COMMAND inside the JUCE source tree). Line-ending independent and idempotent: every
# snippet must match exactly once, otherwise the configure step fails loudly instead of
# silently building an unpatched JUCE.
#
#   1. input-only devices: process a backlog immediately after a stall (no permanent extra
#      latency, no later capture overflow) and keep waiting on a 1 s timeout instead of ending
#      the audio thread for good;
#   2. no busy-spinning at MMCSS priority while the WASAPI session is (re)starting/inactive;
#   3. MMCSS "Pro Audio" thread priority HIGH instead of NORMAL;
#   4. AudioProcessorGraph: no buffer reallocation on the audio thread after a chain change;
#   5. AudioDeviceManager: no automatic switch to another device when the open one disappears.

set(marker "MicVST patch")

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

# --- 4. AudioProcessorGraph: no memory allocation on the audio thread after a chain change -----
# The output buffer of a new render sequence is prepared with more channels than the block it
# renders; setSize() then reallocated it on the audio thread (once per chain change, found by the
# realtime probe of the engine test). It is large enough already - keep the allocation.
set(graphFile "modules/juce_audio_processors_headless/processors/juce_AudioProcessorGraph.cpp")
if(NOT EXISTS "${graphFile}")
    message(FATAL_ERROR "PatchJuce: ${graphFile} not found (working directory must be the JUCE root)")
endif()
file(READ "${graphFile}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(FIND "${content}" "${marker}" graphPatched)
if(graphPatched EQUAL -1)
    replace_once([=[        currentAudioOutputBuffer.setSize (jmax (1, buffer.getNumChannels()), numSamples);]=]
[=[        // MicVST patch: sized in prepareBuffers() already - no reallocation on the audio thread.
        currentAudioOutputBuffer.setSize (jmax (1, buffer.getNumChannels()), numSamples, false, false, true);]=])
    file(WRITE "${graphFile}" "${content}")
    message(STATUS "PatchJuce: applied MicVST graph fix to ${graphFile}")
else()
    message(STATUS "PatchJuce: graph fix already applied")
endif()

# --- 5. AudioDeviceManager: no automatic switch to another device ------------------------------
# When the open device disappears (USB microphone unplugged, cable driver restarted), JUCE opened
# the system's default device instead: MicVST then used the laptop microphone, or played the
# processed microphone on the speakers instead of the virtual cable - and stayed there when the
# chosen device returned. Now the device simply closes; AudioEngine reopens the chosen one as soon
# as it is listed again.
set(admFile "modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
if(NOT EXISTS "${admFile}")
    message(FATAL_ERROR "PatchJuce: ${admFile} not found (working directory must be the JUCE root)")
endif()
file(READ "${admFile}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(FIND "${content}" "${marker}" admPatched)
if(admPatched EQUAL -1)
    replace_once([=[            if (auto e = createStateXml())
                initialiseFromXML (*e, true, preferredDeviceName, &currentSetup);
            else
                initialiseDefault (preferredDeviceName, &currentSetup);]=]
[=[            // MicVST patch: never fall back to another device (another microphone, or the speakers
            // instead of the virtual cable); the application reopens its device when it returns.
            if (auto e = createStateXml())
                initialiseFromXML (*e, false, preferredDeviceName, &currentSetup);]=])
    file(WRITE "${admFile}" "${content}")
    message(STATUS "PatchJuce: applied MicVST device-manager fix to ${admFile}")
else()
    message(STATUS "PatchJuce: device-manager fix already applied")
endif()

# --- 6. Version resource of the exe: language Turkish (Türkiye) instead of English (US) ----------
# Windows shows it under Properties > Details > Language. juceaide writes the .rc from this code.
set(rcFile "extras/Build/juce_build_tools/utils/juce_ResourceRc.cpp")
if(NOT EXISTS "${rcFile}")
    message(FATAL_ERROR "PatchJuce: ${rcFile} not found (working directory must be the JUCE root)")
endif()
file(READ "${rcFile}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(FIND "${content}" "041F04B0" rcPatched)
if(rcPatched EQUAL -1)
    replace_once([=[BLOCK \"040904E4\"]=] [=[BLOCK \"041F04B0\"]=])
    replace_once([=[VALUE \"Translation\", 0x409, 1252]=] [=[VALUE \"Translation\", 0x41F, 1200]=])
    file(WRITE "${rcFile}" "${content}")
    message(STATUS "PatchJuce: version resource language set to Turkish (0x041F) in ${rcFile}")
else()
    message(STATUS "PatchJuce: version resource language already Turkish")
endif()

# --- 7. Windows drawing: graphics card (Direct2D) -> processor (GDI) without a ghost -------------
# Interface only (window drawing; nothing of the audio). Direct2D shows its picture through a
# DirectComposition visual on top of the window; switching a window to GDI (Ayarlar > Arayüz çizimi)
# only released that visual, and Windows kept showing Direct2D's last frame for a while - over the
# new GDI picture or alternating with it (ghost, flicker). Now (a) the visual is taken off the window
# at once (SetRoot (nullptr), Commit, WaitForCommitCompletion) whenever a Direct2D context goes away,
# and (b) for a switch to GDI the new picture is painted first, into the window's own surface that is
# still covered by the visual, and only then is Direct2D removed - so the screen goes from the old
# picture straight to the new one.
set(d2dFile "modules/juce_gui_basics/native/juce_Direct2DHwndContext_windows.cpp")
if(NOT EXISTS "${d2dFile}")
    message(FATAL_ERROR "PatchJuce: ${d2dFile} not found (working directory must be the JUCE root)")
endif()
file(READ "${d2dFile}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(FIND "${content}" "${marker}" d2dPatched)
if(d2dPatched EQUAL -1)
    replace_once([=[
private:
    CompositionTree() = default;
]=] [=[
    // MicVST patch: take the visual off the window at once. Only released, Windows kept showing
    // Direct2D's last frame for a while after the window was switched to GDI (ghost / flicker).
    void detach()
    {
        if (compositionTarget != nullptr)
            compositionTarget->SetRoot (nullptr);

        if (compositionDevice != nullptr)
        {
            compositionDevice->Commit();
            compositionDevice->WaitForCommitCompletion();
        }
    }

private:
    CompositionTree() = default;
]=])
    replace_once([=[    void teardown() override
    {
        compositionTree.reset();]=] [=[    void teardown() override
    {
        if (compositionTree.has_value())   // MicVST patch: see CompositionTree::detach
            compositionTree->detach();

        compositionTree.reset();]=])
    replace_once([=[    ~HwndPimpl() override = default;]=] [=[    ~HwndPimpl() override   // MicVST patch: see CompositionTree::detach
    {
        if (compositionTree.has_value())
            compositionTree->detach();
    }]=])
    file(WRITE "${d2dFile}" "${content}")
    message(STATUS "PatchJuce: applied MicVST Direct2D detach fix to ${d2dFile}")
else()
    message(STATUS "PatchJuce: Direct2D detach fix already applied")
endif()

set(winFile "modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
if(NOT EXISTS "${winFile}")
    message(FATAL_ERROR "PatchJuce: ${winFile} not found (working directory must be the JUCE root)")
endif()
file(READ "${winFile}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(FIND "${content}" "${marker}" winPatched)
if(winPatched EQUAL -1)
    replace_once([=[        // Reset the old context before creating the new context, because some context resources
        // can only be created once per window.
        renderContext.reset();
        renderContext = contextDescriptors[e].construct (*this);
        InvalidateRect (hwnd, nullptr, TRUE);]=] [=[        // MicVST patch: to the software renderer (GDI) the new picture is painted first - into the
        // window's own surface, still covered by Direct2D's composition visual - and only then is
        // Direct2D removed (its visual detached at once, see CompositionTree::detach). Before, the
        // screen showed Direct2D's last frame over or alternating with the GDI picture for a while.
        if (renderContext != nullptr && contextDescriptors[e].name == GDIRenderContext::name)
        {
            auto previous = std::move (renderContext);
            renderContext = contextDescriptors[e].construct (*this);
            RedrawWindow (hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
            previous.reset();
            return;
        }

        // Reset the old context before creating the new context, because some context resources
        // can only be created once per window.
        renderContext.reset();
        renderContext = contextDescriptors[e].construct (*this);
        InvalidateRect (hwnd, nullptr, TRUE);]=])
    file(WRITE "${winFile}" "${content}")
    message(STATUS "PatchJuce: applied MicVST seamless GDI switch to ${winFile}")
else()
    message(STATUS "PatchJuce: seamless GDI switch already applied")
endif()

set(file "modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")

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
