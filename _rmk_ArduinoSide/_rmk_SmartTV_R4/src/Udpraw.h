#pragma once
// Udpraw.h - Ambilight UDP stream receiver (Link B side, port 5568): drain
// queued packets to the newest, map RGB triplets straight onto the TV
// strip, smooth-fade the non-TV zones to match. Ported 1:1 from
// _FuZzAPP_SmartTV_R4.ino's UDPRAW namespace (~387 lines).
#include "Scheduler.h"

namespace UDPRAW {

void Setup();
void Loop();
void T_UDPRAW_SET_COLOR(SCHED::TaskId taskId);
void End(bool handover = true);
void Init();
void UdpSet();

} // namespace UDPRAW
