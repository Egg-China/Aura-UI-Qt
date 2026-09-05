# Aura UI Qt

A Qt 6 Widgets runtime for Aura Launcher's frozen `aura.ui.v1` UI-provider
interface. Aura Launcher launches this process with exactly `--stdio`, exchanges
length-prefixed Bridge Value frames, and can shut it down cleanly without
touching the built-in JavaFX recovery UI.

## Status

- Canonical Bridge Value v1 codec with strict, depth-limited decoding.
- Four-byte big-endian frame transport with a 16 MiB limit.
- Complete launcher handshake: `ui.hello`, `ui.snapshot.replace`, `ui.ready`,
  `core.snapshot.get`, `ui.navigate`, `ui.notify`, and `ui.shutdown`.
- Instance list, launcher details, account display, plugin contribution buttons,
  and launch/refresh/shutdown command routing.
- Schema-v5 `.npl` packaging and CI for Windows x64, Linux x64, macOS x64, and
  macOS ARM64.

## Build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Qt 6.5 or newer with the Widgets and Test modules is required.

## Package

```powershell
python scripts/package_npl.py --platform windows-x64 --binary build/aura-ui-qt.exe
```

Supported package platforms are `windows-x64`, `linux-x64`, `macos-x64`, and
`macos-arm64`. Other Qt-supported platforms can build from source.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
