# Map viewer

DayZ Helper includes a small browser map that can be shared across a local network. The controller publishes completed snapshots over a local named pipe; the map server turns them into WebSocket updates for the browser. The server contains no game-memory reader.

This public edition uses a placeholder data source, so the controller publishes no live entities. The interface and wire format are here for inspection and integration.

## Preview the interface

Build both executables with Visual Studio C++ tools and the Windows SDK:

```powershell
.\build-user.cmd
.\build-maptelemetry.cmd
.\out\dayz-game-tests.exe
.\out\maptelemetry-tests.exe
```

Start the server:

```powershell
.\out\maptelemetry-server.exe
```

Open the local URL printed by the server. In a second PowerShell window, publish a deterministic sample frame:

```powershell
.\out\maptelemetry-tests.exe --layers-fixture
```

The sample publisher exits after sending one frame. The server retains that frame and marks it disconnected, so the browser can still display the sample layers. Restarting the server clears the sample.

## Terrain imagery

No third-party terrain imagery is bundled in this public repository. Missing map tiles use a schematic grid. If you have permission to use a 16,384 × 16,384 image, generate offline tiles with Pillow:

```powershell
python -m pip install Pillow
python .\maptelemetry\tile_maps.py --map chernarusplus --image C:\path\to\your-map.jpg
```

The same command accepts `namalsk` or `nasdara`. The generated `maptelemetry/web/tiles/` folder is ignored by Git. Each terrain can be aligned with the viewer's calibration controls.

## Data and sharing

The versioned packet carries a complete local position plus bounded entity points, category, heading, name, and held-item text. The browser can follow a survivor, show nearby finds, and filter map layers. Labels move to avoid overlap; the survivor list keeps full names and available held items accessible.

On first start, the server creates an ignored `maptelemetry.ini` with a random access token. Use the URL it prints. The server binds local and LAN interfaces; the token is required for browser data and tiles. The controller and server remain separate processes, and the server never opens the game or a data transport.

The web UI uses [Leaflet](https://leafletjs.com/); the server uses [CivetWeb](https://github.com/civetweb/civetweb). Their licenses are included under `maptelemetry/web/vendor/` and `third_party/civetweb/`.
