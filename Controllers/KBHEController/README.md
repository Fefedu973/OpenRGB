# KBHE 75HE — contrôleur natif OpenRGB

Ce contrôleur expose les 82 LED du clavier KBHE 75HE ISO à OpenRGB, sans changement de firmware. Le contrôleur natif n’a pas encore été validé sur le clavier physique ; ses tests utilisent des réponses simulées correspondant au firmware et au plugin SignalRGB existants.

## Identification et protocole

- USB `9172:0002`, interface **1**, usage page `FF00`, usage `0001`. Les interfaces de clavier boot/NKRO et le gamepad XInput sont exclus.
- La détection lit la version (`00`) et les capacités RGB (`7F`). Le descripteur doit annoncer le protocole 1, 82 LED RGB logiques, des blocs de 60 octets, le mode 7 et les capacités nécessaires. Si l’ancien firmware répond « commande inconnue », le repli exige le nom USB contenant `75HE`. Un timeout ou des capacités incompatibles ne déclenchent pas ce repli.
- Le mode **Direct** lit et conserve l’activation initiale (`60`) et le mode initial (`6E`), sélectionne le mode temporaire 7 (`6F`) puis active l’éclairage (`61`). Il ne modifie pas la luminosité matérielle. Si le clavier était déjà en mode 7, il sauvegarde d’abord les 246 octets de l’image live avec cinq requêtes `68` ; une réponse incomplète empêche la prise de contrôle.
- Une image comporte 246 octets RGB, dans l’ordre logique K01…K82, envoyés avec `6A` en cinq blocs de 60/60/60/60/6 octets. Chaque écriture HIDAPI comporte 65 octets : identifiant de rapport nul puis paquet de 64 octets. Les réponses sont vérifiées par commande, statut, index et longueur du bloc.
- Avant chaque image, `6E` confirme que le clavier est toujours en mode 7. Un changement externe suspend l’envoi, car `6A` hors mode 7 modifierait les pixels sauvegardés. Sélectionner de nouveau Direct reprend le contrôle.
- Le mode **Hardware** et la destruction du contrôleur tentent de restaurer l’effet antérieur avec `76`, puis l’activation initiale. Si le firmware répond statut 1 alors qu’il est encore en mode 7, son historique temporaire est absent : le pilote revient au mode non-live réellement observé avant acquisition, ou remet l’image live initiale si le mode initial était déjà 7. Cette dernière voie relit les cinq blocs et exige une égalité exacte ; elle ne prétend pas retrouver l’effet matériel antérieur à SignalRGB. Le mode et l’activation restaurés sont aussi relus. Un effet choisi entre-temps par une autre application est préservé. Une déconnexion USB peut empêcher cette restauration.

Il faut un seul programme propriétaire de l’éclairage à la fois. Ce code n’arbitre pas une course entre plusieurs applications utilisant simultanément le même canal RAW HID. Aucune commande de clavier, de remappage, de flash ou de réinitialisation n’est émise.

## Géométrie et limites

La matrice de 52 × 130 place chaque centre de touche sur une grille de 1/8 d’unité, pour conserver les décalages du clavier ISO, la touche Entrée et les touches larges. Elle contient exactement 82 indices logiques distincts. Le firmware effectue lui-même la permutation physique de la chaîne WS2812 : le contrôleur ne la répète pas.

Les noms sont ceux des positions physiques standard OpenRGB. Ce module contrôle les couleurs ; il ne capture ni n’injecte de frappes et ne prétend pas régler les correspondances Windows AZERTY d’un autre moteur d’effets.

## Sources et validation

Sources primaires du projet [KBHE](https://github.com/Fefe-Nayz/kbhe-monorepo), checkout de référence `6b3be2bba760ace31d6a0223b7c371464cdc881d` avec ses corrections locales conservées :

- `integrations/signalrgb/kbhe_6key_rawhid.js` : géométrie, interface et transferts RGB utilisés par le plugin.
- `firmware/Core/Inc/hid_protocol.h` et `firmware/Core/Src/hid_protocol.c` : commandes, structures et ACK.
- `firmware/Core/Src/led_matrix.c` : permutation physique gérée dans le firmware.
- `firmware/Core/Src/settings.c`, `settings_set_led_effect_mode` / `settings_restore_led_effect_before_third_party` : une réécriture du mode courant ne crée pas de jeton de restauration ; le statut 1 de `76` signifie notamment que ce jeton manque.
- `cmd_get_led_all` dans `hid_protocol.c` et `led_matrix_get_raw_data` dans `led_matrix.c` : `68` retourne `pixels_runtime`, donc une image live peut être conservée sans modifier les pixels sauvegardés.

Depuis la racine OpenRGB :

```console
python Controllers/KBHEController/tests/run.py
```

Les tests compilent le vrai pilote avec des fonctions HID simulées, sans bibliothèque capable d’ouvrir du matériel. Ils vérifient les 246 octets, le dernier bloc, les 82 positions, le mode ancien et le descripteur récent, les réponses tardives, les écritures incomplètes, les délais, les erreurs de longueur, la restauration et la sérialisation de deux images concurrentes. Le wrapper et le détecteur sont également compilés contre les en-têtes OpenRGB présents. Le fichier de test utilise `.cc` pour rester hors de l’autodécouverte `Controllers/**/*.cpp` du projet.

Régression du 27 septembre : les fixtures reproduisent désormais le vrai jeton
du firmware, au lieu de faire réussir `76` systématiquement. Elles couvrent le
mode 7 orphelin, la remise exacte de son image, les blocs de lecture périmés,
l’échec de readback, le mode matériel mémorisé et une restauration d’activation
qui échoue une fois puis réussit sans perdre le contexte. Le getter
`GetRestorationResult()` précise le résultat au diagnostic appelant.

Code du contrôleur : GPL-2.0-or-later, comme OpenRGB. Aucun binaire propriétaire, firmware ou identifiant matériel personnel n’est inclus.
