# SimpleSerialGas
Impulse Counter for my Gas meter with wired serial connection to HomeAssistant

# What does it do?

This is a minimal microcontroller-based impulse counter that tracks the revolutions of my gas meter and exposes the current count via __wired__ serial connection to my home automation system (Home Assistant).

I've chosen an 8-pin SOIC ATtiny402 microcontroller because of its very low power consumption, wide VCC range (1V8-5V), hardware serial support, small physical size and mininmal external circuitry (read: none required apart from maybe a blocking capacitor).

# Basic operation

On one side, the gas meter revolutions are picked up by a simple Reed switch (to GND). In my case, the 0.001m³ wheel (i.e. 1000 per m³) triggers an impulse whenever the "9" digit is reached and left. On the "9", the Reed switch becomes "closed". The reed switch reverts to "open" on the transition from "9" to "0".

For visualization, a LED attached to the microcontroller flashes for about 1/10th of a second on that 9 -> 0 impulse edge. As only the edge is detected, the LED will not light up permanently even when the meter remains stationary for an extended period of time on any digit.

As the last meter wheel produces one impulse per revolution the counter is granular to 0.01m³ (two fractional digits).

The microcontroller maintains the current count value in SRAM. Normal power supply of the µC is via the serial cable, powered by the USB Serial Adapter from the home automation system. A simple 3V coin battery (e.g. CR2032) acts as a backup energy source, should the serial power become interrupted (HA system down).

# Energy Consumption and Power Supply

The microcontroller is in Standby sleep mode most of the time. Energy consumption in this mode is pretty small, around 1,2 µA.

At regular intervals of about 128ms, powered by the built-in 32kHz ULP oscillator, the device wakes up and checks the reed switch input. Some minimal de-bouncing is done on the (mechanical) input so that two consecutive identical readings are required to detect the (rising) edge of the logical signal. The transition from 9 to 0 on the meter wheel corresponds to a Low-to-High transition on the pin.

On the serial side, the µC hardware serial support with start-of-frame detection is used to wake up the unit. Serial reception is interrupt-driven and buffered by the used Arduino core (https://github.com/SpenceKonde/megaTinyCore). Actual interpretation of the received serial characters is performed in the regular 128ms interval. Power down mode (600nA) cannot be used as serial frame detection on ATtiny only works in Standby sleep mode.

Two schottky diodes are used to connect the battery and serial line VSS to form VCC, respectively. One diode goes from the positive pole of the battery to VCC, another from the serial line's VSS to VCC. This way, the microcontroller is powered from whichever source provides higher voltage. In normal operation, the serial connection's VSS is used (5V or even 3V3, depending on USB adaptor jumper settings), with the (3V) battery being a backup only. The diodes prevent the PC from being powered by the battery (or an attempt to do that...) and conversely normal VSS cannot "charge" the battery, which must not be done.

# The serial interface

The unit remains silent unless a properly formatted command is received from the control unit, e.g. the Home Automation system. The serial interface is hard-coded to 57600 baud 8N1. It turned out that any faster baud rate appears to not work with the wake-on-serial approach being used. In any case, the interface speed is not crucial (recall that commands are executed only in a 128ms raster, anyway) and might well be lowered in code.

Only three commands are available. Each command is terminated with a CR character (\\r). The unit replies with a single-style reply in all cases.

- Q:\<ID\>
  - Queries the current counter value.

- S:\<ID\>:\<new numerical value\>
  - Sets the counter value. The idea here is to power up the unit via battery, hook it up the your PC via some USB Serial adaptor, then set the meter value once using this command. Then disconnect from the PC, re-attach to your Home Automation system and Gas meter, then have the HA system poll for the counter value at regular intervals.

- I:*:\<new ID digit 0-9\>
  - Default ID is 0. Use this to set another ID. See below note on "over-engineering"... For a point-to-point serial connection there is no need to change the ID, really.

The reply, in all cases, is just the current counter value:

- C:\<ID\>:\<counter value\>

Example session:

    Q:0
    C:0:0
    S:0:12345
    C:0:12345
    Q:0
    C:0:12345
    ... one impulse coming in ...
    Q:0
    C:0:12346
    ... some more impulses coming in ...
    Q:0
    C:0:12350

    I:*:1
    C:1:12350
    Q:1
    C:1:12350

# Home Assistant Integration

I've integrated this sensor in my Home Assistant using the "tio" serial comms utility.

I added a line to `/homeassistant/configuration.yaml`:

    command_line: !include command_line.yaml

to pull in the "command line" definition for the sensor, in a new dedicated file `/homeassistant/command_line.yaml`:

    - sensor:
      name: "Serial Gas"
      unique_id : s11111111-xxxx-yyyy-zzzz
      command: 'tio -b 57600 /dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller-if00-port0 --script "send(''Q:0\r''); expect(''\r'', 300); print(''''); exit(0)" --mute | cut -d: -f3'
      unit_of_measurement: "m³"
      value_template: "{{ value | multiply(0.01) | round(2) }}"
      scan_interval: 30

Note, to install "tio" and to find out which USB port your serial adapter uses, following approach worked for me:

    # From HA Terminal, connect to the docker image that is running the show...
    docker exec -it homeassistant /bin/bash
    apk add tio

    # list interfaces, with the USB adaptor removed
    tio --list

    # plug in USB connector, check for the newly added line ... that's the one you want to use for above tio command line command
    tio --list

Update 2024-07-06: tio has changed commmand line options in version 2.8 and later, see https://github.com/tio/tio. Changing the `command:` to the following worked for me, using the new LUA script facility. Of coutse, the "cut" could be done in LUA, too, but that's not a crucial point. Note that in the YAML file, one needs to escape single quotes by doubling them.

      # tio 3.3
      tio -b 57600 /dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller-if00-port0 --script "send('Q:0\r'); expect('\r', 1000); print(''); exit(0)" --mute | cut -d: -f3

      # tio <= 2.7
      echo -e "Q:0\r" | tio -b 57600 /dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller-if00-port0 --response-wait --response-timeout 300 | cut -d: -f3

# Hardware

See comment in the INO file on how the (few) components are wired up.

The only external components (apart from connections) are

- two schottky diodes, see above
- LED with series resistor (around 270 Ohm) to VCC. The LED is optional of course, the microcontroller just pulls the pin low.
- One 100nF blocking capacitor, recommended but probably not required, considering the battery and lack of other external circuitry.

# Caveats

Why not ESPHome or AI-on-the-Edge, you ask? I prefered a __wired__ setup as I did not want to have WLAN in the picture, with its setup, keys and router requirements.

The LED is driven from the main µC power supply, i.e. either from the battery backup or from the serial line. One could attach the LED power supply to the serial line only so that no energy is wasted on the LED when in battery backup mode. On the other side, I expect the Serial power supply to be available close to 100% anyway, so this is not a huge concern in my case.

Is the serial interface over-engineered? Yes! For a point-to-point connection like the RS232-style UART used here there really is no need for an "ID" or
or indeed any request format. It would suffice to just detect a transition on, say, the CTS line ot make the unit send it counter value. Or, reception of __any__ character. Alas, the current textual interface is trivial too, can be scripted easily and even exercised manually using any terminal program (like picocom or PuTTY).

The counter value maintained within the unit is a 32 bit unsigned long integer value. The interpretation of that value as 1/100th of a cubic meter is on the HA side only. My gas counter is 6-digits only, with no risk of overflowing anytime soon, so I did not check 7 or longer-digit numbers.

If your gas meter does have the contact on the second fractional digit (not the third as in my case), adjust the HA script accordingly: last line multiply and round. There should not be a need to change the microcontroller code.

Eventually, I might be getting around to upgrading the serial interface to RS485 and ModBus-RTU which would allow multiple sensors on a single serial, plus CRC-based communications error checking, plus ability to use HA's modbus integration w/o the need for DIY-style "tio" scripting. Let's see.

Schematics and visuals might be added later. Ping me if you're interested.
