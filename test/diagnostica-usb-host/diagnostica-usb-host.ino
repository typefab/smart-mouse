// Diagnostica host USB del Pico (vedi 05-hub-usb.md)
//
// - Host USB via PIO su GP0 (D+) / GP1 (D-), come HID Remapper single Pico.
// - Scrive un log sulla seriale USB del Pico (115200). Il log viene conservato in memoria:
//   aprendo la porta più tardi si riceve tutto dall'inizio; inviando 'r' viene ristampato.
// - Riporta: livelli di D+/D- all'avvio, dispositivi montati (VID/PID, velocità, classe),
//   interfacce HID e primi report ricevuti, cambi di stato di GP3 (contatto indesiderato).
// - Compilando con -DCFG_TUSB_DEBUG=2 i log interni di TinyUSB escono da Serial2 su GP4
//   (verso l'ESP32 con lo sketch esp32_echo, che li ripete sulla sua seriale USB).
//
// Scheda: rp2040:rp2040:rpipico, USB stack "Adafruit TinyUSB", CPU 120 MHz.

#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "pico/mutex.h"

#define PIN_USB_HOST_DP 0   // D- = GP1
#define PIN_GP3         3

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
  int s = snprintf(stamp, sizeof(stamp), "[%6.2fs] ", millis() / 1000.0);
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

static const char *speed_name(tusb_speed_t sp) {
  switch (sp) {
    case TUSB_SPEED_LOW:  return "low-speed (1,5 Mbit/s)";
    case TUSB_SPEED_FULL: return "full-speed (12 Mbit/s)";
    case TUSB_SPEED_HIGH: return "high-speed";
    default:              return "sconosciuta";
  }
}

//--------------------------------------------------------------------+
// Core0: seriale USB verso il PC + controllo GP3
//--------------------------------------------------------------------+

static volatile bool core0_ready = false;

void setup() {
  Serial.begin(115200);
#if CFG_TUSB_DEBUG
  Serial2.setTX(4);
  Serial2.setRX(5);
#endif
  pinMode(PIN_GP3, INPUT_PULLUP);
  dlog("=== Diagnostica host USB Pico (D+ = GP%d, D- = GP%d) ===", PIN_USB_HOST_DP, PIN_USB_HOST_DP + 1);
#if CFG_TUSB_DEBUG
  dlog("Log interni TinyUSB attivi (livello %d) su Serial2 / GP4", CFG_TUSB_DEBUG);
#endif
  core0_ready = true;
}

void loop() {
  static int gp3_prev = -1;
  static uint32_t gp3_changes = 0, gp3_last_report = 0;
  int gp3 = digitalRead(PIN_GP3);
  if (gp3 != gp3_prev) {
    if (gp3_prev != -1) {
      gp3_changes++;
      if (gp3_changes <= 10) dlog("GP3 (pin 5) -> %s", gp3 ? "alto (libero)" : "BASSO (qualcosa lo tocca)");
    }
    gp3_prev = gp3;
  }
  if (gp3_changes > 10 && millis() - gp3_last_report > 2000) {
    dlog("GP3: %lu cambi di stato finora", (unsigned long) gp3_changes);
    gp3_last_report = millis();
  }

  static uint32_t last_status = 0;
  if (millis() - last_status > 5000) {
    last_status = millis();
    int n = 0;
    for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; a++) if (tuh_mounted(a)) n++;
    dlog("stato: %d dispositivi montati", n);
  }

  if (Serial) {
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
    Serial.flush();
  } else {
    log_sent = 0;  // alla prossima apertura della porta ristampa tutto
  }
}

//--------------------------------------------------------------------+
// Core1: host USB
//--------------------------------------------------------------------+

void setup1() {
  while (!core0_ready) delay(1);

  // Livelli delle linee prima di avviare l'host (pull-down attivi come fa l'host)
  gpio_init(PIN_USB_HOST_DP);
  gpio_init(PIN_USB_HOST_DP + 1);
  gpio_set_dir(PIN_USB_HOST_DP, GPIO_IN);
  gpio_set_dir(PIN_USB_HOST_DP + 1, GPIO_IN);
  gpio_pull_down(PIN_USB_HOST_DP);
  gpio_pull_down(PIN_USB_HOST_DP + 1);
  delay(50);
  int dp = gpio_get(PIN_USB_HOST_DP), dm = gpio_get(PIN_USB_HOST_DP + 1);
  dlog("linee all'avvio: D+ = %d, D- = %d -> %s", dp, dm,
       (dp && !dm) ? "dispositivo FULL-speed collegato (es. hub)"
       : (!dp && dm) ? "dispositivo LOW-speed collegato (es. mouse diretto)"
       : (!dp && !dm) ? "NESSUN dispositivo collegato (o non alimentato)"
       : "entrambe alte: anomalia (corto tra D+ e D-?)");

  uint32_t cpu_hz = clock_get_hz(clk_sys);
  dlog("clock CPU = %lu Hz", (unsigned long) cpu_hz);
  if (cpu_hz % 12000000UL) {
    dlog("ERRORE: il clock deve essere multiplo di 12 MHz (compila con freq=120)");
    while (1) delay(100);
  }

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = PIN_USB_HOST_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);
  dlog("host USB avviato, in attesa di dispositivi...");
}

void loop1() {
  USBHost.task();
}

//--------------------------------------------------------------------+
// Callback TinyUSB host (girano su core1)
//--------------------------------------------------------------------+

static tusb_desc_device_t desc_device[CFG_TUH_DEVICE_MAX + 1];

static void device_descriptor_cb(tuh_xfer_t *xfer) {
  uint8_t const a = xfer->daddr;
  if (xfer->result != XFER_RESULT_SUCCESS) {
    dlog("dispositivo %u: lettura descriptor FALLITA (risultato %d)", a, xfer->result);
    return;
  }
  tusb_desc_device_t *d = &desc_device[a];
  dlog("dispositivo %u: VID %04x PID %04x, bcdUSB %04x, classe %u%s, bMaxPacketSize0 %u",
       a, d->idVendor, d->idProduct, d->bcdUSB, d->bDeviceClass,
       d->bDeviceClass == TUSB_CLASS_HUB ? " (HUB)" : "", d->bMaxPacketSize0);
}

void tuh_mount_cb(uint8_t daddr) {
  dlog("MONTATO dispositivo %u, velocità %s", daddr, speed_name(tuh_speed_get(daddr)));
  if (daddr <= CFG_TUH_DEVICE_MAX) {
    tuh_descriptor_get_device(daddr, &desc_device[daddr], 18, device_descriptor_cb, 0);
  }
}

void tuh_umount_cb(uint8_t daddr) {
  dlog("SMONTATO dispositivo %u", daddr);
}

static uint32_t hid_reports = 0;

void tuh_hid_mount_cb(uint8_t daddr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  (void) desc_report;
  static const char *proto[] = {"nessuno", "tastiera", "mouse"};
  uint8_t p = tuh_hid_interface_protocol(daddr, instance);
  dlog("HID montato: dispositivo %u, interfaccia %u, protocollo %s, descriptor %u byte",
       daddr, instance, p < 3 ? proto[p] : "?", desc_len);
  if (!tuh_hid_receive_report(daddr, instance)) {
    dlog("HID: impossibile richiedere i report");
  }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t instance) {
  dlog("HID smontato: dispositivo %u, interfaccia %u", daddr, instance);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t instance, uint8_t const *report, uint16_t len) {
  hid_reports++;
  if (hid_reports <= 30 || hid_reports % 100 == 0) {
    char hex[3 * 16 + 1];
    int k = 0;
    for (uint16_t i = 0; i < len && i < 16; i++) k += snprintf(hex + k, sizeof(hex) - k, "%02x ", report[i]);
    hex[k] = 0;
    dlog("report HID #%lu da %u/%u (%u byte): %s", (unsigned long) hid_reports, daddr, instance, len, hex);
  }
  tuh_hid_receive_report(daddr, instance);
}
