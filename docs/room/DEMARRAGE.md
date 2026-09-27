# Utiliser la version Room

Le paquet Windows compilé se trouve dans `dist-room`. Les sources sont publiées
sur les branches `room-integration` (OpenRGB), `room-canvas` (Effects) et
`room-surfaces` (Visual Map) du compte GitHub Fefedu973.

## Effet Web Page

Dans Effects, choisir **Special → Web Page**, saisir l'URL, puis **Apply / reload
page**. Les adresses HTTP/HTTPS et les fichiers HTML locaux absolus
(`file:///C:/.../page.html`) sont acceptés. Sélectionner les zones à alimenter
comme pour les autres effets, puis activer l'effet.

Commencer avec **800 × 600 et 20 FPS**. La taille et la limite de cadence sont
réglables. JavaScript et les animations fonctionnent ; la page est capturée en
image et alimente les écrans complets ou les points des LED ordinaires. Le moteur
WebView2 ne démarre que pour cet effet. Le paquet contient le chargeur et sa
licence ; il utilise le runtime WebView2 installé sur le PC. Le fichier
`WEBPAGE.md` du paquet détaille le fonctionnement et les limites mesurées.

## Configuration séparée

Le lanceur `tools/room-setup/Start-Candidate.ps1` demande explicitement un dossier
de configuration contenant `OpenRGB.json`. Il utilise `dist-room/OpenRGB.exe` et
ne remplace pas la configuration OpenRGB habituelle ou le registre SignalRGB.

Avant d'utiliser les mêmes périphériques dans ce fork, fermer SignalRGB, arrêter
son superviseur de ponts et arrêter l'ancien service Windows OpenRGB. Le lanceur
détecte ces conflits ; il ne tue pas ces programmes. Garder l'application Elgato
ouverte pour le fond du Stream Deck. Le transport Stream Deck natif et l'ancien
pont Python ne doivent pas posséder son compositeur simultanément.

La configuration privée préparée sur cette machine est `private/native-configured`.
Ses 23 contrôleurs, 26 zones configurées, 91 segments et 932 LED de composants
ont été relus par le vrai exécutable. Les fichiers Full Scale exportés restent
inactifs jusqu'à vérification des correspondances restantes. L'original SignalRGB
est conservé. Les clés Bluetooth et fichiers personnels restent dans `private/`.

## Limites encore présentes

- Les trois H6008 et la H6159 ont passé les essais de couleurs, d'allumage et de
  restauration. Le rendu H6008 sans fondu a été confirmé. Une deuxième connexion
  H6008 après déconnexion, dans le même processus, peut toutefois échouer sans
  notification. Redémarrer ce candidat rétablit une première connexion dans les
  essais réalisés ; ce n'est pas présenté comme un correctif de reconnexion.
- Les quatre barrettes RAM nécessitent les droits administrateur/service pour
  l'accès PawnIO. Elles ne figurent pas dans l'inventaire sans élévation.
- Le logiciel de fond d'écran existant est compatible avec le SDK OpenRGB, mais
  son serveur local était désactivé lors des essais ; il n'est pas encore relié
  à cette configuration. Aucun nouveau moteur de wallpaper natif n'est annoncé.
- La G502 X PLUS est détectée avec ses huit LED. La coexistence matérielle avec
  la G915 ne peut être testée tant que le clavier est absent.

Les tests des pilotes et plugins, ainsi que leurs limites, figurent dans
`validation-2026-09-27.md`. Cette version reste une branche de développement,
pas un remplacement automatique de l'installation quotidienne.
