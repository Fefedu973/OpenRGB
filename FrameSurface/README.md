# FrameSurface v1 — images CPU indépendantes des LED

`FrameSurface.h` fournit un transport Windows de la **dernière image uniquement**, utilisable depuis un effet, une capture ou un consommateur C++ sans modifier l’API de plugins OpenRGB. Une image 800 × 600 ne crée pas 480 000 LED. Le mécanisme est une copie CPU en mémoire partagée ; il ne partage pas une texture GPU et ne prétend pas être sans copie.

## Utilisation C++17

Ajouter la racine OpenRGB-Room aux chemins d’inclusion. Le header est autonome, sans Qt, Python ni bibliothèque OpenRGB. Sous MSVC, il lie `advapi32.lib` pour la DACL. Les implémentations des autres systèmes renvoient « indisponible » ; aucun transport Unix n’est revendiqué.

```cpp
#include "FrameSurface/FrameSurface.h"

room_surface::Publisher publisher("ambient-main"); // capacité maximale : 64 MiB
if(publisher.IsOpen())
    publisher.PublishBGRA(pixels, bytes, 800, 600, 800 * 4);

room_surface::Reader reader("ambient-main");
room_surface::Frame frame;
auto status = reader.ReadLatest(frame, 1000); // TTL en ms, verrou : 5 ms
if(status == room_surface::FrameStatus::NewFrame)
    consume(frame.bgra.data(), frame.width, frame.height, frame.stride);
```

Le producteur utilise un seul thread propriétaire. Il conserve l’objet entre les images ; le constructeur refuse un second producteur vivant du même canal. `IsOpen()` et `LastError()` permettent de diagnostiquer un échec sans exception système. `PublishRGB()` accepte aussi des lignes RGB8, qu’il convertit en BGRA. Les méthodes ne lancent aucun thread ni boucle : le producteur et le lecteur choisissent leur cadence.

Le format des pixels est **BGRA8 sRGB, opaque, origine en haut à gauche**, avec un stride explicite. Tous les alpha doivent valoir 255. Le producteur refuse les autres alpha et remet à zéro le padding des lignes ; il ne transmet pas de mémoire de padding du producteur. Une image Qt `Format_ARGB32` sur Windows little-endian convient seulement après vérification/normalisation de son opacité.

`NewFrame` copie la dernière image ; `Unchanged` signifie que la même image est encore fraîche. `Stale`, `Unavailable`, `Invalid` et `Busy` ne fournissent **aucune autorisation de rejouer** le contenu précédent de `frame`, laissé intact. Le consommateur doit respecter ces résultats et son propre bail de sortie. La génération change après une réouverture du producteur, même si la séquence redémarre à 1.

## Contrat binaire et bornes

Le nom du canal contient 1 à 64 caractères `[A-Za-z0-9_-]`. La mémoire s’appelle `Local\OpenRGB-Room.Surface.<canal>` et son mutex `<nom>.Mutex`. La DACL protégée autorise uniquement le SID de l’utilisateur courant ; aucun namespace global, privilège administrateur ou réseau n’est utilisé. Ce n’est pas une frontière de sécurité contre un autre processus du même utilisateur.

L’en-tête v1 est de 128 octets, avec alignement 8 et contrôles `static_assert`. Voir `Header` dans le fichier public pour les offsets : magic `ORGBFRM1`, version, taille d’en-tête, capacité, largeur/hauteur/stride/format, longueur utile, séquence, timestamp, génération et identité du producteur. Le timestamp monotone est l’uptime Windows `GetTickCount64`, commun aux processus de la même session système. Il ne représente pas une date UTC.

La capacité est au plus **64 MiB**, en plus des 128 octets d’en-tête. Les calculs largeur × canaux et stride × hauteur se font en 64 bits et sont validés avant allocation. Le lecteur mappe d’abord 128 octets, valide la capacité, puis mappe exactement la taille bornée ; une mémoire plus petite que sa déclaration est refusée. Il refuse formats/versions inconnus, timestamps futurs, tailles incohérentes et TTL expiré. Le TTL accepté par l’API va de 1 à 60 000 ms. Un échec d’acquisition du mutex ne crée aucune file : la trame est abandonnée ou le lecteur réessaie à son prochain tick.

La capacité ne grandit pas tant que des lecteurs conservent l’ancien mapping. Prévoir la capacité maximale nécessaire lors de la première création, ou fermer les lecteurs avant de l’agrandir. La capacité par défaut de 64 MiB évite ce problème pour les résolutions comprises dans cette limite. Chaque producteur possède aussi un événement de durée de vie `<nom>.Owner.<génération>`, avec la même DACL. `Close()` ferme cet événement même si un lecteur garde le mutex au-delà du délai de fermeture de 100 ms. Le canal peut alors être repris par une nouvelle instance dans le même processus, sans confondre PID vivant et objet encore actif. `owner_flags` annonce cette présence dans l’en-tête v1 ; les anciens en-têtes v1 sans ce flag conservent la vérification PID/date de création. Les lecteurs n’ouvrent l’événement que brièvement et n’en prolongent pas artificiellement la durée de vie.

## Tests hors matériel

`tests/Build-Tests.cmd` compile et lance avec MSVC 2022. Les tests créent des noms de canaux uniques et des images synthétiques : 60 images 800 × 600 à une cadence demandée de 60 Hz, lecteur tardif recevant la seule séquence 60, BGRA/RGB et padding, TTL, seconde instance de processus, refus d’un producteur concurrent, reconnexion, en-tête corrompu, tailles excessives et contention. Ce test ne mesure ni une capture réelle à 60 Hz ni un écran physique.

Le contrôleur Stream Deck peut lire ce transport via `StreamDeckBackground.frame_surface` ; voir `tests/room-streamdeck/README.md`. Aucun adaptateur wallpaper ni compositeur GPU n’est implémenté par ce header.
