#include "appGlobals.h"
#if INCLUDE_PGRAM
#if !INCLUDE_PERIPH
#error "Требуется INCLUDE_PERIPH true"
#endif
uint8_t numberOfPhotos;
float tRPM;
bool clockWise;
uint8_t timeForFocus;
uint8_t timeForPhoto;
int pinShutter;
int pinFocus;
uint8_t photosDone;
float gearing;
bool extCam = false;
bool PGactive = false;
static float mRPM;
static TaskHandle_t pgramHandle = NULL;
static char pFolder[20];
#define MAX_RPM 15.0
#define shutterTime 100
static void prepPgram() {
  if (extCam) {
    pinMode(pinShutter, OUTPUT);
    if (pinFocus) pinMode(pinFocus, OUTPUT);
    LOG_INF("Внешний кулачок, штифт затвора %d", pinShutter);
#ifdef AUXILIARY
  } else {
     lampAuto = true;
     useMotion = doRecording = doPlayback = timeLapseOn = false;
     setLamp(0);
     time_t currEpoch = getEpoch();
     strftime(pFolder, sizeof(pFolder), "/%Y%m%d_%H%M%S", localtime(&currEpoch));
     STORAGE.mkdir(pFolder);
     LOG_INF("Встроенная камера, создана папка фотограмметрии %s", pFolder);
#endif
  }
}
#ifdef AUXILIARY
static void getPhoto() {
  LOG_WRN("Внутренняя камера недоступна на вспомогательной плате");
  photosDone = numberOfPhotos;
  stepperDone();
}
#else
static void getPhoto() {
  setLamp(lampLevel);
  if (timeForPhoto * 1000 > MAX_FRAME_WAIT) delay((timeForPhoto * 1000) - MAX_FRAME_WAIT);
  uint32_t startTime = millis();
  doKeepFrame = true;
  while (doKeepFrame && (millis() - startTime < MAX_FRAME_WAIT)) delay(100);
  if (!doKeepFrame && alertBufferSize) {
    char pName[FILE_NAME_LEN];
    strcpy(pName, pFolder);
    time_t currEpoch = getEpoch();
    strftime(pName + strlen(pFolder), sizeof(pName), "/%Y%m%d_%H%M%S", localtime(&currEpoch));
    strcat(pName, JPG_EXT);
    File pFile = STORAGE.open(pName, FILE_WRITE);
    pFile.write((uint8_t*)alertBuffer, alertBufferSize);
    pFile.close();
    LOG_INF("Фотография %u из % вас сохранена в %s", photosDone + 1, numberOfPhotos, pName);
    alertBufferSize = 0;
  } else LOG_WRN("Не удалось получить фотографию");
  setLamp(0);
}
#endif
static void takePhoto() {
  if (timeForFocus * 1000 > timeForPhoto * 1000 - shutterTime) timeForFocus = timeForPhoto - 1;
  uint32_t waitTime = (timeForPhoto - timeForFocus) * 1000 - shutterTime;
  delay(waitTime);
  if (pinFocus) {
    digitalWrite(pinFocus, HIGH);
    delay(timeForFocus * 1000);
  }
  digitalWrite(pinShutter, HIGH);
  delay(shutterTime);
  digitalWrite(pinShutter, LOW);
  if (pinFocus) digitalWrite(pinFocus, LOW);
  if (photosDone < numberOfPhotos) LOG_INF("Фотография %u из %u сделана", photosDone + 1, numberOfPhotos);
}
static void pgramTask (void *pvParameter) {
  float angle = 1.0 / (float)numberOfPhotos;
  photosDone = 0;
  prepPgram();
  LOG_INF("Начните делать %u фотографий каждые %0.1f градус со скоростью %0.1f об/мин.", numberOfPhotos, angle * 360, tRPM);
  do {
    extCam ? takePhoto() : getPhoto();
    stepperRun(mRPM, angle * gearing, clockWise, BYJ_48);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  } while (++photosDone < numberOfPhotos);
  LOG_INF("Фотографирование завершено.");
  if (extCam) {
    pinMode(pinShutter, INPUT);
    if (pinFocus) pinMode(pinFocus, INPUT);
  }
  pgramHandle = NULL;
  vTaskDelete(NULL);
}
void takePhotos(bool startPhotos) {
  if (stepperUse) {
    if (startPhotos) {
      mRPM = tRPM * gearing;
      if (mRPM > MAX_RPM) LOG_WRN("Запрошенная частота вращения шагового двигателя %0.1f слишком высока", mRPM);
      else {
        if (pgramHandle == NULL) xTaskCreate(&pgramTask, "pgramTask", STICK_STACK_SIZE , NULL, STICK_PRI, &pgramHandle);
        else LOG_WRN("pgramTask все еще работает");
      }
    } else {
      LOG_INF("Пользователь прервал съемку");
      photosDone = numberOfPhotos;
      stepperDone();
    }
  }
}
void stepperDone() {
  if (pgramHandle) xTaskNotifyGive(pgramHandle);
}
#endif