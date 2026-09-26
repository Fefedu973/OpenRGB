# Export canonique du canvas SignalRGB

Cet outil Node.js sans dépendance externe lit le `project.json` de SignalRGB Layout Studio et **appelle son vrai `LayoutCore.worldPoint`**. Il exporte les coordonnées mondiales par LED, les transformations originales, les exclusions et les identités parent/composant. Il n'écrit ni registre ni appareil et ne lance aucun moteur RGB.

```powershell
node .\tools\room-canvas\export-canvas.cjs --project 'C:\chemin\SignalRGB-Layout-Studio\project.json'
```

Le core est lu à côté du projet. `--core <layout-core.js>` permet un autre chemin explicite ; c'est du JavaScript local exécuté par Node, donc utiliser uniquement le core de confiance du projet. `--layout 'Full Scale' --layout 'Music - Pump Up Beats'` sélectionne des noms exacts ; sans sélection, toutes les dispositions sont conservées.

La sortie par défaut `tools/room-canvas/local-output/` est ignorée par Git. Elle contient des identités locales : ne pas la publier automatiquement. `--out` permet de choisir un autre dossier privé ; son exclusion Git devient alors la responsabilité de l'appelant.

- `canvas.json` : dimensions, provenance/hash du projet et du core, géométrie de chaque appareil, points éligibles par disposition et enregistrements non résolus.
- `identity-mapping.template.json` : correspondances OpenRGB **vierges**, à remplir après identification. Le template déjà présent n'est jamais écrasé lors d'un nouvel export ; copier/renommer son travail avant de demander un nouveau template dans un autre dossier.

## Fidélité et limites

Les points éligibles exigent `active:true`, `enabled:true`, aucune raison d'exclusion et un placement dans cette disposition. Un appareil exclu reste dans les métadonnées avec ses LED locales et ses raisons. Une entrée ancienne sans géométrie reste dans `missingGeometry`. Une disposition sans placement pour un appareil n'hérite pas silencieusement de Full Scale. Les points en dehors du canvas sont conservés et signalés, pas déplacés.

Le champ `ledIndex` correspond exactement à l'ordre du tableau LED source. `componentOrder` et `channel` sont conservés séparément : ils ne prouvent aucun décalage de LED dans un contrôleur OpenRGB. Les noms peuvent être dupliqués ; ils ne constituent pas une clé unique. Les valeurs active/enabled reflètent le snapshot enregistré, sans nouvelle détection matérielle.

Le template réclame une identité OpenRGB stable et explicite (`serial`, `name`, éventuellement `vendor` et `location`), un `zoneIndex`, puis un `targetLedIndex` pour chaque LED source. Tous restent `null` avant validation. Une adresse IP ou un numéro de contrôleur dans l'ordre de détection ne doit pas être traité comme une identité durable à lui seul. L'outil ne fait aucun rapprochement automatique.

La luminosité de la disposition et celle de chaque transformation sont conservées séparément. Aucun gain global ou étalonnage de couleurs n'est inventé. Les données ne sont **pas un fichier Visual Map directement importable** : il faut établir la correspondance matérielle puis convertir le format et vérifier l'échantillonnage.

## Tests

```powershell
$env:LAYOUT_STUDIO_CORE = 'C:\chemin\SignalRGB-Layout-Studio\layout-core.js'
node --test .\tools\room-canvas\export-canvas.test.cjs
```

Les tests utilisent des valeurs attendues explicites pour la rotation autour du centre, l'échelle X/Y, les deux retournements et l'ancien format d'échelle scalaire. Ils vérifient aussi l'ordre des LED, les exclusions, les identités et l'absence de correspondance OpenRGB inventée. Sans `LAYOUT_STUDIO_CORE`, les quatre tests du core réel sont marqués comme ignorés ; les tests du format et de l'export restent exécutés.

L'export local validé lors de l'intégration contient 1134 points éligibles pour Full Scale et 1134 pour Music - Pump Up Beats. Ces nombres décrivent ce snapshot, pas une constante imposée par le programme. Le vieux music v2 conserve ses placements connus et signale ses identités anciennes non résolues.
