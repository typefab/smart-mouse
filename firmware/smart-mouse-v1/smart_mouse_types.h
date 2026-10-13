// Tipi condivisi (in un header perché il preprocessore Arduino genera i prototipi
// delle funzioni prima del codice dello sketch).
#pragma once
#include <stdint.h>

#define N_BUTTONS 8

// Evento dal mouse, passato da core1 (host) a core0 (dispositivo)
struct mouse_event_t {
  uint8_t source;  // daddr * 4 + idx
  uint8_t buttons;
  int16_t dx, dy;
  int8_t wheel, pan;
};

// Cosa fa un tasto del mouse
enum action_type_t : uint8_t {
  ACT_NORMALE = 0,   // passa al PC come tasto del mouse
  ACT_DISATTIVATO,   // ignorato
  ACT_TASTI,         // combinazione di tastiera (modificatori + fino a 6 tasti)
  ACT_MEDIA,         // tasto multimediale (consumer control)
};

struct action_t {
  action_type_t type;
  uint8_t modifier;
  uint8_t keys[6];
  uint8_t nkeys;
  uint16_t media;
};

// Report di tastiera inviato al PC
struct kb_report_t {
  uint8_t modifier;
  uint8_t keys[6];
};

struct config_t {
  action_t buttons[N_BUTTONS];
  uint16_t open_combo_ms;  // tempo di pressione dei due laterali per aprire/chiudere la cartella
  uint16_t chord_ms;       // attesa prima di attivare un laterale (per riconoscere la combinazione)
};
