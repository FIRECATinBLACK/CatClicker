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

`Macro` and `MacroEvent` form the stored model. `MacroRecorder` creates the timeline. `MacroSerializer` validates untrusted JSON. `MacroPlayer` schedules a timeline copy on its worker thread and delegates injection. QObject ownership, explicit worker shutdown, and callback generations prevent stale work from outliving consumers. Playback releases held virtual input on completion, cancellation, error, and shutdown.
