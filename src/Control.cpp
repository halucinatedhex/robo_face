#include "Control.h"

Control control;

void Control::begin() {
    // TODO: bring up whatever replaces the IR remote (Wi-Fi, BLE, buttons).
}

Command Control::poll(uint32_t now) {
    (void)now;
    // TODO: translate the new input source into a Command and return it.
    return Command::None;
}
