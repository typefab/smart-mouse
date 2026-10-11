# 04 — Funzioni possibili per l'ESP32 e progetti simili

## Cosa potrebbe fare l'ESP32

| Funzione | Idea |
|---|---|
| Mouse wireless | Il Pico legge tasti/sensore, l'ESP32 trasmette in Bluetooth LE (serve batteria) |
| Configurazione via WiFi | Pagina web sull'ESP32 per cambiare le mappature dal telefono |
| Cambio dispositivo | Una combinazione di tasti sposta il controllo da PC a tablet/secondo PC |
| Domotica | Una combinazione invia comandi MQTT / Home Assistant (luci, musica, timer) |
| Profili + display | Piccolo OLED che mostra il profilo attivo (lavoro, gaming, editing) |
| Mouse Bluetooth → USB | Con la variante sperimentale di HID Remapper (vedi file 02) |

## Progetti trovati online

- **HID Remapper** — https://github.com/jfedor2/hid-remapper — sito: https://www.remapper.org
  Dongle configurabile che rimappa mouse e tastiere, anche tasti del mouse verso scorciatoie
  da tastiera. Si configura dal browser (Chrome/Edge). Varianti: singolo Pico, doppio Pico,
  Feather RP2040 USB Host, versione Bluetooth LE su nRF52840.
- **HID Remapper, versione Bluetooth Classic con ESP32 (sperimentale)** —
  https://forum.remapper.org/t/bluetooth-classic-version-testing-esp32/18
  Pico + ESP32 classico: l'ESP32 riceve dispositivi Bluetooth e li passa al Pico in seriale.
- **esp32_mouse_keyboard (AsTeRICS)** — https://github.com/asterics/esp32_mouse_keyboard
  Firmware ESP32 per tastiera e mouse Bluetooth LE, comandato da un altro microcontrollore
  tramite comandi testuali in seriale. Ideale per l'architettura Pico → UART → ESP32.
- **ESP32-Gamepad** — https://github.com/kaminoer/ESP32-Gamepad
  ESP32-S3 che fa da host USB per tastiera e mouse; configurazione di tasti, macro e profili
  da una pagina web via WiFi.
- **ESP32 BLE KVM Bridge** — https://github.com/KMChris/esp32-kvm
  Usa tastiera e mouse del PC per comandare un altro dispositivo via Bluetooth tramite ESP32.
- **mouse-jiggler** — https://github.com/perryflynn/mouse-jiggler
  Mouse Bluetooth con ESP32, display OLED, pulsanti e impostazioni salvate in flash.
- **esp32-hid-bridge (PyPI)** — https://pypi.org/project/esp32-hid-bridge/
  Controllo di mouse e tastiera tramite ESP32 via WiFi o Bluetooth da script Python.

## Consiglio

Parti con **solo il Pico + HID Remapper**: funziona subito e ti fa capire cosa serve davvero.
Aggiungi l'ESP32 solo quando vuoi wireless, configurazione da telefono o domotica.
