#include "Watchy.h"
#include <Preferences.h>

#ifdef ARDUINO_ESP32S3_DEV
  Watchy32KRTC Watchy::RTC;
  #define ACTIVE_LOW 0
#else
  WatchyRTC Watchy::RTC;
  #define ACTIVE_LOW 1
#endif
GxEPD2_BW<WatchyDisplay, WatchyDisplay::HEIGHT> Watchy::display(
    WatchyDisplay{});

RTC_DATA_ATTR int guiState;
RTC_DATA_ATTR int menuIndex;
RTC_DATA_ATTR BMA423 sensor;
RTC_DATA_ATTR bool WIFI_CONFIGURED;
RTC_DATA_ATTR bool BLE_CONFIGURED;
RTC_DATA_ATTR weatherData currentWeather;
RTC_DATA_ATTR int weatherIntervalCounter = -1;
RTC_DATA_ATTR long gmtOffset = 0;
RTC_DATA_ATTR bool alreadyInMenu         = true;
RTC_DATA_ATTR bool USB_PLUGGED_IN = false;
RTC_DATA_ATTR tmElements_t bootTime;
RTC_DATA_ATTR uint32_t lastIPAddress;
RTC_DATA_ATTR char lastSSID[30];

namespace {
constexpr uint32_t CLOCK_TOOLS_MAGIC = 0x434C4B31;
constexpr uint8_t CLOCK_ALERT_ALARM = 0x01;
constexpr uint8_t CLOCK_ALERT_TIMER = 0x02;

enum ClockToolsButton : int8_t {
  CLOCK_BUTTON_ALERT = -2,
  CLOCK_BUTTON_TIMEOUT = -1,
  CLOCK_BUTTON_MENU,
  CLOCK_BUTTON_BACK,
  CLOCK_BUTTON_UP,
  CLOCK_BUTTON_DOWN
};

struct ClockToolsData {
  uint32_t magic;
  bool alarmEnabled;
  uint8_t alarmHour;
  uint8_t alarmMinute;
  uint32_t lastAlarmDay;
  bool countdownActive;
  uint16_t countdownPresetMinutes;
  uint32_t countdownEnd;
  bool stopwatchRunning;
  uint32_t stopwatchStarted;
  uint32_t stopwatchElapsed;
  uint8_t alertFlags;
  uint32_t lastEventCheck;
};

RTC_DATA_ATTR ClockToolsData clockToolsData;

bool clockToolsButtonPressed(uint8_t pin) {
  return digitalRead(pin) == ACTIVE_LOW;
}

void printTwoDigits(uint32_t value) {
  if (value < 10) {
    Watchy::display.print("0");
  }
  Watchy::display.print(value);
}

bool clockToolsClockIsValid(const tmElements_t &time) {
  int year = tmYearToCalendar(time.Year);
  return year >= 2020 && year <= 2099 && time.Month >= 1 &&
         time.Month <= 12 && time.Day >= 1 && time.Day <= 31 &&
         time.Hour <= 23 && time.Minute <= 59 && time.Second <= 59;
}
} // namespace

void Watchy::init(String datetime) {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause(); // get wake up reason
  #ifdef ARDUINO_ESP32S3_DEV
    Wire.begin(WATCHY_V3_SDA, WATCHY_V3_SCL);     // init i2c
  #else
    Wire.begin(SDA, SCL);                         // init i2c
  #endif
  RTC.init();
  // Init the display since is almost sure we will use it
  display.epd2.initWatchy();
  _loadClockToolsState();

  switch (wakeup_reason) {
  #ifdef ARDUINO_ESP32S3_DEV
  case ESP_SLEEP_WAKEUP_TIMER: { // RTC Alarm
  #else
  case ESP_SLEEP_WAKEUP_EXT0: { // RTC Alarm
  #endif
    RTC.read(currentTime);
    if (_checkClockToolsEvents()) {
      break;
    }
    switch (guiState) {
    case WATCHFACE_STATE:
      showWatchFace(true); // partial updates on tick
      if (settings.vibrateOClock && currentTime.Minute == 0) {
        // The RTC wakes us up once per minute
        vibMotor(75, 4);
      }
      break;
    case MAIN_MENU_STATE:
      // Return to watchface if in menu for more than one tick
      if (alreadyInMenu) {
        guiState = WATCHFACE_STATE;
        showWatchFace(false);
      } else {
        alreadyInMenu = true;
      }
      break;
    }
    break;
  }
  case ESP_SLEEP_WAKEUP_EXT1: { // button Press
    RTC.read(currentTime);
    bool hadAlert = clockToolsData.alertFlags != 0 ||
                    guiState == CLOCK_ALERT_STATE;
    bool hasAlert = _checkClockToolsEvents();
    if (hasAlert && !hadAlert) {
      break; // Do not consume the button that happened to trigger an alert.
    }
    if (hadAlert) {
      clockToolsData.alertFlags = 0;
      _saveClockToolsState();
      showWatchFace(false);
    } else {
      handleButtonPress();
      RTC.read(currentTime);
      _checkClockToolsEvents();
    }
    break;
  }
  #ifdef ARDUINO_ESP32S3_DEV
  case ESP_SLEEP_WAKEUP_EXT0: { // USB plug in
    pinMode(USB_DET_PIN, INPUT);
    USB_PLUGGED_IN = (digitalRead(USB_DET_PIN) == 1);
    RTC.read(currentTime);
    if (!_checkClockToolsEvents() && guiState == WATCHFACE_STATE) {
      showWatchFace(true);
    }
    break;
  }
  #endif
  default: { // reset
    tmElements_t oldTime;
    RTC.read(oldTime);
    bool oldClockValid = clockToolsClockIsValid(oldTime);
    if (!oldClockValid) {
      bool stateChanged = clockToolsData.countdownActive ||
                          clockToolsData.stopwatchRunning;
      clockToolsData.countdownActive = false;
      clockToolsData.countdownEnd = 0;
      clockToolsData.stopwatchRunning = false;
      clockToolsData.stopwatchStarted = 0;
      if (stateChanged) {
        _saveClockToolsState();
      }
    }
    RTC.config(datetime);
    _bmaConfig();
    #ifdef ARDUINO_ESP32S3_DEV
    pinMode(USB_DET_PIN, INPUT);
    USB_PLUGGED_IN = (digitalRead(USB_DET_PIN) == 1);
    #endif
    gmtOffset = settings.gmtOffset;
    RTC.read(currentTime);
    RTC.read(bootTime);
    if (oldClockValid && clockToolsClockIsValid(currentTime)) {
      _rebaseClockTools(static_cast<uint32_t>(makeTime(oldTime)),
                        static_cast<uint32_t>(makeTime(currentTime)));
    }
    if (!_checkClockToolsEvents()) {
      showWatchFace(false); // full update on reset
      vibMotor(75, 4);
    }
    // For some reason, seems to be enabled on first boot
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    break;
  }
  }
  deepSleep();
}
void Watchy::deepSleep() {
  display.hibernate();
  RTC.clearAlarm();        // resets the alarm flag in the RTC
  #ifdef ARDUINO_ESP32S3_DEV
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USB_DET_PIN, USB_PLUGGED_IN ? LOW : HIGH); //// enable deep sleep wake on USB plug in/out
  rtc_gpio_set_direction((gpio_num_t)USB_DET_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)USB_DET_PIN);

  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_LOW); // enable deep sleep wake on button press
  rtc_gpio_set_direction((gpio_num_t)UP_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)UP_BTN_PIN);

  rtc_clk_32k_enable(true);
  //rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
  RTC.read(currentTime);
  int second = currentTime.Second <= 59 ? currentTime.Second : 0;
  int secToNextMin = 60 - second;
  esp_sleep_enable_timer_wakeup(secToNextMin * uS_TO_S_FACTOR);
  #else
  // Set GPIOs 0-39 to input to avoid power leaking out
  const uint64_t ignore = 0b11110001000000110000100111000010; // Ignore some GPIOs due to resets
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if ((ignore >> i) & 0b1)
      continue;
    pinMode(i, INPUT);
  }
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_INT_PIN,
                               0); // enable deep sleep wake on RTC interrupt
  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_HIGH); // enable deep sleep wake on button press
  #endif
  esp_deep_sleep_start();
}

void Watchy::handleButtonPress() {
  uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();
  // Menu Button
  if (wakeupBit & MENU_BTN_MASK) {
    if (guiState ==
        WATCHFACE_STATE) { // enter menu state if coming from watch face
      showMenu(menuIndex, false);
    } else if (guiState ==
               MAIN_MENU_STATE) { // if already in menu, then select menu item
      switch (menuIndex) {
      case 0:
        showAbout();
        break;
      case 1:
        showBuzz();
        break;
      case 2:
        showAccelerometer();
        break;
      case 3:
        setTime();
        break;
      case 4:
        setupWifi();
        break;
      /*case 5:
        showUpdateFW();
        break;*/
      case 5:
        showSyncNTP();
        break;
      case 6:
        _showClockTools();
        break;
      default:
        break;
      }
    } /*else if (guiState == FW_UPDATE_STATE) {
      updateFWBegin();
    }*/
  }
  // Back Button
  else if (wakeupBit & BACK_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // exit to watch face if already in menu
      RTC.read(currentTime);
      showWatchFace(false);
    } else if (guiState == APP_STATE) {
      showMenu(menuIndex, false); // exit to menu if already in app
    } else if (guiState == FW_UPDATE_STATE) {
      showMenu(menuIndex, false); // exit to menu if already in app
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }
  // Up Button
  else if (wakeupBit & UP_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // increment menu index
      menuIndex--;
      if (menuIndex < 0) {
        menuIndex = MENU_LENGTH - 1;
      }
      showMenu(menuIndex, true);
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }
  // Down Button
  else if (wakeupBit & DOWN_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // decrement menu index
      menuIndex++;
      if (menuIndex > MENU_LENGTH - 1) {
        menuIndex = 0;
      }
      showMenu(menuIndex, true);
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }

  /***************** fast menu *****************/
  bool timeout     = false;
  long lastTimeout = millis();
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  while (!timeout) {
    if (millis() - lastTimeout > 5000) {
      timeout = true;
    } else {
      if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState ==
            MAIN_MENU_STATE) { // if already in menu, then select menu item
          switch (menuIndex) {
          case 0:
            showAbout();
            break;
          case 1:
            showBuzz();
            break;
          case 2:
            showAccelerometer();
            break;
          case 3:
            setTime();
            break;
          case 4:
            setupWifi();
            break;
          /*case 5:
            showUpdateFW();
            break;*/
          case 5:
            showSyncNTP();
            break;
          case 6:
            _showClockTools();
            break;
          default:
            break;
          }
        }/* else if (guiState == FW_UPDATE_STATE) {
          updateFWBegin();
        }*/
      } else if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState ==
            MAIN_MENU_STATE) { // exit to watch face if already in menu
          RTC.read(currentTime);
          showWatchFace(false);
          break; // leave loop
        } else if (guiState == APP_STATE) {
          showMenu(menuIndex, false); // exit to menu if already in app
        } else if (guiState == FW_UPDATE_STATE) {
          showMenu(menuIndex, false); // exit to menu if already in app
        }
      } else if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // increment menu index
          menuIndex--;
          if (menuIndex < 0) {
            menuIndex = MENU_LENGTH - 1;
          }
          showFastMenu(menuIndex);
        }
      } else if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // decrement menu index
          menuIndex++;
          if (menuIndex > MENU_LENGTH - 1) {
            menuIndex = 0;
          }
          showFastMenu(menuIndex);
        }
      }
    }
  }
}

void Watchy::showMenu(byte menuIndex, bool partialRefresh) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *menuItems[] = {
      "About Watchy", "Vibrate Motor", "Show Accelerometer",
      "Set Time",     "Setup WiFi",    /*"Update Firmware",*/
      "Sync NTP",     "Clock Tools"};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == menuIndex) {
      display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.println(menuItems[i]);
    } else {
      display.setTextColor(GxEPD_WHITE);
      display.println(menuItems[i]);
    }
  }

  display.display(partialRefresh);

  guiState = MAIN_MENU_STATE;
  alreadyInMenu = false;
}

void Watchy::showFastMenu(byte menuIndex) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *menuItems[] = {
      "About Watchy", "Vibrate Motor", "Show Accelerometer",
      "Set Time",     "Setup WiFi",    /*"Update Firmware",*/
      "Sync NTP",     "Clock Tools"};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == menuIndex) {
      display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.println(menuItems[i]);
    } else {
      display.setTextColor(GxEPD_WHITE);
      display.println(menuItems[i]);
    }
  }

  display.display(true);

  guiState = MAIN_MENU_STATE;
}

void Watchy::_loadClockToolsState() {
  if (clockToolsData.magic == CLOCK_TOOLS_MAGIC) {
    return;
  }

  Preferences preferences;
  preferences.begin("clocktools", true);
  clockToolsData.magic = CLOCK_TOOLS_MAGIC;
  clockToolsData.alarmEnabled = preferences.getBool("alarmOn", false);
  clockToolsData.alarmHour = preferences.getUChar("alarmHour", 7);
  clockToolsData.alarmMinute = preferences.getUChar("alarmMinute", 0);
  clockToolsData.lastAlarmDay = preferences.getUInt("alarmDay", 0);
  clockToolsData.countdownActive = preferences.getBool("timerOn", false);
  clockToolsData.countdownPresetMinutes =
      preferences.getUShort("timerMins", 5);
  clockToolsData.countdownEnd = preferences.getUInt("timerEnd", 0);
  clockToolsData.stopwatchRunning = preferences.getBool("swRunning", false);
  clockToolsData.stopwatchStarted = preferences.getUInt("swStarted", 0);
  clockToolsData.stopwatchElapsed = preferences.getUInt("swElapsed", 0);
  clockToolsData.alertFlags = preferences.getUChar("alerts", 0) &
                              (CLOCK_ALERT_ALARM | CLOCK_ALERT_TIMER);
  clockToolsData.lastEventCheck = 0;
  preferences.end();

  if (clockToolsData.alarmHour > 23) {
    clockToolsData.alarmHour = 7;
  }
  if (clockToolsData.alarmMinute > 59) {
    clockToolsData.alarmMinute = 0;
  }
  if (clockToolsData.countdownPresetMinutes == 0 ||
      clockToolsData.countdownPresetMinutes > 1439) {
    clockToolsData.countdownPresetMinutes = 5;
  }
  if (clockToolsData.countdownEnd == 0) {
    clockToolsData.countdownActive = false;
  }
  if (clockToolsData.stopwatchStarted == 0) {
    clockToolsData.stopwatchRunning = false;
  }
}

void Watchy::_saveClockToolsState() {
  Preferences preferences;
  preferences.begin("clocktools", false);
  preferences.putBool("alarmOn", clockToolsData.alarmEnabled);
  preferences.putUChar("alarmHour", clockToolsData.alarmHour);
  preferences.putUChar("alarmMinute", clockToolsData.alarmMinute);
  preferences.putUInt("alarmDay", clockToolsData.lastAlarmDay);
  preferences.putBool("timerOn", clockToolsData.countdownActive);
  preferences.putUShort("timerMins", clockToolsData.countdownPresetMinutes);
  preferences.putUInt("timerEnd", clockToolsData.countdownEnd);
  preferences.putBool("swRunning", clockToolsData.stopwatchRunning);
  preferences.putUInt("swStarted", clockToolsData.stopwatchStarted);
  preferences.putUInt("swElapsed", clockToolsData.stopwatchElapsed);
  preferences.putUChar("alerts", clockToolsData.alertFlags);
  preferences.end();
}

uint32_t Watchy::_clockEpoch() {
  RTC.read(currentTime);
  return static_cast<uint32_t>(makeTime(currentTime));
}

bool Watchy::_checkClockToolsEvents() {
  bool triggered = false;
  bool stateChanged = false;

  if (clockToolsClockIsValid(currentTime)) {
    uint32_t now = static_cast<uint32_t>(makeTime(currentTime));
    uint32_t day =
        static_cast<uint32_t>(tmYearToCalendar(currentTime.Year)) * 512UL +
        static_cast<uint32_t>(currentTime.Month) * 32UL + currentTime.Day;
    uint16_t currentMinute = static_cast<uint16_t>(currentTime.Hour) * 60 +
                             currentTime.Minute;
    uint16_t alarmMinute =
        static_cast<uint16_t>(clockToolsData.alarmHour) * 60 +
        clockToolsData.alarmMinute;
    tmElements_t alarmTime = currentTime;
    alarmTime.Hour = clockToolsData.alarmHour;
    alarmTime.Minute = clockToolsData.alarmMinute;
    alarmTime.Second = 0;
    uint32_t alarmEpoch = static_cast<uint32_t>(makeTime(alarmTime));
    uint32_t alarmDay = day;
    if (now < alarmEpoch) {
      // The most recent occurrence was yesterday. This matters if an app was
      // left open across midnight when the alarm should have fired.
      alarmEpoch -= 86400UL;
      breakTime(static_cast<time_t>(alarmEpoch), alarmTime);
      alarmDay =
          static_cast<uint32_t>(tmYearToCalendar(alarmTime.Year)) * 512UL +
          static_cast<uint32_t>(alarmTime.Month) * 32UL + alarmTime.Day;
    }
    bool crossedAlarm = clockToolsData.lastEventCheck != 0 &&
                        clockToolsData.lastEventCheck < alarmEpoch &&
                        now >= alarmEpoch;
    bool inGracePeriod = currentMinute >= alarmMinute &&
                         currentMinute - alarmMinute < 5;

    // The crossing check catches alarms while another screen kept the watch
    // awake. The grace period also tolerates a delayed minute wake.
    if (clockToolsData.alarmEnabled &&
        (crossedAlarm || inGracePeriod) &&
        clockToolsData.lastAlarmDay != alarmDay) {
      clockToolsData.lastAlarmDay = alarmDay;
      clockToolsData.alertFlags |= CLOCK_ALERT_ALARM;
      triggered = true;
      stateChanged = true;
    }

    if (clockToolsData.countdownActive && now >= clockToolsData.countdownEnd) {
      clockToolsData.countdownActive = false;
      clockToolsData.countdownEnd = 0;
      clockToolsData.alertFlags |= CLOCK_ALERT_TIMER;
      triggered = true;
      stateChanged = true;
    }
    clockToolsData.lastEventCheck = now;
  } else if (clockToolsData.countdownActive ||
             clockToolsData.stopwatchRunning) {
    // V3's software RTC does not survive power loss. Do not leave restored
    // absolute timestamps running against an uninitialised clock.
    clockToolsData.countdownActive = false;
    clockToolsData.countdownEnd = 0;
    clockToolsData.stopwatchRunning = false;
    clockToolsData.stopwatchStarted = 0;
    stateChanged = true;
  }

  if (stateChanged) {
    _saveClockToolsState();
  }
  if (triggered) {
    _showClockToolsAlert();
    vibMotor(200, 16);
  }
  if (clockToolsData.alertFlags != 0) {
    if (guiState != CLOCK_ALERT_STATE) {
      _showClockToolsAlert();
    }
    guiState = CLOCK_ALERT_STATE;
    return true;
  }
  return false;
}

void Watchy::_showClockToolsAlert() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(18, 45);
  display.println("CLOCK ALERT");
  display.setCursor(18, 90);
  if (clockToolsData.alertFlags & CLOCK_ALERT_ALARM) {
    display.println("ALARM");
  }
  if (clockToolsData.alertFlags & CLOCK_ALERT_TIMER) {
    display.println("TIMER DONE");
  }
  display.setCursor(6, 165);
  display.println("Press any button");
  display.setCursor(32, 187);
  display.println("to dismiss");
  display.display(false);
  guiState = CLOCK_ALERT_STATE;
}

int8_t Watchy::_waitForClockToolsButton(uint32_t timeoutMs) {
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  uint32_t started = millis();
  uint32_t lastClockCheck = started;
  while (clockToolsButtonPressed(MENU_BTN_PIN) ||
         clockToolsButtonPressed(BACK_BTN_PIN) ||
         clockToolsButtonPressed(UP_BTN_PIN) ||
         clockToolsButtonPressed(DOWN_BTN_PIN)) {
    if (millis() - started >= timeoutMs) {
      return CLOCK_BUTTON_TIMEOUT;
    }
    delay(10);
  }

  while (millis() - started < timeoutMs) {
    if (millis() - lastClockCheck >= 1000) {
      RTC.read(currentTime);
      if (_checkClockToolsEvents()) {
        return CLOCK_BUTTON_ALERT;
      }
      lastClockCheck = millis();
    }

    int8_t button = CLOCK_BUTTON_TIMEOUT;
    uint8_t pin = 0;
    if (clockToolsButtonPressed(MENU_BTN_PIN)) {
      button = CLOCK_BUTTON_MENU;
      pin = MENU_BTN_PIN;
    } else if (clockToolsButtonPressed(BACK_BTN_PIN)) {
      button = CLOCK_BUTTON_BACK;
      pin = BACK_BTN_PIN;
    } else if (clockToolsButtonPressed(UP_BTN_PIN)) {
      button = CLOCK_BUTTON_UP;
      pin = UP_BTN_PIN;
    } else if (clockToolsButtonPressed(DOWN_BTN_PIN)) {
      button = CLOCK_BUTTON_DOWN;
      pin = DOWN_BTN_PIN;
    }

    if (button != CLOCK_BUTTON_TIMEOUT) {
      delay(30);
      if (!clockToolsButtonPressed(pin)) {
        continue;
      }
      while (clockToolsButtonPressed(pin) && millis() - started < timeoutMs) {
        delay(10);
      }
      return button;
    }
    delay(10);
  }
  return CLOCK_BUTTON_TIMEOUT;
}

void Watchy::_drawClockToolsMenu(uint8_t selected, bool partialRefresh) {
  uint32_t now = _clockEpoch();
  uint32_t stopwatchSeconds = clockToolsData.stopwatchElapsed;
  if (clockToolsData.stopwatchRunning && now >= clockToolsData.stopwatchStarted) {
    stopwatchSeconds += now - clockToolsData.stopwatchStarted;
  }

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(28, 20);
  display.println("CLOCK TOOLS");

  display.setCursor(0, 52);
  display.print(selected == 0 ? ">" : " ");
  display.print("Alarm ");
  printTwoDigits(clockToolsData.alarmHour);
  display.print(":");
  printTwoDigits(clockToolsData.alarmMinute);
  display.println(clockToolsData.alarmEnabled ? " ON" : " OFF");

  display.setCursor(0, 84);
  display.print(selected == 1 ? ">" : " ");
  display.print("Timer ");
  if (clockToolsData.countdownActive) {
    uint32_t remaining = now < clockToolsData.countdownEnd
                             ? (clockToolsData.countdownEnd - now + 59) / 60
                             : 0;
    printTwoDigits(remaining / 60);
    display.print(":");
    printTwoDigits(remaining % 60);
  } else {
    printTwoDigits(clockToolsData.countdownPresetMinutes / 60);
    display.print(":");
    printTwoDigits(clockToolsData.countdownPresetMinutes % 60);
  }

  display.setCursor(0, 116);
  display.print(selected == 2 ? ">" : " ");
  display.print("Stopwatch ");
  uint32_t stopwatchMinutes = stopwatchSeconds / 60;
  printTwoDigits(stopwatchMinutes / 60);
  display.print(":");
  printTwoDigits(stopwatchMinutes % 60);

  display.setCursor(12, 162);
  display.println("MENU: Select");
  display.setCursor(12, 187);
  display.println("BACK: Exit");
  display.display(partialRefresh);
  guiState = APP_STATE;
}

void Watchy::_showClockTools() {
  uint8_t selected = 0;
  _drawClockToolsMenu(selected, false);

  while (true) {
    int8_t button = _waitForClockToolsButton();
    if (button == CLOCK_BUTTON_ALERT) {
      return;
    }
    if (button == CLOCK_BUTTON_TIMEOUT || button == CLOCK_BUTTON_BACK) {
      showMenu(menuIndex, false);
      return;
    }
    if (button == CLOCK_BUTTON_UP) {
      selected = selected == 0 ? 2 : selected - 1;
      _drawClockToolsMenu(selected, true);
    } else if (button == CLOCK_BUTTON_DOWN) {
      selected = selected == 2 ? 0 : selected + 1;
      _drawClockToolsMenu(selected, true);
    } else if (button == CLOCK_BUTTON_MENU) {
      if (selected == 0) {
        _showAlarmEditor();
      } else if (selected == 1) {
        _showCountdownEditor();
      } else {
        _showStopwatch();
      }
      if (clockToolsData.alertFlags != 0) {
        return;
      }
      _drawClockToolsMenu(selected, false);
    }
  }
}

void Watchy::_showAlarmEditor() {
  uint8_t hour = clockToolsData.alarmHour;
  uint8_t minute = clockToolsData.alarmMinute;
  bool enabled = clockToolsData.alarmEnabled;
  uint8_t field = 0;

  while (true) {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(58, 22);
    display.println("ALARM");
    display.setCursor(12, 60);
    display.print(field == 0 ? ">" : " ");
    display.print("Hour:   ");
    printTwoDigits(hour);
    display.setCursor(12, 90);
    display.print(field == 1 ? ">" : " ");
    display.print("Minute: ");
    printTwoDigits(minute);
    display.setCursor(12, 120);
    display.print(field == 2 ? ">" : " ");
    display.print("Enabled: ");
    display.println(enabled ? "YES" : "NO");
    display.setCursor(3, 160);
    display.println("UP/DOWN: Change");
    display.setCursor(3, 187);
    display.println("MENU: Next/Save");
    display.display(true);

    int8_t button = _waitForClockToolsButton();
    if (button == CLOCK_BUTTON_ALERT || button == CLOCK_BUTTON_TIMEOUT ||
        button == CLOCK_BUTTON_BACK) {
      return;
    }
    if (button == CLOCK_BUTTON_UP || button == CLOCK_BUTTON_DOWN) {
      int8_t direction = button == CLOCK_BUTTON_DOWN ? 1 : -1;
      if (field == 0) {
        hour = (hour + direction + 24) % 24;
      } else if (field == 1) {
        minute = (minute + direction + 60) % 60;
      } else {
        enabled = !enabled;
      }
    } else if (button == CLOCK_BUTTON_MENU) {
      if (field < 2) {
        field++;
      } else {
        clockToolsData.alarmHour = hour;
        clockToolsData.alarmMinute = minute;
        clockToolsData.alarmEnabled = enabled;
        clockToolsData.lastAlarmDay = 0;
        RTC.read(currentTime);
        uint16_t nowMinute = static_cast<uint16_t>(currentTime.Hour) * 60 +
                             currentTime.Minute;
        uint16_t selectedMinute = static_cast<uint16_t>(hour) * 60 + minute;
        if (enabled && clockToolsClockIsValid(currentTime) &&
            nowMinute >= selectedMinute && nowMinute - selectedMinute < 5) {
          clockToolsData.lastAlarmDay =
              static_cast<uint32_t>(tmYearToCalendar(currentTime.Year)) * 512UL +
              static_cast<uint32_t>(currentTime.Month) * 32UL + currentTime.Day;
        }
        _saveClockToolsState();
        return;
      }
    }
  }
}

void Watchy::_showCountdownEditor() {
  if (clockToolsData.countdownActive) {
    while (true) {
      uint32_t now = _clockEpoch();
      uint32_t remaining = now < clockToolsData.countdownEnd
                               ? (clockToolsData.countdownEnd - now + 59) / 60
                               : 0;
      display.setFullWindow();
      display.fillScreen(GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(38, 35);
      display.println("TIMER RUNNING");
      display.setCursor(62, 85);
      printTwoDigits(remaining / 60);
      display.print(":");
      printTwoDigits(remaining % 60);
      display.setCursor(10, 145);
      display.println("MENU: Cancel");
      display.setCursor(10, 175);
      display.println("BACK: Tools");
      display.display(true);

      int8_t button = _waitForClockToolsButton();
      if (button == CLOCK_BUTTON_ALERT || button == CLOCK_BUTTON_TIMEOUT ||
          button == CLOCK_BUTTON_BACK) {
        return;
      }
      if (button == CLOCK_BUTTON_MENU) {
        clockToolsData.countdownActive = false;
        clockToolsData.countdownEnd = 0;
        _saveClockToolsState();
        return;
      }
    }
  }

  uint8_t hours = clockToolsData.countdownPresetMinutes / 60;
  uint8_t minutes = clockToolsData.countdownPresetMinutes % 60;
  uint8_t field = 0;
  while (true) {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(58, 22);
    display.println("TIMER");
    display.setCursor(12, 70);
    display.print(field == 0 ? ">" : " ");
    display.print("Hours:   ");
    printTwoDigits(hours);
    display.setCursor(12, 105);
    display.print(field == 1 ? ">" : " ");
    display.print("Minutes: ");
    printTwoDigits(minutes);
    display.setCursor(3, 155);
    display.println("UP/DOWN: Change");
    display.setCursor(3, 185);
    display.println("MENU: Next/Start");
    display.display(true);

    int8_t button = _waitForClockToolsButton();
    if (button == CLOCK_BUTTON_ALERT || button == CLOCK_BUTTON_TIMEOUT ||
        button == CLOCK_BUTTON_BACK) {
      return;
    }
    if (button == CLOCK_BUTTON_UP || button == CLOCK_BUTTON_DOWN) {
      int8_t direction = button == CLOCK_BUTTON_DOWN ? 1 : -1;
      if (field == 0) {
        hours = (hours + direction + 24) % 24;
      } else {
        minutes = (minutes + direction + 60) % 60;
      }
    } else if (button == CLOCK_BUTTON_MENU) {
      if (field == 0) {
        field = 1;
      } else {
        uint16_t duration = static_cast<uint16_t>(hours) * 60 + minutes;
        if (duration == 0) {
          duration = 1;
        }
        clockToolsData.countdownPresetMinutes = duration;
        clockToolsData.countdownEnd = _clockEpoch() +
                                      static_cast<uint32_t>(duration) * 60UL;
        clockToolsData.countdownActive = true;
        _saveClockToolsState();
        return;
      }
    }
  }
}

void Watchy::_showStopwatch() {
  while (true) {
    uint32_t now = _clockEpoch();
    uint32_t elapsed = clockToolsData.stopwatchElapsed;
    if (clockToolsData.stopwatchRunning && now >= clockToolsData.stopwatchStarted) {
      elapsed += now - clockToolsData.stopwatchStarted;
    }
    uint32_t elapsedMinutes = elapsed / 60;

    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(42, 28);
    display.println("STOPWATCH");
    display.setCursor(62, 75);
    printTwoDigits(elapsedMinutes / 60);
    display.print(":");
    printTwoDigits(elapsedMinutes % 60);
    display.setCursor(55, 108);
    display.println(clockToolsData.stopwatchRunning ? "RUNNING" : "PAUSED");
    display.setCursor(4, 145);
    display.println("MENU: Start/Pause");
    display.setCursor(4, 170);
    display.println("DOWN: Reset");
    display.setCursor(4, 195);
    display.println("BACK: Tools");
    display.display(true);

    int8_t button = _waitForClockToolsButton();
    if (button == CLOCK_BUTTON_ALERT || button == CLOCK_BUTTON_TIMEOUT ||
        button == CLOCK_BUTTON_BACK) {
      return;
    }
    if (button == CLOCK_BUTTON_MENU) {
      if (clockToolsData.stopwatchRunning) {
        if (now >= clockToolsData.stopwatchStarted) {
          clockToolsData.stopwatchElapsed +=
              now - clockToolsData.stopwatchStarted;
        }
        clockToolsData.stopwatchRunning = false;
        clockToolsData.stopwatchStarted = 0;
      } else {
        clockToolsData.stopwatchStarted = now;
        clockToolsData.stopwatchRunning = true;
      }
      _saveClockToolsState();
    } else if (button == CLOCK_BUTTON_DOWN) {
      clockToolsData.stopwatchElapsed = 0;
      clockToolsData.stopwatchStarted =
          clockToolsData.stopwatchRunning ? now : 0;
      _saveClockToolsState();
    }
  }
}

void Watchy::_rebaseClockTools(uint32_t oldEpoch, uint32_t newEpoch) {
  constexpr uint32_t MIN_VALID_EPOCH = 1577836800UL; // 2020-01-01
  if (oldEpoch < MIN_VALID_EPOCH || newEpoch < MIN_VALID_EPOCH) {
    bool changed = clockToolsData.countdownActive ||
                   clockToolsData.stopwatchRunning;
    clockToolsData.countdownActive = false;
    clockToolsData.countdownEnd = 0;
    clockToolsData.stopwatchRunning = false;
    clockToolsData.stopwatchStarted = 0;
    if (changed) {
      _saveClockToolsState();
    }
    return;
  }
  if (oldEpoch == newEpoch) {
    return;
  }
  int64_t delta = static_cast<int64_t>(newEpoch) - oldEpoch;
  bool changed = false;

  tmElements_t oldTime;
  tmElements_t newTime;
  breakTime(static_cast<time_t>(oldEpoch), oldTime);
  breakTime(static_cast<time_t>(newEpoch), newTime);
  if (oldTime.Year != newTime.Year || oldTime.Month != newTime.Month ||
      oldTime.Day != newTime.Day) {
    clockToolsData.lastAlarmDay = 0;
    changed = true;
  }

  if (clockToolsData.lastEventCheck != 0) {
    int64_t rebasedCheck =
        static_cast<int64_t>(clockToolsData.lastEventCheck) + delta;
    clockToolsData.lastEventCheck =
        rebasedCheck > 0 && rebasedCheck <= UINT32_MAX
            ? static_cast<uint32_t>(rebasedCheck)
            : newEpoch;
  }

  if (clockToolsData.countdownActive) {
    int64_t rebased = static_cast<int64_t>(clockToolsData.countdownEnd) + delta;
    if (rebased > 0 && rebased <= UINT32_MAX) {
      clockToolsData.countdownEnd = static_cast<uint32_t>(rebased);
    } else {
      clockToolsData.countdownActive = false;
      clockToolsData.countdownEnd = 0;
    }
    changed = true;
  }
  if (clockToolsData.stopwatchRunning) {
    int64_t rebased = static_cast<int64_t>(clockToolsData.stopwatchStarted) + delta;
    if (rebased > 0 && rebased <= UINT32_MAX) {
      clockToolsData.stopwatchStarted = static_cast<uint32_t>(rebased);
    } else {
      clockToolsData.stopwatchRunning = false;
      clockToolsData.stopwatchStarted = 0;
    }
    changed = true;
  }
  if (changed) {
    _saveClockToolsState();
  }
}

void Watchy::showAbout() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 20);

  display.print("LibVer: ");
  display.println(WATCHY_LIB_VER);

  display.print("Rev: v");
  display.println(getBoardRevision());

  display.print("Batt: ");
  float voltage = getBatteryVoltage();
  display.print(voltage);
  display.println("V");

  #ifndef ARDUINO_ESP32S3_DEV
  display.print("Uptime: ");
  RTC.read(currentTime);
  time_t b = makeTime(bootTime);
  time_t c = makeTime(currentTime);
  int totalSeconds = c-b;
  //int seconds = (totalSeconds % 60);
  int minutes = (totalSeconds % 3600) / 60;
  int hours = (totalSeconds % 86400) / 3600;
  int days = (totalSeconds % (86400 * 30)) / 86400; 
  display.print(days);
  display.print("d");
  display.print(hours);
  display.print("h");
  display.print(minutes);
  display.println("m");  
  #endif
  
  if(WIFI_CONFIGURED){
    display.print("SSID: ");
    display.println(lastSSID);
    display.print("IP: ");
    display.println(IPAddress(lastIPAddress).toString());
  }else{
    display.println("WiFi Not Connected");
  }
  display.display(false); // full refresh

  guiState = APP_STATE;
}

void Watchy::showBuzz() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(70, 80);
  display.println("Buzz!");
  display.display(false); // full refresh
  vibMotor();
  showMenu(menuIndex, false);
}

void Watchy::vibMotor(uint8_t intervalMs, uint8_t length) {
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  bool motorOn = false;
  for (int i = 0; i < length; i++) {
    motorOn = !motorOn;
    digitalWrite(VIB_MOTOR_PIN, motorOn);
    delay(intervalMs);
  }
}

void Watchy::setTime() {

  guiState = APP_STATE;

  RTC.read(currentTime);

  int8_t minute = currentTime.Minute;
  int8_t hour   = currentTime.Hour;
  int8_t day    = currentTime.Day;
  int8_t month  = currentTime.Month;
  const int calendarYear = tmYearToCalendar(currentTime.Year);
  int8_t year = calendarYear >= 2000 && calendarYear <= 2099
                    ? calendarYear - 2000
                    : 0;

  int8_t setIndex = SET_HOUR;

  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  display.setFullWindow();

  bool clockAlert = false;
  uint32_t lastClockCheck = millis();
  while (1) {
    if (millis() - lastClockCheck >= 1000) {
      RTC.read(currentTime);
      if (_checkClockToolsEvents()) {
        clockAlert = true;
        break;
      }
      lastClockCheck = millis();
    }

    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > SET_DAY) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != SET_HOUR) {
        setIndex--;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_HOUR:
        hour == 23 ? (hour = 0) : hour++;
        break;
      case SET_MINUTE:
        minute == 59 ? (minute = 0) : minute++;
        break;
      case SET_YEAR:
        year == 99 ? (year = 0) : year++;
        break;
      case SET_MONTH:
        month == 12 ? (month = 1) : month++;
        break;
      case SET_DAY:
        day == 31 ? (day = 1) : day++;
        break;
      default:
        break;
      }
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_HOUR:
        hour == 0 ? (hour = 23) : hour--;
        break;
      case SET_MINUTE:
        minute == 0 ? (minute = 59) : minute--;
        break;
      case SET_YEAR:
        year == 0 ? (year = 99) : year--;
        break;
      case SET_MONTH:
        month == 1 ? (month = 12) : month--;
        break;
      case SET_DAY:
        day == 1 ? (day = 31) : day--;
        break;
      default:
        break;
      }
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&DSEG7_Classic_Bold_53);

    display.setCursor(5, 80);
    if (setIndex == SET_HOUR) { // blink hour digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (hour < 10) {
      display.print("0");
    }
    display.print(hour);

    display.setTextColor(GxEPD_WHITE);
    display.print(":");

    display.setCursor(108, 80);
    if (setIndex == SET_MINUTE) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (minute < 10) {
      display.print("0");
    }
    display.print(minute);

    display.setTextColor(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(45, 150);
    if (setIndex == SET_YEAR) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    display.print(2000 + year);

    display.setTextColor(GxEPD_WHITE);
    display.print("/");

    if (setIndex == SET_MONTH) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (month < 10) {
      display.print("0");
    }
    display.print(month);

    display.setTextColor(GxEPD_WHITE);
    display.print("/");

    if (setIndex == SET_DAY) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (day < 10) {
      display.print("0");
    }
    display.print(day);
    display.display(true); // partial refresh
  }

  if (clockAlert) {
    return;
  }

  tmElements_t tm;
  tm.Month  = month;
  tm.Day    = day;
  tm.Year   = y2kYearToTm(year);
  tm.Hour   = hour;
  tm.Minute = minute;
  tm.Second = 0;

  tmElements_t oldTime;
  RTC.read(oldTime);
  uint32_t oldEpoch = static_cast<uint32_t>(makeTime(oldTime));
  RTC.set(tm);
  _rebaseClockTools(oldEpoch, static_cast<uint32_t>(makeTime(tm)));

  showMenu(menuIndex, false);
}

void Watchy::showAccelerometer() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);

  Accel acc;

  long previousMillis = 0;
  long interval       = 200;
  uint32_t lastClockCheck = millis();
  bool clockAlert = false;

  guiState = APP_STATE;

  pinMode(BACK_BTN_PIN, INPUT);

  while (1) {

    unsigned long currentMillis = millis();

    if (currentMillis - lastClockCheck >= 1000) {
      RTC.read(currentTime);
      if (_checkClockToolsEvents()) {
        clockAlert = true;
        break;
      }
      lastClockCheck = currentMillis;
    }

    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      break;
    }

    if (currentMillis - previousMillis > interval) {
      previousMillis = currentMillis;
      // Get acceleration data
      bool res          = sensor.getAccel(acc);
      uint8_t direction = sensor.getDirection();
      display.fillScreen(GxEPD_BLACK);
      display.setCursor(0, 30);
      if (res == false) {
        display.println("getAccel FAIL");
      } else {
        display.print("  X:");
        display.println(acc.x);
        display.print("  Y:");
        display.println(acc.y);
        display.print("  Z:");
        display.println(acc.z);

        display.setCursor(30, 130);
        switch (direction) {
        case DIRECTION_DISP_DOWN:
          display.println("FACE DOWN");
          break;
        case DIRECTION_DISP_UP:
          display.println("FACE UP");
          break;
        case DIRECTION_BOTTOM_EDGE:
          display.println("BOTTOM EDGE");
          break;
        case DIRECTION_TOP_EDGE:
          display.println("TOP EDGE");
          break;
        case DIRECTION_RIGHT_EDGE:
          display.println("RIGHT EDGE");
          break;
        case DIRECTION_LEFT_EDGE:
          display.println("LEFT EDGE");
          break;
        default:
          display.println("ERROR!!!");
          break;
        }
      }
      display.display(true); // full refresh
    }
  }

  if (!clockAlert) {
    showMenu(menuIndex, false);
  }
}

void Watchy::showWatchFace(bool partialRefresh) {
  display.setFullWindow();
  // At this point it is sure we are going to update
  display.epd2.asyncPowerOn();
  drawWatchFace();
  display.display(partialRefresh); // partial refresh
  guiState = WATCHFACE_STATE;
}

void Watchy::drawWatchFace() {
  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(5, 53 + 60);
  if (currentTime.Hour < 10) {
    display.print("0");
  }
  display.print(currentTime.Hour);
  display.print(":");
  if (currentTime.Minute < 10) {
    display.print("0");
  }
  display.println(currentTime.Minute);
}

weatherData Watchy::getWeatherData() {
  return _getWeatherData(settings.cityID, settings.lat, settings.lon,
    settings.weatherUnit, settings.weatherLang, settings.weatherURL,
    settings.weatherAPIKey, settings.weatherUpdateInterval);
}

weatherData Watchy::_getWeatherData(String cityID, String lat, String lon, String units, String lang,
                                   String url, String apiKey,
                                   uint16_t updateInterval) {
  currentWeather.isMetric = units == String("metric");
  if (weatherIntervalCounter < 0) { //-1 on first run, set to updateInterval
    weatherIntervalCounter = updateInterval;
  }
  if (weatherIntervalCounter >=
      updateInterval) { // only update if WEATHER_UPDATE_INTERVAL has elapsed
                        // i.e. 30 minutes
    if (connectWiFi()) {
      HTTPClient http; // Use Weather API for live data if WiFi is connected
      http.setConnectTimeout(3000); // 3 second max timeout
      String weatherQueryURL = url;
      if(cityID != ""){
        weatherQueryURL.replace("{cityID}", cityID);
      }else{
        weatherQueryURL.replace("{lat}", lat);
        weatherQueryURL.replace("{lon}", lon);
      }
      weatherQueryURL.replace("{units}", units);
      weatherQueryURL.replace("{lang}", lang);
      weatherQueryURL.replace("{apiKey}", apiKey);
      http.begin(weatherQueryURL.c_str());
      int httpResponseCode = http.GET();
      if (httpResponseCode == 200) {
        String payload             = http.getString();
        JSONVar responseObject     = JSON.parse(payload);
        currentWeather.temperature = int(responseObject["main"]["temp"]);
        currentWeather.weatherConditionCode =
            int(responseObject["weather"][0]["id"]);
        currentWeather.weatherDescription =
		        JSONVar::stringify(responseObject["weather"][0]["main"]);
	      currentWeather.external = true;
		        breakTime((time_t)(int)responseObject["sys"]["sunrise"], currentWeather.sunrise);
		        breakTime((time_t)(int)responseObject["sys"]["sunset"], currentWeather.sunset);
        // sync NTP during weather API call and use timezone of lat & lon
        gmtOffset = int(responseObject["timezone"]);
        syncNTP(gmtOffset);
      } else {
        // http error
      }
      http.end();
      // turn off radios
      WiFi.mode(WIFI_OFF);
      btStop();
    } else { // No WiFi, use internal temperature sensor
      uint8_t temperature = sensor.readTemperature(); // celsius
      if (!currentWeather.isMetric) {
        temperature = temperature * 9. / 5. + 32.; // fahrenheit
      }
      currentWeather.temperature          = temperature;
      currentWeather.weatherConditionCode = 800;
      currentWeather.external             = false;
    }
    weatherIntervalCounter = 0;
  } else {
    weatherIntervalCounter++;
  }
  return currentWeather;
}

float Watchy::getBatteryVoltage() {
  #ifdef ARDUINO_ESP32S3_DEV
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * ADC_VOLTAGE_DIVIDER;
  #else
  if (RTC.rtcType == DS3231) {
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f *
           2.0f; // Battery voltage goes through a 1/2 divider.
  } else {
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * 2.0f;
  }
  #endif
}

uint8_t Watchy::getBoardRevision() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  if(chip_info.model == CHIP_ESP32){ //Revision 1.0 - 2.0
    Wire.beginTransmission(0x68); //v1.0 has DS3231
    if (Wire.endTransmission() == 0){
      return 10;
    }
    delay(1);
    Wire.beginTransmission(0x51); //v1.5 and v2.0 have PCF8563
    if (Wire.endTransmission() == 0){
        pinMode(35, INPUT);
        if(digitalRead(35) == 0){
          return 20; //in rev 2.0, pin 35 is BTN 3 and has a pulldown
        }else{
          return 15; //in rev 1.5, pin 35 is the battery ADC
        }
    }
  }
  if(chip_info.model == CHIP_ESP32S3){ //Revision 3.0
    return 30;
  }
  return -1;
}

uint16_t Watchy::_readRegister(uint8_t address, uint8_t reg, uint8_t *data,
                               uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)address, (uint8_t)len);
  uint8_t i = 0;
  while (Wire.available()) {
    data[i++] = Wire.read();
  }
  return 0;
}

uint16_t Watchy::_writeRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data, len);
  return (0 != Wire.endTransmission());
}

void Watchy::_bmaConfig() {

  if (sensor.begin(_readRegister, _writeRegister, delay) == false) {
    // fail to init BMA
    return;
  }

  // Accel parameter structure
  Acfg cfg;
  /*!
      Output data rate in Hz, Optional parameters:
          - BMA4_OUTPUT_DATA_RATE_0_78HZ
          - BMA4_OUTPUT_DATA_RATE_1_56HZ
          - BMA4_OUTPUT_DATA_RATE_3_12HZ
          - BMA4_OUTPUT_DATA_RATE_6_25HZ
          - BMA4_OUTPUT_DATA_RATE_12_5HZ
          - BMA4_OUTPUT_DATA_RATE_25HZ
          - BMA4_OUTPUT_DATA_RATE_50HZ
          - BMA4_OUTPUT_DATA_RATE_100HZ
          - BMA4_OUTPUT_DATA_RATE_200HZ
          - BMA4_OUTPUT_DATA_RATE_400HZ
          - BMA4_OUTPUT_DATA_RATE_800HZ
          - BMA4_OUTPUT_DATA_RATE_1600HZ
  */
  cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  /*!
      G-range, Optional parameters:
          - BMA4_ACCEL_RANGE_2G
          - BMA4_ACCEL_RANGE_4G
          - BMA4_ACCEL_RANGE_8G
          - BMA4_ACCEL_RANGE_16G
  */
  cfg.range = BMA4_ACCEL_RANGE_2G;
  /*!
      Bandwidth parameter, determines filter configuration, Optional parameters:
          - BMA4_ACCEL_OSR4_AVG1
          - BMA4_ACCEL_OSR2_AVG2
          - BMA4_ACCEL_NORMAL_AVG4
          - BMA4_ACCEL_CIC_AVG8
          - BMA4_ACCEL_RES_AVG16
          - BMA4_ACCEL_RES_AVG32
          - BMA4_ACCEL_RES_AVG64
          - BMA4_ACCEL_RES_AVG128
  */
  cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;

  /*! Filter performance mode , Optional parameters:
      - BMA4_CIC_AVG_MODE
      - BMA4_CONTINUOUS_MODE
  */
  cfg.perf_mode = BMA4_CONTINUOUS_MODE;

  // Configure the BMA423 accelerometer
  sensor.setAccelConfig(cfg);

  // Enable BMA423 accelerometer
  // Warning : Need to use feature, you must first enable the accelerometer
  // Warning : Need to use feature, you must first enable the accelerometer
  sensor.enableAccel();

  struct bma4_int_pin_config config;
  config.edge_ctrl = BMA4_LEVEL_TRIGGER;
  config.lvl       = BMA4_ACTIVE_HIGH;
  config.od        = BMA4_PUSH_PULL;
  config.output_en = BMA4_OUTPUT_ENABLE;
  config.input_en  = BMA4_INPUT_DISABLE;
  // The correct trigger interrupt needs to be configured as needed
  sensor.setINTPinConfig(config, BMA4_INTR1_MAP);

  struct bma423_axes_remap remap_data;
  remap_data.x_axis      = 1;
  remap_data.x_axis_sign = 0xFF;
  remap_data.y_axis      = 0;
  remap_data.y_axis_sign = 0xFF;
  remap_data.z_axis      = 2;
  remap_data.z_axis_sign = 0xFF;
  // Need to raise the wrist function, need to set the correct axis
  sensor.setRemapAxes(&remap_data);

  // Enable BMA423 isStepCounter feature
  sensor.enableFeature(BMA423_STEP_CNTR, true);
  // Enable BMA423 isTilt feature
  sensor.enableFeature(BMA423_TILT, true);
  // Enable BMA423 isDoubleClick feature
  sensor.enableFeature(BMA423_WAKEUP, true);

  // Reset steps
  sensor.resetStepCounter();

  // Turn on feature interrupt
  sensor.enableStepCountInterrupt();
  sensor.enableTiltInterrupt();
  // It corresponds to isDoubleClick interrupt
  sensor.enableWakeupInterrupt();
}

void Watchy::setupWifi() {
  display.epd2.setBusyCallback(0); // temporarily disable lightsleep on busy
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setTimeout(WIFI_AP_TIMEOUT);
  wifiManager.setAPCallback(_configModeCallback);
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  if (!wifiManager.autoConnect(WIFI_AP_SSID)) { // WiFi setup failed
    display.println("Setup failed &");
    display.println("timed out!");
  } else {
    display.println("Connected to:");
    display.println(WiFi.SSID());
		display.println("Local IP:");
		display.println(WiFi.localIP());
    weatherIntervalCounter = -1; // Reset to force weather to be read again
    lastIPAddress = WiFi.localIP();
    WiFi.SSID().toCharArray(lastSSID, 30);
  }
  display.display(false); // full refresh
  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  // enable lightsleep on busy
  display.epd2.setBusyCallback(WatchyDisplay::busyCallback);
  guiState = APP_STATE;
}

void Watchy::_configModeCallback(WiFiManager *myWiFiManager) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Connect to");
  display.print("SSID: ");
  display.println(WIFI_AP_SSID);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
	display.println("MAC address:");
	display.println(WiFi.softAPmacAddress().c_str());
  display.display(false); // full refresh
}

bool Watchy::connectWiFi() {
  return connectWiFi(60000);
}

bool Watchy::connectWiFi(uint32_t timeoutMs) {
  if (WL_CONNECT_FAILED ==
      WiFi.begin()) { // WiFi not setup, you can also use hard coded credentials
                      // with WiFi.begin(SSID,PASS);
    WIFI_CONFIGURED = false;
  } else {
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      lastIPAddress = WiFi.localIP();
      WiFi.SSID().toCharArray(lastSSID, 30);
      WIFI_CONFIGURED = true;
    } else {
      WIFI_CONFIGURED = false;
    }
  }
  if (!WIFI_CONFIGURED) {
    WiFi.mode(WIFI_OFF);
    btStop();
  }
  return WIFI_CONFIGURED;
}
/*
void Watchy::showUpdateFW() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Please visit");
  display.println("watchy.sqfmi.com");
  display.println("with a Bluetooth");
  display.println("enabled device");
  display.println(" ");
  display.println("Press menu button");
  display.println("again when ready");
  display.println(" ");
  display.println("Keep USB powered");
  display.display(false); // full refresh

  guiState = FW_UPDATE_STATE;
}

void Watchy::updateFWBegin() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Bluetooth Started");
  display.println(" ");
  display.println("Watchy BLE OTA");
  display.println(" ");
  display.println("Waiting for");
  display.println("connection...");
  display.display(false); // full refresh

  BLE BT;
  BT.begin("Watchy BLE OTA");
  int prevStatus = -1;
  int currentStatus;

  while (1) {
    currentStatus = BT.updateStatus();
    if (prevStatus != currentStatus || prevStatus == 1) {
      if (currentStatus == 0) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("BLE Connected!");
        display.println(" ");
        display.println("Waiting for");
        display.println("upload...");
        display.display(false); // full refresh
      }
      if (currentStatus == 1) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("Downloading");
        display.println("firmware:");
        display.println(" ");
        display.print(BT.howManyBytes());
        display.println(" bytes");
        display.display(true); // partial refresh
      }
      if (currentStatus == 2) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("Download");
        display.println("completed!");
        display.println(" ");
        display.println("Rebooting...");
        display.display(false); // full refresh

        delay(2000);
        esp_restart();
      }
      if (currentStatus == 4) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("BLE Disconnected!");
        display.println(" ");
        display.println("exiting...");
        display.display(false); // full refresh
        delay(1000);
        break;
      }
      prevStatus = currentStatus;
    }
    delay(100);
  }

  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  showMenu(menuIndex, false);
}
*/
void Watchy::showSyncNTP() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Syncing NTP... ");
  display.print("GMT offset: ");
  display.println(gmtOffset);
  display.display(false); // full refresh
  if (connectWiFi()) {
    if (syncNTP()) {
      display.println("NTP Sync Success\n");
      display.println("Current Time Is:");

      RTC.read(currentTime);

      display.print(tmYearToCalendar(currentTime.Year));
      display.print("/");
      display.print(currentTime.Month);
      display.print("/");
      display.print(currentTime.Day);
      display.print(" - ");

      if (currentTime.Hour < 10) {
        display.print("0");
      }
      display.print(currentTime.Hour);
      display.print(":");
      if (currentTime.Minute < 10) {
        display.print("0");
      }
      display.println(currentTime.Minute);
    } else {
      display.println("NTP Sync Failed");
    }
    WiFi.mode(WIFI_OFF);
    btStop();
  } else {
    display.println("WiFi Not Configured");
  }
  display.display(true); // full refresh
  delay(3000);
  showMenu(menuIndex, false);
}

bool Watchy::syncNTP() { // NTP sync - call after connecting to WiFi and
                         // remember to turn it back off
  return syncNTP(gmtOffset,
                 settings.ntpServer.c_str());
}

bool Watchy::syncNTP(long gmt) {
  return syncNTP(gmt, settings.ntpServer.c_str());
}

void Watchy::setTimezoneOffset(long gmt) {
  gmtOffset = gmt;
  settings.gmtOffset = gmt;
}

bool Watchy::syncNTP(long gmt, String ntpServer) {
  // NTP sync - call after connecting to
  // WiFi and remember to turn it back off
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, ntpServer.c_str(), gmt);
  timeClient.begin();
  if (!timeClient.forceUpdate()) {
    return false; // NTP sync failed
  }
  tmElements_t oldTime;
  RTC.read(oldTime);
  uint32_t oldEpoch = static_cast<uint32_t>(makeTime(oldTime));
  tmElements_t tm;
  breakTime((time_t)timeClient.getEpochTime(), tm);
  RTC.set(tm);
  _rebaseClockTools(oldEpoch, static_cast<uint32_t>(makeTime(tm)));
  return true;
}
