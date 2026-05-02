# Outils dans `SnowRunner\Sources\Bin\`

Tous les exécutables hors `SnowRunner.exe` sont des **outils de build / pipeline offline**. Ils ne s'exécutent jamais pendant le jeu — ils servent au pipeline de création de mods et de ressources.

---

## SnowrunnerResourceConverter.exe

**Rôle** : Convertisseur de ressources principal du pipeline de build Saber. C'est l'outil qui transforme les assets sources (textures, shaders, niveaux) en formats binaires optimisés pour chaque plateforme cible.

**Taille** : 33.4 MB (inclut le compilateur de shaders + le système PhysFS + des bibliothèques de conversion)

**Commandes disponibles** :

| Option | Description |
|---|---|
| `--all-pct` | Convertit des textures au format PCT pour toutes les plateformes |
| `--check-textures` | Vérifie l'intégrité des textures d'un mod |
| `--comparing-paks` | Compare le contenu de deux paks (diff de ressources) |
| `--debug` | Compile les shaders avec informations de debug |
| `--extract-textures` | Extrait les textures d'un mod |
| `--mod-convert` | Convertit les ressources pour les mods |
| `-l` / `--level` | Génère le `.stg` d'un niveau |
| `-c` / `--shader-cache` | Compile le cache de shaders (`.sdc`) |
| `--make-pct-headers` | Génère les headers `.pct_header` lors de la conversion PCT |
| `--make-console-pct` | Convertit un PCT au format console |
| `--common-lsa` | Génère le fichier `common.lsa` |
| `--final-lsa` | Génère `final.lsa` pour un niveau |
| `--fake-phys-fs` | Utilise `mr2database` au lieu de PhysFS (mode offline) |

**Options importantes** :

| Option | Type | Description |
|---|---|---|
| `-i` / `--input` | STRING | Fichier d'entrée |
| `-o` / `--output` | STRING | Fichier de sortie |
| `-p` / `--project` | STRING | Chemin vers `prjenv` (environnement projet) |
| `-t` / `--target` | VALUE | Plateforme cible (voir ci-dessous) |
| `--initial-pak` | VALUE | Paks contenant les templates |
| `--jobCount` | INT | Nombre de threads pour la compilation shader |

**Plateformes cibles** (`-t`) :
```
runtime              ← PC standard (défaut)
editor               ← Build éditeur
runtime_husky_pc     ← PC "Husky" (build interne)
runtime_husky_durango ← Xbox One
runtime_husky_orbis  ← PlayStation 4
runtime_husky_prospero ← PlayStation 5
runtime_husky_scarlett ← Xbox Series X
runtime_husky_nx     ← Nintendo Switch
runtime_husky_osx    ← macOS
runtime_husky_ios    ← iOS
runtime_husky_ggp    ← Google Stadia
```

> **Pour le mod** : cet outil peut re-générer les `.stg` et recompiler les shaders si on veut créer des niveaux custom ou modifier les shaders de terrain. La commande `--mod-convert` est celle qu'utilisent les moddeurs officiels.

---

## TextureConverter.exe

**Rôle** : Convertisseur de textures bas niveau. Convertit entre TGA, PNG, DDS, OpenEXR et le format BNTX (Nintendo). Gère la compression GPU (BC1–BC7, ASTC), les mipmaps, le redimensionnement et la manipulation de canaux.

**Taille** : 304 KB

**Version** : 4.16.0.0

**Formats d'entrée** : TGA, PNG, DDS, OpenEXR

**Formats de sortie** : TGA, PNG, DDS, EXR, BNTX (Nintendo Switch)

**Compressions supportées** :

| Format | Type |
|---|---|
| `unorm_bc1` / `srgb_bc1` | BC1 (DXT1) — diffuse sans alpha |
| `unorm_bc3` / `srgb_bc3` | BC3 (DXT5) — diffuse + alpha |
| `unorm_bc4` / `snorm_bc4` | BC4 — un seul canal (heightmap, roughness) |
| `unorm_bc5` / `snorm_bc5` | BC5 — deux canaux (normal maps) |
| `unorm_bc7` / `srgb_bc7` | BC7 — haute qualité, tous usages |
| `unorm_astc_*` / `srgb_astc_*` | ASTC — compression mobile (iOS, Switch) |

**Options utiles** :
- `--format` / `--format-with-a` / `--format-translucent` — format selon présence d'alpha
- `-i` / `--mip-level` — nombre maximum de mips
- `--mip-gen-filter` — filtre de génération (point / linear / cubic / cubic_sharp)
- `--crop` — recadrage (X,Y,W,H)
- `-x` / `-y` — redimensionnement
- `--replace-r/g/b/a` — remplacer un canal par celui d'une autre image
- `--gpu-encoding` — activation de l'encodage GPU (plus rapide pour BC7)

> **Pour le mod** : utile pour convertir des splatmaps PNG en DDS BC7 avant de les injecter, ou pour inspecter/reconvertir les textures extraites des paks.

---

## lame.exe

**Rôle** : Encodeur MP3 open-source (LAME). Utilisé par le pipeline audio pour convertir des WAV en MP3.

**Taille** : 1.7 MB  
**Version** : LAME 3.100.1 (64-bit)  
**Licence** : LGPL

**Usage typique** :
```
lame -V2 input.wav output.mp3          ← VBR qualité 2
lame -b 128 input.wav output.mp3       ← CBR 128 kbps
lame --preset standard input.wav out.mp3
```

> Pas utile pour le mod (le jeu utilise PCM et Opus pour ses sons, pas MP3).

---

## opusenc.exe

**Rôle** : Encodeur Opus open-source. Convertit WAV / AIFF / FLAC / PCM brut en fichiers `.opus`. C'est le format audio que SnowRunner utilise pour `shared_sound.pak` (les PCM dans le pak sont en fait des fichiers Opus).

**Taille** : 511 KB  
**Formats d'entrée** : WAV, AIFF, FLAC, Ogg/FLAC, PCM brut

**Usage typique** :
```
opusenc --bitrate 96 input.wav output.opus
opusenc --music --vbr input.wav output.opus     ← VBR optimisé musique
opusenc --speech --bitrate 32 voice.wav out.opus ← voix basse qualité
```

**Options clés** :
- `--bitrate` — débit cible (6–256 kb/s par canal)
- `--vbr` / `--cvbr` / `--hard-cbr` — mode débit variable ou constant
- `--music` / `--speech` — tuning basse qualité
- `--comp 0-10` — complexité (10 = plus lent mais meilleur)

> Le pipeline audio du jeu utilise cet outil pour générer les `.pcm` de `shared_sound.pak`.

---

## xma2encode.exe

**Rôle** : Outil Microsoft d'encodage XMA2 — format audio compressé Xbox 360 / Xbox One. Présent pour les builds consoles (Xbox One = `runtime_husky_durango`).

**Taille** : 1.1 MB  
**Version** : Microsoft XMA2 Encoding Tool 10.0.10011.16384  
**Auteur** : Microsoft Corporation

**Usage** :
```
xma2encode input.wav /TargetFile output.xma2    ← encode WAV → XMA2
xma2encode input.xma2 /DecodeToPCM output.wav   ← décode XMA2 → PCM
```

**Options** :
- `/Quality 1-100` — qualité de compression (défaut 60)
- `/BlockSize 2-8190` — taille de bloc en Ko (défaut 64 Ko)
- `/LoopWholeFile` — marque tout le fichier en boucle
- `/Speaker L/R/C/LFE/LS/RS...` — assignation des canaux au haut-parleur

> Pas utile pour le mod PC. Présent uniquement pour le pipeline de build Xbox.

---

## xwmaencode.exe

**Rôle** : Outil Microsoft d'encodage xWMA — format audio compressé Windows/Xbox (alternative légère à XMA2, basé sur WMA). Utilisé pour les sons de faible priorité sur PC et Xbox.

**Taille** : 740 KB  
**Version** : Microsoft xWMA Encoding Tool 10.0.10011.16384  
**Auteur** : Microsoft Corporation

**Usage** :
```
xwmaencode input.wav output.xwma              ← encode PCM → xWMA (48 kbps)
xwmaencode -b 96000 input.wav output.xwma     ← encode à 96 kbps
xwmaencode input.xwma output.wav              ← décode xWMA → PCM
```

**Débits supportés** : 20000, 32000, 48000, 64000, 96000, 160000, 192000 bps

> Pas utile pour le mod. Présent pour le pipeline Xbox / Windows legacy.

---

## crash_reporter.exe

**Rôle** : Outil de rapport de crash Saber Interactive. Quand `SnowRunner.exe` plante, il génère un `.dmp` (minidump Windows) et lance ce processus pour l'envoyer au serveur interne de Saber.

**Taille** : 1.9 MB  
**Auteur** : Saber Interactive (build `d:\projects\okokshin_mr2_highway\...`)

**Comportement** :
- Collecte les logs de crash (`crash_logs`)
- Sauvegarde le dump : `dump.dmp`
- Envoie vers le serveur interne Saber :
  ```
  https://jarvis.saber3d.net/jarvis/dumps?resourceId=
  ```
- Token d'upload : `hydra5_diagnostics_crash_dump_upload_token`
- Nom du service interne : `Global\SaberApp:Jarvis Daemon` (mutex Windows)

**Arguments reconnus** :
- `--silent` / `-s` — envoie le rapport sans afficher de dialogue
- `--send-crashdump` — force l'envoi
- `dont_send_crashdump` — supprime l'envoi (mode debug)
- `reporter-token` — token d'authentification serveur

**Config dans le jeu** :
```
Debug.CrashCommentRequired   ← oblige à saisir un commentaire avant envoi
Debug.SendCrashImmediately   ← envoie sans confirmation
```

> Inoffensif pour le mod. Si notre `.asi` provoque un crash, ce processus sera lancé. Il ne collecte que les fichiers `.dmp` et logs, pas les DLLs injectées.

---

## SnowRunner.exe

Voir `snowrunner_exe_analysis.md` pour l'analyse complète.

---

## Récapitulatif

| Exécutable | Catégorie | Utile pour le mod |
|---|---|---|
| `SnowRunner.exe` | Jeu | Cible d'injection (via `dinput8.dll`) |
| `SnowrunnerResourceConverter.exe` | Build pipeline | **Oui** — conversion PCT, shaders, niveaux |
| `TextureConverter.exe` | Build pipeline | **Oui** — conversion DDS/BC7, manipulation canaux |
| `lame.exe` | Audio | Non |
| `opusenc.exe` | Audio | Non (pipeline son du jeu) |
| `xma2encode.exe` | Audio Xbox | Non |
| `xwmaencode.exe` | Audio Xbox/PC | Non |
| `crash_reporter.exe` | Diagnostics | Passif — se lance si crash |
