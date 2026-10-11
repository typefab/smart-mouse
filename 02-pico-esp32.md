# 02 — Versione Pico + ESP32

Schema: `schemi/schema-pico-esp32.svg`

La parte mouse → Pico è identica al file 01 (opzione A). L'ESP32 si collega al Pico con una
**UART su GP4/GP5**, perché GP0 e GP1 sono già usati dall'USB del mouse.

## Collegamenti

### Porta USB-A (mouse) → Pico

| Porta USB-A | Pico |
|---|---|
| VBUS (5V, rosso) | VBUS — pin 40 |
| D+ (verde) | GP0 — pin 1 |
| D− (bianco) | GP1 — pin 2 |
| GND (nero) | GND — pin 3 |

### Pico → ESP32

| Pico | ESP32 | Note |
|---|---|---|
| VBUS — pin 40 | VIN / 5V | alimentazione |
| GND — pin 3 | GND | massa comune, obbligatoria |
| GP4 TX — pin 6 | GPIO16 RX2 | TX va in RX |
| GP5 RX — pin 7 | GPIO17 TX2 | RX riceve da TX |

## Note importanti

- **TX e RX vanno incrociati.** È l'errore più comune.
- **GND in comune** obbligatorio, altrimenti la seriale non funziona.
- Durante le prove **non** alimentare l'ESP32 sia dal Pico (VIN) sia dal suo USB:
  scollega il filo VBUS→VIN oppure usa solo una delle due alimentazioni.
- Per un mouse davvero **senza fili** serve una batteria LiPo con modulo di ricarica
  al posto del cavo verso il PC: lo schema dell'alimentazione cambia.
- Entrambi a 3,3 V: nessun convertitore di livello.
- **Condensatore da 100–470 µF tra VIN e GND dell'ESP32**: assorbe i picchi di consumo del WiFi/Bluetooth
  (vedi bilancio consumi nel file 05).
- Questo cablaggio è pensato per un **firmware scritto da te** (il Pico manda comandi all'ESP32).

## Variante sperimentale di HID Remapper con ESP32 (pin diversi!)

Esiste una versione sperimentale di HID Remapper in cui il Pico è il "lato A" e un ESP32
il "lato B": l'ESP32 riceve mouse/tastiere Bluetooth (anche Bluetooth Classic) e li passa al Pico.
Richiede un **ESP32 classico** (non S3, C3, C6). Collegamenti indicati dall'autore:

| Pico | ESP32 |
|---|---|
| GPIO20 | GPIO13 |
| GPIO21 | GPIO12 |
| GPIO26 | GPIO15 |
| GPIO27 | GPIO14 |
| VBUS | 5V in (opzionale) |
| GND | GND |

Fonte: forum.remapper.org, discussione "Bluetooth Classic version testing (ESP32)".
Firmware in fase di test.

## Modello usato: ESP32-WROOM-32D (su scheda DevKit)

È un ESP32 classico (Bluetooth Classic + BLE + WiFi), senza PSRAM: GPIO16 e GPIO17 sono liberi.

```cpp
Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX = 16, TX = 17
```

- **Collegamento verificato** con il test in `test/uart-pico-esp32/` (sketch eco sull'ESP32 +
  script MicroPython sul Pico, 115200 baud): 5/5 risposte corrette.
- Serial2 è separata dalla seriale USB: puoi programmare e usare il monitor senza staccare i fili dati.
- Non usare GPIO6–11 (memoria flash).
- Evita come pin generici GPIO0, 2, 12, 15 (pin di avvio). GPIO12 alto all'accensione può bloccare il boot.
- Durante la programmazione via USB stacca il filo VBUS→VIN (lascia GND, TX, RX).

## Alimentazione da powerbank (modalità Bluetooth)

Schema: `schemi/schema-hub-powerbank.svg`

Il powerbank va collegato **al Pico, dove di solito colleghi il PC** (micro-USB o cavo saldato sui pad),
**non** all'ESP32.

Motivo: anche in modalità Bluetooth il Pico deve restare acceso, perché legge il mouse e gli fornisce i 5 V.
Il percorso della corrente resta identico: powerbank → Pico → VBUS → mouse ed ESP32. Nessun filo da cambiare.

### Rilevamento automatico della modalità

Quando è collegato a un alimentatore il Pico non viene "riconosciuto" da nessun computer
(in TinyUSB `tud_mounted()` resta falso). Il firmware può usarlo per:

- collegato al PC → modalità USB (tastiera/mouse via cavo);
- collegato al powerbank → modalità Bluetooth (comandi inviati all'ESP32 in seriale).

Due casi a cui fare attenzione:

- **All'accensione** l'enumerazione da parte del PC non è istantanea: aspetta 2–3 secondi
  prima di decidere che "non c'è un PC".
- **PC in sospensione**: il bus USB va in *suspend* (`tud_suspended()` vero). Non è un powerbank:
  non passare al Bluetooth, resta in modalità USB (eventualmente con *remote wakeup*).

```c
static bool gia_montato = false;
void tud_mount_cb(void) { gia_montato = true; }   // chiamata da TinyUSB quando il PC enumera

// dopo ~3 s dall'avvio
modo = gia_montato ? MODO_USB : MODO_BLUETOOTH;
```

Il flag "è mai stato montato" è più affidabile di `tud_suspended()` da solo: senza un PC il bus resta
fermo e alcuni stack lo possono interpretare anch'esso come suspend.

### Avvertenze

- **Mai due alimentazioni insieme**: non collegare il powerbank all'USB dell'ESP32 mentre il Pico è su PC
  o su powerbank. Tramite il filo VBUS→VIN la corrente tornerebbe verso il PC.
- **Spegnimento automatico**: molti powerbank si spengono sotto 50–100 mA. Il sistema consuma circa
  100–200 mA; se succede, cerca la modalità "basso consumo" / "always on" (spesso: doppia pressione del pulsante).
- Passo successivo per un mouse davvero senza fili: batteria LiPo interna + modulo di ricarica + convertitore a 5 V.
