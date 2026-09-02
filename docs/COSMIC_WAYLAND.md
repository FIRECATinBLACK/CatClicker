# COSMIC Wayland cursor design

Safe mouse macros need a trustworthy absolute pointer position. Passive evdev supplies keys, buttons, wheel events, and relative deltas. Uinput supplies playback. Neither tells a Wayland client the compositor's logical cursor position.

Relative `REL_X` and `REL_Y` values can be transformed, accelerated, dropped, or coalesced. CatClicker treats them only as triggers for an absolute sample and never adds them to a previous coordinate.

COSMIC exposes cursor metadata through the ext image copy capture protocol family. A session can publish enter, position, and mapping data. On the validated COSMIC version, a new session yields the current position but does not continue updating as expected. CatClicker therefore uses bounded session recreation.

Only one refresh may be outstanding. More triggers become one coalesced follow-up. Generations stop late callbacks from an old session publishing into a newer recording or lifetime. This recreation is deliberate until a safe continuous path is demonstrated by tests and host validation.

Consecutive refreshes can occasionally return the same trusted absolute position even while physical `REL` triggers continue. Identical results remain duplicate-suppressed. When that condition becomes pathological, CatClicker performs one bounded deeper recovery: it invalidates the trusted position, destroys the stale `ext_image_capture_source_v1` and dependent pointer cursor metadata session, then recreates both. The display connection, registry, seat, pointer, output, and protocol managers remain intact. Recovery requests are rate-limited to avoid compositor churn.

Only a new position from the current generation can restore trust. Callbacks from objects retired by the reset cannot resolve pending movement. Buttons and scroll remain unanchored until a new trusted position arrives and are dropped if still unresolved at recording stop. Keyboard capture does not wait for cursor recovery. This stale-position recovery has been host validated on Pop!_OS COSMIC.

Buttons and scroll wait for an exact trusted anchor. Unresolved events are dropped at recording stop. Saved anchors are authoritative, and playback moves to them before injection. Smooth playback is separate and cosmetic.

## Adding another compositor backend

A provider must publish trustworthy absolute logical coordinates, never fabricate positions, never silently integrate relative motion, respect layout and mapping, invalidate stale samples, and preserve exact button and scroll safety. This workaround is COSMIC-specific, not a universal Wayland rule.
