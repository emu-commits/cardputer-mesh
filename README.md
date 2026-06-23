# cardputer-mesh

Offline, mesh-first pocket terminal firmware for the **M5Stack Cardputer ADV**,
built on [Plai](https://github.com/d4rkmen/plai) (GPL-3.0). A 2.8" CYD acts as a
dumb ANSI terminal over Port-A UART; the built-in 1.14" screen is a status +
notification surface. No WiFi/BLE — pure LoRa mesh.

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — firmware architecture spec
  (the Plai seam, presentation layer, app model, mesh integration, RAM budget,
  notification center, state persistence).
- [`emulator/`](emulator/) — **Phase 1 PC-native dev emulator**: builds the
  portable UI/app layer (`TextCanvas`, ANSI backend, app framework, `MeshFacade`,
  notification center) and runs it on the host with a stub mesh. `cd emulator &&
  make run`.

> The upstream Plai clone lives at `plai/` locally and is intentionally **not
> vendored** here (it has its own upstream and history).

License: GPL-3.0 (derivative of Plai).
