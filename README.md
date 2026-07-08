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


