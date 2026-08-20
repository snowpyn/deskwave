# DeskWave architecture

DeskWave is split across a Linux host and an ESP32-S3 firmware application.

```text
MPRIS media player -> D-Bus -> DeskWave Host -> authenticated WebSocket -> ESP32
                                                                         |
                                                                  display + input
```

The firmware uses the Arduino framework through a pinned PlatformIO platform.
Hardware-dependent pins and drivers will be isolated behind one configuration
layer. The host uses a Python `src` package and will keep media backends,
protocol validation, persistence, discovery, and HTTP/WebSocket delivery as
separate modules.

Protocol version 1 is the initial compatibility boundary. The implementation
must reject malformed or unauthenticated messages and remain recoverable when
the host, player, or network disappears.
