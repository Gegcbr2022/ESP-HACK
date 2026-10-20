#include "subghz.h"
#include "menu/subghz.h"
#include "explorer.h"
#include "interface/interface.h"
#include "display.h"
#include <SD.h>
#include <SPI.h>
#include <RCSwitch.h>
#include <math.h>
#include "research/raw_capture.h"
#include "research/legacy_guard.h"
#include "research/lab_suite.h"
#include "../shared/chat_radio.h"

// CC1101
#define DEFAULT_RF_FREQUENCY 433.92 // MHz
float frequency = DEFAULT_RF_FREQUENCY;
const float frequencies[] = {315.0, 433.92, 868.0, 915.0};
const int numFrequencies = sizeof(frequencies) / sizeof(frequencies[0]);
int freqIndex = 1; // Default 433.92 MHz

// RCSwitch
RCSwitch rcswitch = RCSwitch();


byte menuIndex = 0;
bool validKeyReceived = false;
bool readRAW = true;
bool autoSave = false;
int signals = 0;
uint64_t lastSavedKey = 0;
tpKeyData keyData1;
static String legacyFileLoadError;
bool isJamming = false;

// Bruteforce state
const char* bruteTypes[] = {"Came", "Nice", "Ansonic", "Holtek", "Chamberlain"};
const int BRUTE_TYPE_COUNT = 5;
const float bruteFreqOptions[] = {315.0, 433.92, 868.0, 915.0};
const char* bruteFreqLabels[] = {"315.00MHz", "433.92MHz", "868.00MHz", "915.00MHz"};
const int BRUTE_FREQ_COUNT = 4;
const int bruteBits = 12;
int bruteTypeIndex = 0;
int bruteFreqIndex = 1;
int bruteConfigSelection = 0;
uint16_t bruteProgress = 0;
const uint16_t bruteTotal = 4096;
unsigned long bruteLastStep = 0;
bool bruteRunning = false;
bool bruteRfActive = false;
int bruteTxPin = CC1101_GDO0;

const int cameZero[] = {-320, 640};
const int cameOne[] = {-640, 320};
const int camePilot[] = {-11520, 320};
const BruteProtocol protoCame = {cameZero, 2, cameOne, 2, camePilot, 2, nullptr, 0};

const int niceZero[] = {-700, 1400};
const int niceOne[] = {-1400, 700};
const int nicePilot[] = {-25200, 700};
const BruteProtocol protoNice = {niceZero, 2, niceOne, 2, nicePilot, 2, nullptr, 0};

const int ansonicZero[] = {-1111, 555};
const int ansonicOne[] = {-555, 1111};
const int ansonicPilot[] = {-19425, 555};
const BruteProtocol protoAnsonic = {ansonicZero, 2, ansonicOne, 2, ansonicPilot, 2, nullptr, 0};

const int holtekZero[] = {-870, 430};
const int holtekOne[] = {-430, 870};
const int holtekPilot[] = {-15480, 430};
const BruteProtocol protoHoltek = {holtekZero, 2, holtekOne, 2, holtekPilot, 2, nullptr, 0};

const int chamberZero[] = {-870, 430};
const int chamberOne[] = {-430, 870};
const int chamberStop[] = {-3000, 1000};
const BruteProtocol protoChamber = {chamberZero, 2, chamberOne, 2, nullptr, 0, chamberStop, 2};
const BruteProtocol* currentBruteProto = nullptr;

#define MAX_FILES 50
static const char* subExts[] = {".sub"};
ExplorerEntry subFileList[MAX_FILES];
ExplorerState subExplorer;
ExplorerConfig subExplorerCfg = {"/subghz", subExts, 1, true, false, true, true};
int nextSignalIndex = 1;
bool nextSignalIndexReady = false;

#define CC1101_SPI_MODULE 1

struct BruceConfig {
  int rfModule = CC1101_SPI_MODULE;
  int rfTx = CC1101_GDO0;
} bruceConfig;

struct BruceConfigPins {
  struct {
    int io0 = CC1101_GDO0;
  } CC1101_bus;
} bruceConfigPins;

extern byte getSubGHzCC1101GDO0Pin();
extern byte getSubGHzCC1101CSPin();

static byte cc1101GDO0Pin = CC1101_GDO0;
static byte cc1101CSPin = CC1101_CS;

const float analyzerFreqs[] = {
  300.0, 302.75, 303.0, 303.87, 303.90, 304.25,
  307.0, 307.50, 307.80, 309.0, 310.0,
  312.0, 312.1, 312.2, 313.0, 313.85, 314.0, 314.35, 314.98, 315.0,
  318.0, 320.0, 320.15,
  330.0, 345.0, 348.0, 350.0,
  387.0, 390.0,
  418.0,
  430.0, 430.5, 431.0, 431.5,
  433.22, 433.42, 433.65, 433.88, 433.92,
  434.07, 434.17, 434.19, 434.39, 434.42, 434.62, 434.77,
  438.90, 440.17,
  462.75, 464.0, 467.75,
  779.0,
  868.35, 868.4, 868.46, 868.80, 868.95,
  906.4
};
const int ANALYZER_FREQ_COUNT = sizeof(analyzerFreqs) / sizeof(analyzerFreqs[0]);
const float ANALYZER_RSSI_LOW = -97.0f;
const float ANALYZER_RSSI_HIGH = -30.0f;
const float ANALYZER_RSSI_MUL = 2.3f;
const float ANALYZER_DEFAULT_TRIG = -75.0f;
const uint8_t ANALYZER_HIST_CNT = 4;
const int ANALYZER_TRIG_STEP = 5;
const uint16_t ANALYZER_STEP_DELAY_US = 3200;
const float ANALYZER_RSSI_GRAPH_SCALE = 2.0f;
const float ANALYZER_LOCK_MARGIN_DB = 6.0f;

bool analyzerIsNoiseFreq(uint32_t freqHz) {
  return
    (freqHz >= 310500000UL && freqHz <= 312300000UL) ||
    (freqHz >= 467500000UL && freqHz <= 468200000UL);
}

struct AnalyzerScanData {
  float rough_rssi = -127.0f;
  uint32_t rough_freq = 0;
  float fine_rssi = -127.0f;
  uint32_t fine_freq = 0;
};

struct AnalyzerState {
  uint32_t curr_freq = 0;
  uint32_t saved_freq = 0;
  float rssi_now = 0.0f;
  uint32_t hist_freq[ANALYZER_HIST_CNT] = {0};
  uint8_t hist_count[ANALYZER_HIST_CNT] = {0};
  bool has_signal = false;
  float last_rssi = 0.0f;
  float threshold = ANALYZER_DEFAULT_TRIG;
};

AnalyzerScanData analyzerScanResult;
AnalyzerState analyzerState;
float analyzerFilterVal = 0.0f;
float analyzerTrigLevel = ANALYZER_DEFAULT_TRIG;
uint8_t analyzerHoldCount = 0;
bool analyzerLocked = false;
unsigned long analyzerLastDraw = 0;
bool analyzerExitRequested = false;
const float RAW_REC_DEFAULT_RSSI = -90.0f; // RAW FILTER
const float RAW_REC_MIN_RSSI = -90.0f;
const float RAW_REC_MAX_RSSI = -45.0f;
const float RAW_REC_RSSI_STEP = 5.0f;
const unsigned long RAW_REC_FRAME_GAP_US = 7000;
const int RAW_REC_MIN_EDGES = 20;
const int RAW_REC_MAX_EDGES = 900;



bool rawRecorderRunning = false;
bool rawRecorderStopped = false;
bool rawRecorderIgnoreOkRelease = false;
bool rawRecorderIgnoreBackRelease = false;
bool transmitIgnoreOkRelease = false;
bool transmitIgnoreBackRelease = false;
bool rawPlaybackActive = false;
bool rawPlaybackStopRequestedFlag = false;
bool rawPlaybackOkWasPressed = false;
bool rawPlaybackBackWasPressed = false;
unsigned long rawPlaybackBackPressedAt = 0;
bool rawPlaybackIgnoreOkRelease = false;
bool rawPlaybackIgnoreBackRelease = false;
float rawPlaybackShownPct = 0.0f;
float rawPlaybackTargetPct = 0.0f;
unsigned long rawPlaybackStartMs = 0;
unsigned long rawPlaybackLastDrawMs = 0;
TaskHandle_t rawPlaybackTaskHandle = nullptr;
float rawRecorderRssiThreshold = RAW_REC_DEFAULT_RSSI;
float rawRecorderLastRssi = -127.0f;
uint16_t rawRecorderSavedCount = 0;
unsigned long rawRecorderLastDraw = 0;
String rawRecorderLastFile = "";
String rawRecorderSessionFile = "";
String rawRecorderStoppedFile = "";
uint8_t rawRecorderSpectrumVals[52];
int rawRecorderEdges[RAW_REC_MAX_EDGES];
int rawRecorderEdgeCount = 0;
int rawRecorderPrevLevel = LOW;
unsigned long rawRecorderPrevEdgeUs = 0;
unsigned long rawRecorderLastEdgeUs = 0;
float rawRecorderPeakRssi = -127.0f;
unsigned long rawRecorderRssiSampleUs = 0;
bool rawRecorderFrameDamaged = false;
bool rawRecorderStorageFailed = false;
bool rawRecorderDiscardFirstInterval = true;
bool rawRecorderFileLastPositive = false;
uint32_t rawRecorderSeenLosses = 0;
uint32_t rawRecorderRejectedFrames = 0;
uint32_t cc1101StateTimeouts = 0;
uint32_t cc1101ReadbackErrors = 0;
uint32_t cc1101FifoRecoveries = 0;

bool setupCC1101();
void configureCC1101();
void restoreReceiveMode();
static void disableRcSwitchReceive();
void read_rcswitch(tpKeyData* kd);
void read_raw(tpKeyData* kd);
void OLED_printWaitingSignal();
void OLED_printRawRecorder();
void drawRawRecorderButton();
void drawRawRecorderSpectrum(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void resetRawRecorderSpectrum();
void stepRawRecorderRssiThreshold();
static bool subGHzFileNameNeedsScroll(const String& fileName);
static void subGHzPrintFileName(DisplayType& display, const String& fileName, int16_t x, int16_t y);
void OLED_printKey(tpKeyData* kd, String fileName = "", bool isSending = false);
void OLED_printError(String st, bool err = true);
void OLED_printCC1101InitError();
void waitBackFromCC1101InitError();
bool saveKeyToSD(tpKeyData* kd);
String allocateNextSignalFileName();
bool startRawRecorderSession();
void stopRawRecorderSession(bool flushPending = true, bool discardFile = false);
bool saveRawFrameToSession();
void flushRawRecorderFrame();
void handleRawRecorderCapture();
bool loadKeyFromSD(String fileName, tpKeyData* kd);
void syncNextSignalIndexFromFiles();
void sendSynthKey(tpKeyData* kd);
bool playRawRecorderFile(const String& fileName);
void drawRawPlaybackWave(float phase, uint8_t progressPct);
void stepFrequency(int step);
void RCSwitch_send(uint64_t data, unsigned int bits, int pulse, int protocol, int repeat);
void RCSwitch_RAW_send(int *ptrtransmittimings);
void analyzerInit();
uint32_t analyzerSmoothAvg(uint32_t newVal);
uint32_t analyzerNearestFreq(uint32_t input);
void analyzerAddHistory(uint32_t freq);
void analyzerSaveFreq();
bool analyzerHandleInput();
void analyzerDoScan();
void analyzerDrawGraph(uint8_t x, uint8_t y);
void OLED_printAnalyzer();
void OLED_printJammer();
void startJamming();
void stopJamming();
String getTypeName(emKeys tp);
bool initRfModule(String mode, float freq);
void deinitRfModule();
void resetButtonStates();
void OLED_printBruteIntro();
void OLED_printBruteConfig(int previousSelection = -1);
void OLED_printBruteProgress(uint16_t progress, uint16_t total);
bool bruteInitTx();
void bruteStopTx();
void bruteSendCode(uint16_t code);

static bool cc1101IsConnected() {
  byte version = ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION);
  return version != 0x00 && version != 0xFF;
}

static bool cc1101PinsReady = false;
static bool cc1101Initialized = false;

static bool rcSwitchReceiveEnabled = false;

static void syncCC1101PinsFromGPIOConfig() {
  byte newGDO0Pin = getSubGHzCC1101GDO0Pin();
  byte newCSPin = getSubGHzCC1101CSPin();
  if (cc1101GDO0Pin == newGDO0Pin && cc1101CSPin == newCSPin) return;

  cc1101GDO0Pin = newGDO0Pin;
  cc1101CSPin = newCSPin;
  bruceConfig.rfTx = cc1101GDO0Pin;
  bruceConfigPins.CC1101_bus.io0 = cc1101GDO0Pin;
  cc1101PinsReady = false;
  cc1101Initialized = false;
}

static void prepareCC1101Pins() {
  syncCC1101PinsFromGPIOConfig();
  if (cc1101PinsReady) return;
  pinMode(cc1101CSPin, OUTPUT);
  digitalWrite(cc1101CSPin, HIGH);
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, cc1101CSPin);
  ELECHOUSE_cc1101.setGDO0(cc1101GDO0Pin);
  cc1101PinsReady = true;
}

static bool ensureCC1101Initialized() {
  prepareCC1101Pins();
  if (cc1101Initialized && cc1101IsConnected()) return true;

  // Init owns the reset sequence and initializes SPI before accessing it.
  ELECHOUSE_cc1101.Init();
  cc1101Initialized = cc1101IsConnected();
  return cc1101Initialized;
}

bool initRfModule(String mode, float freq) {
  if (!ensureCC1101Initialized()) {
    return false;
  }
  research::raw_capture::stop();
  disableRcSwitchReceive();
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SIDLE);
  ELECHOUSE_cc1101.setMHZ(freq);
  ELECHOUSE_cc1101.setModulation(2);
  if (mode == "tx" || mode == "TX") {
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.SetTx();
    pinMode(cc1101GDO0Pin, OUTPUT);
    digitalWrite(cc1101GDO0Pin, LOW);
  } else {
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, 0x0D);
    pinMode(cc1101GDO0Pin, INPUT);
    ELECHOUSE_cc1101.SetRx();
  }
  // MCSM0 enables automatic calibration on IDLE -> RX/TX; SCAL is
  // only valid in IDLE, never issue it after SetRx()/SetTx().
  return true;
}

void deinitRfModule() {
  // Release the MCU output before the radio can drive asynchronous RX data.
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SIDLE);
  pinMode(cc1101GDO0Pin, INPUT);
}

static void enableRcSwitchReceive() {
  if (rcSwitchReceiveEnabled) {
    return;
  }

  pinMode(cc1101GDO0Pin, INPUT);
  rcswitch.enableReceive(cc1101GDO0Pin);
  rcSwitchReceiveEnabled = true;

  Serial.printf(
    "[RCSwitch] RX enabled on GPIO %u\n",
    cc1101GDO0Pin
  );
}

static void disableRcSwitchReceive() {
  if (!rcSwitchReceiveEnabled) {
    return;
  }

  rcswitch.disableReceive();
  rcSwitchReceiveEnabled = false;

  Serial.println(F("[RCSwitch] RX disabled"));
}

void suspendLegacyReceive() {
  disableRcSwitchReceive();
  research::raw_capture::stop();
}

void runSubGHz() {
  if (!setupCC1101()) {
    Serial.println(F("Failed to initialize CC1101"));
    OLED_printCC1101InitError();
    waitBackFromCC1101InitError();
    resetButtonStates();
    return;
  }
  enableRcSwitchReceive();
  emMenuState menuState = menuMain;
  menuIndex = 0;
  OLED_printSubGHzMenu(display, menuIndex);

  while (true) {
    // GPIO capture/decoding runs in interrupts; give the idle task a turn even
    // while the user stays on a static menu or stopped-recording screen.
    delay(1);
    static MenuButtonState upHeld;
    static MenuButtonState downHeld;
    buttonUp.tick();
    buttonDown.tick();
    buttonOK.tick();
    buttonBack.tick();

    if (menuState == menuMain) {
      byte lastMenuIndex = SUBGHZ_MENU_ITEM_COUNT - 1;
      const unsigned long repeatDelayMs = getMenuSubmenuRepeatDelay(submenu == 1);
      if (isMenuButtonPress(BUTTON_UP, upHeld, repeatDelayMs)) {
        byte previousIndex = menuIndex;
        menuIndex = (menuIndex == 0) ? lastMenuIndex : menuIndex - 1;
        OLED_printSubGHzMenu(display, menuIndex, previousIndex);
      }
      if (isMenuButtonPress(BUTTON_DOWN, downHeld, repeatDelayMs)) {
        byte previousIndex = menuIndex;
        menuIndex = (menuIndex == lastMenuIndex) ? 0 : menuIndex + 1;
        OLED_printSubGHzMenu(display, menuIndex, previousIndex);
      }
      if (buttonOK.isClick()) {
        if (menuIndex == 0) {
          if (!ensureSDReadyInteractive(true)) {
            OLED_printSubGHzMenu(display, menuIndex);
            continue;
          }
          menuState = menuReceive;
          setupCC1101();
          disableRcSwitchReceive();
          enableRcSwitchReceive();
          validKeyReceived = false;
          signals = 0;
          memset(&keyData1, 0, sizeof(tpKeyData));
          lastSavedKey = 0;
          rcswitch.resetAvailable();
          resetButtonStates();
          OLED_printWaitingSignal();
        } else if (menuIndex == 1) {
          if (!ensureSDReadyInteractive(true)) {
            OLED_printSubGHzMenu(display, menuIndex);
            continue;
          }
          menuState = menuRawRecorder;
          disableRcSwitchReceive();
          setupCC1101();
          rawRecorderRunning = false;
          rawRecorderStopped = false;
          rawRecorderIgnoreOkRelease = true;
          rawRecorderIgnoreBackRelease = false;
          rawRecorderSavedCount = 0;
          rawRecorderLastRssi = -127.0f;
          rawRecorderLastFile = "";
          rawRecorderSessionFile = "";
          rawRecorderStoppedFile = "";
          rawRecorderEdgeCount = 0;
          rawRecorderPrevLevel = digitalRead(cc1101GDO0Pin);
          rawRecorderPrevEdgeUs = micros();
          rawRecorderLastEdgeUs = rawRecorderPrevEdgeUs;
          rawRecorderLastDraw = 0;
          resetRawRecorderSpectrum();
          resetButtonStates();
          OLED_printRawRecorder();
        } else if (menuIndex == 2) {
          if (!ensureSDReadyInteractive(true)) {
            OLED_printSubGHzMenu(display, menuIndex);
            continue;
          }
          menuState = menuTransmit;
          transmitIgnoreOkRelease = false;
          transmitIgnoreBackRelease = false;
          ExplorerInit(subExplorer, subFileList, MAX_FILES, subExplorerCfg);
          ExplorerLoad(subExplorer, subExplorerCfg);
          syncNextSignalIndexFromFiles();
          ExplorerDraw(subExplorer, display);
          resetButtonStates();
        } else if (menuIndex == 3) {
          menuState = menuAnalyzer;
          setupCC1101();
          disableRcSwitchReceive();
          analyzerInit();
          analyzerExitRequested = false;
          resetButtonStates();
          OLED_printAnalyzer();
        } else if (menuIndex == 4) {
          menuState = menuJammer;
          isJamming = false;
          resetButtonStates();
          OLED_printJammer();
        } else if (menuIndex == 5) {
          menuState = menuBruteforce;
          bruteRunning = false;
          resetButtonStates();
          OLED_printBruteIntro();
        } else if (menuIndex == 6) {
          suspendLegacyReceive();
          runRadioLab();
          restoreReceiveMode();
          resetButtonStates();
          OLED_printSubGHzMenu(display, menuIndex);
        }
      }
      if (buttonBack.isClick()) {
        break;
      }
    } else if (menuState == menuReceive) {
      if (buttonUp.isClick()) {
        stepFrequency(1);
        keyData1.frequency = frequency;
        setupCC1101();
        OLED_printWaitingSignal();
      }
      if (buttonDown.isClick()) {
        stepFrequency(-1);
        keyData1.frequency = frequency;
        setupCC1101();
        OLED_printWaitingSignal();
      }
      if (buttonOK.isHolded() && validKeyReceived) {
        if (saveKeyToSD(&keyData1)) {
          display.clearDisplay();
          display.drawBitmap(16, 6, image_DolphinSaved_bits, 92, 58, SH110X_WHITE);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(6, 16);
          display.print("Saved");
          display.display();
          Serial.println(F("Key saved"));
          delay(1000);
          validKeyReceived = false;
          signals = 0;
          memset(&keyData1, 0, sizeof(tpKeyData));
          lastSavedKey = 0;
          rcswitch.resetAvailable();
        } else {
          ExplorerShowSDError(display);
          Serial.println(F("Failed to save key to SD"));
        }
        OLED_printWaitingSignal();
      }
      if (buttonBack.isClick()) {
        menuState = menuMain;
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (rcswitch.available()) {
        Serial.println(F(" Signal:"));
        if (!readRAW) read_rcswitch(&keyData1);
        else read_raw(&keyData1);
        uint64_t receivedKey = 0;
        for (byte i = 0; i < 8; ++i) receivedKey = (receivedKey << 8) | keyData1.keyID[i];
        if (validKeyReceived && autoSave && lastSavedKey != receivedKey) {
          if (saveKeyToSD(&keyData1)) {
            lastSavedKey = receivedKey;
            validKeyReceived = false;
            signals = 0;
            memset(&keyData1, 0, sizeof(tpKeyData));
            rcswitch.resetAvailable();
          } else {
            Serial.println(F("Auto-save failed"));
          }
        }
      }
    } else if (menuState == menuRawRecorder) {
      if (rawRecorderIgnoreOkRelease) {
        if (digitalRead(BUTTON_OK) == LOW) {
          (void)buttonOK.isClick();
        } else {
          rawRecorderIgnoreOkRelease = false;
          buttonOK.resetStates();
        }
      }
      if (rawRecorderIgnoreBackRelease) {
        if (digitalRead(BUTTON_BACK) == LOW) {
          (void)buttonBack.isClick();
        } else {
          rawRecorderIgnoreBackRelease = false;
          buttonBack.resetStates();
        }
      }

      bool upClick = buttonUp.isClick();
      bool downClick = buttonDown.isClick();
      bool okClick = !rawRecorderIgnoreOkRelease && buttonOK.isClick();
      bool backClick = !rawRecorderIgnoreBackRelease && buttonBack.isClick();

      if (rawRecorderStopped) {
        if (downClick) {
          if (rawRecorderStoppedFile.length() > 0 && SD.exists(rawRecorderStoppedFile)) {
            SD.remove(rawRecorderStoppedFile);
          }
          rawRecorderStopped = false;
          rawRecorderStoppedFile = "";
          rawRecorderLastFile = "";
          rawRecorderSavedCount = 0;
          rawRecorderEdgeCount = 0;
          resetRawRecorderSpectrum();
          OLED_printRawRecorder();
        } else if (upClick) {
          display.clearDisplay();
          display.drawBitmap(16, 6, image_DolphinSaved_bits, 92, 58, SH110X_WHITE);
          display.setTextColor(SH110X_WHITE);
          display.setCursor(6, 16);
          display.print("Saved");
          display.display();
          delay(1000);
          rawRecorderStopped = false;
          rawRecorderStoppedFile = "";
          rawRecorderLastFile = "";
          rawRecorderSavedCount = 0;
          rawRecorderEdgeCount = 0;
          resetRawRecorderSpectrum();
          OLED_printRawRecorder();
        } else if (backClick) {
          menuState = menuMain;
          resetButtonStates();
          OLED_printSubGHzMenu(display, menuIndex);
        } else if (okClick) {
          playRawRecorderFile(rawRecorderStoppedFile);
          OLED_printRawRecorder();
          resetButtonStates();
        }
        continue;
      }

      if (!rawRecorderRunning && upClick) {
        stepRawRecorderRssiThreshold();
        OLED_printRawRecorder();
      }
      if (!rawRecorderRunning && downClick) {
        stepFrequency(1);
        setupCC1101();
        rawRecorderPrevLevel = digitalRead(cc1101GDO0Pin);
        rawRecorderPrevEdgeUs = micros();
        rawRecorderLastEdgeUs = rawRecorderPrevEdgeUs;
        rawRecorderEdgeCount = 0;
        resetRawRecorderSpectrum();
        OLED_printRawRecorder();
      }
      if (backClick) {
        if (rawRecorderRunning) {
          stopRawRecorderSession(true, false);
          rawRecorderRunning = false;
          rawRecorderLastDraw = 0;
          OLED_printRawRecorder();
        } else {
          menuState = menuMain;
          resetButtonStates();
          OLED_printSubGHzMenu(display, menuIndex);
        }
      } else if (okClick) {
        if (!rawRecorderRunning) {
          if (startRawRecorderSession()) {
            rawRecorderRunning = true;
            rawRecorderStopped = false;
            resetRawRecorderSpectrum();
          } else {
            ExplorerShowSDError(display, 700);
          }
        } else {
          String stoppedFile = rawRecorderSessionFile;
          stopRawRecorderSession(true, false);
          rawRecorderRunning = false;
          rawRecorderStopped = !rawRecorderStorageFailed && rawRecorderSavedCount != 0;
          rawRecorderStoppedFile = rawRecorderStopped ? stoppedFile : "";
        }
        rawRecorderLastDraw = 0;
        OLED_printRawRecorder();
      } else if (rawRecorderRunning) {
        handleRawRecorderCapture();
        if (millis() - rawRecorderLastDraw >= 120) {
          OLED_printRawRecorder();
          rawRecorderLastDraw = millis();
        }
      }
    } else if (menuState == menuTransmit && subExplorer.inExplorer) {
      ExplorerAction action = ExplorerHandle(
        subExplorer,
        subExplorerCfg,
        display,
        buttonUp.isClick(),
        buttonDown.isClick(),
        buttonOK.isClick(),
        buttonBack.isClick(),
        buttonBack.isHolded()
      );
      if (action == EXPLORER_SELECT_FILE) {
        subExplorer.inExplorer = false;
        resetButtonStates();
        if (loadKeyFromSD(subExplorer.selectedFile, &keyData1)) {
          OLED_printKey(&keyData1, subExplorer.selectedFile);
        } else {
          OLED_printError(legacyFileLoadError.length() ? legacyFileLoadError : F("Invalid .sub file"), true);
          delay(1000);
          subExplorer.inExplorer = true;
          ExplorerDraw(subExplorer, display);
        }
      } else if (action == EXPLORER_EXIT) {
        menuState = menuMain;
        subExplorer.inExplorer = false;
        subExplorer.selectedFile = "";
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
    } else if (menuState == menuTransmit && !subExplorer.inExplorer) {
      if (transmitIgnoreOkRelease) {
        if (digitalRead(BUTTON_OK) == LOW) {
          (void)buttonOK.isClick();
        } else {
          transmitIgnoreOkRelease = false;
          buttonOK.resetStates();
        }
      }
      if (transmitIgnoreBackRelease) {
        if (digitalRead(BUTTON_BACK) == LOW) {
          (void)buttonBack.isClick();
        } else {
          transmitIgnoreBackRelease = false;
          buttonBack.resetStates();
        }
      }

      bool okClick = !transmitIgnoreOkRelease && buttonOK.isClick();
      bool backClick = !transmitIgnoreBackRelease && buttonBack.isClick();
      static unsigned long lastKeyMarqueeDrawAt = 0;

      if (okClick) {
        sendSynthKey(&keyData1);
        restoreReceiveMode();
        lastKeyMarqueeDrawAt = millis();
      }
      if (backClick) {
        subExplorer.inExplorer = true;
        resetButtonStates();
        ExplorerDraw(subExplorer, display);
      }
      if (!okClick && !backClick && subGHzFileNameNeedsScroll(subExplorer.selectedFile) &&
          millis() - lastKeyMarqueeDrawAt >= 200) {
        lastKeyMarqueeDrawAt = millis();
        OLED_printKey(&keyData1, subExplorer.selectedFile);
      }
    } else if (menuState == menuBruteforce) {
      if (buttonOK.isHolded()) {
        bruteConfigSelection = 0;
        menuState = menuBruteConfig;
        OLED_printBruteConfig();
      } else if (buttonOK.isClick()) {
        bruteProgress = 0;
        freqIndex = bruteFreqIndex;
        frequency = bruteFreqOptions[bruteFreqIndex];
        bruteLastStep = millis();
        if (bruteInitTx()) {
          bruteRunning = true;
          menuState = menuBruteRun;
          OLED_printBruteProgress(bruteProgress, bruteTotal);
        } else {
          bruteRunning = false;
          OLED_printError(F("Brute init fail"), true);
          delay(800);
          OLED_printBruteIntro();
        }
      }
      if (buttonBack.isClick()) {
        menuState = menuMain;
        bruteRunning = false;
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
    } else if (menuState == menuBruteConfig) {
      if (buttonUp.isClick()) {
        int previousSelection = bruteConfigSelection;
        bruteConfigSelection = (bruteConfigSelection == 0) ? 1 : bruteConfigSelection - 1;
        OLED_printBruteConfig(previousSelection);
      }
      if (buttonDown.isClick()) {
        int previousSelection = bruteConfigSelection;
        bruteConfigSelection = (bruteConfigSelection + 1) % 2;
        OLED_printBruteConfig(previousSelection);
      }
      if (buttonOK.isClick()) {
        if (bruteConfigSelection == 0) {
          bruteTypeIndex = (bruteTypeIndex + 1) % BRUTE_TYPE_COUNT;
        } else {
          bruteFreqIndex = (bruteFreqIndex + 1) % BRUTE_FREQ_COUNT;
        }
        OLED_printBruteConfig();
      }
      if (buttonBack.isClick()) {
        freqIndex = bruteFreqIndex;
        frequency = bruteFreqOptions[bruteFreqIndex];
        menuState = menuBruteforce;
        resetButtonStates();
        OLED_printBruteIntro();
      }
    } else if (menuState == menuBruteRun) {
      if (buttonBack.isClick()) {
        bruteRunning = false;
        bruteStopTx();
        menuState = menuMain;
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
      if (buttonOK.isClick()) {
        bruteRunning = false;
        bruteStopTx();
        menuState = menuBruteforce;
        resetButtonStates();
        OLED_printBruteIntro();
      }
      if (bruteRunning && millis() - bruteLastStep >= 15) {
        bruteLastStep = millis();
        if (bruteProgress < bruteTotal) {
          bruteSendCode(bruteProgress);
          bruteProgress++;
          if (bruteProgress % 8 == 0 || bruteProgress == bruteTotal) {
            OLED_printBruteProgress(bruteProgress, bruteTotal);
          }
        }
        if (bruteProgress >= bruteTotal) {
          bruteRunning = false;
          bruteStopTx();
          menuState = menuBruteforce;
          resetButtonStates();
          OLED_printBruteIntro();
        }
      }
    } else if (menuState == menuAnalyzer) {
      if (analyzerExitRequested) {
        analyzerExitRequested = false;
        menuState = menuMain;

        disableRcSwitchReceive();
        restoreReceiveMode();

        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      } else {
        if (!analyzerHandleInput()) {
          continue;
        }

        analyzerDoScan();

        if (analyzerExitRequested) {
          continue;
        }

        if (millis() - analyzerLastDraw >= 50) {
          OLED_printAnalyzer();
          analyzerLastDraw = millis();
        }
      }
    } else if (menuState == menuJammer) {
      if (buttonUp.isClick()) {
        stepFrequency(1);
        if (isJamming) {
          stopJamming();
          startJamming();
        }
        OLED_printJammer();
      }
      if (buttonDown.isClick()) {
        stepFrequency(-1);
        if (isJamming) {
          stopJamming();
          startJamming();
        }
        OLED_printJammer();
      }
      if (buttonOK.isClick()) {
        if (!isJamming) {
          startJamming();
          isJamming = true;
        } else {
          stopJamming();
          isJamming = false;
        }
        OLED_printJammer();
      }
      if (buttonBack.isClick()) {
        if (isJamming) {
          stopJamming();
          isJamming = false;
        }
        menuState = menuMain;
        restoreReceiveMode();
        resetButtonStates();
        OLED_printSubGHzMenu(display, menuIndex);
      }
    }
  }

  if (isJamming) {
    stopJamming();
    isJamming = false;
  }
  if (bruteRfActive) {
    bruteStopTx();
  }
  if (rawRecorderRunning || rawRecorderSessionFile.length() > 0) {
    stopRawRecorderSession(false, true);
    rawRecorderRunning = false;
  }
  restoreReceiveMode();
}

void resetButtonStates() {
  buttonUp.resetStates();
  buttonDown.resetStates();
  buttonOK.resetStates();
  buttonBack.resetStates();
}

static bool waitCC1101State(byte expected, uint32_t timeoutMs = 20) {
  const uint32_t started = millis();
  do {
    if ((ELECHOUSE_cc1101.SpiReadStatus(CC1101_MARCSTATE) & 0x1F) == expected) return true;
    delayMicroseconds(100);
  } while (millis() - started < timeoutMs);
  ++cc1101StateTimeouts;
  Serial.printf("[CC1101] state timeout, wanted=%02X MARC=%02X count=%lu\n",
    expected, ELECHOUSE_cc1101.SpiReadStatus(CC1101_MARCSTATE),
    static_cast<unsigned long>(cc1101StateTimeouts));
  return false;
}

bool setupCC1101() {
  if (!ensureCC1101Initialized()) return false;
  const bool resumeDecoder = rcSwitchReceiveEnabled;
  disableRcSwitchReceive();
  configureCC1101();
  const bool ready = waitCC1101State(0x0D); // RX after automatic calibration.
  rcswitch.resetAvailable();
  if (ready && resumeDecoder) enableRcSwitchReceive();
  return ready;
}

void configureCC1101() {
  // Restore the complete AM270 asynchronous receive profile. Merely changing
  // modulation/BW leaves packet/GDO/sync/AGC state from previous applications.
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SIDLE);
  if (!waitCC1101State(0x01)) return;
  const byte rxBytes = ELECHOUSE_cc1101.SpiReadStatus(CC1101_RXBYTES);
  if (rxBytes & 0x80) ++cc1101FifoRecoveries;
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SFRX);
  ELECHOUSE_cc1101.setCCMode(false);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(frequency);
  const byte registers[][2] = {
    {CC1101_IOCFG0, 0x0D}, {CC1101_FIFOTHR, 0x47},
    {CC1101_PKTCTRL0, 0x32}, {CC1101_PKTCTRL1, 0x04},
    {CC1101_FSCTRL1, 0x06}, {CC1101_FSCTRL0, 0x00},
    {CC1101_MDMCFG0, 0x00}, {CC1101_MDMCFG1, 0x00},
    {CC1101_MDMCFG2, 0x30}, {CC1101_MDMCFG3, 0x32},
    {CC1101_MDMCFG4, 0x67}, {CC1101_DEVIATN, 0x00},
    {CC1101_MCSM0, 0x18}, {CC1101_MCSM1, 0x00},
    {CC1101_MCSM2, 0x07}, {CC1101_FOCCFG, 0x18},
    {CC1101_AGCCTRL0, 0x40}, {CC1101_AGCCTRL1, 0x00},
    {CC1101_AGCCTRL2, 0x03}, {CC1101_WORCTRL, 0xFB},
    {CC1101_FREND0, 0x11}, {CC1101_FREND1, 0xB6}
  };
  bool verified = true;
  for (const auto& reg : registers) {
    ELECHOUSE_cc1101.SpiWriteReg(reg[0], reg[1]);
    if (ELECHOUSE_cc1101.SpiReadReg(reg[0]) != reg[1]) {
      verified = false;
      ++cc1101ReadbackErrors;
      Serial.printf("[CC1101] config readback failed reg=%02X expected=%02X\n", reg[0], reg[1]);
    }
  }
  pinMode(cc1101GDO0Pin, INPUT);
  if (verified) ELECHOUSE_cc1101.SetRx();
  Serial.printf("[CC1101] part=%02X version=%02X GDO0=%u CS=%u freq=%.5f "
                "preset=AM270 MARC=%02X SPIerr=%lu readyTO=%lu recovery=%lu\n",
    ELECHOUSE_cc1101.SpiReadStatus(CC1101_PARTNUM),
    ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION), cc1101GDO0Pin, cc1101CSPin,
    frequency, ELECHOUSE_cc1101.SpiReadStatus(CC1101_MARCSTATE),
    static_cast<unsigned long>(cc1101ReadbackErrors),
    static_cast<unsigned long>(ELECHOUSE_cc1101.spiTimeouts()),
    static_cast<unsigned long>(cc1101FifoRecoveries));
}

void restoreReceiveMode() {
  suspendLegacyReceive();
  if (setupCC1101()) enableRcSwitchReceive();
}

void read_rcswitch(tpKeyData* kd) {
  // rc-switch overwrites shared metadata from its GPIO ISR. Copy and acknowledge
  // it together on the owner core before any display or storage work.
  noInterrupts();
  const uint32_t decoded = rcswitch.getReceivedValue();
  const unsigned int bitLength = rcswitch.getReceivedBitlength();
  const unsigned int pulseDelay = rcswitch.getReceivedDelay();
  const unsigned int protocol = rcswitch.getReceivedProtocol();
  rcswitch.resetAvailable();
  interrupts();
  if (!decoded || bitLength == 0 || bitLength > 32) return;
  memset(kd, 0, sizeof(*kd));
  kd->frequency = frequency;
  kd->bitLength = bitLength;
  kd->codeLenth = bitLength;
  if (!research::packLegacyDecodedKey(decoded, bitLength, kd->keyID)) return;
  ++signals;
  kd->type = (protocol == 1 && bitLength == 24) ? kPrinceton : kRcSwitch;
  if (protocol == 11 && bitLength <= 32) kd->type = kCAME;
  kd->te = pulseDelay;
  strcpy(kd->preset, "AM270");
  // No truncated raw preview: decoded messages retain their key/TE metadata.
  // Unknown waveforms are captured through the dedicated RAW interrupt ring.
  validKeyReceived = true;
  Serial.printf("[RX] decoder=rc-switch protocol=%u bits=%u key=%08lX\n",
                protocol, bitLength, static_cast<unsigned long>(decoded));
  OLED_printKey(kd, "", false);
}

void read_raw(tpKeyData* kd) {
  // Legacy Standard Read used to scan 512 entries from a 67-entry nonterminated
  // rc-switch array. That decoder is not a generic RAW recorder.
  read_rcswitch(kd);
}

void OLED_printWaitingSignal() {
  display.clearDisplay();
  display.drawBitmap(0, 4, image_DolphinReceive_bits, 97, 61, SH110X_WHITE);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(65, 38);
  display.print("Waiting");
  display.setCursor(65, 47);
  display.print("signal...");
  display.setCursor(72, 13);
  display.print(String(frequency, 2) + "MHz");
  display.display();
}

void OLED_printRawRecorder() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  display.setCursor(5, 3);
  display.print(String(frequency, 2) + F("MHz"));
  display.drawRect(66, 2, 2, 9, 1);

  display.setCursor(76, 3);
  display.print(F("RSSI:"));
  display.print((int)rawRecorderRssiThreshold);

  display.drawRect(10, 13, 108, 37, SH110X_WHITE);
  if (rawRecorderRunning || rawRecorderStopped) {
    drawRawRecorderSpectrum(12, 15, 104, 33);
  }

  float filterPct = (rawRecorderRssiThreshold - RAW_REC_MIN_RSSI) / (RAW_REC_MAX_RSSI - RAW_REC_MIN_RSSI);
  if (filterPct < 0.0f) filterPct = 0.0f;
  if (filterPct > 1.0f) filterPct = 1.0f;
  uint8_t arrowY = 45 - (uint8_t)(filterPct * 16.0f + 0.5f);
  display.drawBitmap(120, arrowY, image_ArrowLeft_bits, 3, 5, SH110X_WHITE);
  drawRawRecorderButton();
  display.display();
}

void drawRawRecorderButton() {
  if (rawRecorderStopped) {
    display.fillRoundRect(1, 52, 41, 12, 2, SH110X_WHITE);
    display.fillRect(1, 54, 41, 10, SH110X_WHITE);
    display.fillRoundRect(44, 52, 41, 12, 2, SH110X_WHITE);
    display.fillRect(44, 54, 41, 10, SH110X_WHITE);
    display.fillRoundRect(87, 52, 40, 12, 2, SH110X_WHITE);
    display.fillRect(87, 54, 40, 10, SH110X_WHITE);

    display.setTextColor(SH110X_BLACK);
    display.setCursor(3, 55);
    display.print("Erase");
    display.drawBitmap(35, 58, image_ArrowDown_bits, 5, 3, SH110X_BLACK);

    display.setCursor(47, 55);
    display.print("Play");
    display.drawBitmap(73, 54, image_REC_bits, 9, 9, SH110X_BLACK);

    display.setCursor(92, 55);
    display.print("Save");
    display.drawBitmap(118, 57, image_ArrowUp_bits, 5, 3, SH110X_BLACK);
    display.setTextColor(SH110X_WHITE);
    return;
  }

  const char* label = rawRecorderRunning ? "Stop" : "REC";
  const uint8_t textW = strlen(label) * 6;
  const uint8_t iconW = 9;
  const uint8_t iconH = 9;
  const uint8_t gap = 3;
  const uint8_t contentW = textW + gap + iconW;
  const uint8_t padX = 5;
  const uint8_t x = (SCREEN_WIDTH - (contentW + padX * 2)) / 2;
  const uint8_t y = 52;
  const uint8_t w = contentW + padX * 2;
  const uint8_t h = 12;

  display.fillRoundRect(x, y, w, h, 2, SH110X_WHITE);
  display.fillRect(x, y + 2, w, h - 2, SH110X_WHITE);

  const uint8_t textX = x + padX;
  const uint8_t iconX = textX + textW + gap;
  display.setTextColor(SH110X_BLACK);
  display.setCursor(textX, y + ((strcmp(label, "REC") == 0) ? 3 : 2));
  display.print(label);
  display.drawBitmap(iconX, y + 2, image_REC_bits, iconW, iconH, SH110X_BLACK);
  display.setTextColor(SH110X_WHITE);
}

void drawRawRecorderSpectrum(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  const uint8_t barCount = min((uint8_t)sizeof(rawRecorderSpectrumVals), (uint8_t)(w / 2));

  if (rawRecorderRunning) {
    for (uint8_t i = 0; i < barCount - 1; i++) {
      rawRecorderSpectrumVals[i] = rawRecorderSpectrumVals[i + 1];
    }

    float rssi = ELECHOUSE_cc1101.getRssi();
    if (rssi < -100.0f) rssi = -100.0f;
    if (rssi > -30.0f) rssi = -30.0f;
    rawRecorderSpectrumVals[barCount - 1] = 1 + (uint8_t)(((rssi + 100.0f) * (h - 1)) / 70.0f);
  }

  for (uint8_t i = 0; i < barCount; i++) {
    uint8_t bar = rawRecorderSpectrumVals[i];
    if (bar > 0) {
      uint8_t px = x + i * 2;
      display.drawFastVLine(px, y + h - bar, bar, SH110X_WHITE);
      display.drawFastVLine(px + 1, y + h - bar, bar, SH110X_WHITE);
    }
  }
}

void resetRawRecorderSpectrum() {
  for (uint8_t i = 0; i < sizeof(rawRecorderSpectrumVals); i++) {
    rawRecorderSpectrumVals[i] = 0;
  }
}

void stepRawRecorderRssiThreshold() {
  rawRecorderRssiThreshold += RAW_REC_RSSI_STEP;
  if (rawRecorderRssiThreshold > RAW_REC_MAX_RSSI) {
    rawRecorderRssiThreshold = RAW_REC_MIN_RSSI;
  }
}

static bool subGHzFileNameNeedsScroll(const String& fileName) {
  return fileName.length() > 15;
}

static void subGHzPrintFileName(DisplayType& display, const String& fileName, int16_t x, int16_t y) {
  static String marqueeText = "";
  static unsigned long marqueeStartedAt = 0;

  const int visibleChars = 15;
  if (fileName.length() <= visibleChars) {
    marqueeText = "";
    marqueeStartedAt = 0;
    display.setCursor(x, y);
    display.print(fileName);
    return;
  }

  if (marqueeText != fileName) {
    marqueeText = fileName;
    marqueeStartedAt = millis();
  }

  String marquee = fileName + F("   ");
  int maxOffset = marquee.length() - visibleChars;
  if (maxOffset < 0) maxOffset = 0;

  int offset = 0;
  const unsigned long initialPauseMs = 400;
  const unsigned long loopPauseMs = 400;
  const unsigned long stepMs = 200;
  unsigned long elapsed = millis() - marqueeStartedAt;
  if (maxOffset > 0 && elapsed >= initialPauseMs) {
    unsigned long scrollDuration = static_cast<unsigned long>(maxOffset) * stepMs;
    unsigned long cycleDuration = scrollDuration + loopPauseMs + scrollDuration + loopPauseMs;
    unsigned long cyclePosition = (elapsed - initialPauseMs) % cycleDuration;
    if (cyclePosition < scrollDuration) {
      offset = cyclePosition / stepMs;
    } else if (cyclePosition < scrollDuration + loopPauseMs) {
      offset = maxOffset;
    } else if (cyclePosition < scrollDuration + loopPauseMs + scrollDuration) {
      offset = maxOffset - ((cyclePosition - scrollDuration - loopPauseMs) / stepMs);
    }
  }

  display.setCursor(x, y);
  display.print(marquee.substring(offset, offset + visibleChars));
}

void OLED_printKey(tpKeyData* kd, String fileName, bool isSending) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  uint8_t dataY = 14;
  if (fileName != "") {
    display.setCursor(3, 3);
    display.print(F("File: "));
    subGHzPrintFileName(display, fileName, 39, 3);
    dataY = 14;
  } else {
    display.setCursor(3, 3);
    display.println("Signal:");
    dataY = display.getCursorY() + 3;
  }
  String st = "";
  if (kd->type == kLINEAR) {
    st = "Unknown";
  } else {
    bool leadingZero = true;
    for (int i = 0; i < 8; i++) {
      if (kd->keyID[i] != 0 || !leadingZero || i == 7) {
        leadingZero = false;
        if (kd->keyID[i] < 0x10) st += "0";
        st += String(kd->keyID[i], HEX);
        if (i < 7) st += ":";
      }
    }
  }
  display.setCursor(3, dataY);
  display.println("Code: " + st);
  st = "Type: " + getTypeName(kd->type);
  dataY = display.getCursorY() + 2;
  display.setCursor(3, dataY);
  display.println(st);
  st = "Freq: " + String(kd->frequency) + " MHz";
  dataY = display.getCursorY() + 2;
  display.setCursor(3, dataY);
  display.println(st);
  if (kd->bitLength > 0) {
    st = "Bits: " + String(kd->bitLength);
    dataY = display.getCursorY() + 2;
    display.setCursor(3, dataY);
    display.println(st);
  }
  if (isSending) {
    display.setCursor(67, 54);
    display.print("Sending...");
    display.drawBitmap(109, 40, image_satellite_dish_bits, 15, 16, SH110X_WHITE);
  }
  display.display();
}

void OLED_printError(String st, bool err) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(3, 3);
  display.print(err ? F("Error!") : F("OK"));
  display.setCursor(1, 12);
  display.print(st);
  display.display();
}

void OLED_printCC1101InitError() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(8, 20);
  display.print(F("CC1101 init failed."));
  display.setCursor(29, 32);
  display.print(F("ERROR: 0x001"));
  display.display();
}

void waitBackFromCC1101InitError() {
  resetButtonStates();
  while (digitalRead(BUTTON_BACK) == LOW) {
    buttonBack.tick();
    delay(10);
  }

  resetButtonStates();
  while (true) {
    buttonBack.tick();
    if (buttonBack.isClick()) {
      resetButtonStates();
      return;
    }
    delay(10);
  }
}

static void ensureSubExplorerDir() {
  if (subExplorer.currentDir.length() == 0) {
    subExplorer.currentDir = subExplorerCfg.rootDir;
  }
}

static int signalNumberFromName(String name) {
  int lastSlash = name.lastIndexOf('/');
  if (lastSlash >= 0) name = name.substring(lastSlash + 1);
  if (!(name.startsWith("Signal_") || name.startsWith("signal_")) || !name.endsWith(".sub")) {
    return 0;
  }

  String numStr = name.substring(7, name.length() - 4);
  if (numStr.length() == 0) {
    return 0;
  }
  for (uint16_t i = 0; i < numStr.length(); i++) {
    if (!isDigit(numStr[i])) {
      return 0;
    }
  }

  return numStr.toInt();
}

void syncNextSignalIndexFromFiles() {
  ensureSubExplorerDir();

  int maxSignalIndex = 0;
  File dir = SD.open(subExplorer.currentDir);
  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory() && String(entry.name()).endsWith(".sub")) {
        int num = signalNumberFromName(entry.name());
        if (num > maxSignalIndex) {
          maxSignalIndex = num;
        }
      }
      entry.close();
    }
    dir.close();
  }

  int scannedNextIndex = maxSignalIndex + 1;
  if (scannedNextIndex < 1) scannedNextIndex = 1;
  nextSignalIndex = scannedNextIndex;
  nextSignalIndexReady = true;
}

String allocateNextSignalFileName() {
  ensureSubExplorerDir();
  syncNextSignalIndexFromFiles();

  int fileNum = (nextSignalIndex > 0) ? nextSignalIndex : 1;
  String fileName;
  while (fileNum <= 999) {
    fileName = subExplorer.currentDir + "/Signal_" + String(fileNum) + ".sub";
    if (!SD.exists(fileName)) break;
    fileNum++;
  }
  if (fileNum > 999) {
    Serial.println(F("No free Signal_*.sub slots"));
    return "";
  }

  nextSignalIndex = fileNum + 1;
  nextSignalIndexReady = true;
  return fileName;
}

bool startRawRecorderSession() {
  String fileName = allocateNextSignalFileName();
  if (fileName.length() == 0) {
    return false;
  }

  File file = SD.open(fileName, FILE_WRITE);
  if (!file) return false;

  char header[192];
  const int headerLength = snprintf(header, sizeof(header),
    "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu\n"
    "Preset: FuriHalSubGhzPresetOok270Async\nProtocol: RAW\n",
    static_cast<unsigned long>(frequency * 1000000));
  const bool wroteHeader = headerLength > 0 && headerLength < static_cast<int>(sizeof(header)) &&
    file.write(reinterpret_cast<const uint8_t*>(header), headerLength) == static_cast<size_t>(headerLength);
  file.flush();
  const bool headerOk = wroteHeader && !file.getWriteError();
  file.close();
  if (!headerOk) {
    SD.remove(fileName);
    return false;
  }

  rawRecorderSessionFile = fileName;
  int lastSlash = fileName.lastIndexOf('/');
  rawRecorderLastFile = (lastSlash >= 0) ? fileName.substring(lastSlash + 1) : fileName;
  rawRecorderEdgeCount = 0;
  rawRecorderPeakRssi = -127.0f;
  rawRecorderFrameDamaged = false;
  rawRecorderStorageFailed = false;
  rawRecorderDiscardFirstInterval = true;
  rawRecorderFileLastPositive = false;
  rawRecorderSeenLosses = 0;
  rawRecorderRejectedFrames = 0;
  disableRcSwitchReceive();
  if (!research::raw_capture::start(cc1101GDO0Pin)) {
    SD.remove(fileName);
    rawRecorderSessionFile = "";
    return false;
  }
  Serial.print(F("RAW interrupt session started: "));
  Serial.println(fileName);
  return true;
}

bool saveRawFrameToSession() {
  if (rawRecorderSessionFile.length() == 0 || rawRecorderEdgeCount == 0) return false;
  if (rawRecorderStorageFailed) return false;
  File file = SD.open(rawRecorderSessionFile, FILE_APPEND);
  if (!file) {
    rawRecorderStorageFailed = true;
    Serial.println(F("[RAW] SD append open failed; session will be discarded"));
    return false;
  }
  bool ok = file.print(F("RAW_Data: ")) == 10;
  char chunk[192];
  size_t used = 0;
  for (int i = 0; i < rawRecorderEdgeCount && ok; ++i) {
    // Flipper RAW format permits at most 512 durations on each RAW_Data line.
    // A line break carries no extra timing interval.
    if (i && (i % 512) == 0) {
      if (used) ok = file.write(reinterpret_cast<const uint8_t*>(chunk), used) == used;
      used = 0;
      if (ok) ok = file.print('\n') == 1;
      if (ok) ok = file.print(F("RAW_Data: ")) == 10;
    }
    if (!ok) break;
    char token[16];
    const int length = snprintf(token, sizeof(token), "%s%d", (i % 512) ? " " : "", rawRecorderEdges[i]);
    if (length <= 0 || length >= static_cast<int>(sizeof(token))) { ok = false; break; }
    if (used + length > sizeof(chunk)) {
      ok = file.write(reinterpret_cast<const uint8_t*>(chunk), used) == used;
      used = 0;
    }
    memcpy(chunk + used, token, length);
    used += length;
  }
  if (ok && used) ok = file.write(reinterpret_cast<const uint8_t*>(chunk), used) == used;
  if (ok) ok = file.print('\n') == 1;
  file.flush();
  if (file.getWriteError()) ok = false;
  file.close();
  if (!ok) {
    rawRecorderStorageFailed = true;
    // Delete the session on close: a partially written RAW_Data line is invalid.
    Serial.println(F("[RAW] SD write failed; session will be discarded"));
  }
  return ok;
}

void flushRawRecorderFrame() {
  rawRecorderLastRssi = rawRecorderPeakRssi;
  if (rawRecorderEdgeCount && rawRecorderSavedCount &&
      ((rawRecorderEdges[0] > 0) == rawRecorderFileLastPositive)) {
    // Filtering/damage may remove whole frames. Never export a same-level join
    // that violates Flipper RAW's global alternation rule.
    rawRecorderFrameDamaged = true;
  }
  if (rawRecorderFrameDamaged) {
    ++rawRecorderRejectedFrames;
    Serial.printf("[RAW] damaged frame rejected; total=%lu\n",
                  static_cast<unsigned long>(rawRecorderRejectedFrames));
  } else if (rawRecorderEdgeCount >= RAW_REC_MIN_EDGES &&
             // No in-burst RSSI sample is different from a weak measured burst.
             (rawRecorderPeakRssi <= -127.0f || rawRecorderPeakRssi >= rawRecorderRssiThreshold)) {
    if (saveRawFrameToSession()) {
      ++rawRecorderSavedCount;
      rawRecorderFileLastPositive = rawRecorderEdges[rawRecorderEdgeCount - 1] > 0;
    }
  }
  rawRecorderEdgeCount = 0;
  rawRecorderPeakRssi = -127.0f;
  rawRecorderFrameDamaged = false;
}

void stopRawRecorderSession(bool flushPending, bool discardFile) {
  research::raw_capture::stop();
  const uint32_t captureStoppedAtUs = micros();
  if (flushPending) {
    handleRawRecorderCapture();
    const auto capture = research::raw_capture::snapshot();
    const uint32_t terminal = captureStoppedAtUs - capture.lastEdgeUs;
    if (rawRecorderEdgeCount && terminal && terminal <= INT32_MAX) {
      if (rawRecorderEdgeCount < RAW_REC_MAX_EDGES) {
        rawRecorderEdges[rawRecorderEdgeCount++] = capture.lastLevel ? static_cast<int>(terminal)
                                                                  : -static_cast<int>(terminal);
      } else rawRecorderFrameDamaged = true;
    }
    flushRawRecorderFrame();
  } else rawRecorderEdgeCount = 0;
  const auto capture = research::raw_capture::snapshot();
  Serial.printf("[RAW] edges=%lu captured=%lu dropped=%lu rejected=%lu high=%u frames=%u damaged=%lu\n",
    static_cast<unsigned long>(capture.edges), static_cast<unsigned long>(capture.captured),
    static_cast<unsigned long>(capture.dropped), static_cast<unsigned long>(capture.rejected),
    capture.highWater, rawRecorderSavedCount, static_cast<unsigned long>(rawRecorderRejectedFrames));
  String fileToDiscard = rawRecorderSessionFile;
  rawRecorderSessionFile = "";
  const bool emptySession = flushPending && rawRecorderSavedCount == 0;
  if ((discardFile || rawRecorderStorageFailed || emptySession) && fileToDiscard.length() && SD.exists(fileToDiscard)) {
    SD.remove(fileToDiscard);
    rawRecorderSavedCount = 0;
    rawRecorderStoppedFile = "";
    rawRecorderLastFile = "";
  }
  if (capture.dropped || capture.rejected || rawRecorderRejectedFrames || rawRecorderStorageFailed) {
    OLED_printError(rawRecorderStorageFailed ? F("SD error: discarded") : F("RAW loss: see serial"), true);
    delay(900); // Capture is stopped before displaying the explicit loss warning.
  } else if (emptySession && !discardFile) {
    OLED_printError(F("No valid RAW frames"), true);
    delay(900);
  }
}

void handleRawRecorderCapture() {
  const auto capture = research::raw_capture::snapshot();
  const uint32_t losses = capture.dropped + capture.rejected;
  if (losses != rawRecorderSeenLosses) {
    rawRecorderSeenLosses = losses;
    rawRecorderFrameDamaged = true;
    Serial.printf("[RAW] capture loss=%lu; rejecting frame until next gap\n",
                  static_cast<unsigned long>(losses));
    // The gap may have occurred before the loss in the backlog. Discard the
    // entire queued backlog; resynchronize at a future, observed long gap.
    int32_t discarded;
    for (uint16_t i = 0; i < research::raw_capture::kCapacity &&
         research::raw_capture::pop(discarded); ++i) {}
    rawRecorderEdgeCount = 0;
    return;
  }
  const uint32_t now = micros();
  if (capture.running && capture.edges && now - capture.lastEdgeUs < RAW_REC_FRAME_GAP_US &&
      now - rawRecorderRssiSampleUs >= 1000) {
    rawRecorderRssiSampleUs = now;
    const float rssi = ELECHOUSE_cc1101.getRssi();
    if (rssi > rawRecorderPeakRssi) rawRecorderPeakRssi = rssi;
  }
  int32_t interval;
  // Bound work per UI iteration. ISR capture continues during SD/OLED writes.
  for (uint16_t processed = 0; processed < research::raw_capture::kCapacity &&
       research::raw_capture::pop(interval); ++processed) {
    // SD can stall long enough to overflow while a previous complete frame is
    // written. Recheck before consuming the next interval or saving a new frame.
    const auto current = research::raw_capture::snapshot();
    if (current.dropped + current.rejected != rawRecorderSeenLosses) {
      rawRecorderSeenLosses = current.dropped + current.rejected;
      rawRecorderFrameDamaged = true;
      rawRecorderEdgeCount = 0;
      for (uint16_t i = 0; i < research::raw_capture::kCapacity &&
           research::raw_capture::pop(interval); ++i) {}
      Serial.println(F("[RAW] capture loss during storage/UI; frame rejected"));
      return;
    }
    if (rawRecorderDiscardFirstInterval) {
      // Capture may begin part-way through a HIGH or LOW interval.
      rawRecorderDiscardFirstInterval = false;
      continue;
    }
    // The first persisted Flipper RAW interval must be HIGH. Initial LOW idle
    // time precedes the recorded waveform; do not invent a partial HIGH pulse.
    if (!rawRecorderSavedCount && !rawRecorderEdgeCount && interval < 0 && !rawRecorderFrameDamaged) continue;
    const uint32_t duration = interval < 0 ? -interval : interval;
    if (!rawRecorderFrameDamaged) {
      if (rawRecorderEdgeCount < RAW_REC_MAX_EDGES) rawRecorderEdges[rawRecorderEdgeCount++] = interval;
      else rawRecorderFrameDamaged = true;
    }
    if (duration >= RAW_REC_FRAME_GAP_US) flushRawRecorderFrame();
  }
}

bool saveKeyToSD(tpKeyData* kd) {
  if (!kd || kd->codeLenth == 0 || kd->frequency == 0.0) {
    return false;
  }
  String fileName = allocateNextSignalFileName();
  if (fileName.length() == 0) {
    return false;
  }
  File file = SD.open(fileName, FILE_WRITE);
  if (!file) {
    Serial.print(F("Failed to open file for writing: "));
    Serial.println(fileName);
    return false;
  }
  char keyHex[24];
  for (int i = 0; i < 8; i++) {
    snprintf(keyHex + i * 3, sizeof(keyHex) - i * 3, "%02X%s", kd->keyID[i], i == 7 ? "" : " ");
  }
  char record[320];
  const String protocol = getTypeName(kd->type);
  const int recordLength = snprintf(record, sizeof(record),
    "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: %lu\n"
    "Preset: FuriHalSubGhzPresetOok270Async\nProtocol: %s\nBit: %d\nKey: %s\nTE: %d\n",
    static_cast<unsigned long>(kd->frequency * 1000000), protocol.c_str(), kd->bitLength, keyHex, kd->te);
  const bool wroteRecord = recordLength > 0 && recordLength < static_cast<int>(sizeof(record)) &&
    file.write(reinterpret_cast<const uint8_t*>(record), recordLength) == static_cast<size_t>(recordLength);
  file.flush();
  const bool complete = wroteRecord && !file.getWriteError();
  file.close();
  if (!complete) {
    SD.remove(fileName);
    Serial.println(F("[RX] SD write failed; incomplete recording discarded"));
    return false;
  }
  Serial.print(F("Saved to "));
  Serial.println(fileName);
  return true;
}

static bool inspectLegacySubFile(const String& path, research::SubInfo& info) {
  legacyFileLoadError = "";
  File file = SD.open(path, FILE_READ);
  if (!file) { legacyFileLoadError = F("SD file open failed"); return false; }
  const uint32_t fileBytes = file.size();
  if (!fileBytes || fileBytes > 1024UL * 1024UL) {
    file.close(); legacyFileLoadError = F("File size unsupported"); return false;
  }
  research::SubParser parser;
  uint8_t chunk[192];
  uint32_t readBytes = 0;
  bool valid = true;
  while (readBytes < fileBytes && valid) {
    const size_t count = file.read(chunk, sizeof(chunk));
    if (!count) { valid = false; break; }
    readBytes += count;
    valid = parser.feed(reinterpret_cast<const char*>(chunk), count);
    delay(1);
  }
  file.close();
  valid = valid && readBytes == fileBytes && parser.finish();
  if (!valid) {
    legacyFileLoadError = parser.error() == research::SubError::UnsupportedPreset ?
      F("Unknown .sub preset") : F("Invalid .sub: serial");
    Serial.printf("[SUB] rejected %s line=%lu: %s (read %lu/%lu bytes)\n", path.c_str(),
      static_cast<unsigned long>(parser.line()), parser.errorText(),
      static_cast<unsigned long>(readBytes), static_cast<unsigned long>(fileBytes));
    return false;
  }
  info = parser.info();
  const char* reason = nullptr;
  if (!research::legacyOokFile(info, &reason)) {
    legacyFileLoadError = F("Needs async OOK");
    Serial.printf("[SUB] legacy path rejected: %s\n", reason);
    return false;
  }
  return true;
}

bool loadKeyFromSD(String fileName, tpKeyData* kd) {
  ensureSubExplorerDir();
  research::SubInfo info;
  if (!inspectLegacySubFile(subExplorer.currentDir + "/" + fileName, info)) return false;
  memset(kd, 0, sizeof(*kd));
  kd->frequency = info.frequencyHz / 1000000.0f;
  const research::RadioPreset* preset = research::findPreset(info.preset);
  snprintf(kd->preset, sizeof(kd->preset), "%s", preset ? preset->name : "Custom");
  kd->bitLength = info.bits;
  kd->te = info.teUs;
  for (uint8_t i = 0; i < info.keyBytes; ++i) kd->keyID[8 - info.keyBytes + i] = info.key[i];
  if (info.kind == research::SubKind::Raw) {
    kd->type = kLINEAR;
    kd->codeLenth = 1; // Legacy UI validity flag, not a pulse count.
  } else {
    kd->codeLenth = info.bits > 255 ? 255 : info.bits;
    if (!strcmp(info.protocol, "Princeton")) kd->type = kPrinceton;
    else if (!strcmp(info.protocol, "RcSwitch")) kd->type = kRcSwitch;
    else if (!strcmp(info.protocol, "CAME")) kd->type = kCAME;
    else if (!strcmp(info.protocol, "NICE")) kd->type = kNICE;
    else if (!strcmp(info.protocol, "HOLTEK")) kd->type = kHOLTEK;
    else kd->type = kUnknown;
  }
  Serial.printf("[SUB] validated freq=%lu preset=%s protocol=%s pulses=%lu\n",
    static_cast<unsigned long>(info.frequencyHz), info.preset, info.protocol,
    static_cast<unsigned long>(info.raw.pulses));
  return true;
}


void OLED_printBruteIntro() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  display.setCursor(69, 22);
  display.print(F("to config"));
  display.setCursor(76, 13);
  display.print(F("Hold OK"));
  display.drawBitmap(77, 36, image_AntLogo_bits, 38, 22, 1);
  display.drawBitmap(0, 10, image_DolphinWait_bits, 59, 54, 1);
  display.display();
}

static int16_t getBruteConfigArrowY(uint8_t selection) {
  return selection == 0 ? 24 : 36;
}

static void drawBruteConfigFrame(int16_t arrowY = -1) {
  if (arrowY < 0) arrowY = getBruteConfigArrowY(bruteConfigSelection);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);

  display.setCursor(3, 2);
  display.print(F("Bruteforce"));
  display.setCursor(1, 10);
  display.print(F("====================="));

  display.setCursor(7, 24);
  display.print(F("Type: "));
  String typeLabel = String(bruteTypes[bruteTypeIndex]) + "-" + String(bruteBits);
  if (bruteTypeIndex != 4) typeLabel += F("bit");
  display.print(typeLabel);

  display.setCursor(7, 36);
  display.print(F("Freq: "));
  display.print(bruteFreqLabels[bruteFreqIndex]);

  display.setCursor(1, arrowY);
  display.print(F(">"));
  display.display();
}

void OLED_printBruteConfig(int previousSelection) {
  if (previousSelection < 0 || previousSelection == bruteConfigSelection) {
    drawBruteConfigFrame();
    return;
  }

  int16_t fromY = getBruteConfigArrowY(previousSelection);
  int16_t toY = getBruteConfigArrowY(bruteConfigSelection);
  const byte steps = 4;
  for (byte step = 1; step <= steps; step++) {
    int progress = (step * 100) / steps;
    int eased = progress < 50
      ? (2 * progress * progress) / 100
      : 100 - (2 * (100 - progress) * (100 - progress)) / 100;
    int16_t arrowY = fromY + ((toY - fromY) * eased) / 100;
    drawBruteConfigFrame(arrowY);
    delay(1);
  }
  drawBruteConfigFrame(toY);
}

void OLED_printBruteProgress(uint16_t progress, uint16_t total) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setTextWrap(false);
  int signalDigits = (progress < 10) ? 1 : (progress < 100) ? 2 : (progress < 1000) ? 3 : 4;
  int signalX = 69;
  switch (signalDigits) {
    case 1: signalX = 78; break;
    case 2: signalX = 75; break;
    case 3: signalX = 72; break;
    default: signalX = 69; break;
  }
  display.setCursor(signalX, 24);
  char buf[18];
  snprintf(buf, sizeof(buf), "%u/%u", progress, total);
  display.print(buf);
  display.drawBitmap(77, 36, image_AntLogoON_bits, 38, 22, 1);
  display.drawBitmap(0, 10, image_DolphinWait_bits, 59, 54, 1);
  uint16_t percent = (total == 0) ? 0 : (progress * 100) / total;
  int percentDigits = (percent < 10) ? 1 : (percent < 100) ? 2 : 3;
  int percentX = -1;
  switch (percentDigits) {
    case 1: percentX = 85; break;
    case 2: percentX = 79; break;
    case 3: percentX = 73; break;
    default: percentX = -1; break;
  }
  if (percentX >= 0) {
    display.setTextSize(2);
    display.setCursor(percentX, 6);
    if (percent > 100) percent = 100;
    snprintf(buf, sizeof(buf), "%u%%", percent);
    display.print(buf);
    display.setTextSize(1);
  }
  display.display();
}

const BruteProtocol* getBruteProtocolByIndex(int idx) {
  switch (idx) {
    case 0: return &protoCame;
    case 1: return &protoNice;
    case 2: return &protoAnsonic;
    case 3: return &protoHoltek;
    case 4: return &protoChamber;
    default: return nullptr;
  }
}

bool bruteInitTx() {
  currentBruteProto = getBruteProtocolByIndex(bruteTypeIndex);
  if (!currentBruteProto) return false;
  freqIndex = bruteFreqIndex;
  frequency = bruteFreqOptions[bruteFreqIndex];
  bruteTxPin = (bruceConfig.rfModule == CC1101_SPI_MODULE) ? bruceConfigPins.CC1101_bus.io0 : bruceConfig.rfTx;
  if (!initRfModule("tx", frequency)) return false;
  pinMode(bruteTxPin, OUTPUT);
  digitalWrite(bruteTxPin, LOW);
  bruteRfActive = true;
  return true;
}

void bruteStopTx() {
  if (!bruteRfActive) return;
  digitalWrite(bruteTxPin, LOW);
  deinitRfModule();
  bruteRfActive = false;
}

void bruteSendSequence(const int* seq, size_t len) {
  if (!seq || len == 0) return;
  for (size_t i = 0; i < len; i++) {
    int duration = seq[i];
    bool levelHigh = duration > 0;
    unsigned int delayVal = (duration > 0) ? duration : -duration;
    digitalWrite(bruteTxPin, levelHigh ? HIGH : LOW);
    delayMicroseconds(delayVal);
  }
}

void bruteSendCode(uint16_t code) {
  if (!bruteRfActive || !currentBruteProto) return;
  const BruteProtocol* p = currentBruteProto;

  bruteSendSequence(p->pilot, p->pilotLen);
  for (int bit = bruteBits - 1; bit >= 0; --bit) {
    bool set = (code >> bit) & 0x1;
    bruteSendSequence(set ? p->one : p->zero, set ? p->oneLen : p->zeroLen);
  }
  bruteSendSequence(p->stop, p->stopLen);
  digitalWrite(bruteTxPin, LOW);
}

static bool rawPlaybackStopRequested();

static bool sendRawBlock(const String& block) {
  String line = block;
  line.trim();
  if (line.length() == 0) return false;

  int transmittimings[RAW_REC_MAX_EDGES + 1];
  int count = 0;
  int startIndex = 0;

  while (startIndex < line.length() && count < RAW_REC_MAX_EDGES) {
    if (rawPlaybackActive && rawPlaybackStopRequested()) return false;
    while (startIndex < line.length() && line[startIndex] == ' ') startIndex++;
    if (startIndex >= line.length()) break;
    int index = line.indexOf(' ', startIndex);
    if (index < 0) index = line.length();
    String token = line.substring(startIndex, index);
    token.trim();
    if (token.length() > 0) {
      transmittimings[count++] = token.toInt();
    }
    startIndex = index + 1;
  }

  if (count <= 0) return false;
  transmittimings[count] = 0;
  RCSwitch_RAW_send(transmittimings);
  return true;
}

static void updateRawPlaybackStopFromPins() {
  bool okDown = digitalRead(BUTTON_OK) == LOW;
  bool backDown = digitalRead(BUTTON_BACK) == LOW;
  unsigned long now = millis();

  if (rawPlaybackIgnoreOkRelease) {
    if (!okDown) {
      rawPlaybackIgnoreOkRelease = false;
    }
  } else if (okDown) {
    rawPlaybackOkWasPressed = true;
  }

  if (rawPlaybackIgnoreBackRelease) {
    if (!backDown) {
      rawPlaybackIgnoreBackRelease = false;
      rawPlaybackBackWasPressed = false;
      rawPlaybackBackPressedAt = 0;
    }
  } else if (backDown) {
    if (!rawPlaybackBackWasPressed) {
      rawPlaybackBackWasPressed = true;
      rawPlaybackBackPressedAt = now;
    }
  }

  if (rawPlaybackOkWasPressed && !okDown) {
    rawPlaybackStopRequestedFlag = true;
  }

  if (rawPlaybackBackWasPressed && !backDown) {
    if (now - rawPlaybackBackPressedAt <= BUTTON_RELEASE_CLICK_MS) {
      rawPlaybackStopRequestedFlag = true;
    }
    rawPlaybackBackWasPressed = false;
    rawPlaybackBackPressedAt = 0;
  }
}

static bool rawPlaybackStopRequested() {
  buttonOK.tick();
  buttonBack.tick();
  updateRawPlaybackStopFromPins();
  return rawPlaybackStopRequestedFlag;
}

void drawRawPlaybackWave(float phase, uint8_t progressPct) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(5, 3);
  display.print(String(frequency, 2) + F("MHz"));

  if (progressPct > 100) progressPct = 100;
  String progress = String(progressPct) + F("%");
  display.setCursor(123 - progress.length() * 6, 3);
  display.print(progress);

  display.drawRect(10, 13, 108, 37, SH110X_WHITE);
  static const int8_t rawWaveSin[64] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37,
    40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71, 73,
    76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102,
    104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121,
    122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127
  };

  auto rawWaveSinValue = [&](uint8_t x) -> int8_t {
    int8_t value = rawWaveSin[((x & 0x40) ? -x - 1 : x) & 0x3F];
    return (x & 0x80) ? -value : value;
  };

  const uint8_t phaseIndex = ((uint32_t)(phase / 9.0f)) % 63;
  const int8_t amplitude = 11;
  for (int i = 113; i > 0; i--) {
    const int x1 = 12 + ((i - 1) * 103) / 112;
    const int x2 = min(115, 12 + (i * 103) / 112);
    const int x3 = min(115, 12 + ((i + 1) * 103) / 112);
    const int y1 = 32 - rawWaveSinValue(i + phaseIndex * 16) / amplitude;
    const int y2 = 32 + rawWaveSinValue((i + phaseIndex * 16 + 1) * 2) / amplitude;
    display.drawLine(x1, y1, x2, y2, SH110X_WHITE);
    display.drawLine(x2, y1, x3, y2, SH110X_WHITE);
  }

  const char* label = "Stop";
  const uint8_t textW = strlen(label) * 6;
  const uint8_t iconW = 9;
  const uint8_t iconH = 9;
  const uint8_t gap = 3;
  const uint8_t contentW = textW + gap + iconW;
  const uint8_t padX = 5;
  const uint8_t btnX = (SCREEN_WIDTH - (contentW + padX * 2)) / 2;
  const uint8_t btnY = 52;
  const uint8_t btnW = contentW + padX * 2;
  const uint8_t btnH = 12;

  display.fillRoundRect(btnX, btnY, btnW, btnH, 2, SH110X_WHITE);
  display.fillRect(btnX, btnY + 2, btnW, btnH - 2, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(btnX + padX, btnY + 3);
  display.print(label);
  display.drawBitmap(btnX + padX + textW + gap, btnY + 2, image_REC_bits, iconW, iconH, SH110X_BLACK);
  display.setTextColor(SH110X_WHITE);
  display.display();
}

static void setRawPlaybackTarget(float targetPct) {
  if (targetPct > 100.0f) targetPct = 100.0f;
  if (targetPct < rawPlaybackTargetPct) targetPct = rawPlaybackTargetPct;
  rawPlaybackTargetPct = targetPct;
}

static void pumpRawPlaybackAnimation(bool force = false) {
  if (!rawPlaybackActive) return;

  unsigned long now = millis();
  if (!force && now - rawPlaybackLastDrawMs < 18) return;
  rawPlaybackLastDrawMs = now;

  float delta = rawPlaybackTargetPct - rawPlaybackShownPct;
  rawPlaybackShownPct += delta * 0.16f;
  if (fabs(delta) < 0.35f) {
    rawPlaybackShownPct = rawPlaybackTargetPct;
  }

  float phase = (float)(now - rawPlaybackStartMs) * 0.1f;
  drawRawPlaybackWave(phase, (uint8_t)(rawPlaybackShownPct + 0.5f));
}

static void rawPlaybackAnimationTask(void* parameter) {
  (void)parameter;
  while (rawPlaybackActive) {
    updateRawPlaybackStopFromPins();
    pumpRawPlaybackAnimation();
    delay(8);
  }
  rawPlaybackTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

static void startRawPlaybackAnimationTask() {
  if (rawPlaybackTaskHandle != nullptr) return;
  xTaskCreatePinnedToCore(
    rawPlaybackAnimationTask,
    "raw_play_anim",
    4096,
    nullptr,
    1,
    &rawPlaybackTaskHandle,
    0
  );
}

static void stopRawPlaybackAnimationTask() {
  rawPlaybackActive = false;
  unsigned long startMs = millis();
  while (rawPlaybackTaskHandle != nullptr && millis() - startMs < 120) {
    delay(4);
  }
}

static void armRawPlaybackReleaseGuards() {
  if (digitalRead(BUTTON_OK) == LOW) {
    rawRecorderIgnoreOkRelease = true;
    transmitIgnoreOkRelease = true;
  }
  if (digitalRead(BUTTON_BACK) == LOW) {
    rawRecorderIgnoreBackRelease = true;
    transmitIgnoreBackRelease = true;
  }
  buttonOK.resetStates();
  buttonBack.resetStates();
}

static void finishRawPlaybackAnimation(uint16_t durationMs) {
  setRawPlaybackTarget(100.0f);
  unsigned long startMs = millis();
  while (!rawPlaybackStopRequested() && millis() - startMs < durationMs) {
    delay(18);
  }
}

static void rawPlaybackDelayMicroseconds(unsigned int durationUs) {
  while (durationUs > 0) {
    if (rawPlaybackActive && rawPlaybackStopRequested()) return;
    const unsigned int slice = durationUs > 500 ? 500 : durationUs;
    delayMicroseconds(slice);
    durationUs -= slice;
  }
}

bool playRawRecorderFile(const String& fileName) {
  research::SubInfo info;
  if (!inspectLegacySubFile(fileName, info) || info.kind != research::SubKind::Raw) {
    OLED_printError(legacyFileLoadError.length() ? legacyFileLoadError : F("Requires RAW file"), true);
    delay(1000);
    return false;
  }
  // The full, validated file goes to the bounded RMT laboratory path. Never
  // truncate a RAW line or force a different frequency, preset or modulation.
  suspendLegacyReceive();
  runRadioLabFile(fileName.c_str());
  restoreReceiveMode();
  armRawPlaybackReleaseGuards();
  return false; // The laboratory UI owns/announces its actual TX result.
}

static bool prepareLegacyDecodedTx(const research::LegacyDecodedPlan& plan) {
  if (!ensureCC1101Initialized()) return false;
  suspendLegacyReceive();
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SIDLE);
  if (!waitCC1101State(1)) return false;
  // Start sparse official presets from silicon reset defaults, not from a prior
  // application or the SmartRC library's unrelated register defaults.
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SRES);
  delay(2);
  if (!waitCC1101State(1)) return false;
  uint8_t pa[8] = {};
  ELECHOUSE_cc1101.SpiWriteBurstReg(CC1101_PATABLE, pa, 8);
  for (size_t i = 0; i < research::kRegisterCount; ++i) {
    if (!plan.radio.present[i]) continue;
    ELECHOUSE_cc1101.SpiWriteReg(i, plan.radio.value[i]);
    if (ELECHOUSE_cc1101.SpiReadReg(i) != plan.radio.value[i]) return false;
  }
  const uint32_t word = static_cast<uint32_t>((uint64_t(plan.frequencyHz) * 65536U +
                                              research::kCrystalHz / 2U) / research::kCrystalHz);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FREQ2, word >> 16);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FREQ1, word >> 8);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FREQ0, word);
  // The legacy path stays at the lowest shared nominal power (-10 dBm).
  // Imported PA tables never override the laboratory policy.
  pa[1] = chat_radio_pa(plan.frequencyHz, 0);
  ELECHOUSE_cc1101.SpiWriteBurstReg(CC1101_PATABLE, pa, 8);
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SCAL);
  if (!waitCC1101State(1)) return false;
  digitalWrite(cc1101GDO0Pin, LOW);
  pinMode(cc1101GDO0Pin, OUTPUT);
  ELECHOUSE_cc1101.SetTx();
  return waitCC1101State(0x13);
}


void sendSynthKey(tpKeyData* kd) {
  if (subExplorer.selectedFile.length() == 0) {
    OLED_printError(F("Select validated file"), true); delay(1000); return;
  }
  const String path = subExplorer.currentDir + "/" + subExplorer.selectedFile;
  research::SubInfo info;
  if (!inspectLegacySubFile(path, info)) {
    OLED_printError(legacyFileLoadError, true); delay(1000); return;
  }
  if (info.kind == research::SubKind::Raw) {
    playRawRecorderFile(path);
    OLED_printKey(kd, subExplorer.selectedFile);
    return;
  }
  research::LegacyDecodedPlan plan;
  const char* reason = nullptr;
  if (!research::legacyDecodedPlan(info, plan, &reason)) {
    Serial.printf("[SUB] transmission blocked: %s\n", reason ? reason : "invalid file");
    OLED_printError(F("Unsupported: serial"), true); delay(1200); return;
  }
  if (!radioLabPermitTx(plan.frequencyHz, plan.durationBoundMs)) {
    OLED_printError(F("TX profile/limit off"), true);
    Serial.println(F("[SUB] enable an appropriate shielded laboratory profile in Radio Settings; no TX occurred"));
    delay(1200); return;
  }
  OLED_printKey(kd, subExplorer.selectedFile, true);
  if (prepareLegacyDecodedTx(plan)) {
    Serial.printf("[SUB] exact %s freq=%lu protocol=%u bits=%u repeats=%u bound=%lu ms nominal=-10dBm\n",
      info.preset, static_cast<unsigned long>(plan.frequencyHz), plan.protocol, plan.bits, plan.repeats,
      static_cast<unsigned long>(plan.durationBoundMs));
    RCSwitch_send(plan.key, plan.bits, plan.pulseUs, plan.protocol, plan.repeats);
  } else {
    Serial.println(F("[SUB] TX configuration/readback/state error"));
    OLED_printError(F("TX radio error"), true); delay(900);
  }
  deinitRfModule();
  restoreReceiveMode();
  OLED_printKey(kd, subExplorer.selectedFile);
}

void RCSwitch_send(uint64_t data, unsigned int bits, int pulse, int protocol, int repeat) {
  RCSwitch mySwitch = RCSwitch();

  int txPin = bruceConfig.rfTx;
  if (bruceConfig.rfModule == CC1101_SPI_MODULE) {
    txPin = bruceConfigPins.CC1101_bus.io0;
  }
  mySwitch.enableTransmit(txPin);

  mySwitch.setProtocol(protocol);
  if (pulse) { mySwitch.setPulseLength(pulse); }
  int rep = repeat > 0 ? repeat : 6;
  if (rep > 6) rep = 6;
  mySwitch.setRepeatTransmit(rep);
  mySwitch.send(data, bits);

  mySwitch.disableTransmit();
}

void RCSwitch_RAW_send(int *ptrtransmittimings) {
  int nTransmitterPin = bruceConfig.rfTx;
  if (bruceConfig.rfModule == CC1101_SPI_MODULE) {
    nTransmitterPin = bruceConfigPins.CC1101_bus.io0;
  }

  if (!ptrtransmittimings) return;

  bool hasNeg = false;
  unsigned long sum_us = 0;
  for (int i = 0; ptrtransmittimings[i]; ++i) {
    if (ptrtransmittimings[i] < 0) hasNeg = true;
    int v = ptrtransmittimings[i] >= 0 ? ptrtransmittimings[i] : -ptrtransmittimings[i];
    sum_us += (unsigned long)v;
  }
  int nRepeatTransmit = 1;
  if (sum_us > 0) {
    nRepeatTransmit = (int)(900000UL / sum_us);
    if (nRepeatTransmit < 1) nRepeatTransmit = 1;
    if (nRepeatTransmit > 6) nRepeatTransmit = 6;
  }
  if (rawPlaybackActive) {
    nRepeatTransmit = 1;
  }

  for (int nRepeat = 0; nRepeat < nRepeatTransmit; nRepeat++) {
    if (rawPlaybackActive && rawPlaybackStopRequested()) break;
    unsigned int currenttiming = 0;
    bool level = true;
    while (ptrtransmittimings[currenttiming]) {
      if (rawPlaybackActive && rawPlaybackStopRequested()) break;
      int dur = ptrtransmittimings[currenttiming];
      bool lvl;
      unsigned int t;
      if (hasNeg) {
        lvl = (dur >= 0);
        t = (unsigned int)(dur >= 0 ? dur : -dur);
      } else {
        lvl = level;
        t = (unsigned int)(dur >= 0 ? dur : -dur);
        level = !level;
      }
      digitalWrite(nTransmitterPin, lvl ? HIGH : LOW);
      rawPlaybackDelayMicroseconds(t);
      currenttiming++;
    }
    digitalWrite(nTransmitterPin, LOW);
    rawPlaybackDelayMicroseconds(8000);
  }
}

void stepFrequency(int step) {
  freqIndex = (freqIndex + step + numFrequencies) % numFrequencies;
  frequency = frequencies[freqIndex];
}

void analyzerInit() {
  for (uint8_t i = 0; i < ANALYZER_HIST_CNT; i++) {
    analyzerState.hist_freq[i] = 0;
    analyzerState.hist_count[i] = 0;
  }
  analyzerState.curr_freq = 0;
  analyzerState.saved_freq = 0;
  analyzerState.rssi_now = 0.0f;
  analyzerState.last_rssi = 0.0f;
  analyzerState.has_signal = false;
  analyzerState.threshold = ANALYZER_DEFAULT_TRIG;
  analyzerFilterVal = 0.0f;
  analyzerTrigLevel = ANALYZER_DEFAULT_TRIG;
  analyzerHoldCount = 0;
  analyzerLocked = false;
  analyzerLastDraw = 0;

  ELECHOUSE_cc1101.SpiStrobe(0x36);
  delayMicroseconds(200);
  ELECHOUSE_cc1101.setRxBW(812.0);
  ELECHOUSE_cc1101.SetRx();
}

uint32_t analyzerSmoothAvg(uint32_t newVal) {
  float newFloat = (float)newVal;
  float mix = (fabsf(newFloat - analyzerFilterVal) > 500000.0f) ? 0.9f : 0.03f;
  analyzerFilterVal += (newFloat - analyzerFilterVal) * mix;
  return (uint32_t)analyzerFilterVal;
}

uint32_t analyzerNearestFreq(uint32_t input) {
  uint32_t prev = 0;
  uint32_t out = 0;
  for (int i = 0; i < ANALYZER_FREQ_COUNT; i++) {
    uint32_t cur = (uint32_t)(analyzerFreqs[i] * 1000000.0f);
    if (cur == 0) continue;
    if (cur == input) return cur;
    if (cur > input && prev < input) {
      out = (cur - input < input - prev) ? cur : prev;
      break;
    }
    prev = cur;
  }
  if (!out && prev) out = prev;
  return out;
}

void analyzerAddHistory(uint32_t freq) {
  uint32_t normFreq = analyzerNearestFreq(freq);
  if (!normFreq) return;

  bool found = false;
  for (uint8_t i = 0; i < ANALYZER_HIST_CNT; i++) {
    if (analyzerState.hist_freq[i] != normFreq) continue;
    found = true;
    if (analyzerState.hist_count[i] == 0) analyzerState.hist_count[i] = 1;
    else analyzerState.hist_count[i]++;

    if (i > 0) {
      uint32_t f = analyzerState.hist_freq[i];
      uint8_t c = analyzerState.hist_count[i];
      for (int j = i; j > 0; j--) {
        analyzerState.hist_freq[j] = analyzerState.hist_freq[j - 1];
        analyzerState.hist_count[j] = analyzerState.hist_count[j - 1];
      }
      analyzerState.hist_freq[0] = f;
      analyzerState.hist_count[0] = c;
    }
    break;
  }

  if (!found) {
    for (int i = ANALYZER_HIST_CNT - 1; i > 0; i--) {
      analyzerState.hist_freq[i] = analyzerState.hist_freq[i - 1];
      analyzerState.hist_count[i] = analyzerState.hist_count[i - 1];
    }
    analyzerState.hist_freq[0] = normFreq;
    analyzerState.hist_count[0] = 1;
  }
}

void analyzerSaveFreq() {
  if (!analyzerState.curr_freq) return;
  uint32_t saveFreq = analyzerNearestFreq(analyzerState.curr_freq);
  if (saveFreq && saveFreq != analyzerState.saved_freq) {
    analyzerState.saved_freq = saveFreq;
  }
}

bool analyzerHandleInput() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  if (buttonBack.isClick()) {
    analyzerExitRequested = true;
    return false;
  }

  bool displayChanged = false;

  if (buttonDown.isClick()) {
    const float previousLevel = analyzerTrigLevel;

    analyzerTrigLevel -= ANALYZER_TRIG_STEP;

    if (analyzerTrigLevel < ANALYZER_RSSI_LOW) {
      analyzerTrigLevel = ANALYZER_RSSI_LOW;
    }

    displayChanged = analyzerTrigLevel != previousLevel;

    Serial.printf(
      "[Analyzer] Trigger: %.0f dBm\n",
      analyzerTrigLevel
    );
  }

  if (buttonUp.isClick()) {
    const float previousLevel = analyzerTrigLevel;

    if (analyzerTrigLevel == ANALYZER_RSSI_LOW) {
      analyzerTrigLevel = -95.0f;
    } else {
      analyzerTrigLevel += ANALYZER_TRIG_STEP;
    }

    if (analyzerTrigLevel > ANALYZER_RSSI_HIGH) {
      analyzerTrigLevel = ANALYZER_RSSI_HIGH;
    }

    displayChanged = analyzerTrigLevel != previousLevel;

    Serial.printf(
      "[Analyzer] Trigger: %.0f dBm\n",
      analyzerTrigLevel
    );
  }

  if (buttonOK.isClick()) {
    analyzerSaveFreq();
    displayChanged = true;

    Serial.printf(
      "[Analyzer] Saved frequency: %lu Hz\n",
      analyzerState.saved_freq
    );
  }

  analyzerState.threshold = analyzerTrigLevel;

  if (displayChanged) {
    OLED_printAnalyzer();
    analyzerLastDraw = millis();
  }

  return true;
}

void analyzerDoScan() {
  bool hadSignal = analyzerState.has_signal;
  uint32_t prevFreq = analyzerState.curr_freq;
  analyzerScanResult.rough_rssi = -127.0f;
  analyzerScanResult.fine_rssi = -127.0f;

  ELECHOUSE_cc1101.SpiStrobe(0x36);
  ELECHOUSE_cc1101.setRxBW(812.0);

  for (int i = 0; i < ANALYZER_FREQ_COUNT; i++) {
    if (!analyzerHandleInput()) {
      return;
    }

    uint32_t freqHz = (uint32_t)(analyzerFreqs[i] * 1000000.0f);
    if (freqHz == 462750000 || freqHz == 467750000 || freqHz == 464000000 || freqHz > 920000000) {
      continue;
    }
    if (analyzerIsNoiseFreq(freqHz)) {
      continue;
    }

    ELECHOUSE_cc1101.setMHZ(analyzerFreqs[i]);
    ELECHOUSE_cc1101.SetRx();
    delayMicroseconds(ANALYZER_STEP_DELAY_US);

    float rssi = ELECHOUSE_cc1101.getRssi();
    if (analyzerScanResult.rough_rssi < rssi) {
      analyzerScanResult.rough_rssi = rssi;
      analyzerScanResult.rough_freq = freqHz;
    }
  }

  if (analyzerScanResult.rough_rssi > analyzerTrigLevel && analyzerScanResult.rough_freq > 300000) {
    ELECHOUSE_cc1101.SpiStrobe(0x36);
    ELECHOUSE_cc1101.setRxBW(58.0);
    for (uint32_t freqHz = analyzerScanResult.rough_freq - 300000;
         freqHz < analyzerScanResult.rough_freq + 300000;
         freqHz += 20000) {

      if (!analyzerHandleInput()) {
        return;
      }
      if (analyzerIsNoiseFreq(freqHz)) {
        continue;
      }
      float freqMHz = freqHz / 1000000.0f;
      ELECHOUSE_cc1101.setMHZ(freqMHz);
      ELECHOUSE_cc1101.SetRx();
      delayMicroseconds(ANALYZER_STEP_DELAY_US);

      float rssi = ELECHOUSE_cc1101.getRssi();
      if (analyzerScanResult.fine_rssi < rssi) {
        analyzerScanResult.fine_rssi = rssi;
        analyzerScanResult.fine_freq = freqHz;
      }
    }
  }

  if (analyzerScanResult.fine_rssi > analyzerTrigLevel + ANALYZER_LOCK_MARGIN_DB &&
      !analyzerIsNoiseFreq(analyzerScanResult.fine_freq)) {
    analyzerHoldCount = 20;
    if (analyzerFilterVal != 0.0f) {
      analyzerScanResult.fine_freq = analyzerSmoothAvg(analyzerScanResult.fine_freq);
    }
    analyzerLocked = true;
    analyzerState.curr_freq = analyzerScanResult.fine_freq;
    analyzerState.rssi_now = analyzerScanResult.fine_rssi;
    analyzerState.last_rssi = analyzerScanResult.fine_rssi;
    analyzerState.has_signal = true;
  } else if (analyzerScanResult.rough_rssi > analyzerTrigLevel + ANALYZER_LOCK_MARGIN_DB &&
             analyzerHoldCount < 10 &&
             !analyzerIsNoiseFreq(analyzerScanResult.rough_freq)) {
    analyzerHoldCount = 20;
    if (analyzerFilterVal != 0.0f) {
      analyzerScanResult.rough_freq = analyzerSmoothAvg(analyzerScanResult.rough_freq);
    }
    analyzerLocked = true;
    analyzerState.curr_freq = analyzerScanResult.rough_freq;
    analyzerState.rssi_now = analyzerScanResult.rough_rssi;
    analyzerState.last_rssi = analyzerScanResult.rough_rssi;
    analyzerState.has_signal = true;
  } else {
    analyzerLocked = false;
    analyzerState.has_signal = false;
    analyzerState.curr_freq = 0;
    analyzerState.rssi_now = 0.0f;
    analyzerState.last_rssi = 0.0f;
    analyzerFilterVal = 0.0f;
    if (analyzerHoldCount > 0) {
      analyzerHoldCount--;
    }
  }

  analyzerLocked = (analyzerState.rssi_now > analyzerTrigLevel);
  analyzerState.threshold = analyzerTrigLevel;
  if (analyzerState.has_signal) {
    if (!hadSignal) {
      analyzerAddHistory(analyzerState.curr_freq);
    } else {
      uint32_t diff = (analyzerState.curr_freq > prevFreq)
        ? (analyzerState.curr_freq - prevFreq)
        : (prevFreq - analyzerState.curr_freq);
      if (diff >= 300000UL) {
        analyzerAddHistory(analyzerState.curr_freq);
      }
    }
  }
}

void OLED_printAnalyzer() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(2);
  char buf[32];
  if (analyzerState.has_signal && analyzerState.curr_freq > 0) {
    display.fillRect(6, 0, 88, 20, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    snprintf(buf, sizeof(buf), "%03lu.%03lu",
             analyzerState.curr_freq / 1000000UL % 1000UL,
             analyzerState.curr_freq / 1000UL % 1000UL);
  } else {
    display.setTextColor(SH110X_WHITE);
    snprintf(buf, sizeof(buf), "---.---");
  }
  display.setCursor(8, 3);
  display.print(buf);

  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(96, 6);
  display.print("MHz");

  const uint8_t left = 2, right = 66, top = 28;
  uint8_t line = 0;
  for (uint8_t i = 0; i < ANALYZER_HIST_CNT; i++) {
    uint8_t xpos = (i % 2 == 0) ? left : right;
    uint8_t ypos = top + line * 9;
    if (i % 2 == 1) line++;

    display.setCursor(xpos, ypos);
    if (analyzerState.hist_freq[i] > 0) {
      snprintf(buf, sizeof(buf), "%03lu.%03lu",
               analyzerState.hist_freq[i] / 1000000UL % 1000UL,
               analyzerState.hist_freq[i] / 1000UL % 1000UL);
      display.print(buf);
    } else {
      display.print("---.---");
    }

    display.setCursor(xpos + 41, ypos);
    if (analyzerState.hist_count[i] > 0) {
      snprintf(buf, sizeof(buf), "x%u", analyzerState.hist_count[i]);
      display.print(buf);
    } else {
      display.print("MHz");
    }
  }

  display.setCursor(1, 55);
  display.print((int)analyzerTrigLevel);

  analyzerDrawGraph(29, 57);
  display.display();
}

void analyzerDrawGraph(uint8_t x, uint8_t y) {
  const uint8_t width = 95;

  if (analyzerState.rssi_now != 0.0f) {
    float rssi = analyzerState.rssi_now;
    if (rssi > ANALYZER_RSSI_HIGH) rssi = ANALYZER_RSSI_HIGH;
    rssi = (rssi - ANALYZER_RSSI_LOW) / (ANALYZER_RSSI_MUL * ANALYZER_RSSI_GRAPH_SCALE);

    uint8_t bars = 0;
    for (size_t i = 0; i <= (uint8_t)rssi && (x + 2 * i) < (x + width); i++) {
      if ((i + 1) % 4) {
        bars++;
        display.fillRect(x + 2 * i, (y + 4) - bars, 2, bars, SH110X_WHITE);
      }
    }
  }

  if (analyzerState.last_rssi != 0.0f) {
    float last = analyzerState.last_rssi;
    if (last > ANALYZER_RSSI_HIGH) last = ANALYZER_RSSI_HIGH;
    int lastX = (int)((last - ANALYZER_RSSI_LOW) / ANALYZER_RSSI_MUL) * 2;
    if (lastX < width) {
      int lastH = (int)((last - ANALYZER_RSSI_LOW) / (ANALYZER_RSSI_MUL * ANALYZER_RSSI_GRAPH_SCALE)) + 1;
      lastH -= (lastH / 4) + 3;
      display.drawLine(x + lastX + 1, y - lastH, x + lastX + 1, y + 3, SH110X_WHITE);
    }
  }

  float trigPos = (analyzerTrigLevel - ANALYZER_RSSI_LOW) / ANALYZER_RSSI_MUL;
  uint8_t trigX = (uint8_t)((float)x + (2 * trigPos));
  if (trigX < x + width) {
    display.drawPixel(trigX, y + 4, SH110X_WHITE);
    display.drawLine(trigX - 1, y + 5, trigX + 1, y + 5, SH110X_WHITE);
  }

  display.drawLine(x, y + 3, x + width, y + 3, SH110X_WHITE);
}

void OLED_printJammer() {
  display.clearDisplay();
  display.drawBitmap(0, 3, image_Dolphin_Send_bits, 97, 61, SH110X_WHITE);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(72, 13);
  display.print(String(frequency, 2) + "MHz");
  display.setCursor(65, 42);
  display.print(isJamming ? "Jamming..." : "Press OK");
  if (!isJamming) {
    display.setCursor(65, 51);
    display.print("to start");
  }
  display.display();
}

void startJamming() {
  Serial.println(F("Starting jammer"));
  if (!ensureCC1101Initialized()) return;
  ELECHOUSE_cc1101.setModulation(0);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_DEVIATN, 0);
  ELECHOUSE_cc1101.setRxBW(270.0);
  ELECHOUSE_cc1101.SetTx();
  ELECHOUSE_cc1101.SpiWriteReg(0x3E, 0xFF);
  ELECHOUSE_cc1101.SpiWriteReg(0x35, 0x60);
}

void stopJamming() {
  Serial.println(F("Stopping jammer"));
  ELECHOUSE_cc1101.SpiWriteReg(0x35, 0x00);
  ELECHOUSE_cc1101.SetRx();
  restoreReceiveMode();
}

String getTypeName(emKeys tp) {
  switch (tp) {
    case kUnknown: return F("Unknown");
    case kP12bt: return F("Pre 12bit");
    case k12bt: return F("12bit");
    case k24bt: return F("24bit");
    case k64bt: return F("64bit");
    case kKeeLoq: return F("KeeLoq");
    case kANmotors64: return F("ANmotors");
    case kPrinceton: return F("Princeton");
    case kRcSwitch: return F("RcSwitch");
    case kStarLine: return F("StarLine");
    case kCAME: return F("CAME");
    case kNICE: return F("NICE");
    case kHOLTEK: return F("HOLTEK");
    case kLINEAR: return F("RAW");
    default: return "";
  }
}
