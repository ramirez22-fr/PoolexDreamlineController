# PoolexDreamlineController

Credit goes to [Njanik](https://github.com/njanik/hayward-pool-heater-mqtt)

Credit goes to [CDX-24](https://github.com/CDX-24/PoolexDreamlineController)

Version non finalisée, mais fonctionnelle sur une Dreamline 95

Objectif :
Pilotage d'une pompe à chaleur de piscine POOLEX Dreamline 95 avec un ESP32 sous ESPHome intégrable à Home Assistant.
Détails du projet ici : (https://forum.hacf.fr/t/esphome-tuto-pompe-a-chaleur-poolex-dreamline-95)


Modifications:
- Passage à pin 22 pour être fonctionnel sur un ESP32 mini D1
- Marge de tolérance sur les durées d'impulsion trop stricte (le marqueur de début de trame mesurait ~4650µs réel contre 5000µs nominal, hors de la fenêtre ±300µs prévue). Élargie à ±600µs.
- Le code d'origine ne gardait qu'un seul front entre deux tours de boucle ESPHome. Dès que la boucle principale traînait un peu (log/WiFi/API), toute une rafale de bits partait à la poubelle sans erreur visible. 
- Le compteur d'octets était remis à zéro avant que le composant HA n'ait eu le temps de le lire → "taille de trame invalide (0)" alors que la réception s'était bien passée. Décalé pour que le reset se fasse au prochain début de trame, pas juste après la précédente.
- Trames en réalité longues de 16 octets (pas 9/12 comme sur les PAC Hayward), avec un checksum simple (somme des octets 1 à 14 modulo 256).
- Certaines trames (boîtier de commande) ne valident ce checksum qu'après complément à 1 de tous les octets (en-tête `0xCC`) — `frameIsValid()` teste maintenant les deux cas.
- Deux bugs de décalage de bits sur `action`/`opMode` dans `decode()`, qui empêchaient de lire le bon mode.
- Nombre de répétitions à l'envoi réduit de 3 à 1 (`SEND_MSG_OCCURENCE`) : ~7s de blocage ramenés à ~2,3s.
- Au démarrage de l'ESP, des réglages figés en dur étaient réellement envoyés à la PAC. Les réglages de départ sont maintenant calqués sur le premier état réel reçu.
- Affichage "optimiste avec expiration" côté Home Assistant : la valeur demandée s'affiche tout de suite, confirmée ou annulée sous 10s selon la réponse réelle de la PAC.
