# Performance and memory checks

Build `RelWithDebInfo`, launch CatClicker, then run:

```bash
python3 tools/profile_process.py --pid "$(pidof CatClicker)" --duration 60 --interval 0.25
```

Measure identical durations for idle, active mouse recording, playback with smoothing off, playback with smoothing on, and repeated record/play cycles. The tool reports average and peak CPU, start/average/peak/end RSS, and RSS growth. It reads only the target process CPU and memory values from `/proc`.

Lifecycle regression tests cover repeated construction, recording, playback, refresh, cancellation, and release. ASan, UBSan, and LeakSanitizer provide stronger evidence when supported. RSS growth alone does not prove a leak. Valgrind and heaptrack are optional when installed.

No interactive values are published yet. Representative capture and playback require host interaction after this restructure.

## Current automated evidence

On 2026-09-02, the normal suite passed once and then passed 20 consecutive full cycles. Each cycle ran 101 Qt test functions plus the safe diagnostics and no-network audits. The ASan/UBSan build passed the full suite with leak detection disabled. With `detect_leaks=1`, all test functions passed before LeakSanitizer terminated because the execution environment uses tracing. This is an LSan environment limitation, not evidence for or against leaks. Valgrind and heaptrack were not installed.
