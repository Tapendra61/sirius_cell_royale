#pragma once

#include "ITransport.h"

#include <deque>

namespace cr {

class LocalTransport : public ITransport {
public:
    void sendCommand(Command cmd) override;
    bool pollCommand(Command& out) override;

    void sendSnapshot(Snapshot snap) override;
    bool pollSnapshot(Snapshot& out) override;

    void sendEvent(GameEvent ev) override;
    bool pollEvent(GameEvent& out) override;

private:
    std::deque<Command>   commands_;
    std::deque<Snapshot>  snapshots_;
    std::deque<GameEvent> events_;
};

} // namespace cr
