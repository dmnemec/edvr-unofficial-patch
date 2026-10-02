#include "frame_services.h"

#include "scheduler_stack_probe.h"
#include "static_prop_gate.h"
#include "../common/config.h"

namespace edvr {

void coreFrameConfigure(const Config& cfg) {
    schedulerStackProbeConfigure(cfg.getBool("advanced.scheduler_probe", false));
    staticPropGateConfigure(cfg.getBool("fix.static_prop_updates", false));
}

void coreFrameShutdown() {
    schedulerStackProbeConfigure(false);
    staticPropGateConfigure(false);
}

}  // namespace edvr
