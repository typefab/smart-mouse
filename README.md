# Mouse Trust "smart" con Raspberry Pi Pico (+ ESP32)

Progetto per rimappare i tasti di un mouse Trust (es. tasto laterale alto = Ctrl+C) e,
con una combinazione di tasti, far comparire sul PC una "chiavetta" con un file di testo
per configurare le funzioni.

## Indice

| File | Contenuto |
|---|---|
| [01-pico-solo.md](01-pico-solo.md) | Versione con solo il Pico (consigliata per iniziare) |
| [02-pico-esp32.md](02-pico-esp32.md) | Versione Pico + ESP32 collegati in seriale (UART) |
| [03-usb-saldato-sui-pad.md](03-usb-saldato-sui-pad.md) | Sostituire la micro-USB del Pico con un cavo saldato |
| [04-idee-e-progetti-esp32.md](04-idee-e-progetti-esp32.md) | Funzioni possibili per l'ESP32 e progetti simili trovati online |
| [05-hub-usb.md](05-hub-usb.md) | Aggiungere un hub USB per collegare più dispositivi |
| [06-firmware-v0.md](06-firmware-v0.md) | Primo firmware: mouse passante + tasti laterali come scorciatoie |
| `schemi/` | Schemi di collegamento in formato SVG (apribili nel browser, stampabili) |

## Scelta dell'architettura

- **Il Pico (RP2040) da solo basta** per: rimappare tasti, inviare scorciatoie da tastiera,
  comparire come unità USB con file di configurazione.
- **L'ESP32 classico non ha USB nativo**: non può presentarsi al PC come tastiera USB.
  Serve solo per funzioni wireless (Bluetooth, WiFi, domotica).
- **ESP32-S3** ha USB nativo + Bluetooth LE, ma **un solo controller USB OTG**: può fare da host
  (legge il mouse) **oppure** da device (tastiera verso il PC), non entrambi insieme.
  Può sostituire il Pico solo se l'uscita verso il PC è Bluetooth LE. Non supporta il Bluetooth Classic.
- Il Pico invece fa **host USB via PIO (GP0/GP1)** e **device USB nativo** contemporaneamente:
  per questo è il cuore del progetto.
- **HID Remapper** si configura dal browser: non legge un `config.txt` e non comunica con l'ESP32
  in UART. Per la "chiavetta" con il file di configurazione e per l'ESP32 serve un firmware proprio
  (o un fork di HID Remapper, che usa già TinyUSB + Pico-PIO-USB).

## Strade possibili

1. **Pico come intermediario USB (senza aprire il mouse)**: mouse → porta USB-A cablata sul Pico → PC.
   Firmware pronto: **HID Remapper** (configurazione da browser).
2. **Pico dentro il mouse collegato ai microswitch**: il Pico legge direttamente i pulsanti.
   Serve un piccolo hub USB interno (mouse originale + Pico su un solo cavo) e un firmware scritto da te.
3. **Pico + ESP32**: come la 1, con l'ESP32 in seriale per Bluetooth / WiFi / domotica.

## Hardware scelto

- Raspberry Pi Pico (RP2040)
- ESP32-WROOM-32D su scheda DevKit (Bluetooth Classic + BLE + WiFi)
- Hub USB 2.0 **non alimentato** sulla porta host del Pico (vedi file 05)
- Alimentazione alternativa: powerbank collegato al Pico (vedi file 02)

## Livelli logici e alimentazione

- Pico ed ESP32 lavorano entrambi a **3,3 V**: si collegano direttamente, senza convertitori.
- Tutto è alimentato dalla porta USB del PC attraverso il pin **VBUS (pin 40)** del Pico.
- L'ESP32 con WiFi attivo ha picchi fino a ~500 mA: con hub, mouse e tastiera si supera il limite
  di una porta USB 2.0. In modalità USB tieni il WiFi spento (bilancio completo nel file 05).
- In modalità Bluetooth il powerbank si collega **al Pico**, non all'ESP32.
