# Migration des appareils et composants SignalRGB

L’importeur [`import-signal-devices.py`](../../tools/room-setup/import-signal-devices.py)
fonctionne entièrement hors ligne. Il lit les exports JSON fournis, prépare un
inventaire et une proposition de configuration OpenRGB, puis s’arrête. Il ne
lit pas le registre, ne découvre aucun appareil, n’ouvre aucun port réseau et ne
modifie ni SignalRGB ni les réglages actifs d’OpenRGB.

## Inventaire observé le 27 septembre 2026

Le projet de disposition courant contient **101 éléments actifs, 1134 points
LED/échantillons et 28 groupes de contrôleurs**. Ces nombres ne constituent pas
une preuve que tous les pilotes natifs ont été testés physiquement. Les racines
redondantes des contrôleurs, les composants historiques et les entrées
désactivées sont exclus. Les anciens identifiants, adresses et numéros de série
restent seulement dans la sortie privée de l’importeur.

| Appareil/famille | Quantité et géométrie active | Correspondance native et limite restante |
|---|---|---|
| Corsair Lighting Node CORE | 2 contrôleurs, 6 puis 4 ventilateurs QL de 34 LED : 204 + 136 | `Corsair RGB Header 1` uniquement ; le second connecteur logique exposé par la famille n’est pas un deuxième port physique CORE. Permutation QL conservée : 7 matrices 9×9 et 3 formes 5×5 avec faces superposées nécessitant des routes multiples. |
| Nollie32, ventilateurs | 3 canaux de 8 LED | Zones `Channel 1..3`, permutation USB gérée par le pilote. |
| Nollie32, Strimer ATX | 6 lignes de 20 LED | Zones natives 16..21, canaux USB 19/18/17/16/7/6. |
| Nollie32, Strimer double 8 broches | 4 lignes de 27 LED | Zones natives 22..25, canaux USB 25/24/23/22. Les deux lignes GPU restantes restent inchangées. |
| ASUS Aura Z790-F | 3 LED intégrées, 1 point 12 V, ARGB 35/8/28 | Les 3 sorties adressables sont préparées. Les 3 LED intégrées et le point 12 V appartiennent à la même zone native fixe ; vérifier sa taille et les offsets réellement détectés. |
| ASUS ROG STRIX LC | 4 points | Pilote Aura USB présent ; disposition et ordre à comparer à la description native. |
| Govee LAN H61E1 | 4 × 30 segments contrôlables | Une zone native par appareil ; sous-segments du layout conservés, pas de redimensionnement physique. |
| Govee LAN H61A0 | 2 × 25 segments | Idem. Le découpage historique par adresse IP/SKU n’est pas mélangé aux composants actuels `Govee [identité]`. |
| Govee LAN H61A2 | 1 × 42 segments | Un autre H61A2 est désactivé et exclu de l’import. |
| Govee LAN H6062 | 57 segments, découpés 36/3/18 | Idem ; l’ordre de transmission reste celui du pilote. |
| Govee H6008 | 3 ampoules, 1 point chacune | Contrôleur Bluetooth natif ; import privé séparé de l’authentification. Aucun repli LAN par segment. |
| Govee H6159 | 1 bande non adressable, 1 point | Profil Bluetooth classique séparé ; longueur dessinée différente du nombre de couleurs contrôlables. |
| KBHE 75HE | 82 points | Pilote RAW HID natif ; le firmware réalise la permutation WS2812. |
| HYTE CNVS | 50 points de bordure | Pilote HYTE Mousemat présent ; conserver la forme, pas une simple bande dessinée arbitrairement. |
| G.Skill DDR5 | 4 barrettes, 8 points inférés chacune | Énumération SMBus et taille native à confirmer ; aucune taille fixe forcée à partir de l’inférence. |
| Logitech G502 X PLUS | 8 points, récepteur sans fil | Identifier le périphérique HID++ derrière le récepteur, et non le récepteur comme modèle de souris. |
| Alienware AW3426DW | 1 échantillon global SignalRGB | Le pilote natif possède 2 zones ; dupliquer explicitement le point global ou choisir deux placements. |
| NVIDIA RTX 3080 Ti FE | 2 points | Préserver la différence de capacité entre la zone couleur et la zone monochrome. |
| Stream Deck MK.2 | 15 marqueurs de géométrie, image réelle 480 × 272 | Sortie image native indépendante du nombre de LED. Ne pas importer 15 couleurs comme une image complète. |
| Fond d’écran | surface estimée, aucun point LED fiable | Le pont Delido existant expose un serveur SDK4 optionnel sur127.0.0.1:6743. Le plan propose cette connexion, sans l’activer. Un rendu de fond Windows natif reste distinct de cette compatibilité existante. |

L’utilisateur prévoit également des **Logitech G915**, compatibles avec la
présence du G502 X PLUS. Aucun G915 actif n’est démontré dans cet instantané :
le plan le note comme attendu, sans inventer d’identité, de présence ou de G512.

## Préparer le plan privé

Depuis la racine du dépôt, avec Python 3 standard :

```powershell
python tools/room-setup/import-signal-devices.py plan `
  --project C:\private\signal-layout\project.json `
  --metadata C:\private\signal-layout\root-devices.json `
  --snapshot C:\private\signal-layout\snapshot.json `
  --out private\device-migration
```

Le dossier de sortie doit être nouveau. Il reçoit :

- `migration-plan.json` : groupes, canaux, longueurs, coordonnées originales,
  ordre des composants, réservations désactivées et toutes les dispositions,
  dont **Full Scale conservée**. Les SHA-256 des trois entrées permettent de
  retrouver la version exacte de l’inventaire.
- `bindings.template.json` : canaux prêts à configurer, mais sélecteurs natifs
  volontairement vides. Une suggestion de numéro de zone n’est pas une identité.
- `OpenRGB.govee-lan.fragment.json` : fragment de découverte pour les seuls
  appareils LAN actifs, avec identité stable et IP observée. Ce fichier n’est
  pas fusionné automatiquement dans les réglages.

Si un wallpaper est actif, le plan contient aussi `external_connection_proposals`
pour le serveur SDK4 du pont existant. Sa description est tirée du source audité,
pas inventée comme une énumération live. Le test antérieur a livré 512 couleurs
exactes en loopback ; au contrôle passif du 27 septembre à02:18 local, aucun
listener n’existait sur6743. Il faut donc activer le serveur et choisir la source
OpenRGB SDK dans le pont avant de connecter le client OpenRGB. Le fragment
`Client.clients` reste une proposition : le cœur auto-connecte les entrées
sauvegardées, il n’existe pas de champ `enabled:false` par connexion.
Voir [l’audit wallpaper](wallpaper-and-native-canvas.md).

Un composant désactivé au milieu d’une chaîne garde son allocation physique.
Le supprimer de la géométrie active ne décale pas toutes les LED suivantes.
L’état actif est celui du projet fourni ; le script ne prétend pas vérifier la
présence USB/Bluetooth à l’instant où il est exécuté.

## Compiler les longueurs et segments natifs

Après une énumération native coordonnée, fournir un export JSON de descriptions
OpenRGB contenant `profile_version: 7` et `controllers`. Une configuration
enregistrée ne contient pas nécessairement les appareils encore non configurés :
il faut un export complet des descriptions pour ceux à rattacher.

Dans une copie privée de `bindings.template.json`, chaque sélecteur doit contenir
les valeurs **réellement exportées** de `type`, `name`, `description`, `version`,
`serial`, `location`. Garder seulement les contrôleurs et canaux à appliquer.
Vérifier chaque `native_zone` contre son nom exact, puis exécuter :

La commande `propose` peut préparer cette copie sans rien appliquer :

```powershell
python tools/room-setup/import-signal-devices.py propose `
  --plan private\device-migration\migration-plan.json `
  --native-export C:\private\native-descriptions.json `
  --out private\binding-proposals
```

Elle exige une identité unique : numéro de série USB, VID/PID et instance PnP,
identité Govee stable, port COM HYTE observé, adresse SMBus ou identité explicite
du pilote de surface. Un nom de modèle seul ne suffit pas. Les propositions
restent à relire ; une adresse SMBus ne valide ni le type de RAM ni sa taille.
`identity-proposals.json` conserve les absences/ambiguïtés et les appareils fixes
dont le placement nécessite encore une correspondance. Aucun sélecteur n’est
fabriqué pour une correspondance absente.

Puis compiler les propositions effectivement vérifiées :

```powershell
python tools/room-setup/import-signal-devices.py compile `
  --plan private\device-migration\migration-plan.json `
  --native-export C:\private\native-descriptions.json `
  --bindings C:\private\verified-bindings.json `
  --out private\native-configuration-candidate
```

Cela produit un nouveau `Configuration.json` et `compile-report.json`, sans les
installer. Les identités, modes et zones non sélectionnées sont préservés. Les
LED métadonnées sont adaptées aux nouvelles longueurs pour éviter des pointeurs
de zone hors des anciens tableaux lors de leur lecture par le cœur.

Le compilateur refuse un sélecteur incomplet/ambigu, un nom de connecteur
inattendu, une zone non configurable, une longueur hors bornes et les tailles
fixes ou sorties image sans mapping vérifié. Les tailles Govee restent fixes ;
seules leur géométrie et leurs subdivisions logiques deviennent configurables.

Les segments possédant des coordonnées entières uniques deviennent des matrices
natives, avec `NA` dans les cellules vides. Les autres restent linéaires et sont
signalés comme nécessitant une forme personnalisée Visual Map. Les placements
globaux sont conservés dans le plan, pas appliqués automatiquement à Visual Map.

## Proposition Visual Map affine, sans activation

```powershell
python tools/room-setup/import-signal-devices.py export-map `
  --plan private\device-migration\migration-plan.json `
  --native-export C:\private\native-descriptions.json `
  --bindings C:\private\verified-bindings.json `
  --layout "Full Scale" `
  --out private\fullscale-map-candidate
```

Cette commande revalide la configuration des segments puis produit
`layout-components.vmap.json` et `map-import-report.json`. Elle n'installe aucun
fichier dans le dossier Visual Map. `auto_load`, `auto_register` et `hide_members`
restent faux. La source Full Scale reste inchangée.

Chaque composant actif lié à un segment obtient une forme personnalisée complète,
avec l'identité exportée du contrôleur, `zone_idx`, `is_segment: true` et
`segment_idx`. Les composants désactivés conservent leur index et leur longueur
dans la configuration native, mais ne deviennent pas membres du plan. Les
coordonnées fractionnaires, superposées et le sens `LedMapping` sont conservés.
Le découpage des Strimer conserve le rectangle d'origine et son pivot : chaque
rangée utilise les coordonnées de la forme complète, sans déplacement implicite.

La transformation est `scale=1` plus `affine.scale_x/scale_y/rotation/flip_x/flip_y`,
avec ancrage supérieur gauche avant rotation. Le gain du membre multiplie la
luminosité du layout. `point_origin="cell"` conserve la convention Visual Map
historique : l'équivalence exacte au centre de pixel du moteur SignalRGB n'est
pas démontrée et n'est pas annoncée.

Il faut le fork Visual Map avec **affines et identité des segments**, ainsi que
la configuration de composants correspondante. Une ancienne version omettant
`segment_idx` ne peut pas distinguer plusieurs ventilateurs du même connecteur.
Les appareils fixes, images ou absents sans mapping vérifié sont explicitement
listés dans `unmapped_layout_elements` ; un rapport `complete: false` ne doit
pas être présenté comme une migration totale.

La zone fixe ASUS « Aura Mainboard » utilise aussi des segments : les LED
intégrées précèdent les connecteurs 12 V, dans l'ordre des paquets Signal et du
pilote natif. Un profil déclare cet ordre ; l'import exige que la somme égale
exactement le compte fixe lu par le pilote. Le connecteur 12 V conserve sa propre
position et son pivot. Les sources désactivées gardent leur place dans le paquet,
sans devenir membres du layout. Aucun redimensionnement n'est permis. Il faut
un export du pilote annonçant TYPE+SEGMENTS sur cette zone ; un ancien export
sans ces capacités est refusé. Une projection de capacités issue du code peut
servir de proposition, à condition de la distinguer d'une validation runtime.

### Sorties fixes et images complètes

`export-map` accepte aussi `--fixed-bindings C:\private\fixed-bindings.json`.
Ce fichier est séparé des liaisons de configuration : il ne redimensionne aucun
appareil et ne modifie pas `Configuration.json`. Chaque élément de `outputs`
doit fournir `source_id`, les six champs d'identité native `selector`,
`zone_idx`, `kind` et une preuve non vide dans `evidence`. Une identité ambiguë
ou une zone déjà utilisée par un autre membre provoque une erreur.

Pour `kind: "led"`, `source_wire_indices` contient un index de la source par LED
native, dans l'ordre natif. Il permet notamment de recopier une source globale
vers plusieurs zones, ou de conserver une permutation matérielle démontrée.
Le nom d'un modèle et un nombre égal de LED ne constituent pas cette preuve.

Pour `kind: "image"`, il faut `native_image_capability_verified: true` et un
rectangle `source_rect: {x, y, width, height}` dans la surface source. La zone
cible doit fournir une matrice dense complète. L'export conserve cette matrice
pour le routage image natif de Visual Map ; les points indicatifs d'un plugin
image ne remplacent jamais ses pixels. La transformation compense le pivot de
la surface source entière lorsque le rectangle capturé en exclut une marge.
Ainsi une capture 32×20 dans des limites 33×21 reste correcte après rotation,
miroir et échelle différente sur les deux axes, tout en conservant une matrice
de compatibilité 80×50 dans la sortie native.

Le rapport compte séparément `led_routes`, `image_surfaces` et
`image_compatibility_cells`. Ces dernières ne sont pas présentées comme des LED
physiques. `fixed_outputs` conserve les preuves données par le fichier privé.

`kind: "matrix"` et `matrix_sampling_verified: true` permettent également une
surface échantillonnée par le chemin LED habituel, sans déclarer de capacité
image native. Le pont Wallpaper Engine conservé utilise ainsi sa matrice SDK4
réelle 32×16. `matrix_surfaces` et `matrix_sample_cells` distinguent ces 512
échantillons de l'image native Stream Deck. Les champs `source_rect` et l'affine
conservent la surface source, même si ses dimensions diffèrent du nombre de
cellules transmis au SDK.

La proposition locale complète du 27 septembre contient **111 membres,
1 120 routes LED, une surface image native et une matrice de 512 échantillons**,
pour les 101 éléments sources actifs. Full Scale et Music - Pump Up Beats sont
exportés sans activation automatique. Cela décrit une couverture de mapping,
pas une validation visuelle simultanée de tous les appareils.

La G502 utilise la preuve de lecture native conservée dans
[`tests/room-logitech/validation.json`](../../tests/room-logitech/validation.json) :
identifiants matériels 1 à 8, parcourus dans cet ordre par le pilote et le plugin
Signal. Les quatre RAM sont liées aux descriptions réelles obtenues sous
administrateur : ENE AUDA0-E6K5-0101, huit LED fixes chacune. Leur adresse SMBus
est comparée à l'identifiant Signal, avec refus des correspondances ambiguës.
L'ordre RBG envoyé par les deux implémentations est séquentiel ; la disposition
source inversée verticalement est conservée. Le modèle seul n'aurait pas permis
d'imposer huit LED.

La configuration de composants et l'export-map sont des artefacts séparés.
Un descripteur SDK résumé pour le mapping (Wallpaper) ne doit pas être chargé
comme profil de configuration. Le cœur reconstruit a exporté les nouvelles
capacités ASUS (`flags=40`) et le compte fixe de quatre LED. La proposition
finale combine cet export réel, les descriptions matérielles déjà configurées
et les quatre descriptions RAM obtenues sous administrateur. Elle ne projette
plus de capacité non vérifiée.

Les exports originaux restent inactifs. Pour l'installation quotidienne privée,
seule leur copie `plugins/settings/virtual-controllers/Full Scale.json` active
`auto_load` et `auto_register` ; `hide_members` reste faux. La copie Music garde
ses deux options automatiques désactivées. Toute configuration ou carte existante
est sauvegardée avant remplacement. L'import ne modifie ni le registre SignalRGB,
ni son Full Scale original, ni les préférences globales `OpenRGB.json`.

La convention de composant est **`LedMapping[i] = index physique de la LED située
à LedCoordinates[i]`**. Elle est documentée par la description Signal et
explicitée dans le [chargement de l’éditeur Nollie/Signal](https://github.com/qiangqiang101/Nollie-SignalRGB-Component-Editor/blob/733bd88860d36e1a30e926891be1f378eb215691/SignalRGB-CompGen/frmMain.vb)
et son [import Visual Map](https://github.com/qiangqiang101/Nollie-SignalRGB-Component-Editor/blob/733bd88860d36e1a30e926891be1f378eb215691/SignalRGB-CompGen/frmImport.vb).
Le plan conserve les tableaux bruts et fournit `wire_points`, trié par
`wire_index`, avec cette provenance. Aucun code de l’éditeur tiers n’est copié.

Trois QL utilisent réellement une forme 5×5 : leurs 34 LED partagent seulement
16 coordonnées. Une matrice à un index par cellule perdrait les LED des faces
superposées. Le plan conserve chaque route, et le rapport de compilation exige
un `custom_shape` Visual Map à coordonnées dupliquées pour ces composants. Les
sept autres QL ont 34 coordonnées uniques dans une forme 9×9 et peuvent devenir
des matrices de segment directement. Cette limite est distincte du sens de la
permutation désormais établi.

## Corrections natives associées et vérification

`SetupZones()` vide désormais `leds_channel` pour Corsair Lighting Node et Nollie.
Sans cela, après `[2,3] → [4,1]`, `DeviceUpdateSingleLED(3)` envoyait toujours la
deuxième sortie alors que la LED appartient désormais à la première. Le pilote
Govee annonce les capacités de géométrie/segments, conserve ses tailles fixes et
ignore les indexes de zone invalides avant tout accès au tableau. ASUS conserve
le type et les segments configurés de sa zone fixe après `SetupZones()`, sans
changer le nombre de LED ni l'ordre du paquet.

```powershell
python -m unittest discover -s tools/room-setup -p test_import_signal_devices.py -v
# Dans une console de développement MSVC :
python tests/room-channel-reconfigure/run.py --out C:\private\channel-tests
```

Résultat hors matériel : **37 tests Python** et **218 assertions C++** passent.
Les tests Python incluent le rejet des identités ambiguës et des zones
chevauchantes, l'ordre explicite des sorties fixes, les coordonnées superposées,
et les pivots des images avec rotations, miroirs et échelles non uniformes.
Le test C++ extrait les méthodes de production au moment de la compilation et
remplace uniquement les frontières USB/stockage. Il vérifie les trois pilotes et
les quatre familles Govee de cette pièce, notamment les deux segments ASUS,
le RGB 12 V à l'offset 3 et le canal ARGB suivant. Le paramètre `--without-fix` reproduit
le mauvais canal sur le deuxième redimensionnement et doit échouer.

Sources : les pilotes cités dans
[`device-profiles.json`](../../tools/room-setup/device-profiles.json),
`ProfileManager.cpp::LoadControllerFromListWithOptions`, les descriptions JSON
dans `RGBController.cpp`, et la
[structure officielle des composants SignalRGB](https://docs.signalrgb.com/plugins/component-structure/).
Les fichiers personnels et secrets du pont Bluetooth ne sont pas publiés.

## Un seul effet spatial pour le canvas complet

`tools/room-setup/write-canvas-profile.py` prépare un profil Effects à partir
d'une description native sauvegardée. Le sélecteur reprend l'identité exacte
du contrôleur virtuel et refuse les noms ambigus ; il ne sélectionne jamais les
appareils physiques individuellement. Le profil contient un shader arc-en-ciel
spatial animé, sans texture externe, avec une image de **800×500 à 30 FPS**.
Les sorties image reçoivent le champ complet et les LED en échantillonnent leur
position dans Visual Map. La résolution du rendu reste indépendante de la
matrice de compatibilité du contrôleur virtuel.

```powershell
python tools/room-setup/write-canvas-profile.py `
  --native-export C:\private\native-inventory.json `
  --controller-name "Full Scale.json" `
  --out "C:\private\profiles\Full Scale - Rainbow.json"
python -m unittest discover -s tools/room-setup -p test_canvas_profile.py -v
```

L'outil ne remplace aucun fichier existant, n'ouvre aucun SDK et ne change pas
les préférences de démarrage. Il produit un profil contenant uniquement les
réglages du plugin Effects : aucun mode ni couleur sauvegardée d'un appareil
physique. Quatre tests vérifient l'identité unique, les limites de dimensions
et de cadence, l'absence d'état matériel et la conservation des préférences
étrangères lors de la préparation des options d'ouverture/reprise.

Pour un démarrage quotidien, enregistrer ce profil dans le dossier `profiles`
de la configuration choisie, puis sélectionner son nom dans les profils
d'ouverture et de reprise d'OpenRGB. La carte Visual Map doit être chargée et
enregistrée automatiquement avant l'affectation de l'effet. Un sélecteur
préparé depuis le code source doit être comparé au descripteur réel du
contrôleur après chargement : une version ou une identité différente peut
empêcher l'affectation sans toucher au mapping d'origine.
