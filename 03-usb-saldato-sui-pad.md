# 03 — Sostituire la micro-USB del Pico con un cavo saldato

Schema: `schemi/schema-usb-pad.svg`

L'USB nativo del Pico **non** arriva ai GPIO del connettore laterale, ma a dei pad di test
sul retro della scheda (presenti su Pico e Pico 2). Lì si può saldare un cavo USB,
per esempio il **cavo originale del mouse**, così tutto sta dentro la scocca.

## Collegamenti

| Filo del cavo USB | Pico |
|---|---|
| Rosso (5V) | VBUS — pin 40 |
| Bianco (D−) | pad **TP2** (retro) |
| Verde (D+) | pad **TP3** (retro) |
| Nero (GND) | pad **TP1** oppure un pin GND |

Il Pico funziona esattamente come con la micro-USB. Per aggiornare il firmware:
tieni premuto **BOOTSEL** mentre colleghi il cavo.

## Consigli

- Pad piccoli: punta fine, poco stagno, e una goccia di **colla a caldo** come fermacavo
  per non strappare i pad.
- Fili D+ e D− **corti e intrecciati**, come nel cavo originale.
- **Mai** usare contemporaneamente la micro-USB e il cavo saldato.
- Verifica i colori col multimetro prima di saldare.

## Alternativa senza saldare sui pad

Schede RP2040 compatte con USB-C integrata (es. **Waveshare RP2040-Zero**), molto più piccole
del Pico. Controlla che abbiano abbastanza GPIO liberi per il tuo schema.
