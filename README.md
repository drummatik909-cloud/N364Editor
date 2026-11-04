# Plugin MAQ 16/3 pour Bitwig – étape 2b, lot 3b (gammes)

Lot 3 validé (gamme, apprentissage). Ce lot 3b élargit la liste des gammes d'après le catalogue « Scaler 3 » fourni.

## Les 70 gammes

- Les 15 premières (index 0 à 14) sont inchangées : les projets existants gardent leur gamme.
- **45 nouvelles gammes** du catalogue (modes du mineur harmonique, mélodique et du majeur harmonique, pentatoniques, bebop, diminuées, gammes du monde) ; les noms équivalents sont regroupés (« Phrygien dominant / Spanish Gypsy / Flamenco »…).
- **5 gammes corrigées** (« corrigée ») : les intervalles du PDF ne faisaient pas une octave ; ils sont remplacés par les définitions usuelles : Pentatonic Dominant (Do Ré Mi Sol Sib), Persian (Do Réb Mi Fa Solb Lab Si), Kumoi (Do Ré Mib Sol La), Spanish 8-Note (Do Réb Mib Mi Fa Solb Lab Sib), Balinese (Do Réb Mib Sol Lab).
- **5 gammes approchées** (« approx. ») : le PDF ne donnait pas leurs intervalles ; versions usuelles en 12 notes : Todi, Marva, Purvi, Algerian, In. À vérifier à l'oreille.
- Non incluses : gammes micro-tonales ou sans définition (Slendro, Pelog, Kyoto, Istrian, Takemitsu, Blues Modified).
- Le menu est groupé en 8 catégories (Chromatique, Diatoniques & modes, Modes du mineur harmonique, Modes du mineur mélodique, Modes du majeur harmonique, Pentatoniques & blues, Symétriques & bebop, Exotiques & monde).
- Noms de notes dans les gammes : # dièse, b bémol, « nat » bécarre.

## Fichiers à mettre dans GitHub

Dossiers `plugin` et `src`, plus `tests`, `CMakeLists.txt` (racine) et `README.md`. Seuls changent : src/engine/Scale.cpp, src/engine/Scale.h, plugin/Facade.cpp, tests/test_main.cpp et README.md.

## Tests du moteur (4097 vérifications)

`g++ -std=c++17 -I src src/engine/Unit.cpp src/engine/Events.cpp src/engine/Scale.cpp src/ui/Strings.cpp src/ui/Preferences.cpp src/presets/Bank.cpp src/presets/FolderSettings.cpp tests/test_main.cpp -o maq_tests`
