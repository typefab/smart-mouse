# 05 — Aggiungere un hub USB (più dispositivi sul Pico)

Schema: `schemi/schema-hub-powerbank.svg`

L'hub si collega **al posto del mouse**, sulla porta USB-A femmina cablata sul Pico (GP0/GP1).
Mouse, tastiera o altri dispositivi si collegano all'hub. **Nessun filo nuovo sul Pico.**

HID Remapper supporta più dispositivi collegati tramite hub, anche con mappature diverse
per ogni dispositivo; nella scheda "Monitor" della configurazione si vede da quale porta dell'hub
arriva ogni input.

```
PC / powerbank ─► Pico ─(GP0/GP1)─► porta USB-A ─► HUB ─┬─► Mouse Trust
                   │                                   ├─► Tastiera
                   └─(UART ◄─►)─ ESP32                 └─► altro dispositivo HID
```

## Che hub scegliere

- Un normale **hub USB 2.0**. La porta del Pico è a bassa/piena velocità (1,5 / 12 Mbit/s):
  perfetta per mouse, tastiere, gamepad; **non** adatta a chiavette, dischi, webcam.
- **Hub non alimentato** se colleghi solo mouse e tastiera: è la soluzione più semplice.
- **Hub alimentato** solo se servono più di ~300–400 mA in totale.

## Mouse low-speed dietro l'hub: da provare subito

Molti mouse economici (Trust compresi) sono **low-speed** (1,5 Mbit/s). Dietro un hub full-speed
l'host deve inviare pacchetti speciali ("PRE") per parlare con loro, ed è il caso più delicato per
l'host USB software del Pico (PIO). HID Remapper dichiara il supporto agli hub, ma va verificato
con il tuo mouse:

1. prova il mouse **direttamente** sulla porta del Pico → deve funzionare;
2. prova il mouse **dietro l'hub** → se qui smette di funzionare (o perde input), il problema è
   la combinazione low-speed + hub, non il cablaggio.

Per sapere se il mouse è low-speed: Gestione dispositivi → mouse → Proprietà → Dettagli, oppure
con lo strumento *USB Device Tree Viewer*.

**Mouse Trust del progetto: VID `1BCF` / PID `0005` (chip Sunplus, "USB Optical Mouse") → low-speed**
(letto dal PC, `bMaxPacketSize0` = 8). Hub usato: **USB 3.0** (fili extra arancio/viola/blu/giallo =
coppie SuperSpeed, non servono; trasparente = drain a GND: isolarli tutti).

Risultati (cablaggio verificato: D+ su GP0 a ~3 V, D− su GP1 a 0 V, porta a 4,99 V):

| Firmware sul Pico | Mouse dietro l'hub |
|---|---|
| HID Remapper r2026-05-25 (`remapper.uf2`) | **non riconosciuto** (nessun ingresso del mouse) |
| Diagnostica `test/diagnostica-usb-host` (Adafruit TinyUSB 3.7.7 + Pico-PIO-USB 0.7.2) | **funziona**: montato, report corretti (tasti e movimento) |

Quindi hardware e cablaggio sono a posto: il limite è nello stack USB incluso in HID Remapper.

## Attenzione agli hub alimentati: il "ritorno" di corrente

Molti hub alimentati economici mandano i loro 5 V anche indietro sul cavo verso il "computer".
Nel tuo caso finirebbero sul VBUS del Pico, e da lì verso il PC o il powerbank.

Prova semplice: accendi l'hub con il suo alimentatore **senza collegarlo a nulla** e misura col
multimetro la tensione tra rosso e nero del suo cavo/spina upstream. Se leggi ~5 V, l'hub fa ritorno:
non usarlo così (scegli un altro hub oppure uno non alimentato).

## Bilancio dei consumi (indicativo)

| Dispositivo | Consumo tipico |
|---|---|
| Pico | 20–30 mA |
| Hub non alimentato (il suo chip) | 20–100 mA |
| ESP32 (Bluetooth attivo) | 100–250 mA |
| ESP32 (WiFi attivo, picchi) | fino a ~500 mA |
| Mouse | 50–100 mA |
| Tastiera | 50–100 mA |

Una porta USB 2.0 del PC fornisce 500 mA in totale:

- **con il WiFi spento** (solo Bluetooth o ESP32 inattivo) ci si sta, ma con poco margine;
- **con il WiFi acceso** i picchi superano il limite: il PC può disattivare la porta o l'ESP32
  può riavviarsi (brownout). In modalità USB tieni il WiFi spento.

Metti un **condensatore elettrolitico da 100–470 µF** tra VIN e GND dell'ESP32, vicino alla scheda,
per assorbire i picchi. Un powerbank di solito fornisce 1–2 A, quindi lì nessun problema.
