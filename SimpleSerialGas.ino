/*
   SimpleSerialGas
   - Chip: ATtiny402
   - Clock: 8MHz internal
   - mills/micros: RTC
   - Startup time: 8ms
   - BOD mode: disabled / disabled
   - WDT timeout: disabled

   Arduino Core 2.6.10: https://github.com/SpenceKonde/megaTinyCore/blob/master/megaavr/extras/ATtiny_x02.md

   Hardware:
   - PA2       LED with 220R resistor to VCC, active LOW
   - PA3       Reed Switch against GND
   - PA6, PA7  Serial TXD, RXD, 57600 8N1

   v0.1        December 2023
*/

#include <avr/sleep.h>

//                        // ATtiny402
//                        // Physical Pin 1 VCC
//                PIN_PA6 // Physical Pin 2, mTC Pin 0, TXD
//                PIN_PA7 // Physical Pin 3, mTC Pin 1, RXD
//                PIN_PA1 // Physical Pin 4, mTC Pin 2
#define LED_Y     PIN_PA2 // Physical Pin 5, mTC Pin 3, LED via Jumper
//                PIN_PA0 // Physical Pin 6, mTC Pin 5, UPDI
#define REED_IN   PIN_PA3 // Physical Pin 7, mTC Pin 4
//                        // Physical Pin 8 GND

#define BAUD      57600   // Note: 115200 does not appear to work with SFDEN, no matter how fast we clock.

static uint8_t inputDebounce = 0;
static uint32_t mainCounter;
static char cmd[16];
static int cmd_idx = 0;
static char id = '0';

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;
}

void setup() {
  pinMode(LED_Y, OUTPUT);
  digitalWrite(LED_Y, HIGH);

  // (16.5.11) All pins, no inversion, Pull-up disabled, input disable
  // https://github.com/SpenceKonde/megaTinyCore/issues/695#issue-1215478208
  PORTA.PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN3CTRL = PORT_ISC_INTDISABLE_gc | PORT_PULLUPEN_bm; // Reed
  PORTA.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
  PORTA.PIN7CTRL = PORT_ISC_INTDISABLE_gc | PORT_PULLUPEN_bm; // RXD

  Serial.begin(BAUD, SERIAL_8N1);
  while (!Serial);

  // Set up Periodic Interrupt (PIT) every 128ms
  // Note: Using 1kHz takes less energy during power down in comparison to 32kHz: only 450uA at 1V8
  RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;
  while (RTC.STATUS > 0 || RTC.PITSTATUS);
  RTC.PITINTCTRL = RTC_PI_bm;
  RTC.PITCTRLA = RTC_PERIOD_CYC128_gc | RTC_PITEN_bm;

  //set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Does not work with interrupt-driven serial reception
  set_sleep_mode(SLEEP_MODE_STANDBY);
  sleep_enable();

  // Further power settings?
  //TCA0.SPLIT.CTRLA = 0;
  //ADC0.CTRLA &= ~ADC_ENABLE_bm;

  cmd[0] = 0;
  cmd[sizeof(cmd) - 1] = 0;
}

/**
 * Reply to all commands (for now) is
 *  C:1:12345
 * with current ID and counter value.
 */
void reply() {
  Serial.print("C:");
  Serial.print(id);
  Serial.print(":");
  Serial.println(mainCounter);
  Serial.flush();
}
/**
 * Commands:
 *  Q:1          - Query current counter value
 *  S:1:12345    - Set counter to given value
 *  I:*:1        - Set ID to given digit
 */
void execute() {
  if (cmd[0] == 'Q' && cmd[1] == ':' && cmd[2] == id && cmd[3] == 0) {
    reply();
  } else if (cmd[0] == 'S' && cmd[1] == ':' && cmd[2] == id && cmd[3] == ':' && cmd[4] >= '0' && cmd[4] <= '9') {
    mainCounter = atol(cmd + 4);
    reply();
  } else if (cmd[0] == 'I' && cmd[1] == ':' && cmd[2] == '*' && cmd[3] == ':' && cmd[4] >= '0' && cmd[4] <= '9' && cmd[5] == 0) {
    id = cmd[4];
    reply();
  }
}

void loop() {
  // Enable Start of Frame detection, required for UART to wake up from sleep mode
  USART0.CTRLB |= USART_SFDEN_bm;
  sleep_cpu();

  // we're here every 128ms at least
  inputDebounce = inputDebounce << 1 | digitalRead(REED_IN) | 0xF8; // Debounce
  if (inputDebounce == 0xF9 ) { // .....001 rising edge detected
    mainCounter++;
    // Switch on LED for 1/10th of a second, just flashing it
    digitalWrite(LED_Y, LOW);
  } else {
    // Switch off LED
    digitalWrite(LED_Y, HIGH);
  }

  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\r') {
      execute();
      cmd_idx = 0;
    } else if (ch >= 0x20 && ch < 0x7F) {
      cmd[cmd_idx] = ch;
      cmd_idx = (cmd_idx + 1) % (sizeof(cmd) - 1);
    }
    cmd[cmd_idx] = 0;
  }
}

// END
