// Parser minimo del report descriptor HID: trova dove stanno, nel report del mouse,
// i tasti 1..8, X, Y, rotella e rotella orizzontale (AC Pan).
#pragma once
#include <stdint.h>
#include <string.h>

struct hid_field_t {
  uint16_t bit_offset;
  uint8_t bit_size;
  bool is_signed;
  bool present;
};

struct mouse_layout_t {
  bool valid;
  bool uses_report_id;
  uint8_t report_id;
  hid_field_t buttons[8];
  hid_field_t x, y, wheel, pan;
};

static uint32_t hid_item_u(const uint8_t *p, uint8_t size) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < size; i++) v |= (uint32_t) p[i] << (8 * i);
  return v;
}

static int32_t hid_item_s(const uint8_t *p, uint8_t size) {
  uint32_t v = hid_item_u(p, size);
  if (size > 0 && size < 4) {
    uint32_t sign = 1u << (8 * size - 1);
    if (v & sign) v |= ~((sign << 1) - 1);
  }
  return (int32_t) v;
}

// Layout del "boot protocol": tasti nel byte 0, X, Y e rotella nei byte successivi.
static void mouse_layout_boot(mouse_layout_t *out) {
  memset(out, 0, sizeof(*out));
  for (uint8_t i = 0; i < 5; i++) out->buttons[i] = {i, 1, false, true};
  out->x = {8, 8, true, true};
  out->y = {16, 8, true, true};
  out->wheel = {24, 8, true, true};
  out->valid = true;
}

static bool parse_mouse_layout(const uint8_t *desc, uint16_t len, mouse_layout_t *out) {
  memset(out, 0, sizeof(*out));
  if (!desc || !len) return false;

  struct globals_t {
    uint16_t usage_page;
    int32_t logical_min;
    uint8_t report_size;
    uint8_t report_id;
    uint16_t report_count;
  } g = {}, stack[4];
  uint8_t sp = 0;

  uint32_t usages[16];
  uint8_t n_usages = 0;
  uint32_t usage_min = 0, usage_max = 0;
  bool has_range = false;

  static uint16_t offsets[256];  // posizione (in bit) raggiunta in ogni report, per report ID
  memset(offsets, 0, sizeof(offsets));

  uint8_t depth = 0, mouse_depth = 0;
  bool mouse_done = false, rid_set = false;

  uint16_t i = 0;
  while (i < len) {
    uint8_t b = desc[i++];
    if (b == 0xFE) {  // long item: si salta
      if (i + 1 >= len) break;
      i += 2 + desc[i];
      continue;
    }
    uint8_t size = b & 3;
    if (size == 3) size = 4;
    uint8_t type = (b >> 2) & 3, tag = b >> 4;
    if (i + size > len) break;
    const uint8_t *data = desc + i;
    i += size;
    uint32_t uval = hid_item_u(data, size);

    if (type == 1) {  // global
      switch (tag) {
        case 0x0: g.usage_page = uval; break;
        case 0x1: g.logical_min = hid_item_s(data, size); break;
        case 0x7: g.report_size = uval; break;
        case 0x8: g.report_id = uval; out->uses_report_id = true; break;
        case 0x9: g.report_count = uval; break;
        case 0xA: if (sp < 4) stack[sp++] = g; break;
        case 0xB: if (sp > 0) g = stack[--sp]; break;
      }
    } else if (type == 2) {  // local
      uint32_t full = (size == 4) ? uval : (((uint32_t) g.usage_page << 16) | uval);
      switch (tag) {
        case 0x0: if (n_usages < 16) usages[n_usages++] = full; break;
        case 0x1: usage_min = full; has_range = true; break;
        case 0x2: usage_max = full; has_range = true; break;
      }
    } else if (type == 0) {  // main
      if (tag == 0xA) {  // Collection
        depth++;
        // Collection(Application) con Usage(Generic Desktop / Mouse)
        if (uval == 0x01 && n_usages && usages[0] == 0x00010002 && !mouse_depth && !mouse_done) {
          mouse_depth = depth;
        }
      } else if (tag == 0xC) {  // End Collection
        if (mouse_depth && depth == mouse_depth) {
          mouse_depth = 0;
          if (out->x.present) mouse_done = true;
        }
        if (depth) depth--;
      } else if (tag == 0x8) {  // Input
        bool is_const = uval & 0x01;
        bool is_variable = uval & 0x02;
        uint16_t &off = offsets[g.report_id];
        for (uint16_t k = 0; k < g.report_count; k++) {
          bool usable = !is_const && is_variable && mouse_depth && !mouse_done &&
                        (!rid_set || g.report_id == out->report_id);
          if (usable) {
            uint32_t u = 0;
            if (n_usages) {
              u = usages[k < n_usages ? k : n_usages - 1];
            } else if (has_range && usage_min + k <= usage_max) {
              u = usage_min + k;
            }
            hid_field_t f = {off, g.report_size, g.logical_min < 0, true};
            uint16_t page = u >> 16, id = u & 0xFFFF;
            bool used = true;
            if (page == 0x09 && id >= 1 && id <= 8) out->buttons[id - 1] = f;
            else if (page == 0x01 && id == 0x30) out->x = f;
            else if (page == 0x01 && id == 0x31) out->y = f;
            else if (page == 0x01 && id == 0x38) out->wheel = f;
            else if (page == 0x0C && id == 0x238) out->pan = f;
            else used = false;
            if (used && !rid_set) {
              out->report_id = g.report_id;
              rid_set = true;
            }
          }
          off += g.report_size;
        }
      }
      // Output e Feature non interessano; ogni main item azzera le variabili locali
      n_usages = 0;
      has_range = false;
      usage_min = usage_max = 0;
    }
  }

  out->valid = out->x.present && out->y.present;
  return out->valid;
}

// Legge un campo dal report (senza il byte di report ID).
static int32_t hid_get_field(const uint8_t *data, uint16_t len, const hid_field_t &f) {
  if (!f.present || f.bit_size == 0 || f.bit_size > 32) return 0;
  uint32_t v = 0;
  for (uint8_t b = 0; b < f.bit_size; b++) {
    uint32_t bit = f.bit_offset + b;
    if ((bit >> 3) >= len) break;
    if (data[bit >> 3] & (1u << (bit & 7))) v |= 1u << b;
  }
  if (f.is_signed && f.bit_size < 32 && (v & (1u << (f.bit_size - 1)))) v |= ~((1u << f.bit_size) - 1);
  return (int32_t) v;
}
