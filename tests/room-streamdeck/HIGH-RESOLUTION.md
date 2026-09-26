# Matrices SDK et flux d'images

Audit du 27 septembre 2026. Le contrôleur Stream Deck ajouté ici propose un **adaptateur compatible de 80×50 pixels par défaut** et une entrée native FrameSurface indépendante des LED. Une image d'écran n'est donc pas représentée par autant de LED OpenRGB.

## Limites vérifiées du format existant

Dans `RGBController/RGBController.cpp`, les fonctions `GetColorDescriptionData`, `GetColorDescriptionSize` et `SetColorDescription` emploient encore un compteur de couleurs `unsigned short`. La description du contrôleur contient aussi un nombre de LED sur 16 bits. Ces chemins restent utilisés avec le SDK 6. La limite représentable de ces compteurs est donc **65535**, pas des millions de pixels.

Une seconde limite de représentation concerne la taille du bloc de matrice : deux dimensions 32 bits suivies de N indices 32 bits donnent `8 + 4*N` octets. Le champ qui annonce sa longueur dans une zone est sur 16 bits. Une longueur fidèle tient jusqu'à **16381 cellules**. Cela ne signifie pas que tout OpenRGB refuse nécessairement une matrice plus grande : le parseur C++ actuel vérifie surtout que la longueur annoncée est non nulle, puis lit les dimensions et leurs indices. Certains blocs hors plage peuvent donc passer avec ce parseur permissif. Ils ne constituent pas une représentation interopérable fiable avec les clients qui respectent la longueur annoncée.

| Matrice | Cellules | Bloc de matrice | Compteur de couleurs | Longueur de matrice fidèle sur 16 bits |
|---|---:|---:|---|---|
| 32×16 | 512 | 2056 octets | Oui | Oui |
| 80×50 | 4000 | 16008 octets | Oui | Oui |
| 160×100 | 16000 | 64008 octets | Oui | Oui |
| 320×200 | 64000 | 256008 octets | Oui | Non |
| 256×256 | 65536 | 262152 octets | Non | Non |
| 480×272 | 130560 | 522248 octets | Non | Non |

Les limites binaires ne mesurent pas les performances de l'interface : 4000 LED peuvent déjà coûter davantage à un éditeur d'effets que 15 zones. Le test hors matériel valide le contrôleur et son transport ; le débit de l'application complète doit être mesuré séparément.

## Le projet wallpaper possède déjà un serveur OpenRGB

Le dépôt [Delido/signalrgb-wallpaper](https://github.com/Delido/signalrgb-wallpaper) contient un serveur SDK qui expose une matrice virtuelle par écran, distinct de son client qui lit les périphériques OpenRGB. L'audit porte sur le commit `2d1099eeaf1f59c1693e22503ba95d7a50fc6603`. Le serveur annonce le protocole 4 et utilise le port 6743 par défaut ; OpenRGB peut s'y connecter comme client réseau. La présence de cette fonction invalide l'hypothèse selon laquelle il faudrait créer un fork uniquement pour obtenir un appareil virtuel. [Code source précis](https://github.com/Delido/signalrgb-wallpaper/blob/2d1099eeaf1f59c1693e22503ba95d7a50fc6603/wallpaper_bridge/openrgb_server.py).

Son sérialiseur utilise `min(matrix_block,65535)` pour le champ de longueur mais écrit le bloc complet ; il faut garder la distinction de compatibilité décrite ci-dessus. Il écrit aussi `led_count` avec `struct.pack('<H',...)`, qui ne représente pas 65536. Ces constats n'autorisent pas à annoncer que toute résolution proposée dans l'interface fonctionne dans le SDK. L'agent principal vérifie ces cas dans l'audit du projet ; ce contrôleur ne modifie pas ce dépôt et n'en lance pas de fork.

## Chemin natif pour de vraies images détaillées

Le chemin recommandé pour une évolution du fork est séparé du tableau de LED :

```text
Effet / capture -> surface de pixels native
                   |-> échantillonnage aux positions des LED -> contrôleurs RGB
                   |-> image 480×272 -> découpage BGRA -> API du fond Stream Deck
                   |-> surface écran -> transport d'image du wallpaper
```

Le premier transport natif est maintenant implémenté dans [FrameSurface](../../FrameSurface/README.md) : image BGRA8 CPU en mémoire partagée Windows, versionnée, bornée à 64 MiB, dernière image seulement, avec TTL. Le contrôleur Stream Deck lit directement cette surface via l'option `frame_surface` et ignore alors les mises à jour de sa matrice de compatibilité. Les tests synthétiques couvrent 800 × 600 et le découpage HTTP réel vers un serveur simulé. L'adaptateur 80×50 reste le mode par défaut pour les effets et clients OpenRGB existants. Ce travail ne prouve pas encore le rendu physique ni l'intégration d'un transport wallpaper ; le partage de texture GPU et une scène commune avec transformations restent des étapes distinctes.

Pour Stream Deck, l'API existante reçoit déjà une image détaillée par ses quinze tuiles : 311040 octets par image, soit environ 6,22 Mo/s à 20 images/s sur loopback. Aucun JPEG n'est nécessaire sur ce chemin local. Le plafond physique du fond reste 480×272 et l'application Elgato compose ensuite les icônes. Un flux 4K ne lui apporterait pas plus de pixels physiques.

Pour le wallpaper, il faut d'abord vérifier son transport d'images actuel et son moteur de rendu. S'il faut une extension native, un canal local binaire borné ou une texture partagée peut porter largeur, hauteur, stride, format et numéro de trame, avec file limitée à la dernière image et négociation de capacité. Modifier silencieusement le format des paquets SDK 6 ou augmenter les compteurs 16 bits d'un seul côté casserait l'interopérabilité. Un éventuel nouveau protocole doit être explicitement négocié et testé avec les deux extrémités.

## Sources OpenRGB

- [Format réseau version 1.0](https://gitlab.com/CalcProgrammer1/OpenRGB/-/raw/release_1.0/NetworkProtocol.h).
- [Sérialisation et parsing RGBController version 1.0](https://gitlab.com/CalcProgrammer1/OpenRGB/-/blob/release_1.0/RGBController/RGBController.cpp), confirmé dans le checkout local OpenRGB-Room.
- [API de plugins 5](https://gitlab.com/CalcProgrammer1/OpenRGB/-/raw/release_1.0/OpenRGBPluginInterface.h) : FrameSurface reste indépendant de cette ABI, sans en modifier les structures ni les compteurs de LED.
