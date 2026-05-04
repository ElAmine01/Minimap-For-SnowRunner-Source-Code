# 🛠️ SnowMap : Guide de Maintenance & Mise à jour des Pointeurs

Ce document explique comment retrouver les adresses mémoires (Offsets) de SnowRunner après une mise à jour du jeu. Le mod utilise une lecture RAM directe (Lock-Free) pour contourner l'obfuscation de Denuvo et le lag du GPU.

## 🎯 Nos 2 Cibles en RAM

À chaque mise à jour, nous devons retrouver ces deux pointeurs statiques (Relatifs à `SnowRunner.exe`) :

1. **Le GameSession Manager (`Offset_Session`)**
   - Rôle : Contient le nom du niveau actuel en clair.
   - Pointeur : `[SnowRunner.exe + Offset_Session] + 0x18` -> `std::string` (ex: "level_ru_02_02").
   - Ancien offset connu (Saison 17) : `0x2A4E038`

2. **Le Camera Node / R15 (`Offset_CameraNode`)**
   - Rôle : Pointeur racine vers la caméra active pour récupérer les coordonnées.
   - Chemin : `[[SnowRunner.exe + Offset_CameraNode] + 0x08]` -> `CameraBody`
   - Ancien offset connu : `0x2A876C0`

### Structure du CameraBody (Statique, change très rarement)
Une fois le `CameraBody` trouvé, la structure de la matrice Havok/Husky Engine est la suivante :
- `+0x00 à +0x0F` : Right Vector (Axe X local)
- `+0x10 à +0x1F` : Up Vector (Axe Y local)
- `+0x20 à +0x2F` : Forward Vector (Axe Z local / Direction / Heading)
- `+0xB0` : Float X (Position Monde)
- `+0xB4` : Float Y (Hauteur Monde)
- `+0xB8` : Float Z (Position Monde)

---

## 🚀 Méthode de Récupération (Automatisée avec IA)

**Outils requis :**
- Cheat Engine
- Bridge MCP (Cheat Engine <-> Claude Desktop)
- Une partie de SnowRunner lancée (être en dehors du garage, sur une map).

### LE PROMPT À ENVOYER À CLAUDE (À COPIER-COLLER)

En cas de mise à jour du jeu, connecte Claude à Cheat Engine via MCP, et envoie-lui exactement ce prompt :

***
**Objet : Mise à jour des offsets SnowMap suite à un patch du jeu (Husky Engine)**

"Bonjour Claude. Le jeu SnowRunner a été mis à jour et nos pointeurs statiques sont cassés. Ton objectif est d'utiliser Cheat Engine MCP pour retrouver les 2 nouveaux pointeurs globaux. 

**Cible 1 : GameSession Manager (Pour le nom du niveau)**
1. Fais une recherche de chaîne de caractères (String scan) en RAM pour trouver le nom du niveau actuel (ex: `level_ru_02_02`).
2. Fais un Pointer Scan (ou trouve les références) pour remonter à l'objet parent, puis au pointeur statique de la section `.data` de `SnowRunner.exe` qui pointe vers lui.
3. Vérifie s'il s'agit bien de l'ancre du GameSession.

**Cible 2 : Camera Node (Pour le GPS)**
1. Lance un AOB Scan (Array of Bytes) en te basant sur la logique connue du moteur Husky (historiquement liée aux flags FreeCam ou aux getters du cluster de la caméra `0x7FF7...`).
2. Trouve la fonction de mise à jour de la caméra (qui utilise historiquement des instructions comme `movss xmm4, [rcx+0xB0]`, `movss xmm5,[rcx+0xB4]`). 
3. Pose un breakpoint matériel (Logging only) sur cette fonction pour capturer l'adresse du `CameraBody` (registre `RCX` ou `RSI`).
4. Fais un Reverse Pointer Scan pour retrouver le "Node R15" statique (`[SnowRunner.exe + offset]`) qui pointe indirectement vers ce `CameraBody` (souvent via un saut `+0x08`).

**Vérification :**
Ouvre la structure du `CameraBody` que tu as trouvée :
- `+0xB0, +0xB4, +0xB8` doivent être des Floats représentant X, Y, Z (demande-moi de rouler en jeu pour les voir varier).
- `+0x20, +0x24, +0x28` doivent être les Floats du vecteur de direction.

**Action finale :**
Mets à jour le fichier `src/asi/hooks/CameraMemoryReader.cpp` (ou équivalent) avec ces nouveaux offsets (`SnowRunner.exe + NOUVEL_OFFSET`)."
***