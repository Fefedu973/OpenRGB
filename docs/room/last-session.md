# Reprendre les réglages de la dernière session

L’option **General Settings → Profile Manager → Remember Last Session** est
désactivée par défaut. Quand elle est activée dans l’application avec interface,
OpenRGB conserve les paramètres des plugins dans `last-session.json`, à la racine
du dossier de configuration choisi. Ce fichier est distinct des presets.

Un checkpoint est collecté sur le thread de l’interface toutes les deux secondes,
et avant le déchargement des plugins à la fermeture. Le fichier est remplacé de
façon atomique uniquement si son JSON change. Les chargements de profils,
la détection, la suspension et le démontage de l’application empêchent une
sauvegarde intermédiaire. Une génération de chargement détecte aussi un
chargement SDK qui commence et se termine pendant la collecte.

Au premier démarrage de l’interface, après le chargement des plugins, un checkpoint
valide a priorité sur **Load Profile on Open**. Si le fichier manque, est invalide,
ou si sa restauration échoue, le profil de démarrage configuré reste le repli.
Un rescan des appareils ne relance pas ce choix initial. Les options explicites
de profil à la sortie et à la reprise après veille gardent leur comportement.

Le format version 1 contient seulement `plugins` et `source_profile` : ni couleurs
de contrôleurs, ni configuration matérielle, ni copie des presets. Le nom source
est affiché seulement si ce preset existe toujours. Les effets reçoivent leurs
callbacks habituels de chargement ; Visual Map reçoit une référence canonique
à sa carte, dont la géométrie est sauvegardée par le plugin lui-même.

Les tests hors matériel sont décrits dans
[`tests/room-settings-ui`](../../tests/room-settings-ui/README.md). Une interruption
brutale peut perdre les changements depuis le dernier checkpoint ; aucun état
capturé après le déchargement des plugins ne remplace le dernier fichier valide.
