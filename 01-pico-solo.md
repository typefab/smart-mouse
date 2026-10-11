# 01 — Versione con solo Raspberry Pi Pico

Schema: `schemi/schema-pico-solo.svg`

## Opzione A — Pico come "host USB" per il mouse (HID Remapper)

Il mouse si collega a una porta USB-A femmina cablata sul Pico. Il Pico legge il mouse,
rimappa i tasti e invia al PC movimento + scorciatoie. Nessuna modifica al mouse.

### Collegamenti

| Porta USB-A femmina | Colore tipico | Pico |
|---|---|---|
| VBUS (5V) | rosso | VBUS — pin 40 |
| D+ | verde | GP0 — pin 1 |
| D− | bianco | GP1 — pin 2 |
| GND | nero | GND — pin 3 |

Il Pico va al PC con la sua micro-USB (oppure con un cavo saldato sui pad, vedi file 03).

### Suggerimenti

- La porta si ricava facilmente tagliando una **prolunga USB** e usando il lato femmina.
- **Verifica i colori col multimetro** prima di saldare: nei cavi economici non sono garantiti.
- Firmware: file `remapper.uf2` (variante "single Pico") dal progetto HID Remapper.
  Tieni premuto BOOTSEL, collega il Pico, copia il file `.uf2` sull'unità RPI-RP2.
- Configurazione da browser Chrome/Edge (WebHID) su remapper.org.

## Opzione B — Pico collegato ai microswitch del mouse

1. Con il multimetro verifica che ogni microswitch chiuda verso GND.
2. Collega il GND del mouse al GND del Pico.
3. Collega il lato "segnale" di ogni switch a un GPIO libero (es. GP2, GP3, GP4…)
   configurato con **pull-up interno**.

Attenzione: alcuni mouse leggono i tasti a matrice; in quel caso il collegamento diretto
non funziona bene. Il sensore di movimento resta gestito dal chip originale, quindi serve
un hub USB interno per far uscire due dispositivi da un solo cavo.

## Unità USB con file di configurazione (CircuitPython)

Con CircuitPython il Pico compare al PC come unità **CIRCUITPY**. Esempio di `config.txt`:

```
laterale_alto = CTRL+C
laterale_basso = CTRL+V
```

Idea di funzionamento:

- in `boot.py` l'unità viene nascosta durante l'uso normale (`storage.disable_usb_drive()`);
- se all'avvio è premuta una combinazione di tasti, l'unità resta visibile;
- anche "a caldo": premendo la combinazione il Pico salva un flag in memoria
  (`microcontroller.nvm`) e si riavvia (`microcontroller.reset()`) mostrando la cartella.

Nota: con HID Remapper la configurazione si fa invece dal browser, non tramite file.
