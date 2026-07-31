# V2 architecture

## Product boundary

Android Auto projection is the product core. The Android companion only
bootstraps connectivity and later provides Switch Auto customization. MapLibre,
the legacy dashboard and first-generation widgets are not dependencies of V2.

## Runtime pipeline

```text
Phone socket
  -> TLS/control transport
       -> priority control queue -> touch, sensors, session state
       -> video access-unit queue (bounded)
            -> NVTEGRA H.264 decoder
                 -> NV12 hardware surface
                      -> GPU presenter
                           -> fullscreen surface
```

## Thread ownership

- `Control`: protocol state, acknowledgements, touch and lifecycle.
- `VideoRx`: framing and complete H.264 access-unit assembly.
- `Decode`: NVDEC submission and completion.
- `Render`: frame pacing and presentation only.

No disk logging, allocation-heavy diagnostics or UI work may execute on the
control or video receive paths.

## Queue policy

- Control input has priority and is coalesced only for consecutive move events.
- Video queues are bounded to a few complete access units, never hundreds of
  arbitrary protocol packets.
- Reference frames are never discarded silently.
- Corruption or sustained backlog requests an IDR/keyframe and resets only the
  video pipeline, not the complete Android Auto session.

## Decoder contract

The core depends on `IVideoDecoder`; platform implementations own decoder
resources. `NvTegraH264Decoder` is the production implementation and software
FFmpeg is retained only for diagnostics and unsupported environments.

Decoded surfaces should remain in hardware-compatible YUV/NV12 memory through
presentation. CPU readback and per-frame RGBA conversion are forbidden in the
production path.

## Performance budget at 720p60

- Frame interval: 16.67 ms.
- Network/framing/control: under 1 ms typical and never blocked by decode.
- Hardware decode and synchronization: target under 4 ms.
- Presentation: target under 3 ms without CPU copies.
- Input dispatch: target under 10 ms from Switch touch to socket write.

Metrics must include receive FPS, decode latency percentiles, presentation
latency, queue depth, dropped presentations, keyframe recovery and touch RTT.

