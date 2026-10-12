// Smart Mouse v1 (vedi 07-firmware-v1.md)
//
//   mouse (anche dietro un hub) -> host USB via PIO su GP0 (D+) / GP1 (D-) -> core1
//   core0 -> USB nativa del Pico -> PC (mouse + tastiera + multimedia + unità di configurazione + log)
//
// Ogni tasto del mouse può restare normale, essere disattivato, diventare una combinazione di
// tastiera o un tasto multimediale. Le azioni si leggono da config.txt, su un'unità USB che
// compare tenendo premuti insieme i due tasti laterali e si chiude espellendola (o ripetendo
// la combinazione). L'unità è una piccola partizione FAT12 nella flash del Pico.
//
// Log: seriale USB del Pico a 115200 ('r' ristampa il log, 'c' apre/chiude l'unità).
// Scheda: rp2040:rp2040:rpipico, USB stack "Adafruit TinyUSB", CPU 120 MHz, Flash "FS: 256KB".

#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "Adafruit_SPIFlash.h"
#include "SdFat_Adafruit_Fork.h"
#include "pico/mutex.h"
#include "pico/util/queue.h"
#include "hid_parser.h"
#include "smart_mouse_types.h"
#include "config_parser.h"
#include "fat12_format.h"

#define PIN_USB_HOST_DP 0  // D- = GP1
#define LAT_MASK (MOUSE_BUTTON_BACKWARD | MOUSE_BUTTON_FORWARD)
#define CONFIG_FILE "config.txt"
#define ERRORS_FILE "errori.txt"

//--------------------------------------------------------------------+
// Dispositivo USB verso il PC: tastiera (ID 1) + mouse (ID 2) + multimedia (ID 3) + unità
//--------------------------------------------------------------------+

enum { RID_KEYBOARD = 1, RID_MOUSE = 2, RID_CONSUMER = 3 };

static uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(RID_CONSUMER)),
};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE, 1, false);
Adafruit_USBD_MSC usb_msc;
Adafruit_USBH_Host USBHost;

Adafruit_FlashTransport_RP2040 flashTransport;  // partizione FS scelta nel menu Flash
Adafruit_SPIFlash flash(&flashTransport);
FatVolume fatfs;

//--------------------------------------------------------------------+
// Log condiviso tra i due core
//--------------------------------------------------------------------+

static char log_buf[16 * 1024];
static volatile size_t log_len = 0;
static size_t log_sent = 0;
static bool log_full = false;
auto_init_mutex(log_mtx);

static void dlog(const char *fmt, ...) {
  char line[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int) sizeof(line) - 3) n = sizeof(line) - 3;
  char stamp[16];
  int s = snprintf(stamp, sizeof(stamp), "[%7.2fs] ", millis() / 1000.0);
  mutex_enter_blocking(&log_mtx);
  if (log_len + s + n + 2 < sizeof(log_buf)) {
    memcpy(log_buf + log_len, stamp, s);
    memcpy(log_buf + log_len + s, line, n);
    log_len += s + n;
    log_buf[log_len++] = '\r';
    log_buf[log_len++] = '\n';
  } else {
    log_full = true;
  }
  mutex_exit(&log_mtx);
}

static void log_flush_to_serial() {
  if (!Serial) {
    log_sent = 0;  // alla prossima apertura della porta ristampa tutto
    return;
  }
  mutex_enter_blocking(&log_mtx);
  size_t len = log_len;
  mutex_exit(&log_mtx);
  while (log_sent < len) {
    size_t chunk = len - log_sent;
    if (chunk > 64) chunk = 64;
    size_t w = Serial.write((const uint8_t *) log_buf + log_sent, chunk);
    if (w == 0) break;
    log_sent += w;
  }
  if (log_full) {
    Serial.println("[log pieno: invia 'r' per ristamparlo]");
    log_full = false;
  }
}

//--------------------------------------------------------------------+
// Configurazione su flash
//--------------------------------------------------------------------+

static config_t cfg;
static bool fs_ok = false;
static volatile bool drive_visible = false;
static volatile bool reload_pending = false;
static volatile uint32_t reload_at = 0;

static bool write_text_file(const char *name, const char *text, size_t len) {
  File32 f = fatfs.open(name, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  bool ok = f.write(text, len) == len;
  ok = f.close() && ok;
  return ok;
}

static bool storage_begin() {
  if (!flash.begin()) {
    dlog("ERRORE: flash non inizializzata (manca la partizione FS? compila con flash=2097152_262144)");
    return false;
  }
  dlog("partizione di configurazione: %lu KB", (unsigned long) (flash.size() / 1024));
  if (!fatfs.begin(&flash)) {
    dlog("nessun filesystem: formatto la partizione in FAT12");
    if (!format_fat12(flash, "SMARTMOUSE", 0x534D0001) || !fatfs.begin(&flash)) {
      dlog("ERRORE: formattazione fallita");
      return false;
    }
  }
  if (!fatfs.exists(CONFIG_FILE)) {
    dlog("config.txt assente: scrivo quello predefinito");
    if (!write_text_file(CONFIG_FILE, DEFAULT_CONFIG, strlen(DEFAULT_CONFIG))) dlog("ERRORE: scrittura config.txt");
  }
  flash.syncBlocks();
  return true;
}

static void log_config() {
  for (uint8_t b = 0; b < N_BUTTONS; b++) {
    if (cfg.buttons[b].type == ACT_NORMALE) continue;
    char d[64];
    describe_action(cfg.buttons[b], d, sizeof(d));
    dlog("  %-18s -> %s", BUTTON_NAMES[b], d);
  }
  dlog("  combinazione laterali: %u ms, ritardo accordo: %u ms", cfg.open_combo_ms, cfg.chord_ms);
}

static void load_config() {
  config_t newcfg;
  static char text[4096];
  static char err[1024];
  if (!fs_ok) {
    config_defaults(&newcfg);
    parse_config(DEFAULT_CONFIG, strlen(DEFAULT_CONFIG), &newcfg, err, sizeof(err));
    cfg = newcfg;
    dlog("configurazione predefinita (flash non disponibile)");
    log_config();
    return;
  }
  fatfs.begin(&flash);  // rilegge FAT e cartelle dopo le modifiche fatte dal PC
  size_t len = 0;
  File32 f = fatfs.open(CONFIG_FILE, O_RDONLY);
  if (f) {
    int r = f.read(text, sizeof(text) - 1);
    len = r > 0 ? r : 0;
    f.close();
  } else {
    dlog("config.txt non trovato: lo ricreo");
    write_text_file(CONFIG_FILE, DEFAULT_CONFIG, strlen(DEFAULT_CONFIG));
    len = strlen(DEFAULT_CONFIG);
    memcpy(text, DEFAULT_CONFIG, len);
  }
  text[len] = 0;

  int errors = parse_config(text, len, &newcfg, err, sizeof(err));
  cfg = newcfg;
  if (errors) {
    dlog("config.txt letto con %d errori (vedi errori.txt):", errors);
    char *save = nullptr;
    static char copy[1024];
    strncpy(copy, err, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    for (char *l = strtok_r(copy, "\r\n", &save); l; l = strtok_r(nullptr, "\r\n", &save)) dlog("  %s", l);
    static char out[1200];
    int n = snprintf(out, sizeof(out), "Errori in config.txt (le righe sbagliate sono state ignorate):\r\n\r\n%s", err);
    write_text_file(ERRORS_FILE, out, n > 0 ? n : 0);
  } else {
    dlog("config.txt letto, nessun errore");
    if (fatfs.exists(ERRORS_FILE)) fatfs.remove(ERRORS_FILE);
  }
  flash.syncBlocks();
  log_config();
}

static void show_drive() {
  if (!fs_ok) {
    dlog("unità non disponibile: flash non inizializzata");
    return;
  }
  flash.syncBlocks();
  drive_visible = true;
  dlog("unità di configurazione VISIBILE");
}

static void hide_drive(const char *why) {
  if (!drive_visible) return;
  drive_visible = false;
  flash.syncBlocks();
  reload_pending = true;
  reload_at = millis() + 300;
  dlog("unità di configurazione nascosta (%s), rileggo config.txt", why);
}

int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize) {
  return flash.readBlocks(lba, (uint8_t *) buffer, bufsize / 512) ? (int32_t) bufsize : -1;
}

int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  return flash.writeBlocks(lba, buffer, bufsize / 512) ? (int32_t) bufsize : -1;
}

void msc_flush_cb(void) {
  flash.syncBlocks();
}

bool msc_ready_cb(void) {
  return drive_visible;
}

bool msc_start_stop_cb(uint8_t power_condition, bool start, bool load_eject) {
  (void) power_condition;
  if (load_eject && !start) hide_drive("espulsa dal PC");
  return true;
}

//--------------------------------------------------------------------+
// Eventi dal mouse (core1 -> core0)
//--------------------------------------------------------------------+

#define MAX_SOURCES ((CFG_TUH_DEVICE_MAX + 1) * 4)

static queue_t ev_queue;
static volatile bool core0_ready = false, core1_alive = false;
static volatile uint32_t cnt_ok = 0, cnt_empty = 0, cnt_wrong_id = 0;

//--------------------------------------------------------------------+
// Core0: tasti, scorciatoie e report verso il PC
//--------------------------------------------------------------------+

static uint8_t src_buttons[MAX_SOURCES];
static uint8_t phys = 0, prev_phys = 0;   // tasti premuti fisicamente (unione di tutti i mouse)
static uint8_t act_down = 0;              // tasti la cui azione è attiva (tenuta premuta)
static uint8_t tap_pending = 0;           // azioni da premere e rilasciare subito
static uint8_t chord_wait = 0;            // laterali in attesa: potrebbero diventare la combinazione
static uint32_t chord_since[N_BUTTONS];
static bool combo_active = false, combo_fired = false;
static uint32_t combo_start = 0;

static int32_t acc_x = 0, acc_y = 0, acc_wheel = 0, acc_pan = 0;
static uint8_t sent_mouse_buttons = 0;
static kb_report_t sent_kb = {};
static uint16_t sent_media = 0;
static uint32_t led_off_at = 0;

static int8_t take_int8(int32_t &acc) {
  int32_t v = acc > 127 ? 127 : (acc < -127 ? -127 : acc);
  acc -= v;
  return (int8_t) v;
}

static uint8_t mapped_mask() {
  uint8_t m = 0;
  for (uint8_t b = 0; b < N_BUTTONS; b++) {
    if (cfg.buttons[b].type != ACT_NORMALE) m |= 1u << b;
  }
  return m;
}

static void handle_event(const mouse_event_t &ev) {
  if (ev.source < MAX_SOURCES) src_buttons[ev.source] = ev.buttons;
  uint8_t all = 0;
  for (uint8_t i = 0; i < MAX_SOURCES; i++) all |= src_buttons[i];
  if (all != phys) dlog("tasti mouse: %02x -> %02x", phys, all);
  phys = all;
  acc_x += ev.dx;
  acc_y += ev.dy;
  acc_wheel += ev.wheel;
  acc_pan += ev.pan;
  if (!drive_visible) {
    digitalWrite(LED_BUILTIN, HIGH);
    led_off_at = millis() + 30;
  }
}

static void toggle_drive() {
  if (drive_visible) hide_drive("combinazione laterali");
  else show_drive();
}

// Comandi dalla seriale di log: 'r' ristampa il log, 'c' apre/chiude l'unità (come la combinazione)
static void handle_serial_input() {
  while (Serial && Serial.available()) {
    int c = Serial.read();
    if (c == 'r' || c == 'R') log_sent = 0;
    if (c == 'c' || c == 'C') toggle_drive();
  }
}

static void process_buttons(uint32_t now) {
  uint8_t pressed = phys & ~prev_phys, released = prev_phys & ~phys;
  prev_phys = phys;
  uint8_t mapped = mapped_mask();

  // Combinazione: i due laterali premuti insieme. Annulla le loro azioni finché non vengono rilasciati.
  if ((phys & LAT_MASK) == LAT_MASK && !combo_active) {
    combo_active = true;
    combo_fired = false;
    combo_start = now;
    chord_wait &= ~LAT_MASK;
    tap_pending &= ~LAT_MASK;
    act_down &= ~LAT_MASK;
    dlog("laterali premuti insieme: tieni premuto %u s per aprire/chiudere la configurazione", cfg.open_combo_ms / 1000);
  }
  if (combo_active) {
    if (!combo_fired && (phys & LAT_MASK) == LAT_MASK && now - combo_start >= cfg.open_combo_ms) {
      combo_fired = true;
      toggle_drive();
    }
    if ((phys & LAT_MASK) == 0) combo_active = false;
    pressed &= ~LAT_MASK;
    released &= ~LAT_MASK;
  }

  for (uint8_t b = 0; b < N_BUTTONS; b++) {
    uint8_t bit = 1u << b;
    if (!(mapped & bit)) continue;
    if (pressed & bit) {
      if ((bit & LAT_MASK) && cfg.chord_ms) {
        chord_wait |= bit;
        chord_since[b] = now;
      } else {
        act_down |= bit;
      }
    }
    if (released & bit) {
      if (chord_wait & bit) {  // rilasciato prima della fine dell'attesa: premi e rilascia subito
        chord_wait &= ~bit;
        tap_pending |= bit;
      }
      act_down &= ~bit;
    }
    if ((chord_wait & bit) && now - chord_since[b] >= cfg.chord_ms) {
      chord_wait &= ~bit;
      act_down |= bit;
    }
  }
}

static kb_report_t wanted_keyboard(uint8_t active) {
  kb_report_t r = {};
  uint8_t n = 0;
  for (uint8_t b = 0; b < N_BUTTONS; b++) {
    const action_t &a = cfg.buttons[b];
    if (!(active & (1u << b)) || a.type != ACT_TASTI) continue;
    r.modifier |= a.modifier;
    for (uint8_t k = 0; k < a.nkeys; k++) {
      bool dup = false;
      for (uint8_t j = 0; j < n; j++) dup |= r.keys[j] == a.keys[k];
      if (!dup && n < 6) r.keys[n++] = a.keys[k];
    }
  }
  return r;
}

static uint16_t wanted_media(uint8_t active) {
  for (uint8_t b = 0; b < N_BUTTONS; b++) {
    if ((active & (1u << b)) && cfg.buttons[b].type == ACT_MEDIA) return cfg.buttons[b].media;
  }
  return 0;
}

static uint8_t type_mask(action_type_t t) {
  uint8_t m = 0;
  for (uint8_t b = 0; b < N_BUTTONS; b++) {
    if (cfg.buttons[b].type == t) m |= 1u << b;
  }
  return m;
}

static void send_reports() {
  if (!TinyUSBDevice.mounted()) {  // nessun PC (es. powerbank): non accumulare
    acc_x = acc_y = acc_wheel = acc_pan = 0;
    tap_pending = 0;
    return;
  }
  uint8_t active = act_down | tap_pending;
  kb_report_t kb = wanted_keyboard(active);
  uint16_t media = wanted_media(active);
  uint8_t mbuttons = phys & ~mapped_mask() & 0x1F;
  if (combo_active) mbuttons &= ~LAT_MASK;
  bool kb_changed = memcmp(&kb, &sent_kb, sizeof(kb)) != 0;
  bool mouse_changed = mbuttons != sent_mouse_buttons || acc_x || acc_y || acc_wheel || acc_pan;

  if (!kb_changed && media == sent_media && !mouse_changed) return;
  if (TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
    return;
  }
  if (!usb_hid.ready()) return;

  if (kb_changed) {
    bool new_keys = false;
    for (uint8_t k = 0; k < 6; k++) new_keys |= kb.keys[k] && !memchr(sent_kb.keys, kb.keys[k], 6);
    if ((kb.modifier & ~sent_kb.modifier) && new_keys) {
      // prima i modificatori da soli (es. Ctrl), poi Ctrl + tasto: più compatibile con tutte le app
      kb_report_t mod_only = sent_kb;
      mod_only.modifier = kb.modifier;
      usb_hid.keyboardReport(RID_KEYBOARD, mod_only.modifier, mod_only.keys);
      sent_kb = mod_only;
      return;
    }
    usb_hid.keyboardReport(RID_KEYBOARD, kb.modifier, kb.keys);
    sent_kb = kb;
    tap_pending &= ~type_mask(ACT_TASTI);  // il prossimo report rilascerà i tasti "tap"
    return;
  }
  if (media != sent_media) {
    usb_hid.sendReport16(RID_CONSUMER, media);
    sent_media = media;
    tap_pending &= ~type_mask(ACT_MEDIA);
    return;
  }
  int8_t x = take_int8(acc_x), y = take_int8(acc_y);
  int8_t w = take_int8(acc_wheel), p = take_int8(acc_pan);
  usb_hid.mouseReport(RID_MOUSE, mbuttons, x, y, w, p);
  sent_mouse_buttons = mbuttons;
}

// Ogni 5 s, solo se qualcosa è cambiato: quanti report sono arrivati dal mouse
static void log_status() {
  static uint32_t last = 0, p_ok = 0, p_empty = 0, p_wrong = 0;
  if (millis() - last < 5000) return;
  last = millis();
  uint32_t ok = cnt_ok, empty = cnt_empty, wrong = cnt_wrong_id;
  if (ok != p_ok || empty != p_empty || wrong != p_wrong) {
    dlog("report dal mouse: %lu validi, %lu vuoti, %lu con ID diverso", (unsigned long) ok, (unsigned long) empty,
         (unsigned long) wrong);
    p_ok = ok;
    p_empty = empty;
    p_wrong = wrong;
  }
}

static void update_led(uint32_t now) {
  if (drive_visible) {
    digitalWrite(LED_BUILTIN, (now / 500) & 1);  // lampeggio lento: unità di configurazione aperta
  } else if (led_off_at && (int32_t) (now - led_off_at) > 0) {
    digitalWrite(LED_BUILTIN, LOW);
    led_off_at = 0;
  }
}

void setup() {
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.setManufacturerDescriptor("smart-mouse");
  TinyUSBDevice.setProductDescriptor("Smart Mouse v1");
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  queue_init(&ev_queue, sizeof(mouse_event_t), 64);
  dlog("=== Smart Mouse v1 ===");

  // le scritture su flash mettono in pausa core1: deve essere già avviato
  while (!core1_alive) delay(1);
  fs_ok = storage_begin();
  load_config();

  usb_msc.setID("smartmse", "Configurazione", "1.0");
  usb_msc.setCapacity(fs_ok ? flash.size() / 512 : 0, 512);
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setReadyCallback(msc_ready_cb);
  usb_msc.setStartStopCallback(msc_start_stop_cb);
  usb_msc.setUnitReady(false);
  usb_msc.begin();
  usb_hid.begin();
  // interfacce aggiunte dopo l'avvio dello stack: il PC deve rileggere i descriptor
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  core0_ready = true;
}

void loop() {
  uint32_t now = millis();
  mouse_event_t ev;
  while (queue_try_remove(&ev_queue, &ev)) handle_event(ev);
  process_buttons(now);
  send_reports();
  if (reload_pending && (int32_t) (now - reload_at) >= 0) {
    reload_pending = false;
    load_config();
  }
  update_led(now);
  log_status();
  handle_serial_input();
  log_flush_to_serial();
}

//--------------------------------------------------------------------+
// Core1: host USB verso il mouse
//--------------------------------------------------------------------+

static mouse_layout_t layouts[CFG_TUH_DEVICE_MAX + 1][4];
static uint32_t reports_logged = 0, queue_drops = 0;

void setup1() {
  core1_alive = true;
  while (!core0_ready) delay(1);

  uint32_t cpu_hz = clock_get_hz(clk_sys);
  if (cpu_hz % 12000000UL) {
    dlog("ERRORE: clock CPU %lu Hz, deve essere multiplo di 12 MHz (compila con freq=120)", (unsigned long) cpu_hz);
    while (1) delay(100);
  }

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = PIN_USB_HOST_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);  // report completi: tasti laterali e rotella
  USBHost.begin(1);
  dlog("host USB avviato su GP%d/GP%d", PIN_USB_HOST_DP, PIN_USB_HOST_DP + 1);
}

void loop1() {
  USBHost.task();
}

static void log_field(const char *name, const hid_field_t &f) {
  if (f.present) dlog("  %-8s bit %3u, %2u bit%s", name, f.bit_offset, f.bit_size, f.is_signed ? ", con segno" : "");
}

void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  dlog("collegato dispositivo %u: %04x:%04x", daddr, vid, pid);
}

void tuh_umount_cb(uint8_t daddr) {
  dlog("scollegato dispositivo %u", daddr);
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, uint8_t const *desc_report, uint16_t desc_len) {
  if (daddr > CFG_TUH_DEVICE_MAX || idx >= 4) return;
  mouse_layout_t *lay = &layouts[daddr][idx];
  uint8_t proto = tuh_hid_interface_protocol(daddr, idx);
  bool parsed = parse_mouse_layout(desc_report, desc_len, lay);

  if (!parsed) {
    if (proto == HID_ITF_PROTOCOL_MOUSE && tuh_hid_get_protocol(daddr, idx) == HID_PROTOCOL_BOOT) {
      mouse_layout_boot(lay);
      dlog("HID %u/%u: mouse in boot protocol (layout standard)", daddr, idx);
    } else {
      dlog("HID %u/%u: non è un mouse (protocollo %u, descriptor %u byte), ignorato", daddr, idx, proto, desc_len);
      return;
    }
  } else {
    dlog("HID %u/%u: mouse, descriptor %u byte, report ID %s%u", daddr, idx, desc_len,
         lay->uses_report_id ? "" : "nessuno/", lay->report_id);
    for (uint8_t b = 0; b < 8; b++) {
      char name[10];
      snprintf(name, sizeof(name), "tasto %u", b + 1);
      log_field(name, lay->buttons[b]);
    }
    log_field("X", lay->x);
    log_field("Y", lay->y);
    log_field("rotella", lay->wheel);
    log_field("pan", lay->pan);
  }

  if (!tuh_hid_receive_report(daddr, idx)) dlog("HID %u/%u: impossibile richiedere i report", daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx) {
  if (daddr > CFG_TUH_DEVICE_MAX || idx >= 4) return;
  bool was_mouse = layouts[daddr][idx].valid;
  layouts[daddr][idx].valid = false;
  dlog("HID %u/%u scollegato", daddr, idx);
  if (was_mouse) {
    mouse_event_t ev = {(uint8_t) (daddr * 4 + idx), 0, 0, 0, 0, 0};  // rilascia i tasti di questo mouse
    queue_try_add(&ev_queue, &ev);
  }
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, uint8_t const *report, uint16_t len) {
  // TinyUSB chiama questa funzione anche quando il trasferimento fallisce: arriva con len = 0
  if (len == 0) {
    if (++cnt_empty <= 3) dlog("report VUOTO da %u/%u (trasferimento fallito?)", daddr, idx);
  }
  if (daddr <= CFG_TUH_DEVICE_MAX && idx < 4 && layouts[daddr][idx].valid && len > 0) {
    const mouse_layout_t &lay = layouts[daddr][idx];
    const uint8_t *data = report;
    uint16_t dlen = len;
    bool ok = true;
    if (lay.uses_report_id) {
      ok = report[0] == lay.report_id;
      data++;
      dlen--;
      if (!ok && ++cnt_wrong_id <= 3) dlog("report con ID %u ignorato (atteso %u, %u byte)", report[0], lay.report_id, len);
    }
    if (ok) {
      cnt_ok++;
      mouse_event_t ev = {};
      ev.source = daddr * 4 + idx;
      for (uint8_t b = 0; b < 8; b++) {
        if (hid_get_field(data, dlen, lay.buttons[b])) ev.buttons |= 1u << b;
      }
      ev.dx = hid_get_field(data, dlen, lay.x);
      ev.dy = hid_get_field(data, dlen, lay.y);
      ev.wheel = hid_get_field(data, dlen, lay.wheel);
      ev.pan = hid_get_field(data, dlen, lay.pan);
      if (!queue_try_add(&ev_queue, &ev) && (++queue_drops % 100) == 1) dlog("coda eventi piena (%lu persi)", (unsigned long) queue_drops);

      if (reports_logged < 5) {
        reports_logged++;
        char hex[3 * 12 + 1];
        int k = 0;
        for (uint16_t i = 0; i < len && i < 12; i++) k += snprintf(hex + k, sizeof(hex) - k, "%02x ", report[i]);
        dlog("report %u/%u: %s-> tasti %02x X %d Y %d rotella %d", daddr, idx, hex, ev.buttons, ev.dx, ev.dy, ev.wheel);
      }
    }
  }
  tuh_hid_receive_report(daddr, idx);
}
