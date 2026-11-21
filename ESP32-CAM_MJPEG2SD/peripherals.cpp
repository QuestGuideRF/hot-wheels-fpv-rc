#include "appGlobals.h"
#if INCLUDE_PERIPH
#include "driver/ledc.h"
bool pirUse;
bool ledBarUse;
uint8_t lampLevel;
bool lampAuto = false;
bool lampNight;
int lampType;
bool voltUse;
bool stickUse;
bool buzzerUse;
bool stepperUse;
bool SVactive;
TaskHandle_t heartBeatHandle = NULL;
bool RCactive = false;
int pirPin;
int lampPin;
int buzzerPin;
int servoPanPin;
int servoTiltPin;
int ds18b20Pin;
int voltPin;
int servoMinAngle;
int servoMaxAngle;
int servoMinPulseWidth;
int servoMaxPulseWidth;
int servoDelay;
int servoCenter = 90;
int voltDivider;
float voltLow;
int voltInterval;
int buzzerDuration;
int servoSteerPin;
int lightsRCpin;
int heartbeatRC;
int maxSteerAngle;
int maxDutyCycle;
int minDutyCycle;
int maxTurnSpeed;
bool allowReverse;
bool autoControl;
int waitTime;
int stickzPushPin;
int stickXpin;
int stickYpin;
int relayPin;
bool relayMode;
int ledBarClock;
int ledBarData;
#define stepperPins 4
uint8_t stepINpins[stepperPins];
static void doStep();
void setStickTimer(bool restartTimer, uint32_t interval);
void setLamp(uint8_t lampVal);
bool getPIRval() {
  return digitalRead(pirPin);
}
void buzzerAlert(bool buzzerOn) {
  if (buzzerUse) {
    if (buzzerOn) {
      pinMode(buzzerPin, OUTPUT);
      digitalWrite(buzzerPin, HIGH);
    } else digitalWrite(buzzerPin, LOW);
  }
}
#define PWM_FREQ 50
#define DUTY_BIT_DEPTH 12
TaskHandle_t servoHandle = NULL;
static int newTiltVal, newPanVal, newSteerVal;
static int oldPanVal, oldTiltVal, oldSteerVal;
static int dutyCycle (int angle) {
  angle = constrain(angle, servoMinAngle, servoMaxAngle);
  int pulseWidth = map(angle, servoMinAngle, servoMaxAngle, servoMinPulseWidth, servoMaxPulseWidth);
  return pow(2, DUTY_BIT_DEPTH) * pulseWidth * PWM_FREQ / USECS;
}
static int changeAngle(uint8_t servoPin, int newVal, int oldVal, bool useDelay = true) {
  int incr = newVal - oldVal > 0 ? 1 : -1;
  for (int angle = oldVal; angle != newVal + incr; angle += incr) {
    ledcWrite(servoPin, dutyCycle(angle));
    if (useDelay) delay(servoDelay);
  }
  return newVal;
}
static void servoTask(void* pvParameters) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (newSteerVal != oldSteerVal) oldSteerVal = changeAngle(servoSteerPin, newSteerVal, oldSteerVal, false);
    if (newPanVal != oldPanVal) oldPanVal = changeAngle(servoPanPin, newPanVal, oldPanVal);
    if (newTiltVal != oldTiltVal) oldTiltVal = changeAngle(servoTiltPin, newTiltVal, oldTiltVal);
  }
}
void setCamPan(int panVal) {
  newPanVal = panVal;
  if (servoPanPin && servoHandle != NULL) xTaskNotifyGive(servoHandle);
}
void setCamTilt(int tiltVal) {
  newTiltVal = tiltVal;
  if (servoTiltPin && servoHandle != NULL) xTaskNotifyGive(servoHandle);
}
void setSteering(int steerVal) {
  newSteerVal = steerVal;
  if (servoSteerPin && servoHandle != NULL) xTaskNotifyGive(servoHandle);
}
static void prepServos() {
  if (SVactive) {
    if (servoPanPin) ledcAttach(servoPanPin, PWM_FREQ, DUTY_BIT_DEPTH);
    else LOG_WRN("Не определен вывод панорамирования сервопривода");
    if (servoTiltPin) ledcAttach(servoTiltPin, PWM_FREQ, DUTY_BIT_DEPTH);
    else LOG_WRN("Штифт наклона сервопривода не определен");
  }
  if (RCactive && servoSteerPin) ledcAttach(servoSteerPin, PWM_FREQ, DUTY_BIT_DEPTH);
  oldPanVal = oldTiltVal = oldSteerVal = servoCenter + 1;
  if (SVactive || (RCactive && servoSteerPin)) {
    xTaskCreate(&servoTask, "servoTask", SERVO_STACK_SIZE, NULL, SERVO_PRI, &servoHandle);
    if (servoPanPin) setCamPan(servoCenter);
    if (servoTiltPin) setCamTilt(servoCenter);
    if (servoSteerPin) setSteering(servoCenter);
  }
}
#if INCLUDE_DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>
#endif
static float dsTemp = NULL_TEMP;
TaskHandle_t DS18B20handle = NULL;
static bool haveDS18B20 = false;
static void DS18B20task(void* pvParameters) {
#if INCLUDE_DS18B20
  OneWire oneWire(ds18b20Pin);
  DallasTemperature sensors(&oneWire);
  while (true) {
    dsTemp = NULL_TEMP;
    sensors.begin();
    uint8_t deviceAddress[8];
    sensors.getAddress(deviceAddress, 0);
    if (deviceAddress[0] == 0x28) {
      uint8_t tryCnt = 10;
      while (tryCnt) {
        sensors.requestTemperatures();
        dsTemp = sensors.getTempCByIndex(0);
        if (dsTemp > NULL_TEMP) tryCnt = 10;
        else tryCnt--;
        delay(1000);
      }
    }
    delay(10000);
  }
#endif
}
void prepTemperature() {
#if INCLUDE_DS18B20
  if (ds18b20Pin) {
    xTaskCreate(&DS18B20task, "DS18B20task", DS18B20_STACK_SIZE, NULL, DS18B20_PRI, &DS18B20handle);
    haveDS18B20 = true;
    LOG_INF("Использование датчика DS18B20");
  } else LOG_WRN("Вывод DS18B20 не определен, используется датчик чипа, если он имеется.");
#endif
}
float readTemperature(bool isCelsius, bool onlyDS18) {
  if (onlyDS18) return dsTemp;
  if (!haveDS18B20) dsTemp = readInternalTemp();
  return (dsTemp > NULL_TEMP) ? (isCelsius ? dsTemp : (dsTemp * 1.8) + 32.0) : dsTemp;
}
float getNTCcelsius (uint16_t resistance, float oldTemp) {
  double Temp = log(resistance);
  Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * Temp * Temp )) * Temp);
  Temp = (Temp == 0) ? oldTemp : Temp - 273.15;
  return (float) Temp;
}
static float currentVoltage = -1.0;
TaskHandle_t battHandle = NULL;
float readVoltage()  {
  return currentVoltage;
}
static void battTask(void* parameter) {
  if (voltInterval < 1) voltInterval = 1;
  while (true) {
    currentVoltage = (float)(smoothAnalog(voltPin)) * 3.3 * voltDivider / MAX_ADC;
    static bool sentExtAlert = false;
    if (currentVoltage < voltLow && !sentExtAlert) {
      sentExtAlert = true;
      char battMsg[20];
      sprintf(battMsg, "Напряжение %0.2fВ.", currentVoltage);
      externalAlert("Низкий заряд батареи", battMsg);
    }
    delay(voltInterval * 60 * 1000);
  }
  vTaskDelete(NULL);
}
static void setupBatt() {
  if (voltUse) {
  	if (voltPin) {
      xTaskCreate(&battTask, "battTask", BATT_STACK_SIZE, NULL, BATT_PRI, &battHandle);
      LOG_INF("Следить за напряжением аккумулятора");
      debugMemory("setupBatt");
    } else LOG_WRN("Контакт напряжения не определен");
  }
}
#define RGB_BITS 24
static bool lampInit = false;
#if defined(USE_WS2812)
static rmt_data_t ledData[RGB_BITS];
#endif
static void setupLamp() {
  lampInit = false;
#if defined(LED_GPIO_NUM)
  if (lampPin <= 0) {
    lampPin = LED_GPIO_NUM;
    char lampPinStr[3];
    sprintf(lampPinStr, "%d", lampPin);
    updateStatus("lampPin", lampPinStr);
  }
#endif
  if (lampPin) {
    lampInit = true;
#if defined(USE_WS2812)
    if (rmtInit(lampPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000))
      LOG_INF("Настройка WS2812 Светодиод лампы на контакте %d", lampPin);
    else {
      LOG_WRN("Не удалось настроить WS2812 на контакте %u.", lampPin);
      lampInit = false;
    }
#else
    ledcAttach(lampPin, 5000, DUTY_BIT_DEPTH);
    setLamp(0);
    LOG_INF("Настройка индикатора ШИМ на контакте %d", lampPin);
#endif
  }
  if (lightsRCpin > 1) pinMode(lightsRCpin, OUTPUT);
}
void setLamp(uint8_t lampVal) {
  if (lampPin) {
    if (!lampInit) setupLamp();
    if (lampInit) {
#if defined(USE_WS2812)
      uint8_t RGB[3];
      lampVal = lampVal == 15 ? 255 : lampVal * 16;
      for (uint8_t i = 0; i < 3; i++) {
        RGB[i] = lampVal;
        for (uint8_t j = 0; j < 8; j++) {
          int bit = (i * 8) + j;
          if ((RGB[i] << j) & 0x80) {
            ledData[bit].level0 = 1;
            ledData[bit].duration0 = 8;
            ledData[bit].level1 = 0;
            ledData[bit].duration1 = 4;
          } else {
            ledData[bit].level0 = 1;
            ledData[bit].duration0 = 4;
            ledData[bit].level1 = 0;
            ledData[bit].duration1 = 8;
          }
        }
      }
      rmtWrite(lampPin, ledData, RGB_BITS, RMT_WAIT_FOR_EVER);
#else
      uint8_t valueMax = 15;
      uint32_t duty = (pow(2, DUTY_BIT_DEPTH) / valueMax) * min(lampVal, valueMax);
      ledcWrite(lampPin, duty);
#endif
    }
  }
}
void twinkleLed(uint8_t ledPin, uint16_t interval, uint8_t blinks) {
  bool ledState = true;
  for (int i=0; i<blinks*2; i++) {
    digitalWrite(ledPin, ledState);
    delay(interval);
    ledState = !ledState;
  }
}
void setLightsRC(bool lightsOn) {
  if (lightsRCpin > 0) digitalWrite(lightsRCpin, lightsOn);
}
static void prepPIR() {
  if (pirUse) {
    if (pirPin) pinMode(pirPin, INPUT_PULLDOWN);
    else {
      pirUse = false;
      LOG_WRN("Контакт PIR не определен");
    }
  }
  if (relayPin) pinMode(relayPin, OUTPUT);
}
static const int sRate = 1;
static int xOffset = 0;
static int yOffset = 0;
static bool lightsChanged = false;
TaskHandle_t stickHandle = NULL;
static void IRAM_ATTR buttonISR() {
  lightsChanged = !lightsChanged;
}
static void IRAM_ATTR stickISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (stickHandle) {
    vTaskNotifyGiveFromISR(stickHandle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
  }
}
void setStickTimer(bool restartTimer, uint32_t interval) {
  static hw_timer_t* stickTimer = NULL;
  if (stickTimer) {
    timerDetachInterrupt(stickTimer);
    timerEnd(stickTimer);
    stickTimer = NULL;
  }
  if (restartTimer) {
    stickTimer = timerBegin(OneMHz);
    timerAttachInterrupt(stickTimer, &stickISR);
    timerAlarm(stickTimer, interval, true, 0);
  }
}
static void stickTask (void *pvParameter) {
  static bool lightsStatus = false;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (stickUse) {
      int xPos = smoothAnalog(stickXpin, sRate);
      int steerAngle = (xPos > CENTER_ADC + xOffset) ? map(xPos, CENTER_ADC + xOffset, MAX_ADC, servoCenter, servoCenter + maxSteerAngle)
        : map(xPos, 0, CENTER_ADC + xOffset, servoCenter - maxSteerAngle, servoCenter);
      setSteering(steerAngle);
      int yPos = smoothAnalog(stickYpin, sRate);
      int motorCycle = (yPos > CENTER_ADC + yOffset) ? map(yPos, CENTER_ADC + yOffset, MAX_ADC, 0, 0 - maxDutyCycle)
        : map(yPos, 0, CENTER_ADC + yOffset, maxDutyCycle, 0);
      if (abs(motorCycle) < minDutyCycle) motorCycle = 0;
#if INCLUDE_MCPWM
      motorSpeed(motorCycle);
#endif
      if (lightsChanged != lightsStatus) setLightsRC(lightsChanged);
      lightsStatus = lightsChanged;
      LOG_VRB("Xpos %d, Ypos %d, кнопка %d", xPos, yPos, lightsStatus);
    }
    if (stepperUse) doStep();
  }
}
static void prepJoystick() {
  if (stickUse) {
    if (stickXpin > 0 && stickYpin > 0) {
      xOffset = smoothAnalog(stickXpin, 8) - CENTER_ADC;
      yOffset = smoothAnalog(stickYpin, 8) - CENTER_ADC;
      LOG_VRB("Смещение по оси X: %d, смещение по оси Y: %d", xOffset, yOffset);
      if (stickzPushPin > 0) {
        pinMode(stickzPushPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(stickzPushPin), buttonISR, FALLING);
      }
      if (stickHandle == NULL) xTaskCreate(&stickTask, "stickTask", STICK_STACK_SIZE , NULL, STICK_PRI, &stickHandle);
      setStickTimer(true, waitTime * 1000);
      LOG_INF("Джойстик доступен");
    } else {
      stickUse = false;
      LOG_WRN("Контакты джойстика не определены");
    }
  }
}
#define stepPhases 4
#define modelTypes 2
static bool clockwise = false;
static const uint16_t stepsPerRevolution[modelTypes] = {32 * 64, 20};
static uint32_t stepsToDo = 0;
static uint8_t modelIndex = 0;
static uint8_t stepPhase = 0;
static const uint8_t pinSequence[stepPhases * modelTypes][stepperPins] = {
  {1, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 1},
  {1, 0, 0, 1},
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1},
};
void setStepperPin(uint8_t pinNum, uint8_t pinPos) {
  stepINpins[pinPos] = pinNum;
}
static void prepStepper() {
  if (stepperUse) {
    if (stepINpins[0] > 0 && stepINpins[1] > 0) {
      stepPhase = 0;
      for (int i = 0; i < stepperPins; i++) {
        pinMode(stepINpins[i], OUTPUT);
        digitalWrite(stepINpins[i], LOW);
      }
      if (stickHandle == NULL) xTaskCreate(&stickTask, "stickTask", STICK_STACK_SIZE , NULL, STICK_PRI, &stickHandle);
      LOG_INF("Шаговый двигатель на контактах: %d, %d, %d, %d", stepINpins[0], stepINpins[1], stepINpins[2], stepINpins[3]);
    } else {
      stepperUse = false;
      LOG_WRN("Шаговые штифты не определены");
    }
  }
}
static void nextPhase(bool changeDir = false) {
  if (changeDir) clockwise = !clockwise;
  if (clockwise) {
    if (stepPhase-- <= 0) stepPhase = stepPhases - 1;
  } else {
    if (++stepPhase >= stepPhases) stepPhase = 0;
  }
}
void stepperRun(float RPM, float revFraction, bool _clockwise, stepperModel thisStepper) {
  stepsToDo = revFraction * stepsPerRevolution[thisStepper];
  modelIndex = stepPhases * thisStepper;
  if (clockwise != _clockwise) {
    nextPhase(true);
    nextPhase();
  }
  uint32_t stepDelay = 60 * USECS / RPM;
  stepDelay /= stepsPerRevolution[thisStepper];
  setStickTimer(false, 0);
  setStickTimer(true, stepDelay);
}
static void doStep() {
  if (stepsToDo--) {
    for (int i = 0; i < stepperPins; i++) digitalWrite(stepINpins[i], pinSequence[modelIndex + stepPhase][i]);
    nextPhase();
  } else {
    setStickTimer(false, 0);
    for (int i = 0; i < stepperPins; i++) digitalWrite(stepINpins[i], LOW);
#if (INCLUDE_PGRAM && INCLUDE_PERIPH)
    stepperDone();
#endif
  }
}
#define MY9221_COUNT 12
#define LEDBAR_COUNT 10
#define LED_OFF 0x00
#define LED_FULL 0xFF
static bool reverse = true;
static uint8_t ledLevel[LEDBAR_COUNT];
static void ledBarLatch() {
  digitalWrite(ledBarClock, LOW);
  delayMicroseconds(250);
  bool dataVal = false;
  for (uint8_t i = 0; i < 8; i++, dataVal = !dataVal) {
    digitalWrite(ledBarData, dataVal ? HIGH : LOW);
    delayMicroseconds(1);
  }
}
static void ledBarSend(uint16_t bits) {
  bool clockVal = false;
  for (int i = 15; i >= 0; i--, clockVal = !clockVal) {
    digitalWrite(ledBarData, (bits >> i) & 1 ? HIGH : LOW);
    digitalWrite(ledBarClock, clockVal ? HIGH : LOW);
  }
}
void ledBarClear() {
  for (uint8_t i = 0; i < LEDBAR_COUNT; i++) ledLevel[i] = LED_OFF;
}
void ledBrightness(uint8_t whichLed, float brightness) {
  ledLevel[whichLed] |= (1 << (uint8_t)(8 * brightness)) - 1;
}
void ledBarUpdate() {
  if (ledBarUse) {
    ledBarSend(0);
    for (uint8_t i = 0; i < LEDBAR_COUNT; i++)
      ledBarSend(reverse ? ledLevel[LEDBAR_COUNT - 1 - i] : ledLevel[i]);
    for (uint8_t i = 0; i < MY9221_COUNT - LEDBAR_COUNT; i++) ledBarSend(LED_OFF);
    ledBarLatch();
  }
}
void ledBarGauge(float level) {
  level = abs(level);
  if (ledBarUse) {
    ledBarClear();
    uint8_t fullLedCnt = (uint8_t)(level * LEDBAR_COUNT);
    for (uint8_t i = 0; i < fullLedCnt; i++) ledLevel[i] = LED_FULL;
    ledBrightness(fullLedCnt, (LEDBAR_COUNT * level) - fullLedCnt);
    ledBarUpdate();
  }
}
static void prepLedBar() {
  if (ledBarUse && ledBarClock && ledBarData) {
    pinMode(ledBarClock, OUTPUT);
    pinMode(ledBarData, OUTPUT);
    ledBarClear();
    ledBarUpdate();
    LOG_INF("Настройка светодиодной панели %d с контактами %d, %d", LEDBAR_COUNT, ledBarClock, ledBarData);
  } else ledBarUse = false;
}
void prepPeripherals() {
  setupADC();
  setupBatt();
  setupLamp();
  prepPIR();
  prepTemperature();
  prepServos();
  prepJoystick();
  prepStepper();
  prepLedBar();
  debugMemory("prepPeripherals");
}
#endif