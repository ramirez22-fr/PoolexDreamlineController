# PoolexDreamlineController

Credit goes to [Njanik](https://github.com/njanik/hayward-pool-heater-mqtt)
Credit goes to [CDX-24]((https://github.com/CDX-24/PoolexDreamlineController))

Version non finalisée, mais fonctionnelle sur une Dreamline 95

Objectif :
Pilotage d'une pompe à chaleur de piscine POOLEX Dreamline 95 avec un ESP32 sous ESPHome intégrable à Home Assistant.
Détails du projet ici : (https://forum.hacf.fr/t/esphome-tuto-pompe-a-chaleur-poolex-dreamline-95)


Modifications:
- Passage à pin 22 pour être fonctionnel sur un ESP32 mini D1
- Marge de tolérance sur les durées d'impulsion trop stricte (le marqueur de début de trame mesurait ~4650µs réel contre 5000µs nominal, hors de la fenêtre ±300µs prévue). Élargie à ±600µs.
- Le code d'origine ne gardait qu'un seul front entre deux tours de boucle ESPHome. Dès que la boucle principale traînait un peu (log/WiFi/API), toute une rafale de bits partait à la poubelle sans erreur visible. 
- Le compteur d'octets était remis à zéro avant que le composant HA n'ait eu le temps de le lire → "taille de trame invalide (0)" alors que la réception s'était bien passée. Décalé pour que le reset se fasse au prochain début de trame, pas juste après la précédente.



```yaml
external_components:
  - source:
      type: git
      url: https://github.com/CDX-24/PoolexDreamlineController
    refresh: 5s
    components: [hpci]

hpci:
  id: hpci_dev

sensor:
  - platform: template
    name: "Target Temperature"
    unit_of_measurement: "°C"
    accuracy_decimals: 0
    icon: "mdi:water-thermometer"
    device_class: "temperature"
    state_class: "measurement"
    lambda: |-
      return id(hpci_dev).getTargetTemp();
    update_interval: 30s
  - platform: template
    name: "Water Inlet Temperature"
    unit_of_measurement: "°C"
    accuracy_decimals: 0
    icon: "mdi:water-thermometer"
    device_class: "temperature"
    state_class: "measurement"
    lambda: |-
      return id(hpci_dev).getWaterInTemp();
    update_interval: 30s
  - platform: template
    name: "Water Outlet Temperature"
    unit_of_measurement: "°C"
    accuracy_decimals: 0
    icon: "mdi:water-thermometer"
    device_class: "temperature"
    state_class: "measurement"
    lambda: |-
      return id(hpci_dev).getWaterOutTemp();
    update_interval: 30s
  - platform: template
    name: "Outdoor Temperature"
    accuracy_decimals: 0
    unit_of_measurement: "°C"
    icon: "mdi:sun-thermometer"
    device_class: "temperature"
    state_class: "measurement"
    lambda: |-
      return id(hpci_dev).getOutdoorTemp();
    update_interval: 30s
  - platform: template
    name: "Error Code"
    icon: "mdi:hammer-wrench"
    accuracy_decimals: 0
    lambda: |-
      return id(hpci_dev).getErrorCode();
    update_interval: 30s

number:
  - platform: template
    name: "Target Temperature"
    icon: "mdi:water-thermometer"
    step: 1
    min_value: 20
    max_value: 35
    set_action:
      lambda: |-
        id(hpci_dev).setTargetTemp((uint16_t)x);
    lambda: |-
      return id(hpci_dev).getTargetTemp();
    update_interval: 30s

switch:
  - platform: template
    name: "Power"
    icon: "mdi:power"
    restore_mode: "ALWAYS_OFF"
    turn_on_action:
      lambda: |-
        id(hpci_dev).setOn(true);
    turn_off_action:
      lambda: |-
        id(hpci_dev).setOn(false);
    lambda: |-
        return id(hpci_dev).getOn();
```
