# Fond Stream Deck piloté par OpenRGB

Ce contrôleur C++ accepte une **image native FrameSurface de dimensions variables**, par exemple 800 × 600, ou une matrice **80×50, soit 4000 pixels RGB**, pour les effets OpenRGB existants. Il redimensionne la source par interpolation bilinéaire vers 480×272, la découpe dans l'ordre de `/layout`, puis envoie quinze tuiles 72×72 BGRA opaques à `POST /frame`. Il préserve le détail spatial à l'intérieur de chaque touche. La matrice reste le mode compatible par défaut ; une image native ne transite pas par ses 4000 LED.

Le compositeur utilise **l'application Stream Deck et un hook Frida contrôlé par version**. Deux transports existent : `bridge` garde le serveur Python/HTTP précédent ; `native` embarque Frida Core dans OpenRGB et supprime ce serveur. Il n'écrit aucun paquet HID/USB et ne remplace aucun bouton, action, icône ou profil. La compatibilité reste limitée au MK.2 et aux trois binaires autorisés. Voir [compilation et configuration du transport natif](../../Native/StreamDeckCompositor/README.md).

## Interface d'image générique

Le contrôleur implémente aussi l'interface secondaire `room_image::RGBControllerImageInterface` de `FrameRouting/`, sans modifier la vtable de l'API RGBController existante. Un moteur d'effets découvre la sortie de zone0 avec `GetImageOutput`, puis appelle `SubmitImage` avec une surface BGRA immutable partagée, une transformation affine et un bail de 100 à 5000 ms. Le moteur n'a besoin de connaître ni Elgato, ni le découpage des quinze touches. Les seules dimensions préférées annoncées sont celles du fond natif480×272 ; l'entrée peut avoir une autre résolution.

Le worker conserve une référence à la dernière image, sans recopier ses pixels à la soumission. Il utilise le sampler commun pour crop, rotation, miroir, gain de luminosité et remplissage noir hors scène. Le bail natif donne priorité à cette image sur les couleurs LED ; les valeurs LED reçues entre-temps remplacent une seule matrice de repli. À expiration, le worker reprend cette dernière matrice ou demande `/stop` s'il n'en possède aucune. Une requête HTTP déjà en vol termine avant le prochain envoi. `GetImagePreview` renvoie la même référence immutable et la transformation tant que le bail est actif, sans I/O.

Si un canal externe `frame_surface` est configuré, il reste propriétaire exclusif : `GetImageOutput` renvoie false et `SubmitImage` renvoie Unsupported. Les images directes et celles d'un autre processus ne se mélangent donc pas silencieusement. Ce chemin générique ne supprime pas le pont compositeur Elgato décrit ci-dessus.

## Configuration manuelle

Le détecteur est inactif par défaut. Ajouter dans `OpenRGB.json`, seulement après installation du pont et arrêt de tout autre émetteur de son fond :

```json
"StreamDeckBackground": {
    "enabled": true,
    "session_file": "C:/chemin/prive/streamdeck/api-session.json",
    "fps": 20
}
```

Le chemin doit être absolu. Il désigne le fichier de session privé créé par le lanceur du pont ; **ne pas copier le token dans OpenRGB.json, les logs ou un dépôt**. La session et son port sont relus, ce qui permet de suivre un redémarrage du pont. La destination est toujours 127.0.0.1. Aucun proxy, redirection HTTP ou hôte fourni par la configuration n'est utilisé.

Pour une image native, ajouter à cette configuration :

```json
"frame_surface": { "channel": "ambient-main", "stale_ms": 1000 }
```

Le producteur et ce contrôleur utilisent le même canal [FrameSurface](../../FrameSurface/README.md), avec une image BGRA8 opaque, un stride et des dimensions explicites. Le canal accepte 1 à 64 lettres/chiffres/`_`/`-`, le TTL 100 à 2000 ms. En mode image, les mises à jour de LED OpenRGB sont ignorées, sans repli matriciel. La matrice de compatibilité reste décrite dans l'interface/SDK, mais elle n'est pas la représentation de la surface et ne limite pas sa résolution. Le worker sonde le canal à sa cadence bornée, conserve seulement l'image la plus récente et demande `/stop` une fois si elle expire ou devient invalide/absente. Il vérifie à nouveau sa fraîcheur après une initialisation HTTP lente et accepte une nouvelle génération du producteur.

Lorsque `StreamDeckBackground.enabled` vaut `true`, ce fork supprime automatiquement la détection USB **Elgato Stream Deck MK.2** afin que le détecteur HID ne concurrence pas le compositeur Elgato. Cette garde est intégrée au détecteur MK.2 ; elle ne ferme pas une autre instance d'OpenRGB déjà lancée. Désactiver le client Stream Deck de SignalRGB avant le premier essai OpenRGB. Un seul émetteur doit contrôler le fond : l'API existante n'attribue pas un bail distinct à chaque client HTTP partageant son token.

En transport `bridge`, le pont doit être démarré par son lanceur existant. En transport `native`, OpenRGB charge sa DLL facultative et effectue lui-même l'attachement unique gardé ; aucun Python, serveur ou token n'est nécessaire. Aucun mode ne modifie les tâches Windows. Si le compositeur n'est pas prêt, le worker attend et revalide. La première composition naturelle reste nécessaire : une horloge minute peut retarder ce démarrage, et une page sans rafraîchissement peut rester en attente.

## Cadence et arrêt

Le transport natif rafraîchit aussi un diagnostic agrégé toutes les deux secondes sur son worker existant. Le SDK expose ce cache dans `configuration.configuration.runtime` du contrôleur : compteurs acceptés/rendus, couverture de la dernière trame, cadence et erreur bornée. `sampledSteadyMs` permet de voir si le cache a vieilli. Une lecture SDK n'appelle jamais Frida et ne copie aucune image. Les compteurs de composition ne constituent pas une preuve que les cases vides sont visibles sur l'écran physique ; cette régression reste en cours de diagnostic après le signalement utilisateur du 27 septembre.

Le worker garde une seule image, remplacée par la plus récente. Il n'effectue aucune requête HTTP tant qu'une image valide n'est pas disponible. Le plafond configuré est de 1 à 20 envois/s, 20 par défaut ; les délais d'encodage et de réseau peuvent réduire le débit réel. Une image fixe renouvelle le bail toutes les 500 ms, ou toutes les 1000 ms si le plafond choisi est de 1 image/s. En mode surface, le producteur doit republier son image avant expiration du TTL, y compris si elle est fixe. Les erreurs imposent une seconde d'attente et ne créent pas une file de trames.

Chaque POST demande un bail de 2000 ms. L'arrêt demande `/stop` pour revenir au rendu normal, après terminaison de la requête en cours. Cette réponse confirme une demande, pas une mesure de restauration optique. Si l'API est absente ou le token a changé, l'expiration du bail fournit le repli. Une erreur native du pont peut encore nécessiter un rendu naturel ou une relance propre, conformément à ses notes existantes.

Les requêtes limitent connexion/lecture/écriture à 400 ms par attente ; la réception JSON est limitée à 16 KiB avec une échéance de 1200 ms vérifiée pendant la réception. Le fichier de session est limité à 8 KiB et son token à des caractères base64url. Les erreurs journalisées n'incluent jamais les en-têtes, le token ni les corps de réponse.

## Tests hors matériel

Depuis ce dossier :

```powershell
.\Build-Tests.cmd
```

Le script utilise MSVC 2022 Build Tools (ou le chemin `VCVARS` fourni). Il ne requiert ni Qt de développement, ni Python, ni appareil Elgato. Le serveur simulé écoute seulement une adresse loopback sur un port éphémère et utilise des tokens synthétiques dans un dossier temporaire.

Les scénarios vérifient le désarmement par défaut, les bornes, le BGRA/alpha, le détail horizontal et vertical, l'ordre des tuiles, les vrais POST HTTP, la coalescence, le plafond d'émission, le maintien d'une image fixe, la rotation de session, le refus de géométrie, les délais et l'annulation pendant l'initialisation. Ils vérifient aussi une vraie surface partagée synthétique 800 × 600, les strides avec padding, l'absence de repli LED, l'arrêt sur TTL, la reprise du producteur et le refus d'une image périmée pendant le handshake. La compilation globale et la validation physique sont distinctes.

## Sources et licence

Les tests vérifient également le contrat générique, la conservation de l'allocation partagée, 150 soumissions coalescées, les transformations affines, la priorité du bail, l'expiration pendant une connexion lente, le repli vers la dernière matrice et l'exclusivité du canal externe.

Fichiers ajoutés sous GPL-2.0-or-later, conformément au fork OpenRGB. Le format de l'API vient des fichiers locaux `SignalRGB-Local-Bridges/streamdeck/bridge/background_api.py`, `canvas_transport.py`, `API-NOTES.md` et `full-canvas-design.md`. Aucun secret ni ressource propriétaire Elgato n'est inclus. La copie inchangée du hook GPL et les notices Frida sont dans `Native/StreamDeckCompositor`. Le transport HTTP utilise la dépendance cpp-httplib déjà présente dans OpenRGB et conserve sa licence dans son dossier d'origine.

Le 27 septembre 2026, le harness natif a découvert automatiquement le compositeur et envoyé 126 images 800 × 600 en huit secondes, avec 126 acquittements et 1890 peintures sur les quinze touches, zéro erreur, puis restauration acquittée et détachement. L'utilisateur a confirmé visuellement le fond animé et les quinze icônes correctes. Le débit réel était proche de 16 images/s pour un plafond de 20 ; ce résultat matériel est distinct des tests HTTP simulés ci-dessus.
