/*
TODO:
-- 0. Fix timing bugs!
-- 1. Setting pulse width
-- 2. Setting for direct BPM mode - no click to apply new BPM
-- 3. Reset button to revert in menu system
-- 4. Display channel information on home screen
-- 5. Display 'BPM' on home screen and align big text at 2 chars
-- 6. Fast rotation increments step size on bpm selection
-- 7. Implement start/stop button
-- 8. Implement facory reset
9. Custom font?
10. Clock in & LED
-- 11. Tap to set BPM
*/

// Display
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <EEPROM.h>

//////////////////////////////////////////////////////////////////////
// Logging
//////////////////////////////////////////////////////////////////////
//***************************************************************
#define DEBUG  // if this line is NOT commented, LO and LOG macros will be included in the sketch

#ifdef DEBUG
#define LO(args...)  Serial.print(args)
#define LOG(args...)  Serial.println(args)

#else

#define LO(args...)
#define LOG(args...)
#endif


//////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////
#define VERSION "V1.1"

// IO state management (internal pull-ups used, so values are inverted)
#define PRESSED LOW
#define RELEASED HIGH

// EEPROM
// Magic number for EEPROM initialisation detection
#define MAGIC_NUMBER 0xCC
#define EEPROM_BASE_ADDRESS 16

// Display
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define CHAR_HEIGHT 8
#define CHAR_WIDTH 8

// --- Inputs ---
// Rotary encoder
#define ROTARY_PINA	6
#define ROTARY_PINB	5
#define BUTTON_PIN	2

#define CLOCK_IN_PIN A1
#define RESET_PIN    A2
#define RUN_PIN      A3

// --- Outputs ---
#define CLOCK_IN_LED_PIN   7
#define CLOCK_PIN          8
#define CLOCK_CUSTOM_1_PIN 9
#define CLOCK_CUSTOM_2_PIN 10
#define CLOCK_CUSTOM_3_PIN 11
#define CLOCK_CUSTOM_4_PIN 12

// Clock specification
#define DEFAULT_PULSE_WIDTH 10 // ms

// Update mode
#define DEFAULT_DIRECT_BPM_MODE false

// Config layout
#define CONFIG_PULSE_WIDTH_LINE 4
#define CONFIG_DIRECT_BPM_LINE 5
#define CONFIG_TAP_LINE 6
#define CONFIG_RETURN_LINE 7

//////////////////////////////////////////////////////////////////////
// Structs
//////////////////////////////////////////////////////////////////////
struct customClock{
  char clockType;
  int clockCount;
};

//////////////////////////////////////////////////////////////////////
// Enums
//////////////////////////////////////////////////////////////////////
enum DisplayMode{RUN, CONFIG, TAP};
enum ConfigMode{SELECT_LINE, SELECT_VALUE, UPDATE_PULSE_WIDTH, UPDATE_DIRECT_BPM_MODE, UPDATE_VALUE};
enum ConfigLine{A, B, C, D, RETURN};
enum ConfigOption{FUNCTION, VALUE, LINE_RETURN};

//////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////
const int CUSTOM_CLOCK_PINS[] = {CLOCK_CUSTOM_1_PIN, CLOCK_CUSTOM_2_PIN, CLOCK_CUSTOM_3_PIN, CLOCK_CUSTOM_4_PIN};
const char CHANNEL_NAMES[] = "ABCD";

//////////////////////////////////////////////////////////////////////
// Variables
//////////////////////////////////////////////////////////////////////
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Config state
bool directBpmMode = DEFAULT_DIRECT_BPM_MODE;
int pulseWidth = DEFAULT_PULSE_WIDTH;
customClock customClocks[] = {
  {'/', 4},
  {'/', 2},
  {'*', 2},
  {'*', 4},
};

// All on internal pull up resistors
bool rotaryAState = RELEASED;
bool rotaryBState = RELEASED;
bool rotaryButtonState = RELEASED;
bool resetState = RELEASED;
bool runState = RELEASED;

// State change flags
bool rotaryAStateUpdated = false;
bool rotaryBStateUpdated = false;
bool rotaryButtonStateUpdated = false;
bool resetStateUpdated = false;
bool runStateUpdated = false;

// State change detection
bool lastRotaryAState = RELEASED;
bool lastRotaryButtonState = RELEASED;
bool lastResetState = RELEASED;
bool lastRunState = RELEASED;

// BPM management
int bpm = 60;
int nextBpm = bpm;

// Event management
volatile unsigned long tick = 0;
volatile unsigned long now = millis();
volatile unsigned long mainClockDelayMs = (long) (60.0 / bpm * 1000);
volatile unsigned long nextMainClockStart = now + mainClockDelayMs;
volatile unsigned long nextMainClockFinish = nextMainClockStart + pulseWidth;

bool customClockEnabled[] = {0, 0, 0, 0};
volatile unsigned long customClockDelay[] = {0, 0, 0, 0};
volatile unsigned long nextCustomClockStart[] = {0, 0, 0, 0};
volatile unsigned long nextCustomClockFinish[] = {0, 0, 0, 0};

// Tap mode
long taps[] = {0, 0, 0, 0};

// Start/Stop state
bool running = true;

// UI
bool updateDisplay = true;
DisplayMode displayMode = RUN;

// UI - Config screen
ConfigMode configMode = SELECT_LINE; 
ConfigLine configLine = A;
ConfigOption configOption = FUNCTION;
bool configOptionSelected = false;

//////////////////////////////////////////////////////////////////////
// Utility functions
//////////////////////////////////////////////////////////////////////
static byte wrap(int value, byte low, byte high) {
  if (value < low) return high;
  if (value > high) return low;
  return value;
}

//////////////////////////////////////////////////////////////////////
// Event functions
//////////////////////////////////////////////////////////////////////
static void calculateAllClockDelays() {
  mainClockDelayMs = (long) (60.0 / bpm * 1000.0);
  for (int i=0; i < 4; i++) {
    if (customClocks[i].clockType == '/') {
      customClockDelay[i] = mainClockDelayMs * customClocks[i].clockCount;
    } else if (customClocks[i].clockType == '*') {
      customClockDelay[i] = (long) (mainClockDelayMs / customClocks[i].clockCount);
    } else {
      // Not enabled
      customClockDelay[i] = 0;
    }
  }
}

static void updateNextStartTime(bool allClocks=false, int customClockIndex=-1, bool initialising=false, bool restarting=false) {
  if (initialising) {
    // Set all clocks to trigger now, except DIV ones
    nextMainClockStart = now;
    for (byte i; i < 4; i++) {
      if (customClockEnabled[i]) {
        nextCustomClockStart[i] = nextMainClockStart; 
      }
      if (customClocks[i].clockType == '/') {
        nextCustomClockStart[i] += customClockDelay[i];
      }
    }
  }

  if (allClocks) {
    // Move all expired clock start times forward
    if (now > nextMainClockStart) {
      nextMainClockStart += mainClockDelayMs;
    }
    for (byte i; i < 4; i++) {
      if (customClockEnabled[i] && now > nextCustomClockStart[i]) {
        if (customClocks[i].clockType == '=' || customClocks[i].clockType == '*') {
          nextCustomClockStart[i] = nextMainClockStart; 
        } else {
          nextCustomClockStart[i] += customClockDelay[i]; 
        }
      }
    }

  } else {
    // Move a specific clock forward
    if (customClockIndex == -1) {
      nextMainClockStart += mainClockDelayMs;
    } else {
      nextCustomClockStart[customClockIndex] += customClockDelay[customClockIndex];
      if (restarting) {
        nextCustomClockStart[customClockIndex] = nextMainClockStart + customClockDelay[customClockIndex];
        if (customClocks[customClockIndex].clockType == '/') {
          // Account for changes during the sequence.
          // E.g.: Post tick 3 of 4 when a DIV 4 is enabled, Remove the delay of the first 3 ticks from out next start.
          byte missedTicks = tick % customClocks[customClockIndex].clockCount;
          nextCustomClockStart[customClockIndex] -= missedTicks * mainClockDelayMs;
        }
      }
    }
  }
}

static void updateNextFinishTime(bool allClocks=false, int customClockIndex=-1, bool initialising=false) {
  if (initialising || allClocks) {
    // Set all active clock's finish times
    nextMainClockFinish = nextMainClockStart + pulseWidth;
    for (byte i; i < 4; i++) {
      nextCustomClockFinish[i] = nextCustomClockStart[i] + pulseWidth; 
    }
  } else if (customClockIndex == -1) {
    // Update main clock
    nextMainClockFinish = nextMainClockStart + pulseWidth;
  } else {
    // Update selected custom clock
    nextCustomClockFinish[customClockIndex] = nextCustomClockStart[customClockIndex] + pulseWidth;
  }
}

//////////////////////////////////////////////////////////////////////
// UI drawing functions
//////////////////////////////////////////////////////////////////////
static void drawTriangle(byte rowOffset, byte colOffset, bool filled = false) {
  // Allow for previous lines
  byte width = 7;
  byte height = 6;
  if (filled) {
    display.fillTriangle(
      colOffset, rowOffset, 
      colOffset + width, 4 + rowOffset, 
      colOffset, 8 + rowOffset, 
      SSD1306_WHITE
    );
  } else {
    display.drawTriangle(
      colOffset, rowOffset, 
      colOffset + width, 4 + rowOffset, 
      colOffset, 8 + rowOffset, 
      SSD1306_WHITE
    );
  }
}

static void drawReturnArrow(byte rowOffset, byte colOffset) {
  // Allow for previous lines
  display.drawLine(
    colOffset, rowOffset, 
    colOffset, rowOffset + 4, 
    SSD1306_WHITE
  );
  display.drawLine(
    colOffset, rowOffset + 4, 
    colOffset - 4, rowOffset + 4, 
    SSD1306_WHITE
  );
  display.drawLine(
    colOffset - 4, rowOffset + 4, 
    colOffset - 2, rowOffset + 2, 
    SSD1306_WHITE
  );
  display.drawLine(
    colOffset - 4, rowOffset + 4, 
    colOffset - 2, rowOffset + 6, 
    SSD1306_WHITE
  );
}

static void prettyChannel(byte channel) {
  /*
  Dumps current state of channel to display at current cursor coordinates
  */
  String s = "";
  display.print(CHANNEL_NAMES[channel]);
  display.print(F(":"));
  switch (customClocks[channel].clockType) {
    case 'X':
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Inverse text
      display.print(F(" OFF "));
      display.setTextColor(SSD1306_WHITE);
      display.print(F("  "));
      break;
    case '*':
      display.print(F("MUL/"));
      display.print(customClocks[channel].clockCount);
      if (customClocks[channel].clockCount < 10) {
        display.print(F("  "));
      } else if (customClocks[channel].clockCount < 100) {
        display.print(F(" "));
      }
      break;
    case '/':
      display.print(F("DIV/"));
      display.print(customClocks[channel].clockCount);
      if (customClocks[channel].clockCount < 10) {
        display.print(F("  "));
      } else if (customClocks[channel].clockCount < 100) {
        display.print(F(" "));
      }
      break;
    case '=':
      display.print(F("DUP    "));
      break;
  }
}

//////////////////////////////////////////////////////////////////////
// EEPROM management
//////////////////////////////////////////////////////////////////////
static void initEEPROM(bool force=false) {
  // Look for magic number at end of EEPROM, if it is not there clear the EEPROM and add the magic number
  int temp = 0;
  EEPROM.get(EEPROM.length() - 1, temp);
  if (force || temp != MAGIC_NUMBER) {
    for (int i = 0 ; i < EEPROM.length() ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(EEPROM.length() - 1, MAGIC_NUMBER);
    saveEEPROM();  
  }
}

static void loadEEPROM() {
  // Increment address as you write
  int address = EEPROM_BASE_ADDRESS;

  EEPROM.get(address, directBpmMode);
  address += sizeof(directBpmMode);
  EEPROM.get(address, pulseWidth);
  address += sizeof(pulseWidth);
  EEPROM.get(address, bpm);
  address += sizeof(bpm);
  EEPROM.get(address, customClocks);
  address += sizeof(customClocks);

  normaliseLoadedValues();
}

static void saveEEPROM() {
  // Increment address as you read
  int address = EEPROM_BASE_ADDRESS;

  EEPROM.put(address, directBpmMode);
  address += sizeof(directBpmMode);
  EEPROM.put(address, pulseWidth);
  address += sizeof(pulseWidth);
  EEPROM.put(address, bpm);
  address += sizeof(bpm);
  EEPROM.put(address, customClocks);
  address += sizeof(customClocks);
}

static void normaliseLoadedValues(){
  pulseWidth = constrain(pulseWidth, 5, 100);
  
  bpm = constrain(bpm, 60, 260);
  nextBpm = bpm;
  
  char ct;
  for (byte i=0; i < 4; i++) {
    ct = customClocks[i].clockType;
    if (ct == 'X' || ct == '*' || ct == '/') {
      customClocks[i].clockCount = constrain(customClocks[i].clockCount, 1, 64);
      customClockEnabled[i] = (ct != 'X'); // enable if *, /, or =
    } else {
      // Default the clock to off
      customClocks[i].clockType = 'X';
      customClocks[i].clockCount = 1;
      customClockEnabled[i] = false;
    }
  }
}

//////////////////////////////////////////////////////////////////////
// UI Event handling
//////////////////////////////////////////////////////////////////////
static void updateFunction(byte index, int change) {
  String current = F(" X/=*");
  String next    = F("*X/=*X");
  for (byte i = 1; i < 5; i++) {
    if (current[i] == customClocks[index].clockType) {
      customClocks[index].clockType = next[i + change];
      customClockEnabled[index] = customClocks[index].clockType != 'X';

      return;
    }
  }
  customClocks[index].clockType = 'X';
  customClockEnabled[index] = false;
}

static void updateFunctionParameter(byte index, int change) {
  byte current[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 32, 64, 128};
  byte next[] = {128, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 32, 64, 128, 2};
  for (byte i = 1; i < 16; i++) {
    if (current[i] == customClocks[index].clockCount) {
      customClocks[index].clockCount = next[i + change];
      return;
    }
  }
  customClocks[index].clockCount = 2;
}

static void allOutputsOff() {
  digitalWrite(CLOCK_PIN, LOW);
  for (byte i=0; i < 4; i++) {
    digitalWrite(CLOCK_PIN, LOW);
  }
}

static byte getIncrementFromRotaryUpdateFrequency() {
  // Timestamps of last 5 rotary encoder movement events
  static long timestamps[] = {0, 0, 0, 0, 0};
  for (byte i = 0; i < 4; i++) {
    timestamps[i] = timestamps[i + 1];
  }
  timestamps[4] = now;
  if (timestamps[0] != 0 && (timestamps[4] - timestamps[0] < 1000)) {
    // 5+ updates in one second moves to speedy update
    return 10;
  }
  return 1;

}

//////////////////////////////////////////////////////////////////////
// Loop processing for each mode
//////////////////////////////////////////////////////////////////////
static void loopRunMode() {
  if (rotaryAStateUpdated){
    if (rotaryAState == PRESSED) {
      byte step = getIncrementFromRotaryUpdateFrequency();
      
      if (directBpmMode) {
        if (rotaryBState != rotaryAState) { 
          bpm -= step;
        } else {
          bpm += step;
        }
        bpm = constrain(bpm, 40, 240);
        nextBpm = bpm;
        calculateAllClockDelays();
        updateNextStartTime(true);
        saveEEPROM();
      
      } else {
        if (rotaryBState != rotaryAState) { 
          nextBpm -= step;
        } else {
          nextBpm += step;
        }
        nextBpm = constrain(nextBpm, 40, 240);
      }
      updateDisplay = true;
    }
  } 

  if (rotaryButtonStateUpdated) {
    if (rotaryButtonState == PRESSED) {
      if (bpm != nextBpm) {
        // Apply the change
        bpm = nextBpm;
        calculateAllClockDelays();
        updateNextStartTime(true);
        saveEEPROM();
        updateDisplay = true;
        LOG(F("BPM"));
      } else {
        // Change to config displayMode
        displayMode = CONFIG;
        // Initialise the config page
        configMode = SELECT_LINE; 
        configLine = A;
        configOption = FUNCTION;
        configOptionSelected = false;
        updateDisplay = true;
        LOG(F("CONFIG"));
      }
    }
  }

  if (resetStateUpdated) {
    if (resetState == PRESSED) {
      tick = 0;
      allOutputsOff();
      updateNextStartTime(true, -1, true);
      updateNextFinishTime(true, -1, true);
      LOG(F("Reset"));
    }
    updateDisplay = true;
  }

  if (runState != lastRunState) {     
    if (runState == PRESSED) {
      // Flips from run to stop or vice versa
      running = !running;
      updateDisplay = true;
      LOG(running ? F("Run") : F("Pause"));
    }
  }
}

static void loopConfigMode() {
  // Reset button
  if (resetStateUpdated && resetState == PRESSED) {
    // Reset button moves you back one level in the menu
    switch (configMode) {
      case SELECT_LINE:
        displayMode = RUN;
        updateDisplay = true;
        break;
      case SELECT_VALUE:
        configMode = SELECT_LINE;
        updateDisplay = true;
        break;
      case UPDATE_VALUE:
        configMode = SELECT_VALUE;
        updateDisplay = true;
        break;
      case UPDATE_DIRECT_BPM_MODE:
        configMode = SELECT_LINE;
        updateDisplay = true;
      case UPDATE_PULSE_WIDTH:
        configMode = SELECT_LINE;
        updateDisplay = true;
        break;
    }
  }
  
  // Rotary encoder switch
  if (rotaryButtonStateUpdated && rotaryButtonState == PRESSED) {
    switch (configMode) {
      case SELECT_LINE:
        switch (configLine) {
          case CONFIG_RETURN_LINE:
            // Return selected, go back to RUN displayMode
            saveEEPROM();
            displayMode = RUN;
            break;
          case CONFIG_PULSE_WIDTH_LINE:
            // Updating the pulse width
            configMode = UPDATE_PULSE_WIDTH;
            break;
          case CONFIG_DIRECT_BPM_LINE:
            // Updating Direct BPM mode
            configMode = UPDATE_DIRECT_BPM_MODE;
            break;
          case CONFIG_TAP_LINE:
            // Entering Tap BPM mode
            displayMode = TAP;
            // Clear from previous runs
            for (byte i = 0; i < 4; i++) {
              taps[i] = 0;
            }
            break;
          default: // case 0-3
            // Channel selected, go forward to select a value on the channel
            configMode = SELECT_VALUE;
            configOption = 0;
            break;
        }
        break;

      case SELECT_VALUE:
        switch (configOption) {
          case 2:
            // Return selected, go back to line selection
            configMode = SELECT_LINE;
            break;
          default: // case 0-1
            // Value selected for update, go forward to select the new value  
            configMode = UPDATE_VALUE;
            break;
        }
        break;

      case UPDATE_PULSE_WIDTH:
        // New value selected, return to option selection
        configMode = SELECT_LINE;
        break;

      case UPDATE_DIRECT_BPM_MODE:
        // New value selected, return to option selection
        configMode = SELECT_LINE;
        break;

      case UPDATE_VALUE: 
        // New value selected, return to option selection
        configMode = SELECT_VALUE;
        break;

    } 
    updateDisplay = true;
  }

  // Rotary encoder knob
  if (rotaryAStateUpdated) {     
    if (rotaryAState == PRESSED) {
      // Find direction of rotation -1=CCW, 1=CW
      int dir = (rotaryBState != rotaryAState) ? -1 : 1;
      
      switch (configMode) {
        case SELECT_LINE:
          // Move to prev/next line
          configLine = wrap(configLine + dir, 0, CONFIG_RETURN_LINE);
          break;

        case SELECT_VALUE:
          // Move to prev/next editable value
          configOption = constrain(configOption + dir, 0, 2);
          break;

        case UPDATE_DIRECT_BPM_MODE:
          directBpmMode = !directBpmMode;
          break;

        case UPDATE_PULSE_WIDTH:
          pulseWidth = constrain(pulseWidth + dir, 5, 100);
          break;

        case UPDATE_VALUE:
          // Change the value from the presets
          char oldFunction = customClocks[configLine].clockType;
          switch (configOption) {
            case 0: // Function
              updateFunction(configLine, dir);
              break;
            case 1: // Function parameter
              updateFunctionParameter(configLine, dir);
              break;
          }
          calculateAllClockDelays();
          updateNextStartTime(false, configLine, false, oldFunction == 'X');
          break;
      }
      updateDisplay = true;
    }
  }
}

static void loopTapMode() {
  if (rotaryButtonStateUpdated) {
    if (rotaryButtonState == PRESSED) {
      bpm = nextBpm;
      calculateAllClockDelays();
      updateNextStartTime(true);
      saveEEPROM();
      displayMode = RUN;
      updateDisplay = true;
      LOG(F("TAP"));
    }
  }

  if (resetState != lastResetState) {     
    if (resetState == PRESSED) {
      // Reset tap history
      for (byte i = 0; i < 4; i++) {
        taps[i] = 0;
      }
      updateDisplay = true;
    }
  }

  if (runState != lastRunState) {     
    if (runState == PRESSED) {
      // Add a tap
      for (byte i = 0; i < 3; i++) {
        taps[i] = taps[i + 1];
      }
      taps[3] = now;
    }
    if (taps[0] != 0) {
      long temp = 0;
      for (byte i = 0; i < 3; i++) {
        temp += taps[i + 1] - taps[i];
      }
      temp = (long) (temp / 3);
      nextBpm = (long) (60000 / temp);
    }
    updateDisplay = true;
  }
}

//////////////////////////////////////////////////////////////////////
// Display processing for each mode
//////////////////////////////////////////////////////////////////////
static void displayRunMode() {
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2 * (tick % 3), 0);
  display.setTextSize(2);
  display.println('B');
  display.setCursor(2 * ((tick + 1) % 3), 16);
  display.println('P');
  display.setCursor(2 * ((tick + 2) % 3), 32);
  display.println('M'); 
  // byte h = (byte) (tick / 100);
  // byte t = (byte) ((tick % 100) / 10);
  // byte u = (byte) tick % 10;
  // display.print('B'); display.println(h);
  // display.print('P'); display.println(t);
  // display.print('M'); display.println(u);
  display.setCursor(16 + (bpm < 100 ? 24: 0),0);
  display.setTextSize(6);
  display.println(bpm);
  display.setTextSize(2);
  if (nextBpm != bpm) {
    display.print(F("Next: ")); display.println(nextBpm);
  } else if (resetState == PRESSED) {
    display.println(F("   RESET"));
  } else if (!running) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.println(F("   PAUSE   "));
  } else {
    display.setTextSize(1);
    prettyChannel(0); display.print(F("  ")); prettyChannel(1); display.println();
    prettyChannel(2); display.print(F("  ")); prettyChannel(3); display.println();
  }
}

static void displayConfigMode() {
  // LO(F("displayMode: ")); LO(configMode); LO(F("Option: ")); LO(configOption);
  
  display.setCursor(0,0);

  // Show header
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Show channel table
  String text;
  String functionName;
  for (byte i = 0; i < 4; i++) {
    text = F("  ");
    text.concat(CHANNEL_NAMES[i]);
    text.concat(F(":  "));
    switch(customClocks[i].clockType) {
      case '/':
        functionName = F("DIV");
        break; 
      case '*':
        functionName = F("MUL");
        break; 
      case '=':
        functionName = F("DUP");
        break; 
      default:
        functionName = F("OFF");
    }
    text.concat(functionName);
    text.concat(F("  "));
    text.concat(customClocks[i].clockCount);
    display.println(text);
  }
  // Other options
  display.print(F("  Pulse width: ")); display.print(pulseWidth); display.println(F("ms"));
  display.print(F("  BPM direct: ")); display.println(directBpmMode ? F("Y") : F("N"));
  display.println(F("  Enter Tap mode"));
  
  // Return Option
  display.println(F("  Return"));

  // Display cursors
  byte rowOffset = CHAR_HEIGHT * configLine;
  // Row selection indicator
  drawTriangle(rowOffset, 0, configMode == SELECT_VALUE || configMode == UPDATE_PULSE_WIDTH || configMode == UPDATE_DIRECT_BPM_MODE);

  // Back to select row indicator
  if (configMode != SELECT_LINE && configMode != UPDATE_PULSE_WIDTH && configMode != UPDATE_DIRECT_BPM_MODE) {
    drawReturnArrow(rowOffset, CHAR_WIDTH * 15);
  }

  // Channel A-D, Value selection / value update indicator
  if (configMode == SELECT_VALUE || configMode == UPDATE_VALUE) {
    switch (configOption) {
      case FUNCTION:
        drawTriangle(rowOffset, CHAR_WIDTH * 3, configMode == UPDATE_VALUE);
        break;
      case VALUE:
        drawTriangle(rowOffset, CHAR_WIDTH * 7, configMode == UPDATE_VALUE);
        break;
      default:
        drawTriangle(rowOffset, CHAR_WIDTH * 13, configMode == UPDATE_VALUE);
    }
  }

}

static void displayTapMode() {
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(12,0);
  display.setTextSize(2);
  display.println(F("Tap Mode"));
  display.setTextSize(1);
  display.println(F("Tap Start button to"));
  display.println(F("set new BPM..."));
  byte n = 4;
  for (byte i = 0; i < 4; i++) {
    if (taps[i] != 0) {
      n--;
    }
  }
  if (n > 0) {
    display.print(n); display.println(F(" taps to go"));
  } else {
    display.println(F("New BPM:")); 
    display.setTextSize(2);
    display.setCursor(40, 40);
    display.println(nextBpm); 
    display.setTextSize(1);
    display.println(F("Press dial to apply")); 
  }
}

//////////////////////////////////////////////////////////////////////
// Arduino standard functions
//////////////////////////////////////////////////////////////////////
void setup() {
  while (!Serial) { }; // Wait for serial to be available
  Serial.begin(9600);

  // Initialise pin states
  // Rotary encoder
  pinMode(ROTARY_PINA, INPUT_PULLUP);
  pinMode(ROTARY_PINB, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Reset button
  pinMode(RESET_PIN, INPUT_PULLUP);
  // Run/Stop button
  pinMode(RUN_PIN, INPUT_PULLUP);

  // Outputs
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(CLOCK_CUSTOM_1_PIN, OUTPUT);
  pinMode(CLOCK_CUSTOM_2_PIN, OUTPUT);
  pinMode(CLOCK_CUSTOM_3_PIN, OUTPUT);
  pinMode(CLOCK_CUSTOM_4_PIN, OUTPUT);

  // Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    // LOG(F("SSD1306 allocation failed"));
    for(;;) {
      digitalWrite(CLOCK_CUSTOM_4_PIN, HIGH);
      delay(100);
      digitalWrite(CLOCK_CUSTOM_4_PIN, LOW);
      delay(100);
    }
  }

  // Display splash screen
  display.clearDisplay();
  display.setCursor(0,16);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.println(F("Clocki-"));  
  display.println(F("   poodle!"));  
  display.setCursor(60,54); 
  display.setTextSize(1);
  display.println(F(VERSION));
  display.display();
  delay(2000);

  // EEPROM clean & load
  // Run & Reset held at startup reinitilises to default and stores in EEPROM
  bool factoryReset = digitalRead(RESET_PIN) == PRESSED && digitalRead(RUN_PIN) == PRESSED;
  if (factoryReset) {
    display.clearDisplay();
    display.setCursor(0,16);
    display.setTextSize(2);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.println(F(">Factory<"));
    display.println(F("> Reset <"));
    display.display();
    initEEPROM(true);
    delay(2000);
  }
    
  initEEPROM();
  loadEEPROM();
  calculateAllClockDelays();
  updateNextStartTime(true, -1, true);
  updateNextFinishTime(true, -1, true);
  LOG(F("Ready"));

  // Setup a 1ms timer interrupt on Timer 1A
  TCCR1A = 0;           // Init Timer1A
  TCCR1B = 0;           // Init Timer1B
  TCCR1B |= B00000001;  // Prescaler = 1
  OCR1A = 16000;        // Timer Compare1A Register
  TIMSK1 |= B00000010;  // Enable Timer COMPA Interrupt
}

void loop() {
  // Read current state
  rotaryAState = digitalRead(ROTARY_PINA);
  rotaryBState = digitalRead(ROTARY_PINB);
  rotaryButtonState = digitalRead(BUTTON_PIN);
  resetState = digitalRead(RESET_PIN);
  runState = digitalRead(RUN_PIN);

  // Identify important changed states
  rotaryAStateUpdated = rotaryAState != lastRotaryAState;
  rotaryButtonStateUpdated = rotaryButtonState != lastRotaryButtonState;
  resetStateUpdated = resetState != lastResetState;
  runStateUpdated = runState != lastRunState;

  if (displayMode == RUN) {
    loopRunMode();
  } else if (displayMode == CONFIG) {
    loopConfigMode();
  } else if (displayMode == TAP) {
    loopTapMode();
  }

  if (updateDisplay) {
    display.clearDisplay();
    
    if (displayMode == RUN) {
      displayRunMode();
    } else if (displayMode == CONFIG) {
      displayConfigMode();
    } else if (displayMode == TAP) {
      displayTapMode();
    }
    
    display.display();
    
    updateDisplay = false;
  }

  digitalWrite(CLOCK_IN_LED_PIN, digitalRead(CLOCK_IN_PIN));

  // Store curent state to compare against next time
  lastRotaryAState = rotaryAState;
  lastRotaryButtonState = rotaryButtonState;
  lastResetState = resetState;
  lastRunState = runState;
}

//////////////////////////////////////////////////////////////////////
// Interrupt service routines
//////////////////////////////////////////////////////////////////////
ISR(TIMER1_COMPA_vect) {
  // Handler for 1ms timer interrupt
  // Set timeout for next interrupt (1ms)
  OCR1A += 16000; // Advance The COMPA Register
  
  // Look for clock events
  now = millis();
  if (!running || resetState == PRESSED) {
    // Move all the event times forward by 1ms, and disable all ouputs
    nextMainClockStart += 1;
    nextMainClockFinish += 1;
    digitalWrite(CLOCK_PIN, LOW);
    for (byte i = 0; i < 4; i++) {
      nextCustomClockStart[i] += 1;
      nextCustomClockFinish[i] += 1;
      digitalWrite(CUSTOM_CLOCK_PINS[i], LOW);
    }

  } else {
    // Normal running mode
    if (now >= nextMainClockStart) {
      digitalWrite(CLOCK_PIN, HIGH);
      if (resetState != PRESSED) {
        tick++;
        tick %= 128;
      }
      updateDisplay = true;
      updateNextStartTime();
    }
    for (byte i = 0; i < 4; i++) {
      if (customClockEnabled[i] && now >= nextCustomClockStart[i]) {
        digitalWrite(CUSTOM_CLOCK_PINS[i], HIGH);
        updateNextStartTime(false, i);
      }
    }
      
    if (now >= nextMainClockFinish) {
      digitalWrite(CLOCK_PIN, LOW);
      updateNextFinishTime();
    }
    for (byte i=0; i < 4; i++) {
      if (!customClockEnabled[i]) {
        digitalWrite(CLOCK_PIN, LOW);
      } else if (now >= nextCustomClockFinish[i]) {
        digitalWrite(CUSTOM_CLOCK_PINS[i], LOW);
        updateNextFinishTime(false, i);
      }
    }
  }
}
