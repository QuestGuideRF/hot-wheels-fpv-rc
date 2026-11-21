#include "appGlobals.h"
#if INCLUDE_I2C
#define SENSOR_TIMEOUT 100
#include <Wire.h>
int I2Csda = -1;
int I2Cscl = -1;
static byte I2CDATA[10];
static int I2Cdevices = -1;
static bool isShared = false;
static bool deviceStatus[128] = {false};
static const char* clientName[128] = {
  "", "", "", "", "", "", "", "", "", "", "", "", "AK8963", "", "", "",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "LCD1602", "", "", "", "", "", "", "", "",
  "OV2640", "", "", "", "", "", "", "", "", "", "", "", "OV5640/SSD1306", "SSD1306", "", "",
  "", "", "", "", "", "", "", "", "PCF8591", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
  "OV2640", "OV2640", "", "", "", "", "", "", "MPUxx50/DS3231", "MPUxx50", "", "", "", "", "", "",
  "", "", "", "", "", "", "BMx280", "BMx280", "OV5640", "OV5640", "", "", "", "", "", ""};
static bool prepI2Cdevices();
static bool sendTransmission(int clientAddr, bool scanning) {
  byte result = Wire.endTransmission(true);
  if (!scanning && result > 0) LOG_WRN("Клиент %s по адресу 0x%02X с ошибкой подключения: %d", clientName[clientAddr], clientAddr, result);
  return (result == 0) ? true : false;
}
static void scanI2C() {
  LOG_INF("Сканирование устройств I2C");
  for (byte address = 0; address < 127; address++) {
    Wire.beginTransmission(address);
    if (sendTransmission(address, true)) {
      LOG_INF("Устройство I2C %s присутствует по адресу: 0x%02X", clientName[address], address);
      I2Cdevices++;
      deviceStatus[address] = true;
    }
  }
  LOG_INF("Обнаружены устройства I2C: %d", I2Cdevices);
}
static bool getI2Cdata (uint8_t clientAddr, uint8_t controlByte, uint8_t numBytes) {
  Wire.beginTransmission(clientAddr);
  Wire.write(controlByte);
  if (sendTransmission(clientAddr, false)) {
    Wire.requestFrom (clientAddr, numBytes);
    for (int i=0; i<numBytes; i++) I2CDATA[i] = Wire.read();
    return sendTransmission(clientAddr, false);
  }
  return false;
}
static bool sendI2Cdata(int clientAddr, uint8_t controlByte, uint8_t numBytes) {
  Wire.beginTransmission(clientAddr);
  if (controlByte) Wire.write(controlByte);
  for (int i=numBytes-1; i>=0; i--) Wire.write(I2CDATA[i]);
  return sendTransmission(clientAddr, false);
}
bool shareI2C(int sdaShare, int sclShare) {
  if (I2Csda < 0) {
    I2Csda = sdaShare;
    I2Cscl = sclShare;
    isShared = true;
    Wire.begin(I2Csda, I2Cscl);
    LOG_INF("Шина I2C используется совместно с камерой");
  }
  return isShared;
}
bool prepI2C() {
  if (I2Csda == I2Cscl) {
    LOG_ALT("Контакты I2C не определены: %d", I2Csda);
    return false;
  }
  if (!isShared) {
    Wire.begin(I2Csda, I2Cscl);
  }
  LOG_INF("%sI2C инициализируется на частоте %dкГц с использованием контактов SDA: %d, SCL: %d", isShared ? "Shared " : "", Wire.getClock() / 1000, I2Csda, I2Cscl);
  I2Cdevices = 0;
  scanI2C();
  return prepI2Cdevices();
}
#define SSD1306_BIaddr 0x3d
#define SSD1306_Extaddr 0x3c
#if USE_SSD1306
#include "SSD1306Wire.h"
SSD1306Wire oledBI(SSD1306_BIaddr);
SSD1306Wire oledExt(SSD1306_Extaddr);
SSD1306Wire* thisOled;
static bool oledOK = false;
bool flipOled = false;
void oledLine(const char* msg, int hpos, int vpos, int msgwidth, int fontsize) {
  if (oledOK) {
    thisOled->setTextAlignment(TEXT_ALIGN_LEFT);
    thisOled->setColor(BLACK);
    thisOled->fillRect(hpos, vpos, msgwidth, fontsize*5/4);
    thisOled->setFont(ArialMT_Plain_10);
    if (fontsize == 16) thisOled->setFont(ArialMT_Plain_16);
    if (fontsize == 24) thisOled->setFont(ArialMT_Plain_24);
    thisOled->setColor(WHITE);
    thisOled->drawString(hpos, vpos, msg);
  }
}
static void tellTale() {
  static bool ledState = false;
  ledState = !ledState;
  static const char* tellTaleStr[] = {"*", ""};
  oledLine(tellTaleStr[ledState],124,60,4,10);
}
void oledDisplay() {
  if (oledOK) {
    tellTale();
    thisOled->display();
  }
}
static bool setupOled() {
  if (!oledOK) {
    oledOK = true;
    if (deviceStatus[SSD1306_BIaddr]) thisOled = &oledBI;
    else if (deviceStatus[SSD1306_Extaddr]) thisOled = &oledExt;
    else oledOK = false;
    if (oledOK) {
      thisOled->end();
      if (thisOled->init()) { if (flipOled) thisOled->flipScreenVertically(); }
      else oledOK = false;
    }
    if (!oledOK) LOG_WRN("SSD1306 oled недоступен");
  }
  return oledOK;
}
void finalMsg(const char* finalTxt) {
  if (oledOK) {
    thisOled->resetDisplay();
    oledLine(finalTxt,0,0,128,16);
    thisOled->display();
    delay(2000);
  }
}
#endif
#define PCF8591addr 0x48
byte* getPCF8591() {
  static byte PCF8591[4] = {0};
  if (deviceStatus[PCF8591addr]) {
    if (getI2Cdata(PCF8591addr, 0x44, 5)) {
      for (int i = 0; i < 4; i++) PCF8591[i] = smoothAnalog(I2CDATA[i + 1]);
    }
  } else LOG_WRN("PCF8591 АЦП недоступен.");
  return PCF8591;
}
#define BMx280_Def 0x76
#define BMx280_Alt 0x77
#if USE_BMx280
#define STD_PRESSURE 1013.25
#define DEGREE_SYMBOL "\xC2\xB0"
#include <BMx280I2C.h>
BMx280I2C bmxDef(BMx280_Def);
BMx280I2C bmxAlt(BMx280_Alt);
BMx280I2C* thisBmx;
static bool BMx280ok = false;
static bool isBME = false;
static bool setupBMx() {
  if (!BMx280ok) {
    BMx280ok = true;
    if (deviceStatus[BMx280_Def]) thisBmx = &bmxDef;
    else if (deviceStatus[BMx280_Alt]) thisBmx = &bmxAlt;
    else BMx280ok = false;
    if (BMx280ok) {
      BMx280ok = thisBmx->begin();
      if (BMx280ok) {
        isBME = thisBmx->isBME280();
        thisBmx->resetToDefaults();
        thisBmx->writeOversamplingPressure(BMx280MI::OSRS_P_x16);
        thisBmx->writeOversamplingTemperature(BMx280MI::OSRS_T_x16);
        if (isBME) thisBmx->writeOversamplingHumidity(BMx280MI::OSRS_H_x16);
        thisBmx->measure();
      }
    }
    if (!BMx280ok) LOG_WRN("BMx280 недоступен");
  }
  return BMx280ok;
}
float* getBMx280() {
  static float BMx280[4] = {0};
  if (BMx280ok) {
    thisBmx->measure();
    uint32_t bmxWait = millis();
    while(!thisBmx->hasValue() && millis() - bmxWait < SENSOR_TIMEOUT) delay(10);
    if (thisBmx->hasValue()) {
      BMx280[0] = thisBmx->getTemperature();
      BMx280[1] = thisBmx->getPressure() * 0.01;
      BMx280[2] = 44330.0 * (1.0 - pow(BMx280[1] / STD_PRESSURE, 1.0 / 5.255));
      if (isBME) BMx280[3] = thisBmx->getHumidity();
    }
  }
  return BMx280;
}
bool identifyBMx() {
  return isBME;
}
#endif
#define MPUxx50_HIGH 0x69
#define MPUxx50_LOW 0x68
#if USE_MPU6050
#define SENS_2G (32768.0/2.0)
#define ACCEL_BYTES 6
#define CONFIG 0x1A
#define ACCEL_CONFIG 0x1C
#define ACCEL_XOUT_H 0x3B
#define PWR_MGMT_1 0x6B
static uint8_t MPU6050addr;
static bool MPU6050ok = false;
bool sleepMPU6050(bool doSleep) {
  I2CDATA[0] = doSleep ? 0x40 : 0x01;
  return sendI2Cdata(MPU6050addr, PWR_MGMT_1, 1);
}
static bool setupMPU6050() {
  if (!MPU6050ok) {
    MPU6050ok = true;
    if (deviceStatus[MPUxx50_HIGH]) MPU6050addr = MPUxx50_HIGH;
    else if (deviceStatus[MPUxx50_LOW]) MPU6050addr = MPUxx50_LOW;
    else MPU6050ok = false;
    if (MPU6050ok) {
      I2CDATA[0] = 0x00;
      MPU6050ok = sendI2Cdata(MPU6050addr, CONFIG, 1);
      if (MPU6050ok) sleepMPU6050(false);
    }
    if (!MPU6050ok) LOG_WRN("MPU6050 6-осевой недоступен");
  }
  return MPU6050ok;
}
float* getMPU6050() {
  static float Gforce[4] = {0};
  if (MPU6050ok) {
    if (getI2Cdata(MPU6050addr, ACCEL_XOUT_H, ACCEL_BYTES+2)) {
      int16_t raw[4];
      for (int i=0; i<4; i++) raw[i] = I2CDATA[i*2] << 8 | I2CDATA[(i*2)+1];
      for (int i=0; i<3; i++) Gforce[i] = raw[i] / SENS_2G;
      float gXYZ = sqrt(pow(Gforce[0],2)+pow(Gforce[1],2)+pow(Gforce[2],2));
      LOG_VRB("gXYZ должно быть близко к 1: %0.2f", gXYZ);
      float ratio = Gforce[0] / gXYZ;
      Gforce[0] = (float)((ratio < 0.5) ? 90-fabs(asin(ratio)*RAD_TO_DEG) : fabs(acos(ratio)*RAD_TO_DEG));
      ratio = Gforce[1] / gXYZ;
      Gforce[1] = (float)((ratio < 0.5) ? 90-fabs(asin(ratio)*RAD_TO_DEG) : fabs(acos(ratio)*RAD_TO_DEG));
      ratio = Gforce[2] / gXYZ;
      Gforce[2] = (float)((ratio < 0.5) ? 90-fabs(asin(ratio)*RAD_TO_DEG) : fabs(acos(ratio)*RAD_TO_DEG));
      Gforce[3] = ((float)raw[3] / 340.0) + 36.53;
    }
  }
  return Gforce;
}
#endif
#if USE_MPU9250
#include "MPU9250.h"
#define LOCAL_MAG_DECLINATION (4 + 56/60)
static MPU9250 mpu9250;
static uint8_t MPU9250addr;
static bool MPU9250ok = false;
static bool setupMPU9250() {
  if (!MPU9250ok) {
    MPU9250ok = true;
    if (deviceStatus[MPUxx50_HIGH]) MPU9250addr = MPUxx50_HIGH;
    else if (deviceStatus[MPUxx50_LOW]) MPU9250addr = MPUxx50_LOW;
    else MPU9250ok = false;
    if (MPU9250ok) {
      if (mpu9250.setup(MPU9250addr)) {
        mpu9250.setMagneticDeclination(LOCAL_MAG_DECLINATION);
        mpu9250.selectFilter(QuatFilterSel::MADGWICK);
        mpu9250.setFilterIterations(15);
        LOG_INF("MPU9250 калибруется, оставьте неподвижно");
        mpu9250.calibrateAccelGyro();
      } else MPU9250ok = false;
    }
    if (!MPU9250ok) LOG_WRN("MPU9250 9 осей недоступен");
  }
  return MPU9250ok;
}
float* getMPU9250() {
  static float Gforce[4] = {0};
  if (MPU9250ok) {
    uint32_t mpuWait = millis();
    while (!mpu9250.update() && millis() - mpuWait < SENSOR_TIMEOUT) delay(10);
    if (mpu9250.update()) {
      Gforce[0] = mpu9250.getYaw();
      Gforce[1] = mpu9250.getPitch();
      Gforce[2] = mpu9250.getRoll();
    }
  }
  return Gforce;
}
#endif
#define DS3231_RTC 0x68
#if USE_DS3231
#include "driver/rtc_io.h"
#include <RtcDS3231.h>
RtcDS3231<TwoWire> Rtc(Wire);
static bool DS3231ok = false;
static volatile bool RTCalarmFlag = false;
static void IRAM_ATTR RTCalarmISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  RTCalarmFlag = true;
  if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}
static bool setupRTC() {
  static bool SQWpin = -1;
  if (!DS3231ok) {
    if (deviceStatus[DS3231_RTC]) {
      pinMode(SQWpin, INPUT_PULLUP);
      Rtc.Begin();
      RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
      if (!Rtc.IsDateTimeValid()) {
        LOG_WRN("RTC потерял доверие к DateTime");
        Rtc.SetDateTime(compiled);
      }
      if (!Rtc.GetIsRunning()) {
        LOG_WRN("RTC не работал активно, начиная с настоящего момента");
        Rtc.SetIsRunning(true);
      }
      RtcDateTime now = Rtc.GetDateTime();
      if (now < compiled) {
        LOG_WRN("RTC старше времени компиляции, обновляется DateTime");
        Rtc.SetDateTime(compiled);
      }
      Rtc.Enable32kHzPin(false);
      Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeAlarmBoth);
      Rtc.LatchAlarmsTriggeredFlags();
      attachInterrupt(digitalPinToInterrupt(SQWpin), RTCalarmISR, FALLING);
      DS3231ok = true;
    } else DS3231ok = false;
  }
  if (!DS3231ok) LOG_WRN("DS3231 Часы реального времени недоступны.");
  return DS3231ok;
}
int cycleRange(int currVal, int minVal, int maxVal) {
  if (currVal < minVal) return maxVal;
  if (currVal > maxVal) return minVal;
  return currVal;
}
void setRTCintervalAlarm(int alarmHour, int alarmMin) {
  if (DS3231ok) {
    int nextHour = cycleRange(Rtc.GetDateTime().Hour()+alarmHour, 0, 23);
    int nextMin = cycleRange(Rtc.GetDateTime().Minute()+alarmMin, 0, 59);
    DS3231AlarmOne alarm1(0, nextHour, nextMin, 30, DS3231AlarmOneControl_HoursMinutesSecondsMatch);
    Rtc.SetAlarmOne(alarm1);
  }
}
void setRTCspecificAlarm(int alarmHour, int alarmMin) {
  if (DS3231ok) {
    DS3231AlarmOne alarm1(0, alarmHour, alarmMin, 30, DS3231AlarmOneControl_HoursMinutesSecondsMatch);
    Rtc.SetAlarmOne(alarm1);
  }
}
void setRTCrolloverAlarm(int alarmHour, int alarmMin) {
  if (DS3231ok) {
    DS3231AlarmTwo alarm2(0, alarmHour, alarmMin, DS3231AlarmTwoControl_HoursMinutesMatch);
    Rtc.SetAlarmTwo(alarm2);
  }
}
uint32_t getRTCtime() {
  if (DS3231ok) {
    if (!Rtc.IsDateTimeValid()) LOG_WRN("RTC потерял доверие к DateTime!");
    return Rtc.GetDateTime().Unix32Time();
  }
  return 0;
}
int RTCalarmed() {
  int wasAlarmed = 0;
  if (DS3231ok) {
    if (RTCalarmFlag) {
      RTCalarmFlag = false;
      DS3231AlarmFlag flag = Rtc.LatchAlarmsTriggeredFlags();
      if (flag & DS3231AlarmFlag_Alarm1) wasAlarmed = 1;
      if (flag & DS3231AlarmFlag_Alarm2) wasAlarmed = 2;
    }
  }
  return wasAlarmed;
}
float RTCtemperature() {
  if (DS3231ok) {
    RtcTemperature temp = Rtc.GetTemperature();
    return temp.AsFloatDegC();
  }
  return 0;
}
void RTCdatetime(char* datestring, int datestringLen) {
  if (DS3231ok) {
    if (!Rtc.IsDateTimeValid()) LOG_WRN("RTC потерял доверие к DateTime!");
    RtcDateTime dt = Rtc.GetDateTime();
    snprintf(datestring, datestringLen, "%02u/%02u/%04u %02u:%02u:%02u",
      dt.Day(), dt.Month(), dt.Year(), dt.Hour(), dt.Minute(), dt.Second());
  }
}
#endif
#define LCD1602 0x27
#if USE_LCD1602
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00
#define LCD_BACKLIGHT 0x08
#define LCD_NOBACKLIGHT 0x00
#define En 0b00000100
#define Rw 0b00000010
#define Rs 0b00000001
#define NUM_ROWS 2
#define NUM_COLS 16
static bool LCD1602ok = false;
static uint8_t displaycontrol;
static uint8_t displaymode;
static uint8_t backlightval;
static void lcdWrite(uint8_t data) {
  if (LCD1602ok) {
    I2CDATA[0] = data | backlightval;
    sendI2Cdata(LCD1602, 0, 1);
  }
}
static void writeNibble(uint8_t value) {
	lcdWrite(value);
	lcdWrite(value | En);
	delayMicroseconds(1);
	lcdWrite(value & ~En);
	delayMicroseconds(50);
}
static void lcdSend(uint8_t value, uint8_t mode = 0) {
  if (LCD1602ok) {
    writeNibble((value & 0xf0) | mode);
    writeNibble(((value << 4 ) & 0xf0) | mode);
  }
}
void lcdBacklight(bool lightOn) {
  backlightval = (lightOn) ? LCD_BACKLIGHT : LCD_NOBACKLIGHT;
  lcdWrite(backlightval);
}
void lcdClear() {
  lcdSend(LCD_CLEARDISPLAY);
  delayMicroseconds(2000);
}
void lcdHome() {
  lcdSend(LCD_RETURNHOME);
  delayMicroseconds(2000);
}
void lcdDisplay(bool setDisplay) {
  if (setDisplay) displaycontrol |= LCD_DISPLAYON;
  else displaycontrol &= ~LCD_DISPLAYON;
  lcdSend(LCD_DISPLAYCONTROL | displaycontrol);
}
static bool setupLCD1602() {
  if (!LCD1602ok) {
    if (deviceStatus[LCD1602]) {
      LCD1602ok = true;
      delay(50);
      lcdBacklight(false);
      delay(1000);
      writeNibble(0x03 << 4);
      delayMicroseconds(4500);
      writeNibble(0x03 << 4);
      delayMicroseconds(4500);
      writeNibble(0x03 << 4);
      delayMicroseconds(150);
      writeNibble(0x02 << 4);
      lcdSend(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS);
      displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
      lcdDisplay(true);
      lcdClear();
      displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
      lcdSend(LCD_ENTRYMODESET | displaymode);
      lcdHome();
      lcdBacklight(true);
    } else LCD1602ok = false;
    if (!LCD1602ok) LOG_WRN("Дисплей LCD1602 недоступен.");
  }
  return LCD1602ok;
}
void lcdPrint(const char* str) {
	for (int i=0; i<strlen(str); i++) lcdSend((uint8_t)str[i], Rs);
}
void lcdSetCursorPos(uint8_t row, uint8_t col) {
	int row_offsets[] = {0x00, 0x40, 0x14, 0x54};
  if (row > NUM_ROWS) row = NUM_ROWS - 1;
  if (col > NUM_COLS) col = NUM_COLS - 1;
	lcdSend(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}
void lcdLineCursor(bool showLine) {
  if (showLine) displaycontrol |= LCD_CURSORON;
  else displaycontrol &= ~LCD_CURSORON;
	lcdSend(LCD_DISPLAYCONTROL | displaycontrol);
}
void lcdBlinkCursor(bool showBlink) {
  if (showBlink) displaycontrol |= LCD_BLINKON;
  else displaycontrol &= ~LCD_BLINKON;
	lcdSend(LCD_DISPLAYCONTROL | displaycontrol);
}
void lcdScrollText(bool scrollLeft) {
  uint8_t moveDir = (scrollLeft) ? LCD_MOVELEFT : LCD_MOVERIGHT;
  lcdSend(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | moveDir);
}
void lcdTextDirection(bool scrollLeft) {
  if (scrollLeft) displaymode &= ~LCD_ENTRYLEFT;
  else displaymode |= LCD_ENTRYLEFT;
	lcdSend(LCD_ENTRYMODESET | displaymode);
}
void lcdAutoScroll(bool autoScroll) {
	if (autoScroll) displaymode |= LCD_ENTRYSHIFTINCREMENT;
  else displaymode &= ~LCD_ENTRYSHIFTINCREMENT;
	lcdSend(LCD_ENTRYMODESET | displaymode);
}
void lcdLoadCustom(uint8_t charLoc, uint8_t charmap[]) {
  if (charLoc > 7) LOG_WRN("номер специального символа %u вне диапазона", charLoc);
  else {
  	charLoc &= 0x7;
  	lcdSend(LCD_SETCGRAMADDR | (charLoc << 3));
  	for (int i=0; i<8; i++)	lcdSend(charmap[i], Rs);
  }
}
void lcdWriteCustom(uint8_t charLoc) {
  if (charLoc > 7) LOG_WRN("номер специального символа %u вне диапазона", charLoc);
  else lcdSend(charLoc, Rs);
}
#endif
bool checkI2Cdevice(const char* devName) {
  if (!strcmp(devName, "SSD1306")) return deviceStatus[SSD1306_BIaddr] || deviceStatus[SSD1306_Extaddr] ? true : false;
  if (!strcmp(devName, "PCF8591")) return deviceStatus[PCF8591addr];
  if (!strcmp(devName, "BMx280")) return deviceStatus[BMx280_Def] || deviceStatus[BMx280_Alt] ? true : false;
  if (!strcmp(devName, "MPU6050")) return deviceStatus[MPUxx50_HIGH] || deviceStatus[MPUxx50_LOW]  ? true : false;
  if (!strcmp(devName, "MPU9250")) return deviceStatus[MPUxx50_HIGH] || deviceStatus[MPUxx50_LOW]  ? true : false;
  if (!strcmp(devName, "DS3231")) return deviceStatus[DS3231_RTC];
  if (!strcmp(devName, "LCD1602")) return deviceStatus[LCD1602];
  LOG_WRN("Имя устройства %s не распознано", devName);
  return false;
}
static bool prepI2Cdevices() {
  if (I2Cdevices < 0) LOG_ERR("prepI2C() не вызывается");
  else if (I2Cdevices == 0) LOG_WRN("Устройства I2C не подключены");
  else {
#if USE_SSD1306
    setupOled();
#endif
#if USE_BMx280
    setupBMx();
#endif
#if USE_MPU6050
    setupMPU6050();
#endif
#if USE_MPU9250
    setupMPU9250();
#endif
#if USE_DS3231
    setupRTC();
#endif
#if USE_LCD1602
    setupLCD1602();
#endif
    return true;
  }
  return false;
}
#endif