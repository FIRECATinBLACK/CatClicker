# Performance and memory checks

Build `RelWithDebInfo`, launch CatClicker, then run:

```bash
python3 tools/profile_process.py --pid "$(pidof CatClicker)" --duration 60 --interval 0.25
```

Measure identical durations for idle, active mouse recording, playback with smoothing off, playback with smoothing on, and repeated record/play cycles. The tool reports average and peak CPU, start/average/peak/end RSS, and RSS growth. It reads only the target process CPU and memory values from `/proc`.

Lifecycle regression tests cover repeated construction, recording, playback, refresh, cancellation, and release. ASan, UBSan, and LeakSanitizer provide stronger evidence when supported. RSS growth alone does not prove a leak. Valgrind and heaptrack are optional when installed.

## Representative host workload

A Release build was measured for 3600 seconds at a 5-second sampling interval. The profiler started before the user recorded a fresh macro with normal mouse and keyboard activity. The recorded macro was then used for continuous loop playback for the remainder of the measured hour.

```text
Average CPU: 0.80 %
Peak CPU: 7.20 %
Starting RSS: 176268 KiB
Average RSS: 185938 KiB
Peak RSS: 186012 KiB
Ending RSS: 186012 KiB
RSS growth: 9744 KiB
```

This mixed recording and sustained-playback workload showed no obvious sustained RSS growth in the one-hour summary. The summary does not identify when the CPU peak occurred and does not establish that the process is leak-free or that memory use is formally bounded. CatClicker continued running and looping successfully for approximately three hours in total, but only the first hour was instrumented.

## Current automated evidence

On 2026-09-02, the normal suite passed once and then passed 20 consecutive full cycles. Each cycle ran the complete Qt test suite plus the safe diagnostics and no-network audits. The ASan/UBSan build passed the full suite with leak detection disabled. With `detect_leaks=1`, all test functions passed before LeakSanitizer terminated because the execution environment uses tracing. This is an LSan environment limitation, not evidence for or against leaks. Valgrind and heaptrack were not installed.
