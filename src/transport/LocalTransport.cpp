#include "LocalTransport.h"

#include <utility>

namespace cr {

void LocalTransport::sendCommand(Command cmd) {
    commands_.push_back(std::move(cmd));
}

bool LocalTransport::pollCommand(Command& out) {
    if (commands_.empty()) return false;
    out = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

void LocalTransport::sendSnapshot(Snapshot snap) {
    snapshots_.push_back(std::move(snap));
}

bool LocalTransport::pollSnapshot(Snapshot& out) {
    if (snapshots_.empty()) return false;
    out = std::move(snapshots_.front());
    snapshots_.pop_front();
    return true;
}

void LocalTransport::sendEvent(GameEvent ev) {
    events_.push_back(std::move(ev));
}

bool LocalTransport::pollEvent(GameEvent& out) {
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

} // namespace cr
