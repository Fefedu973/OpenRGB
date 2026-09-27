# Reprise Govee du 27 septembre 2026

Deux défauts distincts ont été corrigés : la recherche Bluetooth après un démarrage
Windows à froid et l'omission d'une seconde bande LAN H61A2 lors de l'import du
layout. Ce document ne contient ni adresse privée, ni clé d'authentification.

## Bluetooth : alimenter le cache Windows avant la connexion

Après le redémarrage signalé, les quatre appareils configurés échouaient avant
toute authentification : `FromBluetoothAddressAsync` ne renvoyait aucun objet.
[Microsoft documente ce résultat](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.bluetoothledevice.frombluetoothaddressasync)
pour un appareil non appairé absent du cache système et recommande une découverte
avant de réessayer. L'ancien transport Bleak effectuait déjà cette découverte.
Un scan passif ponctuel a observé les quatre appareils et permis au processus
OpenRGB existant de quitter cette boucle d'échec. Des erreurs GATT ultérieures
sont distinctes : la découverte ne prouve ni l'authentification ni le streaming.

Le correctif natif utilise un watcher passif partagé entre les workers, uniquement
après un résultat nul de la recherche directe. Le scan dure au plus cinq secondes ;
le budget d'un appel, attente du scanner comprise, est de six secondes. Les
annulations sont vérifiées toutes les 25 ms. Seules les adresses déjà configurées
sont retenues, avec leur type public/aléatoire observé, avant une recherche typée.
Il n'y a ni ajout automatique d'appareil, ni appairage, ni accès GATT aux annonceurs
inconnus. Les contrôles d'identité et d'authentification existants restent requis.

Sources : [coordinateur portable](../../Controllers/GoveeBluetoothController/GoveeBluetoothDiscovery.h),
[transport Windows](../../Controllers/GoveeBluetoothController/GoveeBluetoothController_Windows.cpp)
et [documentation du pilote](../../Controllers/GoveeBluetoothController/README.md).

La sonde utilise le watcher de production et simule seulement la recherche
d'objet : **aucun objet Bluetooth ouvert, aucune opération GATT et aucune écriture
d'éclairage**. Les deux résultats sont conservés, sans masquer le premier échec :

| Essai natif passif | Résultat | Durée | Watchers |
| --- | --- | --- | --- |
| Immédiatement après l'arrêt d'OpenRGB | 0 appareil sur 4 | 5 048 ms | 1 |
| Reprise plus de 40 secondes après l'arrêt | 4 appareils sur 4, tous de type public | 2 938 ms | 1 |

Un délai de libération ou de réannonce est plausible, mais sa cause n'est pas
établie. Aucune attente arbitraire supplémentaire n'a été ajoutée au pilote.
Les preuves privées sont `passive-native-recovery-probe.json` et `probe-retry.json` ;
la [sonde reproductible](../../Controllers/GoveeBluetoothController/tests/passive_discovery_probe.cc)
ne publie pas les identités des appareils.

## Seconde H61A2 : identité actuelle, positions conservées

Le générateur local `SignalRGB-Layout-Studio/build_project.py` lisait les composants
du registre vivant en parcourant un ensemble de parents non ordonné. Il écrivait
ensuite chaque composant dans un dictionnaire indexé par UUID, sans détecter les
collisions. L'ancien parent et le parent actuel contenaient encore les mêmes
15 UUID : l'ancien parent désactivé pouvait donc écraser l'identité actuelle.
[L'importeur](../../tools/room-setup/import-signal-devices.py) excluait ensuite ces
éléments sur leurs champs `active`/`enabled`, avant de consulter le snapshot.

Une copie privée du projet a été réconciliée avec le parent unique du snapshot et
son canal stable, corroborés par `root-devices.json`. Les 15 composants n'avaient
aucune désactivation explicite propre ; leur parent actuel était présent et non
désactivé. Dimensions, coordonnées et `LedMapping` correspondaient exactement.
Le snapshot ne stockant pas l'ordre de chaîne, celui conservé dans le projet a
aussi été vérifié contre le canal stable du registre : 13 points d'une LED,
puis deux tronçons de 10 et 19 LED, soit **42 LED**. L'utilisateur a confirmé cette
forme physique. Les sources originales n'ont pas été modifiées.

Après export natif isolé de la seconde bande, la fusion sauvegardée du layout
`Full Scale` conserve **les 111 membres existants sans différence** et ajoute
seulement les 15 composants manquants : **126 membres, représentant 116 sources**.
Les transformations et géométries existantes sont conservées ; aucune nouvelle
interprétation de leurs positions n'a été introduite.

## Validation et limites au moment de cette note

- **60 tests BLE hors ligne réussis** : 28 de session, 14 d'authentification/cache,
  8 de lecture/retry et 10 de découverte. S'y ajoutent les vecteurs cryptographiques
  et 256 allers-retours chiffrés synthétiques. Voir
  [run-msvc-tests.cmd](../../Controllers/GoveeBluetoothController/tests/run-msvc-tests.cmd).
- Compilation des unités Windows et du nouveau cœur réussie. La sonde passive
  réelle valide le watcher, pas une connexion GATT ni un cycle complet après reboot.
- **15 assertions LAN réussies**, sur les vraies méthodes d'update avec horloge et
  radio simulées : acquisition, fenêtre bornée d'allumage, maintien du mode Direct
  et de la luminosité sous flux continu, sans répétition indéfinie de ON. Voir
  [tests LAN](../../tests/room-govee-lan/README.md).
- Le nouveau cœur a été installé puis lancé par la tâche Windows existante.
  Les deux H61A2 sont énumérées, chacune avec 42 LED ; la seconde possède les
  15 segments attendus. Les 42 couleurs de chacune changent entre deux captures
  SDK. Les 30 contrôleurs sont présents et les deux sorties image natives restent
  disponibles. Cette mesure porte sur les buffers logiciels, pas sur le rendu optique.
- Les quatre connexions Bluetooth ont atteint l'état streaming en 18,3 secondes.
  Deux délais GATT au démarrage ont été récupérés automatiquement. Des fermetures
  périodiques de session H6008 sont encore observées ensuite ; ce correctif du
  cache vide ne prétend pas résoudre leur cause distincte.
- L'utilisateur a ensuite confirmé visuellement que les deux H61A2, les trois
  H6008 et la bande H6159 suivent l'effet. Le placement récupéré correspond aux
  deux tronçons horizontaux et au serpentin vertical décrits.
- Les premières fermetures H6008 arrivent environ 44,4 secondes après le passage
  en streaming. Aucun keepalive manquant n'a été identifié : l'ancien pont et
  le natif interrogent AA01 toutes les 500 ms ; aucun des deux n'envoie AA14
  périodiquement. La cause reste inconnue. Le journal ne réaffiche pas toujours
  « streaming » après récupération, car ce message est dédupliqué par état.
- **Un véritable redémarrage Windows avec ce nouveau binaire n'est pas validé par
  cette note**, ni la stabilité prolongée des quatre connexions.
