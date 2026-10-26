# Dear ImGui vendoring note

- Upstream: https://github.com/ocornut/imgui
- Version: `v1.92.9b`
- Archive: https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip
- Vendored: 2026-10-12
- Included: core sources/headers, Win32 backend, DX11 backend, and upstream `LICENSE.txt`

The controller uses the Win32 platform backend and DX11 renderer backend. `build-user.cmd`
compiles these sources with `/W4 /WX` and links them into the controller and the
single-translation-unit controller test executable.
