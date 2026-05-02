# Structure du Projet

### `src/asi/`
Le cœur du mod (`SnowMap.asi`). Organisé en sous-modules :

| Sous-dossier | Rôle |
|---|---|
| `core/` | Logger, Config (lecture `options.json` en direct) et Globals (état atomique partagé). |
| `game/` | PlayerState — Stocke l'état instantané (Snapshot) de la position X,Y,Z et du Heading (Yaw). |
| `hooks/` | **Le moteur d'injection.** Contient `DXGIHook` (Rendu), `D3D11Hook` (Textures de synchronisation), et `CameraMemoryReader` (Thread asynchrone lisant les pointeurs statiques en RAM 60 fois par seconde). |
| `render/` | MinimapRenderer (orchestration ImGui, clipping, XInput polling, rotation mathématique), **MapDownloader** (téléchargement CDN HTTP), TextureLoader. |
| `win32/` | MemScan et utilitaires natifs Windows. |