# Debugging

Enable Developer Info in Settings to preview the safe report. Inspect it before choosing Copy Diagnostics. Actionable permission and recording errors remain visible when Developer Info is off.

`CATCLICKER_TRACE_STARTUP`, `CATCLICKER_TRACE_LOOP`, and `CATCLICKER_TRACE_CURSOR_HEALTH` provide startup, loop, and aggregate cursor details. `CATCLICKER_DISABLE_COSMIC_CURSOR` and `CATCLICKER_DISABLE_COSMIC_CURSOR_SAMPLER` disable provider paths for troubleshooting.

`CATCLICKER_TRACE_CURSOR`, `CATCLICKER_TRACE_RECORDING_CURSOR`, and `CATCLICKER_TRACE_UINPUT_EVENTS` can disclose keys, buttons, coordinates, or activity. They intentionally remain environment-only and should not appear in public reports.
