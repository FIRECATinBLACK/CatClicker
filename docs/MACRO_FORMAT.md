# `.catmacro` version 1

The file is UTF-8 JSON with `format`, `version`, `name`, `duration_us`, `created_at_utc`, `display`, and `events`. Version 1 uses `format: "CatClicker Macro"` and `version: 1`.

Display metadata records stream and logical dimensions, logical origin, scale, and identifier. Playback checks compatibility because absolute anchors need a compatible mapping.

Events have a non-negative chronological `time_us` and a supported type: `key`, `mouse_move`, `mouse_button`, or `scroll`. Key events contain a Linux key code and pressed state. Mouse actions contain finite coordinates. Buttons and scroll include trusted anchor fields.

Files are limited to 64 MiB, 2,000,000 events, and 1024 characters for names and identifiers. Numeric domains must be finite and timestamps cannot exceed the declared duration. These bounds support long recordings while limiting memory and parse work.

Playback speed and smoothing are runtime options and are not stored. Smoothing never changes saved anchors. Version 1 is supported; additional versions and stricter validation may appear before 1.0.
