# Test UART Pico <-> ESP32 (vedi 02-pico-esp32.md)
# UART1: TX = GP4 (pin 6) -> GPIO16 ESP32, RX = GP5 (pin 7) <- GPIO17 ESP32
from machine import UART, Pin
import time

u = UART(1, 115200, tx=Pin(4), rx=Pin(5), timeout=200)
time.sleep(0.1)
u.read()  # svuota eventuali residui

ok = 0
for i in range(5):
    u.write("ping %d\n" % i)
    risposta = u.readline()
    print("inviato ping %d -> %s" % (i, risposta))
    if risposta and ("ping %d" % i) in risposta.decode():
        ok += 1
    time.sleep(0.2)

print("RISULTATO: %d/5 risposte corrette" % ok)
