# Analyse de SnowRunner.exe

## Identité du binaire

| Champ | Valeur |
|---|---|
| Chemin | `SnowRunner\Sources\Bin\SnowRunner.exe` |
| Taille | 44.6 MB |
| Version produit | `1.846268.SNOW_DLC_17` (patch DLC 17, build courant) |
| Timestamp PE | 2026-03-03 13:20 UTC |
| Architecture | x86-64 (PE32+) |
| Image base | `0x140000000` (standard 64-bit) |
| Linker | MSVC 14.16 (Visual Studio 2017) |
| Éditeur | Focus Home Interactive |

---

## Sections PE

| Section | VA | Taille (virtuelle) | Rôle |
|---|---|---|---|
| `.text` | `0x00001000` | **33.7 MB** | Code exécutable |
| `.rdata` | `0x021B8000` | 7.9 MB | Données read-only (strings, vtables, hints d'import) |
| `.data` | `0x0299C000` | 2.1 MB (712 KB on-disk) | Globales read-write |
| `.pdata` | `0x02BAD000` | 1.8 MB | Tables d'exception/unwind SEH (obligatoire x64) |
| `_RDATA` | `0x02D75000` | 37 KB | RTTI étendu + données XFG (eXtended Flow Guard) |
| `.rsrc` | `0x02D7F000` | 43 KB | Ressources Win32 (icône, version info) |
| `.reloc` | `0x02D8A000` | 212 KB | Table de relocations |
| `.bind` | `0x02DC0000` | 228 KB | IAT pré-résolue (bound imports — démarrage rapide / protection IAT) |

> **Note** : La section `.bind` et la table d'imports obfusquée indiquent un traitement post-link de l'IAT (probablement Denuvo ou un packer propriétaire). Les noms de DLLs restent lisibles dans `.rdata` mais les descripteurs d'import standards sont scramblés.

---

## DLLs chargées (extraites de `.rdata`)

### Graphiques & Audio
| DLL | Rôle |
|---|---|
| `d3d11.dll` | DirectX 11 — API graphique principale |
| `dxgi.dll` | DXGI — swap chain, enumération adapters |
| `D3DCOMPILER_47.dll` | Compilation HLSL runtime |
| `vulkan-1.dll` | Vulkan — path de rendu alternatif + transpilation GLSL→SPIR-V (glslang embarqué) |
| `XAudio2_9Redist.dll` | Audio XAudio2 |
| `xinput9_1_0.dll` | Input manette Xbox |
| `GFSDK_Aftermath_Lib.x64.dll` | NVIDIA Aftermath — diagnostics crash GPU |
| `renderdoc.dll` | RenderDoc — chargé optionnellement pour le debug graphique |

### Physique & Moteur
| DLL | Rôle |
|---|---|
| `HFFBSDK.dll` | Havok Fiber Framework — moteur physique |
| `ATICompressDLL.dll` | Compression de textures AMD (BC7/BC1) |
| `PVRTexLib.dll` | Compression PowerVR (format PVR) |

### Online & Plateforme
| DLL | Rôle |
|---|---|
| `steam_api64.dll` | Steam SDK — achats, succès, cloud |
| `EOSSDK-Win64-Shipping.dll` | Epic Online Services — cross-play, vérification DLC (`EOS_Ecom_QueryOwnership`) |
| `pros.sdk.x64.dll` | SDK inconnu (probablement PlayStation Online Services, présent en build PC pour compatibilité cross-play) |
| `DINPUT8.dll` | DirectInput8 — **c'est ici que notre proxy `dinput8.dll` s'intercale** |

### Système
| DLL | Rôle |
|---|---|
| `KERNEL32.dll` / `USER32.dll` / `GDI32.dll` | Win32 standard |
| `bcrypt.dll` | Cryptographie Windows |
| `dbghelp.dll` / `imagehlp.dll` | Stack traces Havok |
| `ADVAPI32.dll` / `SHELL32.dll` / `WS2_32.DLL` | Système / réseau |

---

## Moteur : architecture source

Les chemins de build embarqués révèlent la structure interne :

```
C:\_\_task_dir\
├─ MudRunner2\Sources\SpinTires\        ← Codebase core "SpinTires" (MudRunner 2)
│   ├─ Husky\Network\net_physics.cpp    ← Physique réseau
│   ├─ Husky\Network\net_ssl_info.cpp   ← SSL game state réseau
│   └─ ...
├─ common\code\
│   ├─ drv\source\video\d3d_11\         ← Driver DirectX 11
│   │   ├─ d3d_cfg_11.cpp               ← Configuration D3D11 (device, swap chain)
│   │   ├─ d3d_coll_11.cpp              ← Collections D3D11
│   │   └─ d3d_const_buffer_11.cpp      ← Constant buffers
│   ├─ drv\source\streaming2\           ← Texture streaming
│   │   └─ strm2_texmem_mng.cpp         ← Gestionnaire mémoire texture
│   ├─ network\redstone_dust\           ← Réseau P2P "Redstone Dust"
│   ├─ network\redstone_torch\          ← Réseau "Redstone Torch"
│   ├─ ssl_game\                        ← Sérialisation game state (SSL = SpinTires Scripting Language)
│   └─ common\ap\                       ← Application Platform (log, alloc, config)
```

Le moteur s'appelle en interne **"SpinTires"** / **"Husky"** — c'est l'évolution directe du moteur original de Spintires.

---

## Système de fichiers virtuel (PhysFS)

PhysFS est **lié statiquement** dans l'exécutable (les symboles sont dans `.rdata` comme strings de résolution dynamique interne, pas comme imports DLL classiques).

**API complète utilisée :**
`PHYSFS_init`, `PHYSFS_mount`, `PHYSFS_addToSearchPath`, `PHYSFS_removeFromSearchPath`, `PHYSFS_openRead`, `PHYSFS_read`, `PHYSFS_close`, `PHYSFS_exists`, `PHYSFS_enumerateFiles`, `PHYSFS_getSearchPath`, `PHYSFS_isDirectory`, `PHYSFS_setWriteDir`, `PHYSFS_getWriteDir` + toutes les variantes read/write big/little-endian.

**Montage des paks :**
```
Info,System| PHYSFS_mount %s.zip      ← les .pak sont montés comme .zip
Info,System| PHYSFS_mount('%s').
```
Le jeu monte chaque `.pak` en tant qu'archive ZIP via `PHYSFS_mount`. Le chemin de montage `preload/paks/client/` n'est pas spécifié dans le string — il est construit en code.

**Lecture des textures :**
```
%s\%s.pct          ← chemin template (dossier\nom.pct)
_map.pct           ← suffixe pour les cartes de niveau
.pct.resource      ← ressource associée au PCT
```

---

## Pipeline de rendu

### API et passes

Le jeu utilise **DirectX 11** comme API principale et embarque un **compiler Vulkan/SPIR-V** (via glslang) pour convertir ses shaders au format SPIR-V si nécessaire.

**Render targets nommés (extrait) :**
- Atmosphère : HDR, TAA (prev frame 0/1, history 0/1), DOF, Lightshafts, Motion blur
- Eau : Water refraction, Water mirror, Water caustics, Water domain, Water SSR, Water waves, Water sim
- Terrain : `blend_map__cmp.dds`, `height_map.dds`, `tint_map__cmp.dds`, `snow_map.dds`, `water_height_map.dds`, `extrude_map.dds`, `fog_height.dds`
- Lumière : SSAO (fullres/halfres), IBR (Image-Based Rendering), Capsule shadows, Indirect lights
- Post-process : Radial blur, Fog volumes, Outline buffers, Mud AUX, Wheel tracks
- UI : Truck garage, Loading screen, GFx (Flash)

**Shaders importants :**
```
SpinTires/MinimapNavigation.fxs      ← Shader de navigation minimap
SpinTires/MinimapSearchArea.fxs      ← Shader zone de recherche
```

Ces `.fxs` sont compilés en `.sdc` (blobs dans `shader.pak`).

---

## Système minimap interne

Le jeu a sa **propre minimap intégrée**, qui utilise exactement les mêmes textures que notre mod :

| Élément | Valeur |
|---|---|
| Uniform texture | `g_txMinimapMapBase` |
| Uniform sampler | `g_samMinimapMapBase` |
| Uniforms shader | `g_vScale`, `g_vDetailTiling`, `g_fDetailPower`, `g_fWaterReflectivity` |
| Texture source | `blend_map__cmp.dds` (splatmap niveau) |
| Shader | `SpinTires/MinimapNavigation.fxs` |
| Données SSL | `sslMINIMAP_POINT_INFO`, `sslMinimapInit`, `sslOBJ<sslMINIMAP_SEARCH_AREA>` |

> **Conséquence pour le mod** : le jeu charge déjà `blend_map__cmp` en VRAM quand un niveau est actif. Notre `D3D11Hook` pourrait intercepter la création de cette texture via `hkCreateTexture2D` au lieu de la recharger depuis le pak.

---

## Système de sérialisation : SSL

Le moteur utilise un langage de scripting/sérialisation propriétaire appelé **SSL** (SpinTires Scripting Language). Les objets SSL visibles :

```
sslOBJ<sslLEVEL_DATA>
sslOBJ<sslLEVEL_TRUCKS_DATA>
sslOBJ<sslMINIMAP_POINT_INFO>
sslOBJ<sslMINIMAP_SEARCH_AREA>
sslOBJ<sslSAVED_LEVEL_DATA>
sslOBJ<sslZONE_LEVEL_DATA>
```

Les fichiers `.stg` dans les paks de niveau sont des archives SSL. Les `.sslbundle` dans `initial.pak` sont des bundles de définitions SSL.

---

## Online & DRM

### Epic Online Services
- `EOS_Auth_Login` / `EOS_Auth_CopyUserAuthToken` — authentification EpicGames
- `EOS_Ecom_QueryOwnership` — vérification de propriété des DLCs
- `EOS_EpicAccountId_FromString/ToString` — gestion d'identifiants
- Le launcher Epic est reconnu : `com.epicgames.launcher://store/product/snowrunner/`

### Steam
- `steam_api64.dll` — stats, succès, cloud save, lobby
- `/external/steamauth?%s` — authentification Steam

### Sécurité & DRM (Denuvo)
- **Denuvo Anti-Tamper confirmé** : La section `.text` (33.7 MB) est **entièrement chiffrée** sur le disque. Elle ne contient que des octets aléatoires et n'est déchiffrée qu'au Runtime (en RAM) par le stub DRM de la section `.bind`. 
- **Conséquence pour le modding** : L'analyse statique des offsets de code et des VTables est impossible (les entrées sont à zéro dans le fichier `.exe`). Tout hook ou scan doit impérativement se faire *après* l'initialisation du jeu en RAM (Runtime Hooking) ou en se basant sur les sections `.data` / `.rdata` qui ne sont pas chiffrées.
- **XFG** (eXtended Flow Guard) et **BCrypt** : Protections complémentaires contre l'altération du flux d'exécution et du réseau.

### Découverte RTTI (Run-Time Type Information)
Pour contourner le chiffrement de la section `.text`, le mod utilise les métadonnées C++ (RTTI) laissées en clair dans la section `.rdata`.
- Le jeu stocke son état dans des **Singletons**.
- Exemple : Le camion du joueur est géré par `TRUCK_CONTROL`.
- Son Complete Object Locator (COL) a une **RVA statique stable : `0x25A2228`**.
- Notre mod scanne la section `.data` en RAM à la recherche d'un pointeur de VTable lié à ce COL pour retrouver l'instance du camion sans jamais avoir à lire le code assembleur chiffré.

---

## Points d'accroche du mod (Architecture Cloud & Direct RAM)

Suite aux tests de performance (le Constant Buffer causait du stuttering) et aux découvertes mathématiques (rapport 1:2 entre la carte 3D et le CDN), l'architecture a muté vers de la pure lecture mémoire passive (Lock-Free) :

| Mécanisme | Comment notre mod interagit (V3 Final) |
|---|---|
| **Injection & Rendu** | `DINPUT8.dll` charge le `.asi`. `IDXGISwapChain::Present` est hooké pour dessiner l'overlay ImGui (Renderer AAA avec AddImageQuad). |
| **Niveau courant** | **GameSession RAM** : Le mod déréférence le pointeur statique `[SnowRunner.exe + 0x2A4E038]` pour lire la string du niveau (ex: `level_ru_02_02`) en clair dans la RAM. Zéro hook I/O. |
| **Chargement Maps** | **Cloud Cache (MapDownloader)** : Requête WinHTTP asynchrone vers le CDN MapRunner. L'image 4K est mise en cache locale. L'origine géométrique (0,0) est toujours le centre exact du PNG. |
| **Position & GPS** | **Static Pointer Chain** : Un thread lit le `Camera Body` ou le Véhicule via une chaîne statique `[SnowRunner.exe + 0x2A876C0] ->[+0x08]`. On extrait les floats `X,Y,Z` et on calcule le `Yaw` avec `atan2` sur le *Forward Vector*. Zéro interception GPU. Échec et Mat Denuvo. |
