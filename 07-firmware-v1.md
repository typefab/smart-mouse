# 07 — Firmware Smart Mouse v1: configurazione da `config.txt`

Codice: `firmware/smart-mouse-v1/`

Evoluzione della v0 (file 06): stesso mouse passante, ma le azioni dei tasti si configurano
modificando un file di testo su un'unità USB che compare a comando.

## Come si usa

1. **Tieni premuti insieme i due tasti laterali per 3 secondi.** Compare l'unità **SMARTMOUSE**
   e il LED del Pico lampeggia lentamente.
2. Apri `config.txt`, modifica e salva.
3. **Espelli l'unità** da Windows (oppure ripeti la combinazione dei laterali): il Pico rilegge il file
   e applica subito le nuove azioni. Se qualche riga è sbagliata, viene ignorata e il motivo finisce
   in `errori.txt` (visibile alla prossima apertura).

Mentre l'unità è nascosta risponde al PC "supporto non presente": con le impostazioni predefinite
Esplora file non la mostra.

## Formato di `config.txt`

```
# commenti con # oppure ;
laterale_avanti   = CTRL+C
laterale_indietro = CTRL+V
centrale          = normale
tasto6            = VOLUME_SU

apri_configurazione_secondi = 3     # 1-10
ritardo_accordo_ms = 100            # 0-500
```

| Tasti configurabili | |
|---|---|
| `sinistro`, `destro`, `centrale` | tasti 1–3 |
| `laterale_indietro`, `laterale_avanti` | tasti 4 e 5 (i due laterali) |
| `tasto6`, `tasto7`, `tasto8` | per mouse con più tasti |

| Azioni | |
|---|---|
| `normale` | il tasto funziona come sempre (predefinito) |
| `disattivato` | il tasto non fa niente |
| combinazione con `+` | modificatori `CTRL`, `SHIFT`, `ALT`, `ALTGR`, `WIN` (anche `CTRL_DX`, `SHIFT_DX`) + fino a 6 tasti |
| tasti | `A`–`Z`, `0`–`9`, `F1`–`F24`, `INVIO`, `ESC`, `TAB`, `SPAZIO`, `BACKSPACE`, `CANC`, `INS`, `HOME`, `FINE`, `PAGSU`, `PAGGIU`, `SU`, `GIU`, `SINISTRA`, `DESTRA`, `STAMP`, `MENU` (anche i nomi inglesi) |
| multimedia | `VOLUME_SU`, `VOLUME_GIU`, `MUTO`, `PLAY_PAUSA`, `SUCCESSIVO`, `PRECEDENTE`, `STOP`, `CALCOLATRICE`, `EMAIL` |

Maiuscole/minuscole e spazi attorno a `=` e `+` non contano. I tasti sono posizionali (come su una
tastiera USA): lettere e cifre coincidono con la tastiera italiana, i simboli no.

### Il "ritardo accordo"

Per distinguere "premo un laterale" da "premo i due laterali insieme", l'azione di un laterale
parte dopo `ritardo_accordo_ms` (100 ms). Se nel frattempo premi anche l'altro, nessuna delle due
azioni viene eseguita e parte il conteggio per aprire la configurazione. Con `0` le azioni dei
laterali partono subito, ma premendoli insieme si attivano entrambe prima della combinazione.

## Come funziona dentro

- **Partizione FAT12 da 256 KB** nella flash del Pico (opzione di compilazione `flash=2097152_262144`).
  Al primo avvio viene formattata (`fat12_format.h`: il formattatore di SdFat crea solo FAT16/32,
  troppo grandi) e viene scritto un `config.txt` predefinito con la spiegazione nei commenti.
  Caricare un nuovo firmware non cancella la partizione.
- **Unità USB (MSC)** sempre presente ma "senza supporto" finché non la apri: niente riavvii né
  ri-enumerazioni. L'espulsione da Windows arriva come comando SCSI START STOP UNIT.
- **Report verso il PC**: tastiera (ID 1), mouse (ID 2), multimedia (ID 3). Le combinazioni con nuovi
  modificatori vengono inviate in due passi (prima `Ctrl`, poi `Ctrl+C`).
- **Scritture su flash**: mettono in pausa per qualche millisecondo core1 (host USB del mouse).
  Mentre salvi il file il mouse può scattare o, al limite, ricollegarsi.

## Compilare e caricare

```
arduino-cli compile --fqbn "rp2040:rp2040:rpipico:usbstack=tinyusb,freq=120,flash=2097152_262144" --output-dir firmware/build/smart-mouse-v1 firmware/smart-mouse-v1
arduino-cli upload  --fqbn "rp2040:rp2040:rpipico:usbstack=tinyusb,freq=120,flash=2097152_262144" -p COMx --input-dir firmware/build/smart-mouse-v1
```

Librerie in più rispetto alla v0: `Adafruit SPIFlash`, `SdFat - Adafruit Fork` (installate insieme
ad Adafruit TinyUSB).

## Log e comandi di sviluppo

Seriale USB del Pico a 115200 baud: `r` ristampa il log dall'avvio, `c` apre/chiude l'unità
(come la combinazione). Il log mostra la configurazione letta, gli errori, le combinazioni e i
contatori dei report del mouse.

## Verificato

- Primo avvio: formattazione, creazione di `config.txt`, lettura senza errori.
- Dal PC: apertura dell'unità (F:, FAT, 252 KB), modifica di `config.txt`, espulsione da Windows →
  configurazione riletta; file con errori → `errori.txt` corretto; ripristino → `errori.txt` rimosso.
- `CTRL+SHIFT+T`, `alt + f4` (minuscolo, con spazi), `VOLUME_SU` interpretati correttamente.

Da provare con il mouse in mano: combinazione dei laterali, scorciatoie con il ritardo accordo,
tasti multimediali.
