# V2 milestones

## M0 — NVTEGRA capability probe

- Create the NVTEGRA FFmpeg hardware device on the Switch.
- Decode a known H.264 sample without CPU fallback.
- Report decoder format, latency and errors on screen and in diagnostics.

## M1 — Fullscreen 720p30

- Connect the existing Android Auto transport to the V2 decoder.
- Present hardware surfaces fullscreen with correct frame pacing.
- Sustain a 30-minute interaction test without corruption or restart.

## M2 — Low-latency input and recovery

- Give control/touch independent priority.
- Bound video queues by complete access units.
- Recover through IDR request without restarting the projection session.

## M3 — 720p60 qualification

- Negotiate and verify actual 60 fps output from the phone.
- Keep frame-time and touch-latency percentiles within the V2 budget.
- Pass rapid-touch, route recalculation and app-switch stress tests.

## Deferred until projection is qualified

- Widgets and overlays.
- Themes and animations.
- Companion customization UI.
- Offline maps and MapLibre fallback.

