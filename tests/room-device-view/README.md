# Validation de DeviceView pour de grands périphériques

Ce harness compile le **vrai `DeviceView.cpp` et le vrai `RGBController.cpp`** avec Qt/MSVC. Le contrôleur utilisé contient seulement des données synthétiques : aucun transport HID, BLE, réseau ou accès matériel n'est lié au programme. L'application Qt utilise le backend `offscreen` et n'ouvre pas de fenêtre sur le bureau de l'utilisateur.

## Correctifs examinés

- Un snapshot du tableau de couleurs remplace trois appels verrouillés `GetColor` par LED et par peinture. La méthode ajoutée à `RGBController` est non virtuelle ; aucun champ ni entrée de vtable de cette classe n'est ajouté.
- `LEDRect` constitue la même géométrie pour le dessin et la sélection. Une dimension positive inférieure à un pixel devient un pixel ; une dimension nulle, négative, non finie ou hors plage reste absente. Les calculs flottants et l'arrondi des rectangles ordinaires sont conservés.
- Au-delà de 4 096 LED, les rectangles de moins de cinq pixels sur au moins un axe sont écrits dans une `QImage` de la région visible puis composés ensemble. Ce chemin ne suppose aucune résolution, disposition matricielle ou marque de périphérique. Les grandes LED restent dessinées normalement.
- Le raster est vidé avant chaque LED normale ou transparente afin de conserver l'ordre des chevauchements. Les noms de zones, bordures, textes lisibles et rectangle de sélection restent dessinés ensuite par le chemin existant.
- Le raster est réutilisé et plafonné à 4 194 304 pixels, soit **16 Mio** en ARGB32. Une région visible plus grande utilise le chemin QPainter ordinaire ; aucune allocation par LED et par texture n'est créée.

La mise à jour de l'interface reste coalescée par un drapeau atomique et un timer GUI de 33 ms dans `OpenRGBDevicePage`. Le callback ne peint pas depuis le thread du contrôleur. `UnregisterUpdateCallback` se synchronise avec l'invocation des callbacks via `UpdateMutex`, que l'implémentation actuelle de `SignalUpdate` conserve pendant l'appel. Les événements Qt ont la page comme destinataire et le timer est son enfant.

## Exécution

```powershell
$env:QT_ROOT = 'C:\chemin\Qt\6.8.3\msvc2022_64'
& tests/room-device-view/Build-Tests.cmd
```

`VCVARS` peut désigner un autre script `vcvars64.bat`. Le runner n'installe rien. Le répertoire `.build` est ignoré par Git. Les seuls stubs désactivent l'en-tête ResourceManager inutilisé dans ce widget et le diagnostic de logging du contrôleur. Les méthodes de données, leurs verrous et la peinture sont celles de production. Les membres privés du widget sont rendus accessibles uniquement dans les unités de compilation du test, sans changer son code de calcul.

Tests réussis avec **Qt 6.8.3 / MSVC 14.44** :

- Géométrie ordinaire, dimensions subpixel, zéro, négatif, NaN et infini ; contrôleur vide.
- Clic réel Qt et sélection CTRL ajout/retrait ; sélection d'un pixel d'une matrice 800 × 600.
- Sélection programmée `{5,3,5}` donnant exactement `{3,5}`, rejet d'un index hors plage sans modifier la sélection, et liste vide.
- Comparaison pixel par pixel entre le chemin QPainter ordinaire et le raster : layout sparse, clipping, chevauchements petites/grandes LED, highlight opaque et semi-transparent.
- Cent snapshots cohérents pendant des écritures concurrentes de couleur via le verrou réel du contrôleur.
- Mesure de douze peintures d'un périphérique synthétique de 480 000 LED dans le widget réel 800 × 600.
- Aperçu générique de deux sorties image arbitraires : dimensions et ratio natifs, mapping miroir, cache de frame immutable, expiration, frame/mapping invalides, absence de polling quand le widget est caché et aucun appel de soumission au contrôleur.

## Mesures locales du 27 septembre 2026

| Mesure | Avant raster | Avec raster |
|---|---:|---:|
| Peinture médiane | 82,32 ms | 24,76 ms |
| Peinture maximale observée | 94,97 ms | 27,24 ms |
| Initialisation + première peinture | 146,24 ms | 68,01 ms |
| Clic/sélection | 18,95 ms | 7,96 ms |

Ce sont des mesures locales hors écran, pas une garantie de fréquence de l'application complète ni une mesure d'écriture matérielle. Après l'ajout des tests de sélection et d'aperçu, le dernier passage donne 23,84 ms de médiane et 24,81 ms au maximum. L'initialisation géométrique et la sélection parcourent encore les LED ; le raster réduit surtout le nombre d'appels QPainter. Les chiffres détaillés du dernier passage sont dans `latest-result.json`.

## Aperçu d'une sortie image native

`ImageOutputView(RGBController*, QWidget*)`, sans `Q_OBJECT`, découvre l'interface secondaire `room_image::RGBControllerImageInterface`. `HasOutputs()`, `OutputCount()` et `RefreshOutputs()` permettent à la page d'intégrer le widget sans connaître un modèle de matériel. Le contrôleur doit vivre plus longtemps que son widget, comme pour DeviceView.

Un timer de 67 ms interroge uniquement les pages visibles. La vignette conserve le ratio préféré de la sortie, dans une limite de 320 × 180 pixels. Elle échantillonne directement la frame immutable avec `SampleBGRA`, sans recopier le grand buffer. Identité des pixels, séquence, dimensions et mapping inchangés évitent un nouveau rendu. Une source absente/expirée ou invalide efface l'image précédente ; aucun aperçu périmé n'est présenté comme actif.

## Limites de la revue

`CopyColorsSnapshot` protège les écritures respectant `AccessMutex`. Les écritures via un pointeur brut obtenu par `GetColorsPointer`, ainsi qu'une reconfiguration structurelle concurrente de la géométrie lue par `InitDeviceView`, restent des limites préexistantes de l'API. Aucun nouveau risque de cycle de vie n'a été identifié dans le callback coalescé ; le harness n'instancie pas la page complète avec ResourceManager.

Le défaut préexistant de `SelectLEDs(vector)` est corrigé : les drapeaux utilisent les valeurs des indices validés, puis le chemin existant filtre les doublons. Le test couvre également la conservation de la sélection après un rejet hors plage.
