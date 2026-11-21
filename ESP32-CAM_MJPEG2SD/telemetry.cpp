#include "appGlobals.h"
#if INCLUDE_TELEM
#if !INCLUDE_I2C
#error "Требуется INCLUDE_I2C true"
#endif
#define NUM_BUFF 2
#define MAX_LINE_LEN 128
TaskHandle_t telemetryHandle = NULL;
bool teleUse = false;
static int teleInterval = 1;
static char* teleBuf[NUM_BUFF];
size_t highPoint[NUM_BUFF];
static bool capturing = false;
static char teleFileName[FILE_NAME_LEN];
char srtBuffer[MAX_LINE_LEN];
char csvHeader[MAX_LINE_LEN];
size_t srtBytes = 0;
#define BME_CSV ",Температура (C),Влажность (%),Давление (мб),Высота (м)"
#define BMP_CSV ",Температура (С),Давление (мб),Высота (м)"
#define MPU_CSV ",Направление,Наклон,Крен"
#define BME_SRT "  %0.1fC %0.1fRH %0.1fmb %0.1fm"
#define BMP_SRT "  %0.1fC %0.1fмб %0.1fм"
#define MPU_SRT "  %0.1f  %0.1f  %0.1f"
static bool isBME = false;
static bool haveBMX = false;
static bool haveMPU = false;
static bool setupSensors() {
  bool res = false;
#if USE_BMx280
  if (checkI2Cdevice("BMx280")) {
    bool isBME = identifyBMx();
    LOG_INF("%s доступен", isBME ? "BME280" : "BMP280");
    if (isBME) strncat(csvHeader, BME_CSV, MAX_LINE_LEN - strlen(csvHeader) - 1);
    else strncat(csvHeader, BMP_CSV, MAX_LINE_LEN - strlen(csvHeader) - 1);
    haveBMX = res = true;
  } else LOG_WRN("%s недоступен", isBME ? "BME280" : "BMP280");
#endif
#if USE_MPU9250
  if (checkI2Cdevice("MPU9250")) {
    LOG_INF("MPU9250 доступен");
    strncat(csvHeader, MPU_CSV, MAX_LINE_LEN - strlen(csvHeader) - 1);
    haveMPU = res = true;
  } else LOG_WRN("MPU9250 недоступен");
#endif
  return res;
}
static void getSensorData() {
#if USE_BMx280
  if (haveBMX) {
    float* bmxData = getBMx280();
    if (isBME) {
      highPoint[0] += sprintf(teleBuf[0] + highPoint[0], ",%0.1f,%0.1f,%0.1f,%0.1f", bmxData[0], bmxData[3], bmxData[1], bmxData[2]);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], BME_SRT, bmxData[0], bmxData[3], bmxData[1], bmxData[2]);
    } else {
      highPoint[0] += sprintf(teleBuf[0] + highPoint[0], ",%0.1f,%0.1f,%0.1f", bmxData[0], bmxData[1], bmxData[2]);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], BMP_SRT, bmxData[0], bmxData[1], bmxData[2]);
    }
  #if INCLUDE_MQTT
    if (mqtt_active) {
      sprintf(jsonBuff, "{\"Temp\":\"%0.1f\", \"TIME\":\"%s\"}", bmxData[0], esp_log_system_timestamp());
      mqttPublish(jsonBuff);
    }
#endif
  }
#endif
#if USE_MPU9250
  if (haveMPU) {
    float* mpuData = getMPU9250();
    highPoint[0] += sprintf(teleBuf[0] + highPoint[0], ",%0.1f,%0.1f,%0.1f", mpuData[0], mpuData[1], mpuData[2]);
    highPoint[1] += sprintf(teleBuf[1] + highPoint[1], MPU_SRT, mpuData[0], mpuData[1], mpuData[2]);
  }
#endif
}
void storeSensorData(bool fromStream) {
  if (fromStream) {
    if (capturing) return;
    else highPoint[0] = highPoint[1] = 0;
  }
  size_t startData = highPoint[1];
  getSensorData();
  if (!srtBytes) {
    srtBytes = min(highPoint[1] - startData, (size_t)MAX_LINE_LEN);
    memcpy(srtBuffer, teleBuf[1] + startData, srtBytes);
  }
}
static void telemetryTask(void* pvParameters) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    capturing = true;
    int srtSeqNo = 1;
    uint32_t srtTime = 0;
    char timeStr[10];
    uint32_t sampleInterval = 1000 * (teleInterval < 1 ? 1 : teleInterval);
    if (STORAGE.exists(TELETEMP)) STORAGE.remove(TELETEMP);
    if (STORAGE.exists(SRTTEMP)) STORAGE.remove(SRTTEMP);
    File teleFile = STORAGE.open(TELETEMP, FILE_WRITE);
    File srtFile = STORAGE.open(SRTTEMP, FILE_WRITE);
    highPoint[0] = sprintf(teleBuf[0], "Время%s\n", csvHeader);
    highPoint[1] = 0;
    while (capturing) {
      uint32_t startTime = millis();
      formatElapsedTime(timeStr, srtTime, true);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "%d\n%s,000 --> ", srtSeqNo++, timeStr);
      srtTime += sampleInterval;
      formatElapsedTime(timeStr, srtTime, true);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "%s,000\n", timeStr);
      time_t currEpoch = getEpoch();
      for (int i = 0; i < NUM_BUFF; i++) highPoint[i] += strftime(teleBuf[i] + highPoint[i], 10, "%H:%M:%S", localtime(&currEpoch));
      storeSensorData(false);
      highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "\n");
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "\n\n");
      for (int i = 0; i < NUM_BUFF; i++) {
        if (highPoint[i] >= RAMSIZE) {
          highPoint[i] -= RAMSIZE;
          if (i) srtFile.write((uint8_t*)teleBuf[i], RAMSIZE);
          else teleFile.write((uint8_t*)teleBuf[i], RAMSIZE);
          memcpy(teleBuf[i], teleBuf[i]+RAMSIZE, highPoint[i]);
        }
      }
      while (millis() - sampleInterval < startTime) delay(10);
    }
    if (highPoint[0]) teleFile.write((uint8_t*)teleBuf[0], highPoint[0]);
    if (highPoint[1]) srtFile.write((uint8_t*)teleBuf[1], highPoint[1]);
    teleFile.close();
    srtFile.close();
    changeExtension(teleFileName, CSV_EXT);
    STORAGE.rename(TELETEMP, teleFileName);
    changeExtension(teleFileName, SRT_EXT);
    STORAGE.rename(SRTTEMP, teleFileName);
    LOG_INF("Сохранено %d записей в файлах телеметрии.", srtSeqNo);
  }
}
void prepTelemetry() {
  if (teleUse) {
    teleInterval = srtInterval;
    for (int i=0; i < NUM_BUFF; i++) teleBuf[i] = psramFound() ? (char*)ps_malloc(RAMSIZE + MAX_LINE_LEN) : (char*)malloc(RAMSIZE + MAX_LINE_LEN);
    if (setupSensors()) xTaskCreate(&telemetryTask, "telemetryTask", TELEM_STACK_SIZE, NULL, TELEM_PRI, &telemetryHandle);
    else teleUse = false;
    LOG_INF("Доступна запись телеметрии %s", teleUse ? "is" : "NOT");
    debugMemory("prepTelemetry");
  }
}
bool startTelemetry() {
  bool res = true;
  if (teleUse && telemetryHandle != NULL) xTaskNotifyGive(telemetryHandle);
  else res = false;
  return res;
}
void stopTelemetry(const char* fileName) {
  if (teleUse) strcpy(teleFileName, fileName);
  capturing = false;
}
#endif