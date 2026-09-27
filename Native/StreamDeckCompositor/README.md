# Compositeur Stream Deck natif dans OpenRGB

Ce module optionnel Windows x64 remplace le serveur Python/HTTP par **Frida Core embarqué dans le processus OpenRGB**. Le découpage des images, le contrôle des baux et le transport sont natifs. Elgato Stream Deck reste ouvert et compose normalement les icônes, titres et actions après le fond injecté. Un hook en mémoire demeure nécessaire ; ce module ne remplace pas l'application Elgato et n'écrit ni profil, ni stockage, ni USB directement.

## Validation du 27 septembre 2026

Le test physique `test_elgato` a produit 126 images 800 × 600 sur huit secondes : 126 notifications acquittées, 1890 peintures couvrant les quinze touches, zéro erreur, environ 16 images/s réelles pour un plafond de 20. La latence d'acquittement avait un p95 de 7 ms. La restauration normale a été acquittée et le détachement terminé. L'utilisateur a également confirmé visuellement le fond animé natif et les quinze icônes correctes.

La découverte automatique a pris 1492 ms pendant ce test, sans changement de page. Elle attend néanmoins la **première composition naturelle** d'une touche, exactement comme le pont précédent. Une horloge à rafraîchissement par minute peut donc retarder le démarrage jusqu'à sa prochaine mise à jour. Une page entièrement immobile peut rester en attente ; aucun scan de tas, pointeur d'une ancienne session ou appel sur un objet non validé ne contourne cette limite. Le premier essai de huit secondes, trop court, était resté en attente sans écrire de pixels. Le contrôleur conserve son attachement et réessaie l'état après une seconde, sans boucle d'injections.

## Compilation facultative

Installer MSVC 2022 Build Tools et extraire le devkit officiel **Frida Core 17.18.0 Windows x86_64** avec 7-Zip. Il n'est pas nécessaire d'exécuter son archive auto-extractible.

```powershell
.\Build-Native.ps1 -FridaDevkit C:\outils\frida-core-17.18.0 -Tests
```

Le devkit officiel est compilé avec `/MT`, alors qu'OpenRGB/Qt utilise `/MD`. `RoomStreamDeckNative.dll` isole cette différence : son API C n'échange que des scalaires et des buffers appartenant à l'appelant. Aucune allocation STL, exception ou libération de mémoire ne traverse cette frontière. OpenRGB charge cette DLL par chemin absolu avec une recherche de dépendances restreinte au dossier de la DLL et à System32. La DLL est épinglée pour la durée du processus, car Frida conserve des threads internes. Les sessions et scripts sont libérés à l'arrêt du contrôleur ; le module de 69 Mo environ reste chargé jusqu'à la fermeture d'OpenRGB.

Le build principal ne dépend pas du SDK Frida et reste utilisable sans cette DLL. Le code du contrôleur découvre son ABI à l'exécution. Régénérer qmake après ajout de `StreamDeckNativeClient.cpp`.

Le script global `tools/room-build/Build-Room.ps1` accepte `-NativeStreamDeck -FridaDevkit <SDK>` pour construire et, avec `-Package`, inclure ce module. Avec `-PackageExisting -NativeStreamDeck`, il empaquette la DLL déjà construite, ses notices et ses hashes sans lancer de nouveau build. Le paquet conserve les autres hashes core/plugins et ajoute `nativeStreamDeckBinarySha256`, `nativeStreamDeckScriptSha256` et `fridaCoreVersion` à `BUILD-INFO.json`.

`-Tests` compile puis exécute seulement un enfant synthétique créé par le test. Il vérifie SHA256, refus de cible non Elgato, verrou compatible `_locking`, RPC et 20 transferts binaires de 311040 octets, expiration du bail, timeout RPC et restauration/détachement. Il construit également `test_elgato.exe`, **sans le lancer**.

## Configuration

```json
"StreamDeckBackground": {
  "enabled": true,
  "transport": "native",
  "native_library": "C:/OpenRGB-Room/RoomStreamDeckNative.dll",
  "native_lock_directory": "C:/chemin/prive/streamdeck/locks",
  "fps": 20
}
```

`native_lock_directory` doit être **le même dossier** que celui de l'ancien pont : `<runtime_directory>/locks`. Le fichier `observer-<PID>.lock` utilise un verrou Windows de plage de bytes, compatible avec `msvcrt.locking`. Cela empêche deux propriétaires d'instrumenter simultanément le même processus. Arrêter proprement le pont avant de choisir le transport natif. Aucun token réseau n'est nécessaire. `transport: "bridge"` reste le défaut pour les configurations existantes.

Les images de l'interface générique `RGBControllerImageInterface`, le canal FrameSurface facultatif et la matrice de compatibilité 80 × 50 utilisent tous ce transport. Les quinze tuiles ne limitent pas la résolution de l'image source. Le détecteur USB MK.2 est déjà supprimé lorsque `StreamDeckBackground.enabled` vaut true.

## Gardes et arrêt

Avant tout attachement : chemin exact `C:\Program Files\Elgato\StreamDeck\StreamDeck.exe`, processus unique, SHA256 exact des trois binaires, puis verrou propriétaire. Le JS vérifie encore chemin, architecture, taille d'image et signatures de fonctions ; les objets doivent avoir les vtables, grille 5 × 3, géométrie et QImage temporaire attendues. Aucun mode de désactivation des gardes n'est exposé. Un échec de garde/attach/RPC suspend les nouveaux attachements pour ce PID ; un nouveau processus Elgato permet une nouvelle tentative. Une occupation du verrou reste réessayable après libération.

L'export `setframe` ne reçoit que 311040 bytes BGRA opaques avec un bail de deux secondes. Le code validé n'a qu'une notification en cours et une dernière image. L'arrêt demande le fond normal et attend son acquittement pendant au plus trois secondes. Les appels Frida de chargement/détachement utilisent une annulation à deux secondes chacun ; ce mécanisme borne les opérations normales et ne prétend pas terminer de force un processus tiers bloqué. Les erreurs de restauration ou de détachement sont remontées. Aucun arrêt brutal de Frida/Elgato n'est utilisé.

## Provenance et licences

- Code natif ajouté : GPL-2.0-or-later, comme le fork OpenRGB.
- `background-core.js` est la copie **identique octet pour octet** du compositeur de [SignalRGB-Local-Bridges](https://github.com/Fefedu973/SignalRGB-Local-Bridges), commit `5e2cffcec0cc750974d736442e8b0f6f06ec04f2`, sous la licence GPL du dépôt. SHA256 : `0C9E4D92F2F7B3CBD7B2A7F2E0289A38AFFCB696EC5E72DF4E23515AF0755069`. Il est incorporé au build ; aucun script externe arbitraire n'est chargé en production.
- [Frida Core 17.18.0](https://github.com/frida/frida-core/tree/17.18.0) : **wxWindows Library Licence 3.1**, texte amont conservé dans `FRIDA-LICENSE.txt`. Ce n'est pas la licence MIT. [API C officielle](https://frida.re/docs/frida-core-api/).
- [Devkit officiel](https://github.com/frida/frida/releases/download/17.18.0/frida-core-devkit-17.18.0-windows-x86_64.exe), SHA256 `2d512af923edabb2287aed355c01214b54a8b757bb4a092a1b1a5bab6c59e886`.
- Le devkit inclut notamment GLib sous LGPL-2.1-or-later ; son en-tête original contient les mentions de copyright. Les textes GNU Library GPL 2 et Lesser GPL 2.1 accompagnent la notice Frida, depuis les [textes officiels GNU](https://www.gnu.org/licenses/old-licenses/). Ses dépendances ne deviennent pas MIT par cette intégration. Le SDK et les binaires générés restent hors Git ; conserver leurs licences et sources correspondantes lors d'une distribution binaire.

`Package-Native.ps1` copie la DLL et les informations de provenance dans un paquet local sans secret. Le code source et les commandes de reconstruction restent dans ce dossier.
