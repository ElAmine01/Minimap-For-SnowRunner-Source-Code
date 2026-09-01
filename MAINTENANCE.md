# 🛠️ SnowMap : Guide de Maintenance & Mise à jour des Pointeurs

Ce document explique comment retrouver les adresses mémoires (Offsets) de SnowRunner après une mise à jour du jeu. Le mod utilise une lecture RAM directe (Lock-Free) pour contourner l'obfuscation de Denuvo et le lag du GPU.

> [!IMPORTANT]
> **Depuis `src/asi/game/OffsetScanner.cpp`, plus aucun offset n'est codé en dur.** Le mod retrouve seul les deux globales au démarrage, à partir de leur *contenu* et non de leur adresse. Un patch du jeu ne demande donc plus de rebuild. Les valeurs listées ci-dessous sont celles que le scanner résout sur le build courant — elles servent de référence et de point de comparaison, pas de constantes compilées.
>
> Voir [Découverte automatique](#-découverte-automatique-offsetscanner) pour le fonctionnement, et les sections suivantes pour la procédure manuelle si jamais le scanner échoue.

## 🎯 Nos 2 Cibles en RAM

Les deux pointeurs statiques (relatifs à `SnowRunner.exe`) recherchés par le scanner :

1. **Le GameSession Manager (`Offset_Session`)**
   - Rôle : Contient le nom du niveau actuel en clair.
   - Pointeur : `[SnowRunner.exe + Offset_Session] + 0x18` -> `std::string` (ex: "level_ru_02_02").
   - Offset actuel (relevé le 2026-08-22) : `0x2A543C0`
   - Offset précédent (Saison 17) : `0x2A4E038`
   - Objet `GameSession` : vtable en `+0x00` (DLL Husky), longueur du nom en `+0x10` (uint32), buffer inline en `+0x18` (capacité 32).

2. **Le TruckControl / Chassis Body (`Offset_TruckControl`)**
   - Rôle : Pointeur racine vers le nœud de simulation du camion actif, pour récupérer la position et le cap du châssis.
   - Chaîne complète (4 niveaux) :
     ```
     [SnowRunner.exe + Offset_TruckControl]  →  TruckControl
     [TruckControl + 0x08]                   →  TruckSimNode (TSN)
     [TSN + 0x60]                            →  Objet intermédiaire
     [Objet intermédiaire + 0x68]            →  ChassisBody  ← lecture finale
     ```
   - Offset actuel (relevé le 2026-08-22) : `0x2A8EDD8` — RTTI de l'objet pointé : `combine::TRUCK_CONTROL`
   - Offset précédent (Saison 17) : `0x2A876A8`
   - Bonus : `SnowRunner.exe + 0x2A8EDE0` pointe vers `combine::DRIVE_CAMERA` (même chaîne `+0x08/+0x60/+0x68` vers le ChassisBody du camion piloté).
   - Formule cap (SnowRunner world : nord = +Z, est = +X) : `heading = atan2f(fwd[2], fwd[0]) + π/2`
   - ⚠️ `+0xB0` stocke le **vecteur Right** (axe local X, perpendiculaire à la direction de marche), PAS le Forward. L'ajout de π/2 corrige cet écart de 90°.

### Structure du ChassisBody (Husky Engine — Corps physique brut)

Le ChassisBody est un corps physique Husky Engine **sans vtable** (208 octets). Il stocke 3 états :

| Offset absolu | Contenu                                     |
|---------------|---------------------------------------------|
| `+0x00`       | Pointeur parent (heap, ne pas déréférencer) |
| `+0x10`       | State A — Right (repère local)              |
| `+0x20`       | State A — Up                                |
| `+0x30`       | State A — Forward                           |
| `+0x40`       | State A — Position locale (ancre joint)     |
| `+0x50`       | State B — Right (frame précédente)          |
| `+0x60`       | State B — Up                                |
| `+0x70`       | State B — Forward                           |
| `+0x80`       | State B — Position monde (frame N-1)        |
| `+0x90`       | State C — Right (frame courante)            |
| `+0xA0`       | State C — Up                                |
| **`+0xB0`**   | **State C — Right Vector (local X, ⊥ forward)**  |
| **`+0xC0`**   | **State C — Position monde (X, Y, Z)**           |

> Le mod lit toujours **State C** (frame courante) : `+0xB0` pour le vecteur Right, `+0xC0` pour la position monde.  
> ⚠️ `+0xB0` est le vecteur **Right** (axe local X), pas Forward. Il est perpendiculaire à la direction de marche (90° CW).  
> Formule du cap boussole : `heading = atan2f(fwd[2], fwd[0]) + π/2` (plan X/Z, axe Y = hauteur, nord = +Z).

---

## 🤖 Découverte automatique (OffsetScanner)

`src/asi/game/OffsetScanner.cpp` lance un thread de fond au chargement du mod. Il ne cherche jamais une adresse : il cherche une **forme**.

**Préparation :** parsing des en-têtes PE du process pour délimiter `.rdata` et `.data`, puis instantané de toutes les pages committées et lisibles via `VirtualQuery`. Chaque déréférencement est vérifié contre cet instantané — le scan ne touche jamais une page de garde.

**Cible 1 — GameSession (prédicat structurel).** Balayage des QWORD de `.data` ; pour chaque pointeur `p` plausible, on lit `len` en `p+0x10` et le buffer inline en `p+0x18`, et on exige `len` dans `[6, 31]`, préfixe `"level_"`, `buf[len] == 0` et `strlen(buf) == len`. **Un seul résultat sur tout `.data`.**

**Cible 2 — TruckControl (ancre RTTI).** Même balayage ; pour chaque objet dont la vtable tombe dans `.rdata`, on remonte `vtable-8` → `RTTICompleteObjectLocator` (`signature == 1`, `self_rva` cohérent) → `TypeDescriptor` → nom mangé, et on compare à `.?AVTRUCK_CONTROL@combine@@`. Sur le build courant : 2016 objets passent le préfiltre vtable, **un seul** correspond au nom.

**Repli** si une future build supprime le RTTI : le scanner accepte alors toute globale dont la chaîne `+0x08/+0x60/+0x68` aboutit à un corps dont le vecteur en `+0xB0` a une norme dans `[0.99, 1.01]` et dont la position en `+0xC0` est finie et bornée à ±20000.

**Cache.** Une fois les deux globales trouvées, elles sont écrites dans `SnowMap/offsets.cache`, clé = taille + date de `SnowRunner.exe` + `SizeOfImage`. Au lancement suivant sur le même build, le scan est sauté (les valeurs sont malgré tout revalidées avant usage). Un patch change la clé, le cache est ignoré, le scan repart.

**Auto-réparation.** `MinimapRenderer` signale au scanner chaque lecture réussie ou ratée. La globale GameSession résout même en menu principal : 3 s d'échec continu ⇒ les deux offsets sont jetés et un rescan démarre. La chaîne camion est légitimement nulle au garage ou hors partie, donc elle ne déclenche un rescan qu'après 60 s d'échec **sur un niveau chargé**.

**Limite.** Cette mécanique absorbe les déplacements d'adresses, pas les changements de *layout* : si Husky réordonne `TruckSimNode` et que `+0x60` cesse d'être l'objet intermédiaire, il faut repasser par la procédure manuelle ci-dessous et corriger les constantes de `namespace layout` dans `OffsetScanner.h`.

---

## ⚡ Méthode rapide : balayage Lua de la section statique (recommandée)

Plus fiable et bien plus rapide qu'un Pointer Scan. Le MCP est configuré dans `.mcp.json` (serveur `cheatengine`). Lancer le jeu sur une map avec un camion sorti, attacher Cheat Engine à `SnowRunner.exe`, exécuter `ce_mcp_bridge.lua`, puis via `evaluate_lua` :

**Cible 1 — GameSession :** faire un `AOBScan` de `6C 65 76 65 6C 5F` (`"level_"`) en mémoire `+W-C`, garder les adresses alignées sur 8, puis balayer tout le module `SnowRunner.exe` par blocs de `readBytes` en cherchant un QWORD `p` tel que `p + 0x18` (fenêtre testée : `+0x00` à `+0x40`) tombe sur une de ces chaînes. Le seul résultat en `+0x18` est le pointeur statique cherché.

**Cible 2 — TruckControl :** balayer les mêmes QWORD statiques, ne garder que ceux qui pointent dans le tas, puis dérouler `+0x08 / +0x60 / +0x68` et valider le ChassisBody : norme du vecteur en `+0xB0` comprise entre 0.99 et 1.01, position en `+0xC0` finie et `|x|, |z| < 20000`. Ça sort 2-3 candidats ; `get_rtti_classname` sur l'objet pointé tranche — celui qui répond `combine::TRUCK_CONTROL` est le bon (les autres sont `combine::DRIVE_CAMERA` ou des objets sans RTTI).

⚠️ Un pointeur brut du tas dans ce process ressemble à `0x000002754B993765` : les **deux** octets de poids fort sont nuls. Un filtre qui exige l'octet 6 non nul ne trouvera jamais rien.

**Validation finale :** échantillonner `+0xC0` et `+0xB0` toutes les 2 s pendant que le joueur roule ; la position et le vecteur Right doivent bouger.

---

## 🚀 Méthode de Récupération (Automatisée avec IA)

**Outils requis :**
- Cheat Engine
- Bridge MCP (Cheat Engine <-> Claude Desktop)
- Une partie de SnowRunner lancée (être en dehors du garage, sur une map, avec un camion sorti).

### LE PROMPT À ENVOYER À CLAUDE (À COPIER-COLLER)

En cas de mise à jour du jeu, connecte Claude à Cheat Engine via MCP, et envoie-lui exactement ce prompt :

***
**Objet : Mise à jour des offsets SnowMap suite à un patch du jeu (Husky Engine)**

"Bonjour Claude. Le jeu SnowRunner a été mis à jour et nos pointeurs statiques sont cassés. Ton objectif est d'utiliser Cheat Engine MCP pour retrouver les 2 nouveaux pointeurs globaux.

**Cible 1 : GameSession Manager (Pour le nom du niveau)**
1. Fais une recherche de chaîne de caractères (String scan) en RAM pour trouver le nom du niveau actuel (ex: `level_ru_02_02`).
2. Fais un Pointer Scan (ou trouve les références) pour remonter à l'objet parent, puis au pointeur statique de la section `.data` de `SnowRunner.exe` qui pointe vers lui.
3. Vérifie que `[pointeur_statique + 0x18]` est bien la `std::string` du niveau.

**Cible 2 : TruckControl → TruckSimNode → ChassisBody (Pour le GPS et le cap)**

L'objectif est de retrouver la chaîne de pointeurs menant au corps physique du châssis du camion actif.

1. **Trouver TruckControl :** Lance un AOB Scan ciblant le code qui accède au TruckSimNode (historiquement, une instruction `mov rXX, [rXX+0x08]` immédiatement après lecture du pointeur TruckControl). Pose un breakpoint pour capturer l'adresse de TruckControl dans les registres.
2. **Valider la chaîne :** Depuis TruckControl, déréférence manuellement :
   - `[TruckControl + 0x08]` → TruckSimNode (TSN). Il doit avoir une vtable valide (pointeur code, DLL Husky).
   - `[TSN + 0x60]` → objet intermédiaire (heap).
   - `[objet_intermédiaire + 0x68]` → ChassisBody. Pas de vtable — le premier QWORD doit être un pointeur heap.
3. **Valider le ChassisBody :** Lis 208 octets depuis ChassisBody.
   - `+0xC0, +0xC4, +0xC8` : 3 floats = position monde X, Y, Z. Demande au joueur de bouger le camion → les valeurs doivent changer.
   - `+0xB0, +0xB4, +0xB8` : 3 floats = vecteur **Right** (axe local X, norme ≈ 1.0). ⚠️ Ce n'est PAS le Forward.
4. **Trouver le pointeur statique :** Fais un Reverse Pointer Scan depuis TruckControl pour trouver `[SnowRunner.exe + NOUVEL_OFFSET]`.

**Vérification finale :**
- Roule sur la map → `[ChassisBody + 0xC0]` varie (X et Z suivent le déplacement, nord = +Z, est = +X).
- Tourne le camion → `[ChassisBody + 0xB0]` change (vecteur Right tourne).
- `atan2f(fwd[2], fwd[0]) + π/2` doit donner un cap cohérent avec la direction affichée à l'écran.

**Action finale :**
Mets à jour `src/asi/render/MinimapRenderer.cpp` dans la fonction `PollVehicleFromRam()` avec le nouvel offset (`SnowRunner.exe + NOUVEL_OFFSET`). Les offsets internes de la chaîne (`+0x08`, `+0x60`, `+0x68`) et du corps (`+0xB0` = Right vector, `+0xC0` = position) sont propres au moteur Husky et ne changent pas entre patches."
***

---

## Map blips: what works, what was tried, what is left

*(English section — the discovery notes below were produced against build `SnowRunner.exe`
image size 48242688, session global `+0x2A543C0`, TRUCK_CONTROL global `+0x2A8EDD8`.)*

### Shipped

`SnowMap/blips.png` is a uniform grid of square cells, 640x640 with 64px cells on the
stock sheet: `0` player arrow, `1` fuel, `2` truck, `3` trailer, `4` waypoint pin,
`5` cargo. Cell size, column count and the per-kind cell index all live in
`options.json`, so a replacement sheet needs no rebuild. Only the player marker and
mod-local waypoints are drawn today.

Two traps that cost real time here, both now fixed:

* `LoadImageFromDisk` flips images vertically because the satellite map wants north at
  row 0. A sprite sheet must be loaded with `flip_vertically = false` or every cell
  samples the wrong row. The atlas UV maths was correct the whole time.
* Blip rotation. Screen up is world `+Z`, screen right is `+X`, and a truck at heading
  `h` faces `(cos h, -sin h)` on screen. An up-pointing icon therefore needs a screen
  rotation of `pi/2 - h`, and `DrawBlipIcon` rotates by `-a`, so `a = h - pi/2`. An
  extra `pi` in that expression flips every icon 180 degrees.

### Trucks, trailers, cargo: abandoned, and why

The idea was: the player's chassis body is already resolved, its vtable identifies the
class, so sweeping the heap for that one qword finds every other vehicle body. This does
not work on this engine. Measured, on a loaded level with trailers present:

```
sweep 1 — 5 vtable hits, 1 bodies kept, 6523 MiB heap, 41859 ms
sweep 2 — 11 vtable hits, 1 bodies kept, 6525 MiB heap, 15266 ms
sweep 3 — 11 vtable hits, 1 bodies kept, 6526 MiB heap, 13297 ms
```

Only 5-11 objects in 6.5 GB share the chassis vtable and exactly one passes the shape
test: the player's own truck. Trailers and other trucks do not share the class. The
sweep also costs 13-42 s per pass, which is unusable regardless. Anyone retrying this
needs a different route entirely — the level's object registry, not a vtable sweep.

Note the anchor detail that is worth keeping: **Denuvo fills vtables at runtime onto the
heap**, so the chassis body's vtable pointer is *not* inside the module's `.rdata`.
Rejecting a vtable because it falls outside the module will silently break discovery.

### In-game waypoints: unfinished, discovery done

The game's own map waypoints are **not** read yet. What is already established:

Save format, in `CompleteSave.cfg` (plain JSON, Steam cloud path
`userdata/<id>/1465360/remote/`), keyed by level id:

```json
"waypoints": {
  "level_us_09_01": [
    {"point":{"x":4.5939602851867676,"y":0.21297536790370941,"z":-2.3439652919769287},
     "modelHeightBounds":null,"type":0}
  ]
}
```

* **Coordinates are world units divided by exactly 150.** This is the single fact that
  makes or breaks the search: a waypoint on a truck at world `(687.7, -352.1)` stores as
  `(4.585, -2.347)`. Scanning for world-range floats will never find it.
* `y` is terrain height at the marker, not truck height, so it implies a different scale
  (~154) and must not be used as a consistency check. Only `x` and `z` are usable.
* In memory the live vec3 sits at `object + 0x18`. Nearby are a `+/-19.30` pair
  (`modelHeightBounds`) and a render-instance buffer repeating the vec3 at `0x10` stride.
  Several stale copies of the value also exist; only one address updates when the player
  moves the marker.

What blocks it: no stable anchor was found. The containing object has no resolvable RTTI
name, no module global points into its heap block (176 referencing qwords, all
heap-to-heap), and a 4-level BFS from the session global did not reach it. Finishing this
needs a proper multi-level reverse pointer scan plus validation across sessions and
levels — a real pointer-scan job, not a quick lookup.

Reproducing the find, with the game running and a waypoint placed on the truck:

1. Read the truck world position through the existing chain
   (`[SnowRunner.exe + truck_control_rva]` → `+0x08` → `+0x60` → `+0x68`, position at `+0xC0`).
2. Scan `float` between `worldX/152` and `worldX/148`.
3. Keep hits whose `+8` float lies in the matching `worldZ` band. That reduced ~75000
   candidates to a handful, of which one tracks the marker.
