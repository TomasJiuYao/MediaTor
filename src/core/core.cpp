#include "ter/core.hpp"

namespace ter {

Core::Core() = default;

Core::~Core() {
    shutdown();
}

bool Core::init() {
    running_ = true;
    return true;
}

void Core::run() {
    while (running_) {
        // main loop
    }
}

void Core::shutdown() {
    running_ = false;
}

} // namespace ter
