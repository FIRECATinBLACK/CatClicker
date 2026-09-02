# Architecture

CatClicker separates physical capture, trusted cursor positions, the macro timeline, virtual playback, and UI orchestration.

```text
physical evdev input
    -> GlobalInputMonitor
    -> EvdevCaptureBackend
    -> MacroRecorder
    -> .catmacro timeline

COSMIC cursor metadata
    -> CosmicCursorPositionProvider
    -> trusted absolute mouse coordinates

.catmacro
    -> MacroPlayer
    -> UinputInputSender
    -> virtual keyboard and pointer events
```

`ApplicationController` owns long-lived services and exposes state to QML. QML does not access devices. `GlobalInputMonitor` reads physical evdev devices on a worker thread and excludes CatClicker's virtual devices. Capture backends translate input into domain events. Relative motion only requests a trusted cursor sample.

Input devices have independent lifecycles. A fatal poll or read failure retires only the affected physical fd while healthy devices remain active. The monitor rescans missing pointer or keyboard roles and reopens them when available, while continuing to exclude CatClicker's virtual uinput devices. After `SYN_DROPPED`, incomplete events from that fd are ignored until `SYN_REPORT`. Physical pointer disconnect and reconnect recovery has been host validated, and missing input is never reconstructed.

The COSMIC provider normally recreates its pointer cursor metadata session to sample an absolute position. If continuing physical motion produces a pathological streak of identical generation-matched positions, the sampler performs a bounded deeper reset of the capture source and its dependent cursor session. The trusted snapshot is invalidated during this reset. Old-generation callbacks cannot resolve pending movement, and keyboard capture remains independent.

`Macro` and `MacroEvent` form the stored model. `MacroRecorder` creates the timeline. `MacroSerializer` validates untrusted JSON. `MacroPlayer` schedules a timeline copy on its worker thread and delegates injection. QObject ownership, explicit worker shutdown, and callback generations prevent stale work from outliving consumers. Playback releases held virtual input on completion, cancellation, error, and shutdown.
