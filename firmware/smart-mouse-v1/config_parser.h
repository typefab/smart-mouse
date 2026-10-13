// Lettura di config.txt: righe "tasto = azione", commenti con # o ;
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Adafruit_TinyUSB.h"  // HID_KEY_*, KEYBOARD_MODIFIER_*, HID_USAGE_CONSUMER_*
#include "smart_mouse_types.h"

static const char *const BUTTON_NAMES[N_BUTTONS] = {
  "sinistro", "destro", "centrale", "laterale_indietro", "laterale_avanti", "tasto6", "tasto7", "tasto8",
};

struct name_code_t {
  const char *name;
  uint16_t code;
};

static const name_code_t MODIFIER_NAMES[] = {
  {"CTRL", KEYBOARD_MODIFIER_LEFTCTRL},   {"CONTROL", KEYBOARD_MODIFIER_LEFTCTRL},
  {"SHIFT", KEYBOARD_MODIFIER_LEFTSHIFT}, {"MAIUSC", KEYBOARD_MODIFIER_LEFTSHIFT},
  {"ALT", KEYBOARD_MODIFIER_LEFTALT},     {"ALTGR", KEYBOARD_MODIFIER_RIGHTALT},
  {"WIN", KEYBOARD_MODIFIER_LEFTGUI},     {"GUI", KEYBOARD_MODIFIER_LEFTGUI},
  {"CMD", KEYBOARD_MODIFIER_LEFTGUI},     {"CTRL_DX", KEYBOARD_MODIFIER_RIGHTCTRL},
  {"SHIFT_DX", KEYBOARD_MODIFIER_RIGHTSHIFT},
};

static const name_code_t KEY_NAMES[] = {
  {"INVIO", HID_KEY_ENTER},          {"ENTER", HID_KEY_ENTER},
  {"ESC", HID_KEY_ESCAPE},           {"TAB", HID_KEY_TAB},
  {"SPAZIO", HID_KEY_SPACE},         {"SPACE", HID_KEY_SPACE},
  {"BACKSPACE", HID_KEY_BACKSPACE},  {"CANC", HID_KEY_DELETE},
  {"DEL", HID_KEY_DELETE},           {"DELETE", HID_KEY_DELETE},
  {"INS", HID_KEY_INSERT},           {"INSERT", HID_KEY_INSERT},
  {"HOME", HID_KEY_HOME},            {"INIZIO", HID_KEY_HOME},
  {"FINE", HID_KEY_END},             {"END", HID_KEY_END},
  {"PAGSU", HID_KEY_PAGE_UP},        {"PAGEUP", HID_KEY_PAGE_UP},
  {"PAGGIU", HID_KEY_PAGE_DOWN},     {"PAGEDOWN", HID_KEY_PAGE_DOWN},
  {"SU", HID_KEY_ARROW_UP},          {"UP", HID_KEY_ARROW_UP},
  {"GIU", HID_KEY_ARROW_DOWN},       {"DOWN", HID_KEY_ARROW_DOWN},
  {"SINISTRA", HID_KEY_ARROW_LEFT},  {"LEFT", HID_KEY_ARROW_LEFT},
  {"DESTRA", HID_KEY_ARROW_RIGHT},   {"RIGHT", HID_KEY_ARROW_RIGHT},
  {"STAMP", HID_KEY_PRINT_SCREEN},   {"PRINTSCREEN", HID_KEY_PRINT_SCREEN},
  {"MENU", HID_KEY_APPLICATION},
};

static const name_code_t MEDIA_NAMES[] = {
  {"VOLUME_SU", HID_USAGE_CONSUMER_VOLUME_INCREMENT},
  {"VOLUME_GIU", HID_USAGE_CONSUMER_VOLUME_DECREMENT},
  {"MUTO", HID_USAGE_CONSUMER_MUTE},
  {"PLAY_PAUSA", HID_USAGE_CONSUMER_PLAY_PAUSE},
  {"SUCCESSIVO", HID_USAGE_CONSUMER_SCAN_NEXT_TRACK},
  {"PRECEDENTE", HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK},
  {"STOP", HID_USAGE_CONSUMER_STOP},
  {"CALCOLATRICE", HID_USAGE_CONSUMER_AL_CALCULATOR},
  {"EMAIL", HID_USAGE_CONSUMER_AL_EMAIL_READER},
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static bool lookup(const name_code_t *table, size_t n, const char *name, uint16_t *code) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(table[i].name, name) == 0) {
      *code = table[i].code;
      return true;
    }
  }
  return false;
}

// Tasto singolo: lettere, cifre, F1-F24 o nomi della tabella
static bool parse_key(const char *t, uint8_t *key) {
  size_t len = strlen(t);
  if (len == 1 && t[0] >= 'A' && t[0] <= 'Z') {
    *key = HID_KEY_A + (t[0] - 'A');
    return true;
  }
  if (len == 1 && t[0] >= '1' && t[0] <= '9') {
    *key = HID_KEY_1 + (t[0] - '1');
    return true;
  }
  if (len == 1 && t[0] == '0') {
    *key = HID_KEY_0;
    return true;
  }
  if (t[0] == 'F' && len >= 2 && len <= 3 && isdigit((unsigned char) t[1]) && (len == 2 || isdigit((unsigned char) t[2]))) {
    int n = atoi(t + 1);
    if (n >= 1 && n <= 12) {
      *key = HID_KEY_F1 + (n - 1);
      return true;
    }
    if (n >= 13 && n <= 24) {
      *key = HID_KEY_F13 + (n - 13);
      return true;
    }
    return false;
  }
  uint16_t code;
  if (lookup(KEY_NAMES, COUNT_OF(KEY_NAMES), t, &code)) {
    *key = (uint8_t) code;
    return true;
  }
  return false;
}

static void trim(char *s) {
  char *p = s;
  while (*p && isspace((unsigned char) *p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && isspace((unsigned char) s[n - 1])) s[--n] = 0;
}

static void str_upper(char *s) {
  for (; *s; s++) *s = toupper((unsigned char) *s);
}

static void str_lower(char *s) {
  for (; *s; s++) *s = tolower((unsigned char) *s);
}

static void err_append(char *err, size_t err_size, int line, const char *fmt, const char *arg) {
  size_t n = strlen(err);
  if (n + 1 >= err_size) return;
  int w = snprintf(err + n, err_size - n, "riga %d: ", line);
  if (w < 0) return;
  n += w;
  if (n + 1 >= err_size) return;
  w = snprintf(err + n, err_size - n, fmt, arg);
  if (w < 0) return;
  n += w;
  if (n + 3 < err_size) {
    err[n++] = '\r';
    err[n++] = '\n';
    err[n] = 0;
  }
}

// Interpreta il valore di un tasto ("CTRL+C", "normale", "VOLUME_SU"...). Ritorna false se non valido.
static bool parse_action(char *value, action_t *act, char *bad_token, size_t bad_size) {
  memset(act, 0, sizeof(*act));
  str_upper(value);
  if (strcmp(value, "NORMALE") == 0) {
    act->type = ACT_NORMALE;
    return true;
  }
  if (strcmp(value, "DISATTIVATO") == 0 || strcmp(value, "NIENTE") == 0) {
    act->type = ACT_DISATTIVATO;
    return true;
  }
  uint16_t code;
  if (lookup(MEDIA_NAMES, COUNT_OF(MEDIA_NAMES), value, &code)) {
    act->type = ACT_MEDIA;
    act->media = code;
    return true;
  }

  act->type = ACT_TASTI;
  char *save = nullptr;
  for (char *tok = strtok_r(value, "+", &save); tok; tok = strtok_r(nullptr, "+", &save)) {
    trim(tok);
    uint8_t key;
    if (!*tok) {
      snprintf(bad_token, bad_size, "+");
      return false;
    }
    if (lookup(MODIFIER_NAMES, COUNT_OF(MODIFIER_NAMES), tok, &code)) {
      act->modifier |= (uint8_t) code;
    } else if (parse_key(tok, &key)) {
      if (act->nkeys >= 6) {
        snprintf(bad_token, bad_size, "%s (massimo 6 tasti)", tok);
        return false;
      }
      act->keys[act->nkeys++] = key;
    } else {
      snprintf(bad_token, bad_size, "%s", tok);
      return false;
    }
  }
  return act->nkeys || act->modifier;
}

static void config_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->open_combo_ms = 3000;
  cfg->chord_ms = 100;
}

// Legge il testo di config.txt. Gli errori (uno per riga) finiscono in err. Ritorna il numero di errori.
static int parse_config(const char *text, size_t len, config_t *cfg, char *err, size_t err_size) {
  config_defaults(cfg);
  err[0] = 0;
  int errors = 0, line_no = 0;
  size_t i = 0;
  while (i < len) {
    char line[160];
    size_t n = 0;
    while (i < len && text[i] != '\n') {
      if (n < sizeof(line) - 1) line[n++] = text[i];
      i++;
    }
    i++;  // salta '\n'
    line[n] = 0;
    line_no++;

    char *c = strpbrk(line, "#;");
    if (c) *c = 0;
    trim(line);
    if (!line[0]) continue;

    char *eq = strchr(line, '=');
    if (!eq) {
      err_append(err, err_size, line_no, "manca '=' in \"%s\"", line);
      errors++;
      continue;
    }
    *eq = 0;
    char *name = line, *value = eq + 1;
    trim(name);
    trim(value);
    str_lower(name);

    if (strcmp(name, "apri_configurazione_secondi") == 0) {
      int s = atoi(value);
      if (s < 1 || s > 10) {
        err_append(err, err_size, line_no, "apri_configurazione_secondi deve essere tra 1 e 10 (letto \"%s\")", value);
        errors++;
      } else {
        cfg->open_combo_ms = s * 1000;
      }
      continue;
    }
    if (strcmp(name, "ritardo_accordo_ms") == 0) {
      int ms = atoi(value);
      if (ms < 0 || ms > 500 || (!isdigit((unsigned char) value[0]))) {
        err_append(err, err_size, line_no, "ritardo_accordo_ms deve essere tra 0 e 500 (letto \"%s\")", value);
        errors++;
      } else {
        cfg->chord_ms = ms;
      }
      continue;
    }

    int b = -1;
    for (int k = 0; k < N_BUTTONS; k++) {
      if (strcmp(name, BUTTON_NAMES[k]) == 0) b = k;
    }
    if (b < 0) {
      err_append(err, err_size, line_no, "tasto o impostazione sconosciuta \"%s\"", name);
      errors++;
      continue;
    }
    char bad[48] = "";
    action_t act;
    if (!parse_action(value, &act, bad, sizeof(bad))) {
      err_append(err, err_size, line_no, "azione non valida, non riconosco \"%s\"", bad[0] ? bad : value);
      errors++;
      continue;
    }
    cfg->buttons[b] = act;
  }
  return errors;
}

// Descrizione leggibile di un'azione (per il log)
static void describe_action(const action_t &a, char *out, size_t size) {
  switch (a.type) {
    case ACT_NORMALE: snprintf(out, size, "normale"); return;
    case ACT_DISATTIVATO: snprintf(out, size, "disattivato"); return;
    case ACT_MEDIA:
      for (size_t i = 0; i < COUNT_OF(MEDIA_NAMES); i++) {
        if (MEDIA_NAMES[i].code == a.media) {
          snprintf(out, size, "%s", MEDIA_NAMES[i].name);
          return;
        }
      }
      snprintf(out, size, "media 0x%03x", a.media);
      return;
    case ACT_TASTI: {
      int n = snprintf(out, size, "modificatori 0x%02x, tasti", a.modifier);
      for (uint8_t k = 0; k < a.nkeys && n > 0 && (size_t) n < size; k++) n += snprintf(out + n, size - n, " %02x", a.keys[k]);
      return;
    }
  }
}

static const char DEFAULT_CONFIG[] =
  "# Smart Mouse - configurazione dei tasti\r\n"
  "#\r\n"
  "# Formato:   tasto = azione\r\n"
  "# Tasti:     sinistro, destro, centrale, laterale_indietro, laterale_avanti,\r\n"
  "#            tasto6, tasto7, tasto8\r\n"
  "# Azioni:\r\n"
  "#   normale        il tasto funziona come sempre\r\n"
  "#   disattivato    il tasto non fa niente\r\n"
  "#   CTRL+C         combinazione di tasti; modificatori: CTRL, SHIFT, ALT, ALTGR, WIN\r\n"
  "#   tasti:         A-Z, 0-9, F1-F24, INVIO, ESC, TAB, SPAZIO, BACKSPACE, CANC, INS,\r\n"
  "#                  HOME, FINE, PAGSU, PAGGIU, SU, GIU, SINISTRA, DESTRA, STAMP, MENU\r\n"
  "#   multimedia:    VOLUME_SU, VOLUME_GIU, MUTO, PLAY_PAUSA, SUCCESSIVO, PRECEDENTE,\r\n"
  "#                  STOP, CALCOLATRICE, EMAIL\r\n"
  "#\r\n"
  "# Aprire/chiudere questa unita': tieni premuti INSIEME i due tasti laterali.\r\n"
  "# Dopo aver salvato, espelli l'unita' (o ripeti la combinazione): le modifiche\r\n"
  "# si applicano subito. Gli eventuali errori finiscono nel file errori.txt.\r\n"
  "# Attenzione: se cambi \"sinistro\" non potrai piu' cliccare normalmente\r\n"
  "# (la combinazione dei laterali funziona comunque).\r\n"
  "\r\n"
  "laterale_avanti   = CTRL+C\r\n"
  "laterale_indietro = CTRL+V\r\n"
  "\r\n"
  "# Impostazioni\r\n"
  "apri_configurazione_secondi = 3\r\n"
  "ritardo_accordo_ms = 100\r\n";
