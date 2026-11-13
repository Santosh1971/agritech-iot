#pragma once
// Zone turns: one pressure switch (on the zone "leader" node) feeds several
// nodes, but the pipe pressure is only enough for some valves at a time. The
// user lists the valves in turns; turn 1 runs first, and the next turn starts
// only when every valve of the earlier turns has finished today's program.
// Pure C++ so the rules are unit tested on the PC.
#include <stdint.h>

namespace wm2core {

constexpr int SEQ_MAX_TURNS = 8;
constexpr int SEQ_MAX_PER_TURN = 8;
constexpr uint32_t SEQ_LOST_S = 90;     // no report this long: node is lost, its valves are skipped
constexpr uint32_t SEQ_STARTUP_S = 45;  // after the leader boots, wait this long for first reports
constexpr uint32_t SEQ_NEVER = 0xFFFFFFFFu;

enum SeqState : uint8_t { SEQ_PENDING = 0, SEQ_FINISHED, SEQ_LOST };

// State of one listed valve. `self`: the valve is on the leader itself.
// heardAgoS: seconds since the member last reported (SEQ_NEVER = never).
inline uint8_t seqEntryState(bool self, bool due, bool done, uint32_t heardAgoS, uint32_t uptimeS) {
  if (!self) {
    if (heardAgoS == SEQ_NEVER) return uptimeS < SEQ_STARTUP_S ? SEQ_PENDING : SEQ_LOST;
    if (heardAgoS > SEQ_LOST_S) return SEQ_LOST;
  }
  return due && !done ? SEQ_PENDING : SEQ_FINISHED;
}

// 0-based index of the first turn that still has a pending valve, -1 when
// every turn is finished for today (lost valves count as finished).
inline int seqCurrentTurn(const uint8_t st[][SEQ_MAX_PER_TURN], const uint8_t* counts, int nTurns) {
  for (int t = 0; t < nTurns; t++)
    for (int k = 0; k < counts[t]; k++)
      if (st[t][k] == SEQ_PENDING) return t;
  return -1;
}

}  // namespace wm2core
