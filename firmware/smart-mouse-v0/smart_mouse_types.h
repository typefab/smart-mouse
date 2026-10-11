// Tipi condivisi (in un header perché il preprocessore Arduino genera i prototipi
// delle funzioni prima del codice dello sketch).
#pragma once
#include <stdint.h>

// Evento dal mouse, passato da core1 (host) a core0 (dispositivo)
struct mouse_event_t {
  uint8_t source;  // daddr * 4 + idx
  uint8_t buttons;
  int16_t dx, dy;
  int8_t wheel, pan;
};
