import machine
import utime

# Define a list of GPIO pins you want to monitor
pins = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27]

# Set up each pin as an input with a pull-up resistor
switches = [(pin, machine.Pin(pin, machine.Pin.IN, machine.Pin.PULL_UP)) for pin in pins]

print("Press a switch...")

while True:
    for pin, switch in switches: 
        if not switch.value():  # Check if the pin is pulled low
            print(f"Switch connected to pin {pin} pressed")
            utime.sleep(0.5)  # Debounce delay