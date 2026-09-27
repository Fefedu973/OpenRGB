# G915 et G502 X PLUS : coexistence native

Audit du 27 septembre 2026. Le clavier demandé est une **G915**, pas une G512.
La G502 X PLUS est présente sur un récepteur USB `046D:C547` ; la G915 n'est
plus disponible sur ce PC. L'audit initial a été fait sans ouvrir de handle HID. La sonde a ensuite été
autorisée après fermeture de SignalRGB et arrêt confirmé du service OpenRGB.

## Pourquoi l'ancienne version pouvait échouer

Le dossier historique `OpenRGB fixed logitech and govee` conserve des modifications
intéressantes pour la souris, mais ne constitue pas un port générique :

1. `BundleLogitechUsages` y énumère tous les appareils d'un même VID/PID et
   retient leurs usages sur l'interface 2 sans comparer l'instance physique.
   Avec deux récepteurs identiques, le premier handle de chaque usage peut
   être utilisé pour le mauvais appareil. L'ordre d'énumération devient décisif.
2. `RGBController_Logitech::SetupZones` y impose **8 LED à tout appareil 0x8081**.
   `DeviceUpdateLEDs` et `setDirectCompressed` utilisent aussi des tableaux de
   8 couleurs et des plages matérielles 1 à 8. Cela correspond à la souris,
   mais tronque un clavier qui emprunterait ce chemin commun.
3. Le cache de cette compression historique est avancé sans propager toutes
   les erreurs de `send5E`. Un paquet perdu peut ainsi être considéré comme envoyé.

Ces défauts sont établis par lecture des sources historiques. Ils expliquent
des modes de panne possibles ; aucun ancien journal complet ne permet de
reconstituer avec certitude chaque échec physique de l'utilisateur.

Le [fork historique public](https://gitlab.com/Fefedu973/open-rgb-logi) existe,
avec notamment les branches `logi_FP_refactor` et `logi-G502-common`. Le dossier
local n'a pas de métadonnées Git : ses modifications non publiées ne doivent
pas être attribuées automatiquement à un commit de ce fork.

## Ce qui est déjà corrigé dans la base actuelle

| Source primaire | État consulté | Apport pertinent |
|---|---|---|
| [Issue 4583](https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues/4583) | Fermée | G502 X confondue avec une G915 sur le PID partagé. |
| [MR 1316](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/1316) | Fusionnée le 19 juillet 2022 | Ancienne attribution de C547 au clavier. Un PID seul ne suffit donc pas. |
| [MR 3270](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/3270), [3352](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/3352) | Fusionnées en juin 2026 | Contrôleur HID++ 2.0 avec découverte de fonctions ; auteur ayant testé G502 X PLUS et G515. |
| [MR 3272](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/3272) | Fusionnée le 28 juin 2026 | Variante G915 TKL C547 et rapports complets de 20 octets. |
| [MR 3485](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/3485) | Fusionnée le 17 août 2026 | Identification par réponses, isolation des récepteurs, zones découvertes et ACK par transaction. |
| [MR 3490](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/3490) | Fusionnée le 19 août 2026 | Regroupement des deux collections Windows d'une seule interface, sans fusionner deux instances physiques. |

Les commits `c852af2a` et `779514a6` sont déjà inclus dans cette base. Ils ne
sont pas présentés comme de nouveaux correctifs Room. Les détecteurs spécifiques
activés gardent priorité ; la G502 X utilise le contrôleur à découverte de
fonctions, et une G915 classique peut conserver son pilote historique.

La découverte différencie notamment `0x8071` (effets/prise de contrôle) et
`0x8081` (couleurs par LED). Les index de fonctions sont propres à l'appareil,
pas au PID du récepteur. Le nombre de zones n'est plus globalement limité à 8.

## Correctif Room supplémentaire

La sonde `ProbeG915ReceiverName` du détecteur C547 avait échappé au correctif
Windows : elle envoyait encore des rapports **0x10 de 7 octets** au handle
**usage 2**, qui est une collection de rapports longs. Elle supposait aussi
que la fonction de nom était toujours à l'index 3 et ne lisait qu'un fragment.
La conséquence établie par le code est une identification du pilote historique
fragile ou impossible sur cette collection ; le passage éventuel au pilote
générique ne prouve pas que la sonde initiale a fonctionné.

`LogitechReceiverIdentity.h` fournit désormais :

- une requête IRoot `0x0005` pour découvrir la fonction de nom ;
- des requêtes `0x11` de **20 octets**, compatibles avec la collection longue ;
- la corrélation du slot, de la fonction et du software ID `0xE` ;
- la lecture paginée du nom, avec bornes et refus des réponses incomplètes ;
- le regroupement de chemins Windows insensible à la casse, qui conserve
  l'identité physique et filtre interface, usage page `0xFF00` et usages 1/2/4.

Les handles ouverts en doublon sont fermés. Un échec ne transforme jamais une
souris inconnue en clavier. Aucun nouveau paquet de couleur, paramètre DPI,
profil interne ou firmware n'est introduit par ce correctif.

## Comparaison SignalRGB et limites

### Échec du scan complet et correction des registres du récepteur

Le scan natif du 27 septembre a exposé un second défaut concret : après l'ACK
du récepteur `10 FF 80 02 ...`, une lecture expirée, remplie de zéros, était
enregistrée comme périphérique de slot 0/PID 0000. Le pilote générique sondait
ensuite ce faux slot, alors que la sonde indépendante avait confirmé le slot 1.
Un watcher ne suffisait pas à reconstruire une cible correctement énumérée.

`LogitechReceiverProtocol.h` corrèle maintenant les réponses RAP, refuse ACK,
timeout et notification invalide comme identité, et complète les notifications
absentes par les vrais registres d'appairage `B5/20+n-1`. Sous Windows, les
requêtes RAP courtes passent sur usage 1 et les réponses longues aux registres
d'appairage/nom/numéro de série sont lues sur usage 2. Les autres bits du
registre de notifications sont conservés. Aucun PID de souris, nom ou slot 1
n'est imposé. Les attentes sont bornées ; un récepteur qui ne fournit ni
notification ni registre valide reste inconnu.

La structure des registres provient de la [mise en œuvre Solaar](https://github.com/pwr-Solaar/Solaar/blob/master/lib/logitech_receiver/receiver.py)
(`device_pairing_information`, `device_codename`) et des formats RAP déjà définis
dans OpenRGB. Les tests reproduisent les ACK/timeouts et deux récepteurs avec
leurs collections séparées. Compilation des deux unités concernées réussie ;
le scan complet natif du 27 septembre a ensuite enregistré la G502 X PLUS au
slot 1 avec ses huit zones, en environ 1,36 s, au lieu des tentatives visant
auparavant le faux slot 0. Le profil natif a ensuite été sauvegardé avec les
23 contrôleurs détectés et un arrêt propre. Un défaut CLI indépendant appliquait
les modes même pour une sauvegarde seule : le mode statique Govee sans couleur
provoquait `exit(0)` avant la sauvegarde. Le guard `DeviceOptions.hasOption`
corrige ce cas, avec un test reproduisant l'ancienne sortie zéro trompeuse.
Ces preuves valident la détection et l'export, pas les couleurs visibles.

Le plugin utilisateur `Logitech_Modern_Device.js` et celui de SignalRGB 2.5.77
ont été comparés en lecture seule ; ils diffèrent, notamment sur la découverte
et l'option de compression. Le plugin utilisateur associe la G915 à ses IDs
de modèle (`C33E`/`407C`) et la G502 X PLUS à `C095`/`4099`, avec huit LED
adressées 1 à 8. La base OpenRGB contient déjà les mêmes huit LED de souris,
une carte 7 × 3 et une découverte distincte des touches de clavier.

La position dessinée d'une LED latérale diffère d'une case entre les deux
cartes. Cela ne prouve pas une inversion de couleurs et n'a pas été modifié
sans validation visuelle. La G915 X est une famille distincte ; elle reste
dirigée vers le pilote unifié plutôt que le pilote G915 classique.

Les tests MSVC simulent deux récepteurs C547 avec noms, fonctions et files de
réponses distincts, dans les deux ordres d'énumération. Ils passent. La vraie
unité de compilation du détecteur passe également. Voir
[tests/room-logitech](../../tests/room-logitech/README.md).

La sonde native de lecture a confirmé le nom G502 X PLUS sur C547, interface 2,
FF00/2, slot 1 ; 8071 index 09 v2 ; 8081 index 0A v2 ; zones 1 à 8 ;
4522/8070/8080 absents. Une première tentative n'avait reçu aucune réponse :
la cause reste inconnue (veille possible). Un GET borné a ensuite trouvé le
slot 1, puis la lecture complète a réussi. Aucun contrôle, couleur ou
notification n'a été modifié. Résultat anonymisé :
`tests/room-logitech/validation.json`.

**Non vérifié matériellement dans cette passe :** affichage des huit couleurs,
G915 physique, coexistence des
deux appareils, veille/réveil prolongée et branchement à chaud. La coexistence de deux récepteurs ne doit pas être
confondue avec plusieurs appareils appairés au même récepteur : le chemin
historique G915 reste un contrôleur de slot 1, sans nouvelle promesse de
multi-appairage. La limite amont de changement câble/radio nécessitant un
rescan reste documentée dans la MR 3485.

Pour une validation matérielle ultérieure, libérer explicitement les appareils
de SignalRGB, vérifier les noms et chemins indépendants, tester huit couleurs
distinctes sur la souris puis les touches du clavier, et répéter avec ordre
de branchement inversé. Cette validation ne nécessite pas de modifier Full Scale.
