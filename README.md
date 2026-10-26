# DayZ Helper

A minimalist overlay preview for **DayZ 1.30.164200.56**. The Direct3D 11 and Dear ImGui interface includes settings for entity, skeleton, and item visualizations, plus a compact coordinate and heading display.

This public copy leaves game-data acquisition as a placeholder. It contains no driver, IOCTL implementation, or live memory access, so visualizations will not populate until a data source is integrated. Fake-memory tests exercise projection and sampling without a game process.

On Windows x64 with Visual Studio C++ tools and the Windows SDK:

```powershell
.\build-user.cmd
.\out\dayz-game-tests.exe
```

The build copies `dayz.ini.example` to a local `dayz.ini`. See the [research notes](docs/REVERSE_ENGINEERING.md) for context. Screenshot space: [`docs/screenshots/`](docs/screenshots/). Dear ImGui retains its [upstream license](third_party/imgui/LICENSE.txt).
