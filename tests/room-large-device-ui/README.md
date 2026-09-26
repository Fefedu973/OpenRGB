# Liste de LED massive : tests Qt hors écran

Ce test compile le **modèle et le helper de production** `qt/LazyLEDListModel.h` et construit un vrai `QComboBox` Qt avec 500 000 LED synthétiques. Il ne lance pas OpenRGB, aucun détecteur, aucun plugin ni matériel. Il ne mesure donc pas les performances globales d'OpenRGB ou de `DeviceView`.

```powershell
$env:QT_ROOT = 'C:/chemin/Qt/6.8.3/msvc2022_64'
.\Build-Tests.cmd
```

MSVC 2022 Build Tools est utilisé par défaut ; `VCVARS` peut désigner un autre `vcvars64.bat` compatible. Qt doit inclure Core, Gui, Widgets, Test et le plugin `offscreen`. Le script place compilation et mesures dans `.build/`, ignoré par Git. Il définit `QT_QPA_PLATFORM=offscreen`, sans fenêtre interactive ni computer use.

Les vérifications couvrent :

- création d'une plage de 500 000 lignes sans fabriquer ses libellés ; bornes et rôles non textuels ;
- préfixes Entire Device/Zone/Segment, offsets de segment et LED unique sans préfixe ;
- ajout, modification et suppression de la ligne Multiple ; clear et retour aux petites listes de couleurs de mode ;
- positionnement réel du menu sur la dernière LED et navigation clavier Home/End ;
- nombre borné de demandes de libellés pendant l'ouverture ;
- destruction des widgets sans transférer ni détruire le style partagé de QApplication.

## Mesure de référence et défaut découvert

Mesuré sur cette machine Windows avec Qt 6.8.3/MSVC x64, en mode offscreen. Les temps sont ceux d'un passage, pas une moyenne statistique. Les valeurs mémoire sont les deltas de mémoire privée du processus entier et restent sensibles aux caches/allocation de Qt.

| Configuration, 500 000 LED | Initialisation | Libellés à l'initialisation | Ouverture synchrone | Libellés du menu | Dernière ligne visible |
|---|---:|---:|---:|---:|---|
| Modèle lazy + menu Qt usuel + Batched | 0,106 ms | 0 | 3579,4 ms | 500004 | Non |
| Menu style liste + Batched | 0,055 ms | 0 | 1,90 ms | 3 | Non |
| Helper de production, style liste + uniforme SinglePass | 0,064 ms | 0 | 22,15 ms | 13 | Oui |

Le passage mesuré sur le helper de production a ajouté environ 5,28 Mo de mémoire privée à l'ouverture du menu. L'initialisation de la plage n'a pas entraîné d'augmentation mesurable à cette granularité. La mesure actuelle de chaque exécution est enregistrée dans `.build/measurements.json`.

Le défaut venait de deux comportements distincts de Qt : son menu de combo peut mesurer tous les libellés, et le mode Batched ne sait pas immédiatement défiler jusqu'à une ligne dont la position n'a pas encore été calculée. Le helper force `SH_ComboBox_Popup=0`, conserve `uniformItemSizes=true` et choisit `SinglePass`. Qt calcule alors les positions sans créer 500 000 QString. Chaque proxy de style possède une nouvelle instance de base, jamais celle partagée par QApplication.

La régression impose moins de 2000 demandes de libellés pour ce scénario, plutôt qu'une limite de temps dépendant de la machine. Les opérations de recherche textuelle exhaustive, le chargement des contrôleurs, la visualisation de leur géométrie et la peinture de l'application complète ne sont pas couverts par ce benchmark.
