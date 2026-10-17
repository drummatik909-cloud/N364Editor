# N364 Editor (v4) : plugin VST3 d'édition et de bibliothèque pour Korg N264/N364

Le plugin ne produit aucun son. Il pilote le N364 par SysEx (manuel, « Format de données MIDI »,
p. 219-229), lit ses sons, et range tes sons dans une bibliothèque. Environ 900 paramètres,
tous automatisables dans Bitwig.

> Ce code n'a pas pu être compilé ni testé sur un vrai N364 au moment de sa rédaction.
> Seules la table des paramètres et le décodage des dumps ont été vérifiés par des tests automatiques.
> Voir « Points à vérifier » plus bas.

## Nouveautés de la v4

- **Lecture depuis le N364** : « Lire depuis le N364 » (onglet Bibliothèque) demande le son courant
  (Program, Combination, Drum Kit ou Global) et met tous les potards à jour.
- **Synchro dans les deux sens** : quand tu tournes un bouton sur la face avant du N364, le potard du
  plugin suit (case « Synchro depuis le N364 »). Entrer en Edit Program sur le N364 recharge aussi le plugin.
- **Bibliothèque de presets** : enregistrer, charger, filtrer, supprimer des sons (fichiers) pour
  Programmes, Combinations (performances), Drum Kits et Global.
- **Importer la banque du N364** : lit les 200 Programs (A+B) ou les 200 Combinations du N364 et en
  fait des presets (lecture seule, rien n'est modifié dans le N364).
- **Écrire dans la mémoire du N364** : envoie les réglages puis les écrit dans un emplacement
  (banque A ou B, numéro 00 à 99), après confirmation.
- **Apparence sombre** et messages d'état en haut de la fenêtre.

## Correction de la v2 (rappel)

Le Parameter Change utilise **sa propre numérotation** (tables 8 à 13 du manuel), différente de celle
des dumps (table 1). La v2 utilisait par erreur la table 1. La v3 et la v4 utilisent les bonnes tables.

## Ce que couvre le plugin

| Onglet | Message | Mode du N364 |
|---|---|---|
| Programme, OSC-1, OSC-2, Effets | Parameter Change (0x41), tables 8 et 9 : 188 paramètres | Edit Program |
| Combination (8 timbres + effets) | Parameter Change (0x41), tables 11 et 9 : 169 paramètres | Edit Combination |
| Drums | Drum Kit Parameter Change (0x53), table 13 : 60 instruments × 9 réglages, kits A1/A2/B1/B2 | mode Global |
| Global | Global Data Dump (0x51), table 3 | mode Global |
| Bibliothèque | demandes de dump (0x10, 0x19, 0x0E, 0x0D, 0x1C, 0x1D), écriture mémoire (0x11, 0x1A) | voir plus bas |

Les enveloppes (Pitch EG, filtre, ampli) sont dessinées et suivent les potards.

## Pas inclus (et pourquoi)

- **Noms des paramètres d'effets selon le type** : le manuel décrit les 7 « Parameter 1 à 7 » du
  Parameter Change (table 9) et les emplacements propres à chaque effet (table 6), mais la
  correspondance exacte entre les deux n'est pas documentée. Étiqueter à l'aveugle risquerait de
  tromper. Elle se confirme facilement par l'enregistrement MIDI du N364 (voir section 5).
- **Noms des Multisounds et des sons de batterie** : le manuel fourni n'en contient pas les listes.
  Si tu as le document « liste des sons » de Korg, on peut les ajouter.
- **Lecture des paramètres d'effets** (hors type d'effet) lors d'une lecture du N364 : leur
  disposition dépend du type d'effet. Ils restent éditables en direct.
- Pattern Set, séquenceur, tables « Performance Edit » (tables 10 et 12, plages non documentées),
  noms de Programs/Combinations.

## 1. Compiler (Windows ou Mac)

**Sans rien installer** : mets le dossier dans un dépôt GitHub. La recette `.github/workflows`
compile automatiquement Windows et macOS (onglet Actions). Voir les explications détaillées
données dans la conversation.

**Windows en local** : Visual Studio 2022 (« Développement Desktop en C++ »), puis

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release

et copier `N364 Editor.vst3` dans `C:\Program Files\Common Files\VST3\`.

**Mac en local** : Xcode et CMake, puis

    cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build --config Release

copier `N364 Editor.vst3` dans `~/Library/Audio/Plug-Ins/VST3/`, puis `xattr -cr` sur le fichier.

## 2. Branchements et réglages du N364

- **MIDI OUT de l'interface → MIDI IN du N364** (envoyer) et **MIDI OUT du N364 → MIDI IN de l'interface** (lire).
- Mode **Global** du N364 : le **filtre MIDI** doit laisser passer le **System Exclusive** (ENA).
- Le **canal global MIDI** du N364 doit être le même que « Canal global » dans le plugin.
- Dans le plugin, mode de sortie **Port MIDI direct** : choisis **MIDI OUT** (vers le N364) et
  **MIDI IN** (depuis le N364). Mode **Hôte** : route la sortie MIDI du plugin vers l'interface dans
  Bitwig, et la sortie du N364 vers le plugin.

## 3. Utilisation

1. **Éditer en direct** : bouge un réglage, le son change. Rien n'est envoyé à l'ouverture d'un projet ni
   quand tu changes d'onglet. Les modifications agissent sur le son en cours d'édition.
2. **Lire un son** (onglet Bibliothèque) : choisis le type, puis « Lire depuis le N364 ».
   Fais-le avant de modifier, pour que le plugin soit synchronisé avec le son du synthé.
3. **Enregistrer un preset** : donne un nom (ou laisse vide pour reprendre le nom lu sur le N364) puis « Enregistrer ».
4. **Charger** : double-clic sur un preset, ou « Charger ». « Charger + envoyer au N364 » règle aussi le synthé.
5. **Importer la banque** : « Importer la banque A+B du N364 » crée 200 presets (Programmes ou Combinations).
6. **Écrire en mémoire** : choisis banque et numéro, puis « Écrire dans la mémoire du N364... ».
   **Attention : cela écrase le contenu de l'emplacement.** Le plugin envoie d'abord tous les réglages.
7. Le dossier des presets s'ouvre avec « Ouvrir le dossier » ; tu peux le sauvegarder ou le partager.

**Global** : chaque modification envoie le Global **en entier** (28 octets). Fais « Lire depuis le N364 »
avant de t'en servir. **Drums** : choisis le kit avant de modifier ; changer de kit ne renvoie rien
(utilise « Envoyer tout »).

## 4. Points à vérifier au premier test

- **Valeurs négatives** : complément à deux sur 14 bits (NOTE 13 du manuel).
- **Interrupteurs « Time Mod » des enveloppes** : envoyés -1 / 0 / +1 (déduit de la table 1).
- **Timbre Mode** (Combination) : champ de 2 bits (INT/EXT et ON/OFF), ordre des valeurs déduit.
- **Program No. des timbres** : 0 = OFF, 1 à 100 = programs 00 à 99, 101 à 236 = banque G (table 11).
  À la lecture d'une Combination, un Program de banque B ou D est ramené à 1-100 (la banque n'est pas conservée).
- **Multisound** : un seul paramètre 0 à 429 (table 8, n° 24 / 115), lu depuis les deux octets du dump.
- **Pan** : valeurs OFF/PRG du N364 (255, 31) ramenées au centre à la lecture.
- **Demandes de dump** : le manuel signale que certaines demandes ne sont pas reçues dans certains modes
  (§2-6). Si « Lire » ne répond pas, essaie en mode Program (ou Combination), puis Global pour Global et Drums.
- **Plages d'effets** : niveaux et balances supposés 0 à 100 ; « Param 1 à 7 » en -200..800.
- **Écriture en mémoire** : code 0x11 / 0x1A, banque 0 = A, 1 = B. Le N364 répond « écriture terminée »
  ou « erreur » (affiché en haut du plugin).

## 5. Valider avec ton N364 (enregistrement MIDI)

Le N364 envoie lui-même un Parameter Change quand tu tournes un réglage en face avant (manuel, p. 219-221).
Avec un moniteur MIDI (MIDI-OX sous Windows, MIDI Monitor sous Mac), note les messages `F0 42 3g 35 41 ...`
pour quelques réglages (négatifs, Time Mod, effets) : on pourra confirmer les numéros et valeurs, et ajouter
l'étiquetage des effets.

## 6. Ajouter ou corriger des paramètres

Tout se passe dans `Source/ParamTable.h` (une ligne par paramètre : `knob`, `toggle`, `choice`, `signChoice`)
et, pour la lecture des dumps, dans `Source/DumpDecode.h`.
