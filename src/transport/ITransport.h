#pragma once

#include "core/Command.h"
#include "core/Events.h"
#include "core/Snapshot.h"

namespace cr {

// Bidirectional pipe between Client (input/render) and Simulation (authoritative state).
// LocalTransport is in-process queues; NetworkTransport (Phase 10) is ENet over UDP.
class ITransport {
public:
    virtual ~ITransport() = default;

    // Client -> Sim
    virtual void sendCommand(Command cmd)     = 0;
    virtual bool pollCommand(Command& out)    = 0;

    // Sim -> Client
    virtual void sendSnapshot(Snapshot snap)  = 0;
    virtual bool pollSnapshot(Snapshot& out)  = 0;

    virtual void sendEvent(GameEvent ev)      = 0;
    virtual bool pollEvent(GameEvent& out)    = 0;
};

} // namespace cr
