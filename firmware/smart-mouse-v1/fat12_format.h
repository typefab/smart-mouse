// Formattazione FAT12 minima della partizione FS del Pico (settori da 512 byte).
// Serve perché il FatFormatter di SdFat crea solo FAT16/FAT32, troppo grandi per pochi KB.
#pragma once
#include <stdint.h>
#include <string.h>
#include "Adafruit_SPIFlash.h"

static void put16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF;
}

static bool format_fat12(Adafruit_SPIFlash &dev, const char *label, uint32_t serial) {
  const uint32_t sectors = dev.size() / 512;
  const uint8_t spc = sectors <= 4000 ? 1 : (sectors <= 8000 ? 2 : 4);  // cluster < 4085 => FAT12
  const uint16_t reserved = 1, root_entries = 64;
  const uint8_t nfats = 2;
  const uint16_t root_sectors = root_entries * 32 / 512;
  const uint16_t fat_sectors = ((sectors / spc + 2) * 3 / 2 + 511) / 512;
  if (sectors < 64 || sectors > 65535) return false;

  uint8_t s[512];

  // Settore 0: boot sector con BPB
  memset(s, 0, sizeof(s));
  s[0] = 0xEB; s[1] = 0x3C; s[2] = 0x90;
  memcpy(s + 3, "MSDOS5.0", 8);
  put16(s + 11, 512);
  s[13] = spc;
  put16(s + 14, reserved);
  s[16] = nfats;
  put16(s + 17, root_entries);
  put16(s + 19, (uint16_t) sectors);
  s[21] = 0xF8;  // disco fisso
  put16(s + 22, fat_sectors);
  put16(s + 24, 32);  // settori per traccia (non usato)
  put16(s + 26, 2);   // testine (non usato)
  s[36] = 0x80;
  s[38] = 0x29;
  put32(s + 39, serial);
  char lab[11];
  memset(lab, ' ', sizeof(lab));
  memcpy(lab, label, strnlen(label, sizeof(lab)));
  memcpy(s + 43, lab, 11);
  memcpy(s + 54, "FAT12   ", 8);
  s[510] = 0x55; s[511] = 0xAA;
  if (!dev.writeBlocks(0, s, 1)) return false;

  // Tabelle FAT: primi 3 byte riservati, il resto libero
  for (uint8_t f = 0; f < nfats; f++) {
    for (uint16_t k = 0; k < fat_sectors; k++) {
      memset(s, 0, sizeof(s));
      if (k == 0) { s[0] = 0xF8; s[1] = 0xFF; s[2] = 0xFF; }
      if (!dev.writeBlocks(reserved + f * fat_sectors + k, s, 1)) return false;
    }
  }

  // Cartella radice: solo l'etichetta del volume
  uint32_t root_start = reserved + nfats * fat_sectors;
  for (uint16_t k = 0; k < root_sectors; k++) {
    memset(s, 0, sizeof(s));
    if (k == 0) {
      memcpy(s, lab, 11);
      s[11] = 0x08;  // attributo: etichetta di volume
    }
    if (!dev.writeBlocks(root_start + k, s, 1)) return false;
  }
  return dev.syncBlocks();
}
