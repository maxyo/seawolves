// S_EVENTTAB::HasActiveHandler, the interpreter's answer to the engine's question whether the boarding continuation
// handler LAi_ActivateReload is armed (COMPILER::HasActiveEventHandler).
#include "s_eventtab.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// Function codes of the script program.
constexpr uint32_t activateReload = 0, other = 1;

int main()
{
    S_EVENTTAB events;
    const char *event = "Control Activation";
    assert(!events.HasActiveHandler(event, activateReload)); // no such event yet
    events.AddEventHandler(event, 1, 0, 0);
    events.ProcessFrame();
    assert(!events.HasActiveHandler(event, activateReload)); // another handler only
    assert(!events.HasActiveHandler(nullptr, activateReload));
    events.AddEventHandler(event, 0, 0, 1); // SetEventHandler(..., 1): from the next frame on
    assert(!events.HasActiveHandler(event, activateReload));
    events.ProcessFrame();
    assert(events.HasActiveHandler(event, activateReload));  // armed: ProcessEvent would run it
    events.SetStatus(event, 0, FSTATUS_DELETED);             // DelEventHandler
    assert(!events.HasActiveHandler(event, activateReload)); // deleted: skipped at once
    events.ProcessFrame();
    assert(!events.HasActiveHandler(event, activateReload));
    assert(events.HasActiveHandler(event, other));

    // A removal shifts the later handlers down: the one moved onto index 0 must be looked at too.
    const auto handlers = [&](const char *name) {
        const EVENTINFO *info = events.GetEvent(events.FindEvent(name));
        assert(info);
        return info->elements;
    };
    const char *unloaded = "Unloaded segment";
    events.AddEventHandler(unloaded, 0, 7, 0);
    events.AddEventHandler(unloaded, 1, 7, 0);
    events.AddEventHandler(unloaded, 2, 8, 0);
    events.InvalidateBySegmentID(7);
    assert(handlers(unloaded) == 1); // both handlers of segment 7 gone
    const char *removed = "Two removed";
    events.AddEventHandler(removed, 0, 0, 0);
    events.AddEventHandler(removed, 1, 0, 0);
    events.AddEventHandler(removed, 2, 0, 0);
    events.SetStatus(removed, 0, FSTATUS_DELETED);
    events.SetStatus(removed, 1, FSTATUS_DELETED);
    events.ProcessFrame();
    assert(handlers(removed) == 1);                           // both deleted handlers gone in one frame
    assert(events.HasActiveHandler(removed, other) == false); // func 1 was deleted
    events.AddEventHandler(removed, 0, 0, 1);                 // new handler: armed next frame
    events.SetStatus(removed, 2, FSTATUS_DELETED);
    events.ProcessFrame();
    assert(handlers(removed) == 1 && events.HasActiveHandler(removed, activateReload));
    std::cout << "PASS HasActiveEventHandler: absent event, other handler, not yet armed, armed, deleted; "
                 "removals of adjacent handlers (segment unload, deleted status) take them all\n";
}
