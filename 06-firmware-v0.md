# 06 — Firmware Smart Mouse v0

Codice: `firmware/smart-mouse-v0/`

> Superato dalla **v1** (file 07), che aggiunge la configurazione da `config.txt`.
> La v0 resta come riferimento più semplice.

Primo firmware del progetto. Sostituisce HID Remapper, che non riconosce il mouse Trust
(low-speed) dietro l'hub (vedi file 05). Usa lo stesso stack che nella diagnostica funziona:
**Adafruit TinyUSB + Pico-PIO-USB** su core Arduino **arduino-pico**.

## Cosa fa

```
mouse (anche dietro hub) ─► GP0/GP1 host PIO ─► core1 ─(coda)─► core0 ─► USB del Pico ─► PC
                                                                         (mouse + tastiera + seriale log)
```

- Movimento, rotella e tasti **non mappati** passano al PC invariati.
- I tasti **mappati** non arrivano al PC come tasti del mouse: diventano scorciatoie.
- Il mouse viene letto in **report protocol** (non boot): il report descriptor viene analizzato
  (`hid_parser.h`) per trovare tasti 1–8, X, Y, rotella e rotella orizzontale.
- Funziona con più mouse insieme (tasti uniti, movimenti sommati).
- Se il PC è in sospensione, un movimento lo risveglia; senza PC (powerbank) il movimento viene scartato.

### Scorciatoie (fisse in v0)

| Tasto del mouse | Scorciatoia |
|---|---|
| Laterale avanti (tasto 5, bit 4) | Ctrl+C |
| Laterale indietro (tasto 4, bit 3) | Ctrl+V |

Per cambiarle: tabella `SHORTCUTS` in `smart-mouse-v0.ino`. Il tasto viene inviato come
"prima Ctrl, poi Ctrl+C" per massima compatibilità con le applicazioni.

### Mouse Trust rilevato

`1bcf:0005`, descriptor 75 byte, report ID 1: tasti 1–5 (bit 0–4), X e Y a 12 bit con segno,
rotella e pan a 8 bit.

## Compilare e caricare

Strumenti: arduino-cli, core `rp2040:rp2040` (arduino-pico), librerie
`Adafruit TinyUSB Library` e `Pico PIO USB`.

```
arduino-cli compile --fqbn "rp2040:rp2040:rpipico:usbstack=tinyusb,freq=120" --output-dir firmware/build/smart-mouse-v0 firmware/smart-mouse-v0
arduino-cli upload  --fqbn "rp2040:rp2040:rpipico:usbstack=tinyusb,freq=120" -p COMx --input-dir firmware/build/smart-mouse-v0
```

- **CPU a 120 MHz obbligatoria**: l'host USB via PIO richiede un multiplo di 12 MHz.
- L'upload usa il reset a 1200 baud sulla seriale del Pico: non serve premere BOOTSEL.
  Se il firmware non risponde: BOOTSEL premuto mentre si collega il cavo, poi copiare il file `.uf2`.

## Log

Seriale USB del Pico a 115200 baud. Il log resta in memoria (16 KB): aprendo la porta si riceve
tutto dall'avvio; inviando `r` viene ristampato. Contiene: layout del mouse letto dal descriptor,
primi report ricevuti, cambi dei tasti, scorciatoie attivate, contatori dei report
(validi / vuoti = trasferimenti falliti / con report ID diverso).

## Limiti di v0 e prossimi passi

- Scorciatoie fisse nel codice → **v1**: file `config.txt` sull'unità USB che compare con una
  combinazione di tasti (MSC di TinyUSB, ri-enumerazione senza riavvio).
- Tastiere collegate all'hub non vengono inoltrate (solo mouse).
- VID/PID sono quelli predefiniti della libreria (`239A:CAFE`): vanno bene per lo sviluppo.
- Comunicazione con l'ESP32 (UART GP4/GP5) non ancora usata.
