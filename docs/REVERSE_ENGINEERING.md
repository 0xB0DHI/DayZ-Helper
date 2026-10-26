# Structure research behind DayZ Helper

These are selected findings from the original private, read-only controller for **DayZ 1.30.164200.56**. They are research context; this public preview has no live game-data source. Community notes targeted an older patch, so current-build structures were rechecked in Ghidra and then against local live sessions before use.

| Area | Finding in the original controller |
| --- | --- |
| World and entities | A build-specific game root leads to several entity tables and separate item buckets. Membership refreshes on a slower cadence than the camera and visible positions. |
| Projection | Live camera and viewport observations established the DayZ world-to-screen path; related Enfusion games were used only as architecture examples. |
| Skeletons | Player and infected rigs have different roots. Bone names resolve through an animation name map, then bounded matrix spans supply the current pose. |
| Item presentation | Category gates limit reads before rendering; nearby items can use numbered dots and a matching anchored name list. |
| Publication | Incomplete or changing samples are discarded before a frame reaches the renderer. The renderer performs no target-memory reads. |

The public copy retains the renderer, configuration, projection helpers, and deterministic fake-memory fixtures. `src/data/placeholder_reader.h` is the integration boundary and always returns failure without touching the caller's output.
