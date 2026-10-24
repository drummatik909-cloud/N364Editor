# N364 Editor (v6) : plugin VST3 d'édition, de banques et de bibliothèque pour Korg N264/N364

Le plugin ne produit aucun son. Il pilote le N364 par SysEx (manuel, « Format de données MIDI »,
p. 219-229), lit ses sons, et range tes sons dans une bibliothèque. Environ 900 paramètres,
tous automatisables dans Bitwig.

> Ce code n'a pas pu être compilé ni testé sur un vrai N364 au moment de sa rédaction.
> Seules la table des paramètres et le décodage des dumps ont été vérifiés par des tests automatiques.
> Voir « Points à vérifier » plus bas.

## Nouveautés de la v6 : gestion complète des banques et de la bibliothèque

- **Données complètes des sons** : chaque son lu sur le N364 (lecture, import de banque, import .syx) est conservé avec ses
  données brutes (164 octets par Program, 136 par Combination), y compris les paramètres d'effets. Les exports et
  restaurations sont donc fidèles à l'original. Quand tu enregistres un preset après avoir modifié des potards, ils sont recopiés
  dans ces données brutes.
- **Export et import SysEx (.syx)** (onglet **SysEx**) :
  - sauvegarder les 200 Programs A+B ou les 200 Combinations A+B du N364 dans un fichier .syx (copie exacte du dump) ;
  - restaurer un tel fichier dans le N364, **après une sauvegarde automatique** de l'état actuel (dossier de sauvegardes,
    bouton « Ouvrir le dossier des sauvegardes automatiques ») ;
  - importer un .syx (un son ou une banque) dans la bibliothèque ;
  - exporter la bibliothèque (presets A00 à B99) en banque .syx ; exporter un preset seul en .syx (onglet Bibliothèque).
- **Renommer** : un preset de la bibliothèque (le nom des 10 premiers caractères est aussi mis à jour dans ses données),
  et un son du N364 (onglet Banques, « Renommer sur le N364 »).
- **Déplacer, copier, échanger** (onglet Banques) : copier un son d'une banque quelconque (A, B, C, D, GM) vers un emplacement
  A ou B ; échanger deux sons des banques A et B. Les écritures passent par le dump complet puis la demande d'écriture du N364.
- **Envoyer un son entier au N364** (onglet Bibliothèque) : « Envoyer au N364 (dump complet) » charge le son dans le tampon
  d'édition ; « Écrire dans un emplacement... » l'écrit en plus dans la banque A ou B.
- **Sécurités** : seules les banques A et B sont écrivables (les banques C, D et GM sont des sons d'usine) ; toute écriture
  demande une confirmation et indique l'emplacement écrasé ; la restauration d'une banque exige le mode « Port MIDI direct »
  et fait d'abord une sauvegarde automatique ; aucun autre message n'est envoyé pendant l'envoi d'une banque ; une opération
  s'arrête avec un message d'erreur si le N364 ne répond pas.

**Ordre de test conseillé** (n'essaie pas les écritures avant les lectures) :
1. onglet Banques : cliquer sur un son, vérifier que le N364 le sélectionne et que le plugin le lit ;
2. onglet SysEx : « Sauvegarder les Programs A+B » et « Sauvegarder les Combinations A+B », et garder ces fichiers ailleurs ;
3. copier un son vers un emplacement que tu n'utilises pas, et vérifier sur le N364 ;
4. seulement ensuite : échanger, renommer, restaurer une banque.

## Nouveautés de la v5

- **Onglet Banques** : les Programs A, B, C, D et GM (136 sons) et les Combinations A, B, C, D, avec leurs noms
  (liste d'usine du manuel, chapitre 14 de la Prise en main). **Un clic sur un son** le sélectionne sur le N364
  (changement de mode, Bank Select, Program Change), puis le plugin le lit et met tous ses potards à jour.
- **Suivi en temps réel** : quand tu changes de banque ou de son sur le N364, le plugin le détecte, se place sur
  le même son dans la liste, le lit et met ses potards à jour (case « Suivre le N364 en temps réel »).
- **Import d'une banque son par son** : « Importer cette banque » parcourt les 100 (ou 136) sons de la banque
  choisie, lit chacun sur le N364 et crée un preset. Cela marche pour **toutes** les banques, y compris C, D et GM
  qui ne partent pas dans le dump A+B. Les noms lus sur le N364 (en bleu clair) remplacent ceux du manuel et sont mémorisés.
- **Noms des sons** : les 430 Multisounds (nom affiché sous le potard) et les 215 sons de batterie (colonne Inst No
  des Drum Kits).
- **Paramètres d'effets par type** : pour chacun des 47 effets, les paramètres portent leur vrai nom
  (Reverb Time, Pre Delay, Delay Time L, Mod Speed...) avec leur plage (info-bulle) ; les paramètres inutilisés par
  l'effet sont grisés. Source : tableau « Les paramètres d'effets » (chapitre 5 du manuel de référence, p. 90-91).
  Quand tu changes de type d'effet, les paramètres sont renvoyés dans la plage du nouveau type.

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

### Points à vérifier propres à la v5

- **Numérotation des banques** : Bank Select (CC0/CC32) + Program Change d'après le tableau « MIDI In (Hex) » du manuel
  (A à D : mm=00, bb=00 à 03 ; GM : mm=38 ou 39, G129 à G136 : mm=3E). Le N364 doit avoir la **réception/émission des
  Program Change activée** dans les réglages MIDI du mode Global.
- **Suivi** : le N364 doit **émettre** ses Program Change (MIDI OUT du N364 relié à l'entrée de l'interface). Si le plugin
  ne suit pas, vérifie ce réglage global.
- **Import son par son** : compte environ une à deux minutes par banque. Si le N364 ne répond pas à une demande de
  lecture, ce son est ignoré (le compte des échecs s'affiche).
- **Noms d'usine** : extraits du manuel par reconnaissance de texte puis relus ; quelques noms peuvent différer
  légèrement de ceux du synthé. Les noms lus sur le N364 les remplacent.
- **Paramètres d'effets** : les « Parameter 1 à 7 » du Parameter Change sont supposés suivre l'ordre du tableau du
  manuel (Dry:Effect Mix = Balance 1, et Balance 2 pour Dual Delay et les effets combinés). À confirmer avec
  l'enregistrement MIDI du N364 (section suivante).
- **Plages d'effets** : la plage envoyée est la plage « brute » (par exemple Reverb Time 0 à 97 pour 0,2 à 9,9 s ;
  Mod Speed 0 à 216, non linéaire). L'info-bulle de chaque paramètre l'indique.
- **Sons de batterie** : la valeur « Inst No » est supposée valoir le numéro du son de batterie + 1 (0 = OFF).
- **Accusés de réception** : les écritures supposent que le N364 répond « données reçues » (0x23) à un dump de Program ou de
  Combination, et « écriture terminée » (0x21) à une demande d'écriture, comme le décrit le manuel. Sans réponse, l'opération
  s'arrête au bout de quelques secondes avec un message.
- **Mode du N364** : pour recevoir un dump, le N364 est mis en mode Program (ou Combination) par le plugin. Une opération
  d'écriture quitte donc un éventuel mode d'édition du N364 : enregistre ce que tu édites avant.
- **Envoi d'une banque** : environ 12 secondes par banque sur une prise MIDI DIN (la durée est affichée). Ne touche pas au N364
  pendant ce temps.

## 5. Valider avec ton N364 (enregistrement MIDI)

Le N364 envoie lui-même un Parameter Change quand tu tournes un réglage en face avant (manuel, p. 219-221).
Avec un moniteur MIDI (MIDI-OX sous Windows, MIDI Monitor sous Mac), note les messages `F0 42 3g 35 41 ...`
pour quelques réglages (négatifs, Time Mod, effets) : on pourra confirmer les numéros et valeurs, et ajouter
l'étiquetage des effets.

## 6. Ajouter ou corriger des paramètres

Tout se passe dans `Source/ParamTable.h` (une ligne par paramètre : `knob`, `toggle`, `choice`, `signChoice`)
et, pour la lecture des dumps, dans `Source/DumpDecode.h`.
