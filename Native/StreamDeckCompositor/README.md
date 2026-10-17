# Compositeur Stream Deck natif dans OpenRGB

Ce module optionnel Windows x64 remplace le serveur Python/HTTP par **Frida Core embarqué dans le processus OpenRGB**. Le découpage des images, le contrôle des baux et le transport sont natifs. Elgato Stream Deck reste ouvert et compose normalement les icônes, titres et actions après le fond injecté. Un hook en mémoire demeure nécessaire ; ce module ne remplace pas l'application Elgato et n'écrit ni profil, ni stockage, ni USB directement.

## Validation du 27 septembre 2026

Le test physique **antérieur au correctif des cases vides**, sur une page peuplée, a produit 126 images 800 × 600 sur huit secondes : 126 notifications acquittées, 1890 peintures couvrant les quinze touches, zéro erreur, environ 16 images/s réelles pour un plafond de 20. La latence d'acquittement avait un p95 de 7 ms. La restauration normale a été acquittée et le détachement terminé. L'utilisateur a également confirmé visuellement le fond animé natif et les quinze icônes correctes. Ces mesures ne validaient pas les cases sans action : leur défaut a été signalé ensuite sur une autre page.

### Cases sans action : correctif de cache confirmé visuellement

Le slot d'animation natif ignorait les coordonnées absentes de sa table d'actions,
même si notre notification demandait les quinze indices. Le correctif utilise,
pour cette seule notification Qt marquée, le rendu natif de la grille complète.
Ses flags sélectionnent la sortie du périphérique sans reconstruire les couches
ni créer d'action. Les notifications Elgato ordinaires conservent leur fonction
d'origine. Les icônes restent composées après le fond pour les touches peuplées.
Deux cas vides sont distingués positivement dans les branches natives : aucun
bundle, ou bundle conservé sans couche de premier plan active. La branche qui
copie le fond de base est observée sous le verrou du compositeur ; seule cette
copie est détachée avec `QImage::bits()` puis peinte. Ni l'image vide partagée,
ni le profil ne sont modifiés. Une simple absence du callback de composition
ne suffit jamais pour autoriser l'écrasement d'une image contenant des icônes.

Le premier correctif omettait le second cas : après 79 images complètes, la
garde a refusé une image avec quinze retours natifs mais seulement huit fonds
injectés. Le diagnostic a identifié sept bundles sans couche active. Après
correction de cette branche, le test réel de vingt secondes a soumis et acquitté
**315 images**, avec une dernière couverture de **8 touches actives et 7 cases
vides**, quinze fonds injectés et zéro erreur. Le p95 d'acquittement était de
2 ms ; la cadence moyenne de soumission/acquittement était de 15,75 images/s
pour un plafond de 20. La restauration native et le détachement ont réussi.
Le processus Elgato est resté ouvert. **L'utilisateur a ensuite confirmé que
les cases vides restaient noires.** Ces compteurs prouvaient une peinture dans
les QImage temporaires, pas que leur contenu avait été transmis aux LCD.

La lecture du binaire épinglé explique cet écart : le descripteur `Image_t`
conserve un sélecteur de cache à `+0x18`. La tâche `ESDCommUploadXIconTask`
le copie à `+0x48` et, s'il est positif, transmet l'image déjà encodée en cache
avant de lire la QImage. La branche de rendu forcé des actions met déjà ce
sélecteur à zéro (`0x5c8b51`) ; la branche sans couche active (`0x5c8b95`) le
conservait. Le nouveau correctif le met à zéro **uniquement dans le descripteur
temporaire dont nous venons de peindre le fond**. Le cache et le bundle d'origine
restent inchangés. La restauration reprend leur chemin natif avec le sélecteur
original. Dans le cas sans bundle, où le descripteur natif est entièrement nul,
la copie peinte reçoit également l'indice de destination demandé au factory.
Deux signatures supplémentaires vérifient la remise à zéro native et le test
de cache de l'upload (`0x60ed0a`) avant tout hook. Aucun nouvel attachement ni
nouvelle fonction native appelée ne sont nécessaires.

Les tests simulent désormais cette décision d'upload, y compris un JPEG vide
déjà en cache : peindre une QImage seule ne suffit plus à les faire réussir.
Ils vérifient aussi les quinze destinations sans bundle et le maintien de
l'identité de cache lors de la restauration. Le correctif `c05a830e`, DLL SHA256
`88607FDF9DEE48ADD03D57C8F6F11BEA0D95063B5E9C6D8B2DEE0B419E12E7EA`,
a ensuite été confirmé sur le matériel : **« Oui, les cases vides sont animées
aussi »**. Le statut de cette session indiquait une couverture de quinze fonds,
dont six cases vides, sans erreur ; les ACK progressaient de 120 à 150, environ
14,6 images/s, avec un p95 d'acquittement de 13 ms. Ces nombres décrivent cette
fenêtre de test, pas une garantie de cadence universelle.

Le premier démarrage après installation avait néanmoins échoué avec un timeout
RPC générique : le dernier statut valide à 3019 ms n'avait pas encore découvert
le compositeur. Ce statut ancien n'exclut pas qu'une composition soit survenue
ensuite. La fermeture/reprise d'Elgato seule a permis la reconnexion avec le même
processus OpenRGB et la même DLL. La cause précise du premier blocage reste
**non démontrée** ; aucun rollback ni hausse arbitraire de délai n'a été utilisé.

Le correctif de diagnostic ultérieur ajoute méthode RPC, PID, durée monotone,
délai configuré et détachement au message d'erreur. Il ne change pas le délai
RPC de 1500 ms, la limite du rendu Qt, les gardes ou l'interdiction de réessayer
un PID fautif. La validation synthétique vérifie le message d'un timeout de
100 ms et une restauration/déconnexion normale, sans viser Elgato. Ce diagnostic
est une modification de source séparée : la DLL 88607FDF validée ci-dessus ne
le contient pas encore.

### Timeout isolé : récupération sur la session existante

Le 27 septembre, le flux d'images continuait d'arriver dans OpenRGB alors que
les ACK restaient figés après une erreur RPC. Le verrou par PID avait arrêté les
tentatives. Une relance d'Elgato a ensuite échoué à son tour, avant le premier
statut `ready`. La dernière valeur `uptimeMs` conservée est un cache : elle ne
permet pas de situer exactement l'opération qui a bloqué. La cause de ces délais
reste à déterminer avec le diagnostic RPC détaillé ; les deux applications
répondaient encore à Windows.

Le correctif du client distingue maintenant un timeout isolé d'une session
inutilisable. Il conserve le **même handle** uniquement si la lecture de santé
supplémentaire, déjà effectuée sur ce handle, répond avec `errors=0`. Il ne rejoue
pas l'ancienne image et ne prolonge aucun bail : le tour suivant prend l'image
la plus récente. Au maximum deux récupérations consécutives sont permises sans
image acquittée par le RPC. Si la santé ne répond pas, si le script annonce une
erreur, si le transport est détaché, ou si la borne est atteinte, le détachement
et le verrou par PID restent en vigueur. Les échecs de nettoyage sont également
journalisés. Aucun deuxième attachement ni changement du délai de 1500 ms.

`Test-ClientRecovery.ps1` compile le vrai `StreamDeckNativeClient.cpp` contre une
DLL C ABI synthétique : 41 vérifications couvrent les diagnostics ancien/nouveau,
le succès après timeout, l'absence de rejeu et de deuxième ouverture, les erreurs
de santé/JSON/script/détachement, la borne des tentatives et un nouveau PID.
Aucun processus Elgato ni Frida n'est utilisé par ce test. Ce test ne démontre
pas la récupération de l'incident réel dont les requêtes de santé échouaient.

Après installation du core et de la DLL native diagnostique, puis redémarrage
d'OpenRGB, la connexion au processus Elgato déjà ouvert a été rétablie. Une
composition de page a permis la découverte du compositeur : les ACK observés
ont progressé de 62 à 1063, avec quinze fonds injectés par image et `errors=0`.
La dernière fenêtre de dix secondes est passée de 910 à 1063 ACK, sans nouvelle
erreur. Les 77 fichiers de configuration vérifiés avant/après la copie étaient
identiques. La DLL native installée a le SHA256
`A25D7089B2BBF33A218BC71C2703180990A4EE951813A9EB5B87A98D65BECF95`.
Cette reprise confirme le fonctionnement après redémarrage du propriétaire,
pas la cause du blocage précédent ni une validation d'endurance prolongée.

### Retard de notification Qt : reprise sans détachement

Un incident ultérieur de la même journée a fourni une cause différente et
précise : après **46 019 images acquittées**, le watchdog JavaScript a déclaré
`Native queued frame timeout; bridge stopped` pour la notification 46 020.
Le RPC suivant répondait en 2 ms ; ce n'était donc pas un timeout du transport
RPC. La dernière image terminée couvrait les quinze touches (dix actions, cinq
cases vides), avec un p95 d'acquittement antérieur de 2 ms. Le watchdog de trois
secondes, hérité de la première version native, transformait ce simple retard
en faute mémorisée, puis le client fermait et verrouillait le PID. La cause
externe du retard Elgato n'est pas établie par ces traces.

Le correctif distingue une notification **encore en file Qt** d'un rendu natif
déjà commencé. Après trois secondes en file, il conserve la même session et la
même notification, suspend les nouvelles images et expose un état `stalled`
sans erreur native. Il ne supprime pas la notification et n'en poste pas une
deuxième. Quand elle arrive, elle restaure uniquement le fond natif ; une
nouvelle image fraîche est nécessaire pour reprendre l'animation. Les anciennes
couleurs ne sont donc pas rejouées. Sans retour de Qt, l'état reste en attente,
avec une mémoire et une file bornées ; il ne prétend pas réparer un Elgato
définitivement bloqué. Un rendu déjà commencé n'est jamais transformé en
restauration au milieu de sa composition.

Les signatures, durées de bail, contrôles de cible, d'image, de couverture et
les erreurs natives réelles restent protégés. Le correctif ne rajoute ni boucle
d'attachement ni serveur externe. Les tests isolés retardent volontairement la
notification au-delà de trois secondes, vérifient la restauration tardive et
la reprise avec une image fraîche sur le même handle. Une nouvelle observation
longue reste nécessaire pour confirmer le comportement sur l'incident réel.

Validation avant installation : **21 tests du vrai script** sous mémoire Qt
simulée, **63 assertions du vrai client** contre une DLL C ABI de test, suite
complète du contrôleur et compilation core réussis. Le test Frida utilise
uniquement son propre processus enfant synthétique : vingt images binaires,
arrêt/restauration et détachement validés. Les 2 000 réponses RPC concurrentes
passent également. L'arrêt suivi d'un nouveau bail et l'expiration suivie d'un
réarmement sont testés : ils ne réactivent jamais l'ancienne image en file.
Le SDK expose `stalled`, `queuedStalls`, `queueRecoveries`, `pendingAgeMs`,
`pendingEntered` et une phase bornée à `queued`/`rendering` ; aucune image ni
adresse mémoire ne passe dans ces diagnostics.

Installation du correctif `7b7e7290` : après relance d'OpenRGB, Elgato est resté
dans le même processus. La découverte naturelle a repris en environ quatorze
secondes, puis 199 images supplémentaires ont été acquittées pendant une
fenêtre de vingt secondes (161 à 360), avec quinze touches couvertes, aucune
erreur et aucun stall. Le p95 d'acquittement était de 8 ms. Le profil utilisateur
restauré était Full Blanc ; cette mesure prouve le transport et la composition,
pas un test optique d'animation ni une reproduction physique du retard de trois
secondes. Les 77 fichiers JSON et les DLL Effects/VisualMap étaient inchangés.
DLL native installée :
`CABF6015B8C0C8448226C078001CCD3EB2B953DBB7DA7C5163F62109F1B4D150`.

`lastFrameCoverage` rapporte les nombres `rendered`, `injected`, `empty` et
`actions` **pour la dernière image**, avec son numéro et son état `restore`.
L'ACK exige quinze images natives valides et quinze injections ; une restauration
exige quinze images natives et aucune injection. Le compteur cumulé `paintedKeys`
reste informatif mais ne prouve pas la couverture de la page actuelle. L'ACK
confirme le retour du rendu/soumission natif, pas une mesure optique des LCD.

Les douze tests isolés exécutent le vrai script avec une mémoire Qt/Frida simulée :
page mixte, page entièrement vide, quinze tuiles distinctes, icônes conservées,
copie vide inchangée, changement de page, restauration, notification non marquée,
target expiré, image/stride invalides, couverture manquante, arrêt et expiration
pendant l'attente Qt. Ils incluent explicitement les bundles vides conservés.
Le build ne lance aucun test matériel ; le test ci-dessus a été lancé séparément
avec autorisation. Le harness accepte les deux derniers arguments facultatifs
`ANIMATION_SECONDS DISCOVERY_SECONDS`, bornés à 30 et 300 secondes ; son message
`ATTACHED` confirme le chargement réel avant d'attendre un rendu naturel.

La version précédente attendait la première composition naturelle : une page
immobile pouvait rester en attente jusqu'à un changement de page. Le test
historique ci-dessus avait rencontré cette limite, malgré un démarrage réussi
en 1492 ms dans un autre essai.

### Démarrage d'une page statique — candidat du 17 octobre 2026

Le bootstrap cherche le compositeur **déjà vivant** parmi les QThread du même
processus. Il lit les TEB/TLS via les API Windows de requête de thread ; il ne
parcourt pas le tas et ne réemploie aucune adresse d'une ancienne session.
Chaque handle ouvert est fermé. Le code demande au plus 512 threads par
recherche, une recherche par seconde et 60 recherches. Un candidat non traité
expire après trois secondes. En cas d'absence ou d'ambiguïté, la découverte
naturelle reste disponible sans erreur fatale.

Les trois fichiers Elgato restent verrouillés par leur SHA256. Quatre guards Qt
supplémentaires vérifient la provenance exacte des offsets :

| Code Qt6Core | Relation vérifiée |
| --- | --- |
| `0x26cab0` | TLS index à `0x9cb570`, TEB `+0x58`, bloc TLS `+0x68` vers QThreadData |
| `0x1a54e0` | `QThread::currentThread()` lit le QThread à QThreadData `+0x50` |
| `0x100e80` | `QObject::thread()` lit l'affinité du propriétaire |
| `0x25e7f0` | Entrée de `QEventDispatcherWin32::processEvents()` |

Le candidat doit vérifier les deux vtables natives, l'offset du compositeur
physique `owner+0x4f0`, la grille 5×3 et l'image native existante de 72×72.
Un message Windows `WM_NULL` réveille le thread propriétaire, sans action de
bouton, changement de page ou profil. À l'entrée de sa boucle Qt, le script
revérifie le propriétaire et son affinité, puis programme le probe. Le timer
existant revérifie cette identité et émet **la même notification Qt marquée**
que les images normales, hors du callback Interceptor. Celle-ci effectue une première
restauration native complète, sans injection de pixels. `ready` reste faux et
les nouvelles images sont refusées jusqu'à son ACK des quinze cases. Le chemin
de composition des icônes demeure inchangé. Un arrêt annule tout candidat qui
n'a pas encore été envoyé ; une notification déjà envoyée reste suivie jusqu'à
son retour comme les autres restaurations.

Cette séparation corrige l'échec réel du premier candidat : appeler `activate`
depuis `Interceptor.onEnter` masquait le listener de copie Qt et produisait
`Qt vector copy not observed`, sans ACK. Le test Frida 17.18 dans un enfant
synthétique reproduit le mécanisme : hook A → `NativeFunction` B n'appelle pas
le hook B (`nested=0`), alors que le même appel différé au timer l'appelle
(`deferred=1`). La garde de copie n'est pas supprimée. Le flag de probe est
consommé avant `activate`, afin qu'un arrêt pendant cet appel coopératif conserve
la notification déjà envoyée jusqu'à son ACK.

Le statut `bootstrap` expose uniquement `phase`, `detail` et quatre compteurs
(`attempts`, `threadQueries`, `candidates`, `wakeups`), sans adresse mémoire.
Pour valider le démarrage réel sans geste, exiger `phase=complete`, `ready=true`,
une couverture native de quinze cases et des ACK de nouvelles images. `natural`
indique que le chemin historique a trouvé la cible avant le bootstrap.

Validation hors matériel : 32 groupes du script de production passent, dont
page statique sans composition préalable, mauvais thread/candidat/affinité,
mémoire du candidat libérée, ambiguïté, bornes/retry, arrêt avant dispatch,
ACK ou arrêt pendant `activate`, arrêt pendant la vérification d'affinité,
annulation entre validation et timer, grille incomplète et refus des nouveaux guards modifiés.
Le modèle simule maintenant la suppression des listeners imbriqués constatée
avec le vrai Frida ; quatre tests échouent sur l'ancien chemin immédiat.
Les 17 guards ont été comparés aux octets des PE installés. Les tests hors matériel
ne remplacent pas le contrôle physique. Le 17 octobre 2026, le correctif différé
a également été validé après relance d'OpenRGB, en gardant le même processus
Elgato et la même page : `bootstrap.phase=complete`, `ready=true`, ACK croissants,
quinze cases rendues/injectées dont quatre vides, aucune erreur de composition.
L'utilisateur a confirmé le retour automatique du fond sans changement de page.
Cette validation concerne les versions exactes protégées par les guards ci-dessus.


### Plusieurs délais RPC consécutifs — 17 octobre 2026

Après environ 67 minutes de fonctionnement, un appel `setframe` a dépassé
1500 ms, mais sa sonde de santé a répondu. Vingt secondes plus tard, `status`
et la demande de restauration ont également dépassé leurs délais. Le client
a fermé la session puis refusé définitivement le même PID Elgato. Les images
continuaient d'arriver dans OpenRGB. Le dernier statut valide ne signalait
aucune erreur native et avait acquitté 39 468 notifications ; son absence de
notification en attente ne permet pas de connaître l'état après cet instant.
La cause du ralentissement initial reste inconnue. Windows a refusé la lecture
non invasive des stacks du processus Elgato.

La politique de timeout isolé décrite plus haut est remplacée par une mise en
attente du **même handle**, avec son verrou propriétaire conservé. Un timeout
de réponse ne déclenche plus de détachement ni de nouvel attachement. Seules
des lectures de santé sont autorisées, espacées de 1, 2, 4, 8 puis 10 secondes
au maximum. Chaque appel conserve son délai de 1500 ms. Une réponse récente
`errors=0, ready=true` autorise la reprise ; le contrôleur sélectionne alors
une image encore valide, sans rejouer celle dont l'envoi a échoué. Tant que le
dispatcher ne répond pas, le transport reste en récupération et n'accumule pas
d'images. Les erreurs natives réelles, réponses invalides et détachements
restent traités comme des fautes. La fermeture normale reste bornée.

Une commande déjà transmise reçoit désormais son échéance d'origine et son
numéro de séquence. `GetTickCount64`, commun aux deux processus, mesure le bail
de deux secondes dès l'entrée dans la DLL. Un RPC exécuté en retard ne peut
plus recommencer ce bail ; une commande périmée ou plus ancienne est refusée.
Une notification Qt conservée possède également sa propre échéance : une
nouvelle image ne prolonge pas celle déjà en file.

Le diagnostic distingue `transportState`, `cached`, `ageMs`,
`recoveryAttempts`, `retryAfterMs` et l'erreur de transport. `sampledSteadyMs`
désigne la vraie dernière réponse de santé, même lors d'une nouvelle publication
du cache ; `ready` devient faux pendant la récupération. L'âge affiché est
calculé à la publication par le worker, pas par un appel Frida depuis le SDK.
Le lecteur `tools/room-sdk/streamdeck_status.py` expose aussi l'erreur du worker.

Ce changement permet de récupérer un dispatcher qui se remet à répondre ; il
ne prétend pas débloquer de force un appel natif définitivement coincé. Les
tests de transport synthétique et la validation après installation sont
distincts d'une reproduction du ralentissement réel après une heure.

Validation du candidat : 38 groupes du vrai script, 69 assertions du vrai
client contre une DLL synthétique, 2 000 réponses RPC concurrentes et la suite
du contrôleur passent. Dans un enfant Frida isolé, un appel natif `Sleep` lancé
par le timer bloque effectivement les réponses `status` et `setframe`. À son
retour, l'image périmée est rejetée et une image fraîche est acceptée sur la
même session. Ce test reproduit le mécanisme de récupération, sans attribuer
à cet appel synthétique la cause de l'incident Elgato.

Après installation du commit `0e4a9608`, OpenRGB a retrouvé le compositeur du
processus Elgato resté ouvert, sans changement de page. Les quinze touches,
dont quatre vides, ont repris leur rendu ; l'utilisateur a confirmé le fond
revenu et synchronisé. Les 77 fichiers JSON de configuration et les DLL
Effects/VisualMap ont conservé leurs hashes.

Une nouvelle panne transitoire est ensuite survenue pendant le contrôle réel :
`setframe` a expiré à 63 389 ms, puis `status` à 65 936 ms. Le client est resté
en récupération, a effectué deux sondes et a repris sur la même session sans
redémarrage. Le statut a montré une restauration native complète, puis de
nouvelles images ; les ACK ont progressé de 621 à 796 sur les dix échantillons
suivants, avec quinze fonds injectés, zéro erreur native et aucun nouvel échec.
La reprise de ce cas est donc observée sur le matériel. Cela ne démontre pas
la cause des ralentissements ni leur disparition.

Binaires installés : core SHA256
`ED02489444EAA684BA4162F1DD006E9F9D0E91945C256795FD985FEF677D461D` ;
DLL native SHA256
`D0C66326E9A09833CCE1FD1024D007CCBBAE98703F5D6EB4F05061B850092966`.

## Compilation facultative

Installer MSVC 2022 Build Tools et extraire le devkit officiel **Frida Core 17.18.0 Windows x86_64** avec 7-Zip. Il n'est pas nécessaire d'exécuter son archive auto-extractible.

```powershell
.\Build-Native.ps1 -FridaDevkit C:\outils\frida-core-17.18.0 -Tests
```

Le devkit officiel est compilé avec `/MT`, alors qu'OpenRGB/Qt utilise `/MD`. `RoomStreamDeckNative.dll` isole cette différence : son API C n'échange que des scalaires et des buffers appartenant à l'appelant. Aucune allocation STL, exception ou libération de mémoire ne traverse cette frontière. OpenRGB charge cette DLL par chemin absolu avec une recherche de dépendances restreinte au dossier de la DLL et à System32. La DLL est épinglée pour la durée du processus, car Frida conserve des threads internes. Les sessions et scripts sont libérés à l'arrêt du contrôleur ; le module de 69 Mo environ reste chargé jusqu'à la fermeture d'OpenRGB.

Le build principal ne dépend pas du SDK Frida et reste utilisable sans cette DLL. Le code du contrôleur découvre son ABI à l'exécution. Régénérer qmake après ajout de `StreamDeckNativeClient.cpp`.

Le script global `tools/room-build/Build-Room.ps1` accepte `-NativeStreamDeck -FridaDevkit <SDK>` pour construire et, avec `-Package`, inclure ce module. Avec `-PackageExisting -NativeStreamDeck`, il empaquette la DLL déjà construite, ses notices et ses hashes sans lancer de nouveau build. Le paquet conserve les autres hashes core/plugins et ajoute `nativeStreamDeckBinarySha256`, `nativeStreamDeckScriptSha256` et `fridaCoreVersion` à `BUILD-INFO.json`.

`-Tests` exécute `node --test test_background_core.cjs` (Node.js nécessaire), puis compile et exécute seulement un enfant synthétique créé par le test. Il vérifie SHA256, refus de cible non Elgato, verrou compatible `_locking`, RPC et 20 transferts binaires de 311040 octets, expiration du bail, timeout RPC et restauration/détachement. Il construit également `test_elgato.exe`, **sans le lancer**. Ce dernier exige désormais une couverture complète de la dernière image, et conserve les nombres de cases vides et peuplées dans son résultat.

Les réponses RPC sont publiées sous un mutex commun : identifiant attendu,
payload, erreur et indicateur de disponibilité restent cohérents, même si le
callback GLib s'exécute hors du thread qui attend. Le détachement est atomique.
`test_rpc_response.cpp` vérifie 2000 réponses de 16 Ko entre deux threads,
les identifiants périmés, les doublons, les erreurs et le `null` légitime avant
découverte du layout. La version précédente marquait une réponse disponible
avant de copier son JSON ; une interruption à cet endroit pouvait exposer
`null`. Cet entrelacement a été reproduit avec l'ancien callback extrait, sans
processus Elgato. Cela corrige une course réelle, sans prétendre que tous les
avertissements observés en session provenaient de cette seule cause. Les
réponses de forme inattendue indiquent maintenant l'opération et le type JSON,
sans enregistrer leur contenu.

Une faute conserve désormais son premier message, sa stack et la couverture
de l'image concernée. Seul le RPC de lecture `status` reste disponible après
cette faute ; les nouvelles écritures et tentatives de reprise restent refusées.
Le client conserve ce diagnostic avant de fermer, ainsi que l'erreur de la
première fermeture : un second `Close()` ne peut plus faire croire qu'une
restauration précédemment refusée a réussi. Les tests vérifient aussi le maintien
de la cause initiale et l'accès au statut après faute dans l'enfant synthétique.

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

Avant tout attachement : chemin exact `C:\Program Files\Elgato\StreamDeck\StreamDeck.exe`, processus unique, SHA256 exact des trois binaires, puis verrou propriétaire. Le JS vérifie encore chemin, architecture, taille d'image et signatures de fonctions ; les objets doivent avoir les vtables, grille 5 × 3, géométrie et QImage temporaire attendues. Aucun mode de désactivation des gardes n'est exposé. Un échec de garde/attachement ou une faute native suspend les nouveaux attachements pour ce PID ; un nouveau processus Elgato permet une nouvelle tentative. Un simple timeout de réponse RPC conserve la session et utilise la récupération décrite plus haut. Une occupation du verrou reste réessayable après libération.

L'export `setframe` ne reçoit que 311040 bytes BGRA opaques avec un bail de deux secondes. Le code validé n'a qu'une notification en cours et une dernière image. L'arrêt demande le fond normal et attend son acquittement pendant au plus trois secondes. Les appels Frida de chargement/détachement utilisent une annulation à deux secondes chacun ; ce mécanisme borne les opérations normales et ne prétend pas terminer de force un processus tiers bloqué. Les erreurs de restauration ou de détachement sont remontées. Aucun arrêt brutal de Frida/Elgato n'est utilisé.

## Provenance et licences

- Code natif ajouté : GPL-2.0-or-later, comme le fork OpenRGB.
- `background-core.js` dérive du compositeur de [SignalRGB-Local-Bridges](https://github.com/Fefedu973/SignalRGB-Local-Bridges), commit `5e2cffcec0cc750974d736442e8b0f6f06ec04f2`, sous la licence GPL du dépôt. La copie initiale avait le SHA256 `0C9E4D92F2F7B3CBD7B2A7F2E0289A38AFFCB696EC5E72DF4E23515AF0755069` ; les modifications natives ultérieures sont suivies dans Git. Le hash du script courant est inscrit par le packaging. Il est incorporé au build ; aucun script externe arbitraire n'est chargé en production.
- [Frida Core 17.18.0](https://github.com/frida/frida-core/tree/17.18.0) : **wxWindows Library Licence 3.1**, texte amont conservé dans `FRIDA-LICENSE.txt`. Ce n'est pas la licence MIT. [API C officielle](https://frida.re/docs/frida-core-api/).
- [Devkit officiel](https://github.com/frida/frida/releases/download/17.18.0/frida-core-devkit-17.18.0-windows-x86_64.exe), SHA256 `2d512af923edabb2287aed355c01214b54a8b757bb4a092a1b1a5bab6c59e886`.
- Le devkit inclut notamment GLib sous LGPL-2.1-or-later ; son en-tête original contient les mentions de copyright. Les textes GNU Library GPL 2 et Lesser GPL 2.1 accompagnent la notice Frida, depuis les [textes officiels GNU](https://www.gnu.org/licenses/old-licenses/). Ses dépendances ne deviennent pas MIT par cette intégration. Le SDK et les binaires générés restent hors Git ; conserver leurs licences et sources correspondantes lors d'une distribution binaire.

`Package-Native.ps1` copie la DLL et les informations de provenance dans un paquet local sans secret. Le code source et les commandes de reconstruction restent dans ce dossier.
