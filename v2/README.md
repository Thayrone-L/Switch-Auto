# Switch Auto V2

The V2 codebase is a focused Android Auto head-unit implementation for the
Nintendo Switch. Its first release target is a stable, low-latency 1280x720
projection session. Widgets, themes and companion customization remain future
layers and must not compromise the projection pipeline.

## Non-negotiable goals

- Hardware H.264 decoding through FFmpeg's NVTEGRA backend.
- Stable 720p30 before enabling 720p60.
- Touch/control traffic must never wait for video decode or presentation.
- Bounded queues, observable latency and explicit recovery on packet loss.
- Software decode is diagnostic fallback only.

See [architecture](docs/architecture.md) and [milestones](docs/milestones.md).

