# Noir à la fermeture normale

Le profil de sortie utilise la fonction native d’OpenRGB :

```json
{
  "ProfileManager": {"exit_profile": {"enabled": true, "name": "Full Noir"}},
  "GoveeBluetooth": {"keep_black_on_exit": true},
  "KBHE": {"keep_black_on_exit": true}
}
```

Fusionner ces champs avec la configuration existante, sans remplacer les autres
réglages. Les deux options de pilote sont visibles dans les réglages généraux,
désactivées par défaut et prises en compte à la prochaine détection/démarrage.
Le profil `Full Noir.json` doit contenir `"base_color": 0`, en plus de son shader
interactif éventuel. Le shader seul ne suffit pas : la fermeture normale décharge
les plugins avant de charger le profil de sortie. Aucun nom de profil n’est codé
en dur dans les pilotes.

Après l’arrêt de leur producteur core, les pilotes lisent le **dernier buffer
demandé**, pas seulement la dernière couleur déjà envoyée. Cela couvre le noir
du profil de sortie encore en attente dans la file du pilote. Si la politique
est désactivée ou si la couleur finale n’est pas noire, la restauration de l’état
d’origine reste identique.

- Govee BLE H6008/H6159 : sur une session déjà acquise et connectée, le worker
  envoie OFF, attend l’écriture ATT et vérifie `AA01=0`. Il ne restaure alors
  ni l’ancienne couleur ni l’alimentation initiale. Le budget de finalisation
  existant de cinq secondes s’applique ; aucune nouvelle connexion n’est tentée.
  Une absence de connexion ou de confirmation est signalée, jamais présentée
  comme un succès. Le mode/brightness d’origine restent restaurés dans les autres
  cas.
- KBHE : si Direct est toujours possédé, cinq paquets écrivent les 82 touches
  noires puis cinq lectures confirment l’image. Les échanges HID conservent leur
  délai de réponse de150ms chacun. Le mode live noir est conservé sans commande
  de sauvegarde EEPROM ni restauration colorée, même si une confirmation échoue.
  Un changement externe du mode matériel est respecté.
- Govee LAN/NVIDIA et les autres sorties qui conservent leur dernière couleur
  reçoivent le `base_color` natif. Le délai historique de250ms n’est pas une
  preuve générale de livraison pour tous les matériels : aucun nouvel accusé
  matériel universel n’est revendiqué.
- Stream Deck retire normalement le hook et rend son fond à Elgato ; ses icônes
  et actions restent disponibles. Il ne transforme pas cette politique en
  modification persistante de profil Elgato.

Une terminaison forcée, une coupure de courant ou un périphérique déconnecté
ne permettent pas de garantir l’extinction. Utiliser Quitter dans OpenRGB pour
exécuter `closeEvent`; fermer vers la zone de notification ne quitte pas l’app.

## Vérification hors matériel

`Controllers/GoveeBluetoothController/tests/run-msvc-tests.cmd` compile les vrais
protocoles/sessions avec un faux transport. Les tests KBHE lient le backend réel
à des fonctions HID simulées (`Controllers/KBHEController/tests/run.py`).
`tests/room-shutdown/test_final_frame.py`, lancé dans un environnement MSVC x64,
compile les corps des destructeurs et de `Controller::Stop` extraits des sources
de production. Il vérifie noir/non-noir, mode Static, brightness zéro, option
désactivée, dernière touche colorée et conservation de la décision au second Stop.
