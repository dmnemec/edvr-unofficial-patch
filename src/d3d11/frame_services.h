#pragma once

namespace edvr {

class Config;

// Decoupled frame-level engine services (static prop gate, scheduler stack probe)
// that run independently of whether temporal-aa is installed or enabled.
void coreFrameConfigure(const Config& cfg);
void coreFrameShutdown();

}  // namespace edvr
