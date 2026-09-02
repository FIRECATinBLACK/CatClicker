# COSMIC Wayland cursor design

Safe mouse macros need a trustworthy absolute pointer position. Passive evdev supplies keys, buttons, wheel events, and relative deltas. Uinput supplies playback. Neither tells a Wayland client the compositor's logical cursor position.

Relative `REL_X` and `REL_Y` values can be transformed, accelerated, dropped, or coalesced. CatClicker treats them only as triggers for an absolute sample and never adds them to a previous coordinate.

COSMIC exposes cursor metadata through the ext image copy capture protocol family. A session can publish enter, position, and mapping data. On the validated COSMIC version, a new session yields the current position but does not continue updating as expected. CatClicker therefore uses bounded session recreation.

Only one refresh may be outstanding. More triggers become one coalesced follow-up. Generations stop late callbacks from an old session publishing into a newer recording or lifetime. This recreation is deliberate until a safe continuous path is demonstrated by tests and host validation.

Buttons and scroll wait for an exact trusted anchor. Unresolved events are dropped at recording stop. Saved anchors are authoritative, and playback moves to them before injection. Smooth playback is separate and cosmetic.

## Adding another compositor backend

A provider must publish trustworthy absolute logical coordinates, never fabricate positions, never silently integrate relative motion, respect layout and mapping, invalidate stale samples, and preserve exact button and scroll safety. This workaround is COSMIC-specific, not a universal Wayland rule.
