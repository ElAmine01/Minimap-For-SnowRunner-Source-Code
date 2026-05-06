# 🛠️ SnowMap : Guide de Maintenance & Mise à jour des Pointeurs

Ce document explique comment retrouver les adresses mémoires (Offsets) de SnowRunner après une mise à jour du jeu. Le mod utilise une lecture RAM directe (Lock-Free) pour contourner l'obfuscation de Denuvo et le lag du GPU.

## 🎯 Nos 2 Cibles en RAM

À chaque mise à jour, nous devons retrouver ces deux pointeurs statiques (Relatifs à `SnowRunner.exe`) :

1. **Le GameSession Manager (`Offset_Session`)**
   - Rôle : Contient le nom du niveau actuel en clair.
   - Pointeur : `[SnowRunner.exe + Offset_Session] + 0x18` -> `std::string` (ex: "level_ru_02_02").
   - Offset connu (Saison 17) : `0x2A4E038`

2. **Le TruckControl / Chassis Body (`Offset_TruckControl`)**
   - Rôle : Pointeur racine vers le nœud de simulation du camion actif, pour récupérer la position et le cap du châssis.
   - Chaîne complète (4 niveaux) :
     ```
     [SnowRunner.exe + Offset_TruckControl]  →  TruckControl
     [TruckControl + 0x08]                   →  TruckSimNode (TSN)
     [TSN + 0x60]                            →  Objet intermédiaire
     [Objet intermédiaire + 0x68]            →  ChassisBody  ← lecture finale
     ```
   - Offset connu (Saison 17) : `0x2A876A8`
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
