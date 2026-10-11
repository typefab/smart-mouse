// Smart Mouse v0 (vedi 06-firmware-v0.md)
//
// Il Pico sta in mezzo tra mouse e PC:
//   mouse (anche dietro un hub) -> host USB via PIO su GP0 (D+) / GP1 (D-) -> core1
//   core0 -> USB nativa del Pico -> PC (mouse + tastiera + seriale di log)
//
// Il movimento, la rotella e i tasti non mappati passano al PC così come sono.
// I tasti mappati (per ora: i due laterali) non arrivano al PC come tasti del mouse
// ma diventano scorciatoie da tastiera (tabella SHORTCUTS qui sotto).
//
// Log: seriale USB del Pico a 115200 (invia 'r' per ristamparlo dall'inizio).
// Scheda: rp2040:rp2040:rpipico, USB stack "Adafruit TinyUSB", CPU 120 MHz.

#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "pico/mutex.h"
#include "pico/util/queue.h"
#include "hid_parser.h"
#include "smart_mouse_types.h"

#define PIN_USB_HOST_DP 0  // D- = GP1

//--------------------------------------------------------------------+
// Scorciatoie (in v1 verranno lette da config.txt)
//--------------------------------------------------------------------+

struct shortcut_t {
  uint8_t button;    // bit del tasto del mouse (MOUSE_BUTTON_...)
  uint8_t modifier;  // KEYBOARD_MODIFIER_...
  uint8_t key;       // HID_KEY_...
  const char *name;
};

static const shortcut_t SHORTCUTS[] = {
  {MOUSE_BUTTON_FORWARD,  KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_C, "laterale avanti (tasto 5) -> Ctrl+C"},
  {MOUSE_BUTTON_BACKWARD, KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_V, "laterale indietro (tasto 4) -> Ctrl+V"},
};
static const uint8_t N_SHORTCUTS = sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0]);

//--------------------------------------------------------------------+
// Dispositivo USB verso il PC: tastiera (ID 1) + mouse (ID 2)
//--------------------------------------------------------------------+

enum { RID_KEYBOARD = 1, RID_MOUSE = 2 };

static uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE)),
};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE, 1, false);
Adafruit_USBH_Host USBHost;

//--------------------------------------------------------------------+
// Log condiviso tra i due core
//--------------------------------------------------------------------+

static char log_buf[16 * 1024];
static volatile size_t log_len = 0;
static size_t log_sent = 0;
static bool log_full = false;
auto_init_mutex(log_mtx);

static void dlog(const char *fmt, ...) {
  char line[160];
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
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'r' || c == 'R') log_sent = 0;
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
// Eventi dal mouse (core1 -> core0)
//--------------------------------------------------------------------+

#define MAX_SOURCES ((CFG_TUH_DEVICE_MAX + 1) * 4)

static queue_t ev_queue;
static volatile bool core0_ready = false;

//--------------------------------------------------------------------+
// Core0: dispositivo USB verso il PC
//--------------------------------------------------------------------+

static uint8_t src_buttons[MAX_SOURCES];
static uint8_t phys_buttons = 0;  // tasti premuti (unione di tutti i mouse)
static uint8_t mapped_mask = 0;   // tasti trasformati in scorciatoie
static int32_t acc_x = 0, acc_y = 0, acc_wheel = 0, acc_pan = 0;
static bool mouse_buttons_dirty = false, kb_dirty = false;
static uint8_t kb_sent_modifier = 0;
static uint32_t led_off_at = 0;

static int8_t take_int8(int32_t &acc) {
  int32_t v = acc > 127 ? 127 : (acc < -127 ? -127 : acc);
  acc -= v;
  return (int8_t) v;
}

void setup() {
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.setManufacturerDescriptor("smart-mouse");
  TinyUSBDevice.setProductDescriptor("Smart Mouse v0");
  Serial.begin(115200);
  usb_hid.begin();
  // l'interfaccia HID è stata aggiunta dopo l'avvio dello stack: il PC deve rileggere i descriptor
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  pinMode(LED_BUILTIN, OUTPUT);
  queue_init(&ev_queue, sizeof(mouse_event_t), 64);
  for (uint8_t i = 0; i < N_SHORTCUTS; i++) mapped_mask |= SHORTCUTS[i].button;

  dlog("=== Smart Mouse v0 ===");
  for (uint8_t i = 0; i < N_SHORTCUTS; i++) dlog("scorciatoia: %s", SHORTCUTS[i].name);
  core0_ready = true;
}

static void handle_event(const mouse_event_t &ev) {
  if (ev.source < MAX_SOURCES) src_buttons[ev.source] = ev.buttons;
  uint8_t all = 0;
  for (uint8_t i = 0; i < MAX_SOURCES; i++) all |= src_buttons[i];

  uint8_t changed = all ^ phys_buttons;
  if (changed) {
    dlog("tasti mouse: %02x -> %02x", phys_buttons, all);
    for (uint8_t i = 0; i < N_SHORTCUTS; i++) {
      if (changed & SHORTCUTS[i].button) {
        dlog("%s %s", (all & SHORTCUTS[i].button) ? "PREMUTO " : "rilasciato", SHORTCUTS[i].name);
        kb_dirty = true;
      }
    }
    if (changed & ~mapped_mask) mouse_buttons_dirty = true;
    phys_buttons = all;
  }
  acc_x += ev.dx;
  acc_y += ev.dy;
  acc_wheel += ev.wheel;
  acc_pan += ev.pan;
  digitalWrite(LED_BUILTIN, HIGH);
  led_off_at = millis() + 30;
}

static void send_reports() {
  bool pending = kb_dirty || mouse_buttons_dirty || acc_x || acc_y || acc_wheel || acc_pan;
  if (!pending) return;

  if (!TinyUSBDevice.mounted()) {  // nessun PC (es. powerbank): non accumulare movimento
    acc_x = acc_y = acc_wheel = acc_pan = 0;
    mouse_buttons_dirty = false;
    return;
  }
  if (TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
    return;
  }
  if (!usb_hid.ready()) return;

  if (kb_dirty) {
    uint8_t modifier = 0, keys[6] = {0}, n = 0;
    for (uint8_t i = 0; i < N_SHORTCUTS; i++) {
      if (phys_buttons & SHORTCUTS[i].button) {
        modifier |= SHORTCUTS[i].modifier;
        if (n < 6) keys[n++] = SHORTCUTS[i].key;
      }
    }
    if ((modifier & ~kb_sent_modifier) && n) {
      // prima i modificatori da soli (Ctrl), poi Ctrl + tasto: più compatibile con tutte le app
      uint8_t none[6] = {0};
      usb_hid.keyboardReport(RID_KEYBOARD, modifier, none);
      kb_sent_modifier = modifier;
      return;
    }
    usb_hid.keyboardReport(RID_KEYBOARD, modifier, keys);
    kb_sent_modifier = modifier;
    kb_dirty = false;
    return;
  }

  int8_t x = take_int8(acc_x), y = take_int8(acc_y);
  int8_t w = take_int8(acc_wheel), p = take_int8(acc_pan);
  usb_hid.mouseReport(RID_MOUSE, phys_buttons & ~mapped_mask & 0x1F, x, y, w, p);
  mouse_buttons_dirty = false;
}

static volatile uint32_t cnt_ok = 0, cnt_empty = 0, cnt_wrong_id = 0;  // scritti da core1, letti qui

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

void loop() {
  mouse_event_t ev;
  while (queue_try_remove(&ev_queue, &ev)) handle_event(ev);
  send_reports();
  if (led_off_at && (int32_t) (millis() - led_off_at) > 0) {
    digitalWrite(LED_BUILTIN, LOW);
    led_off_at = 0;
  }
  log_status();
  log_flush_to_serial();
}

//--------------------------------------------------------------------+
// Core1: host USB verso il mouse
//--------------------------------------------------------------------+

static mouse_layout_t layouts[CFG_TUH_DEVICE_MAX + 1][4];
static uint32_t reports_logged = 0, queue_drops = 0;

void setup1() {
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
