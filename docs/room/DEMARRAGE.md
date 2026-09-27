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

## Configuration de la pièce

Pour cette installation, ouvrir `Demarrer-OpenRGB.cmd` à la racine du projet.
Il démarre la tâche installée avec les droits nécessaires ; si OpenRGB tourne
déjà, ouvrir sa fenêtre depuis l'icône près de l'horloge. Ne pas lancer en plus
SignalRGB ou l'ancien service OpenRGB, qui utiliseraient les mêmes périphériques.

Le lanceur `tools/room-setup/Start-Candidate.ps1` demande explicitement un dossier
de configuration contenant `OpenRGB.json`. Il utilise `dist-room/OpenRGB.exe` et
ne remplace pas la configuration OpenRGB habituelle ou le registre SignalRGB.

Cette installation remplace les ponts Govee Bluetooth, NVIDIA et Stream Deck par
les pilotes intégrés à OpenRGB. Le superviseur et leur démarrage automatique sont
retirés. Garder l'application Elgato ouverte : elle fournit les actions et icônes
du Stream Deck, dont OpenRGB compose le fond en natif.

**Exception choisie par l'utilisateur : le pont Wallpaper Engine est conservé.**
Il expose le wallpaper via son serveur SDK local sur `127.0.0.1:6743`. OpenRGB s'y
connecte et lui envoie ses couleurs ; SignalRGB ne participe plus à ce trajet.
La configuration et les fichiers du wallpaper existant sont conservés.

La configuration privée préparée sur cette machine est `private/native-configured`.
La carte **Full Scale** reprend les **116 éléments actifs sur 116** : 126 membres,
1 162 routes LED, une image native Stream Deck et 512 échantillons de la matrice
Wallpaper Engine. Les positions, rotations, miroirs et formes personnalisées
sont conservés, y compris les LED superposées. Les 28 contrôleurs matériels
utilisent 28 zones configurées et 108 segments. Le wallpaper est un contrôleur
réseau distinct. L'original SignalRGB reste intact ; les exports et secrets sont
privés. La seconde H61A2 a été réintégrée depuis ses 15 morceaux d'origine :
13 points individuels et deux tronçons de 10 et 19 segments. Un ancien alias
réseau l'avait fait exclure lors de l'import initial. La carte Music reste
disponible séparément, sans activation simultanée ; cette réparation concerne
Full Scale. Voir `govee-recovery-2026-09-27.md` pour le diagnostic.

Les 21 sorties inutilisées des contrôleurs Nollie et Corsair sont explicitement
configurées à zéro dans `Configuration.json`. La vérification des zones reste
activée : une nouvelle sortie réellement non configurée sera toujours signalée.
Ne pas ajouter de LED fictives à une sortie vide pour masquer cette fenêtre.

Au démarrage, le profil **Full Scale - Rainbow** anime le canvas entier à
800 × 500 et 30 images/s. Les pilotes adaptent ensuite la cadence aux appareils,
notamment l'AW3426DW limité à dix mises à jour/s pour éviter l'accumulation de
commandes. Le profil de démarrage contient uniquement les réglages d'effets ;
il ne remplace pas les tailles de zones et segments enregistrées.

`tools/room-setup/Install-NativeStartup.ps1` installe une tâche **OpenRGB Room**
qui lance directement `OpenRGB.exe` quinze secondes après ouverture de session.
Elle utilise les droits administrateur requis par PawnIO pour la RAM, retire les
anciens démarrages SignalRGB/OpenRGB et conserve celui du wallpaper. Il n'y a pas
de superviseur externe. Le script exige une carte Full Scale validée et garde les
anciens réglages de démarrage sous `private/startup-backups/`. L'état effectivement
installé est consigné dans `private/native-configured/startup-install-result.json`.

## Limites encore présentes

- Les trois H6008 et la H6159 ont passé les essais de couleurs, d'allumage et de
  restauration. Le rendu H6008 sans fondu a été confirmé. La reprise d'une session
  conservée par le firmware est maintenant vérifiée par AA14 : neuf cycles sur
  les trois ampoules, puis trois recréations complètes du transport ont réussi.
  Une coupure radio physique ou électrique n'a pas été provoquée dans cette série.
  L'essai global observe encore des déconnexions périodiques sur deux ampoules,
  suivies d'une reconnexion automatique. Le pilote quitte maintenant l'attente
  dès qu'une session fermée est signalée ; la cause de ces coupures reste à isoler.
- Les quatre barrettes RAM ont été identifiées en administrateur : huit LED par
  barrette. Un lancement manuel sans élévation n'aura pas accès à PawnIO.
- Le wallpaper conservé utilise son SDK4 et une matrice 32 × 16. Il ne reçoit pas
  l'API image SDK7 ; le Stream Deck, lui, reçoit une image native complète.
- La G502 X PLUS est détectée avec ses huit LED. La coexistence matérielle avec
  la G915 ne peut être testée tant que le clavier est absent.

Les tests des pilotes et plugins, ainsi que leurs limites, figurent dans
`validation-2026-09-27.md`. Cette version reste un fork de développement. Lors
d'une mise à jour Elgato, le garde-fou de version du compositeur peut refuser
l'activation : conserver les icônes/actions, mettre à jour le hook pour cette
version et refaire son test. Ne pas désactiver le garde-fou pour forcer le patch.
