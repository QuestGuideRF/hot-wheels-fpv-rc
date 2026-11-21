#include "appGlobals.h"
#define FB_CNT 4
bool useMotion  = true;
bool dbgMotion  = false;
bool forceRecord = false;
int moveStartChecks = 5;
int moveStopSecs = 2;
int maxFrames = 20000;
int tlSecsBetweenFrames;
int tlDurationMins;
int tlPlaybackFPS;
uint8_t FPS = 0;
bool nightTime = false;
uint8_t fsizePtr;
uint8_t minSeconds = 5;
bool doRecording = true;
uint8_t xclkMhz = 20;
bool doKeepFrame = false;
static bool haveSrt = false;
char camModel[11];
static int siodGpio = SIOD_GPIO_NUM;
static int siocGpio = SIOC_GPIO_NUM;
size_t maxFrameBuffSize;
static int frameLimit;
static uint32_t vidSize;
static uint16_t frameCnt;
static uint32_t startTime;
static uint32_t dTimeTot;
static uint32_t fTimeTot;
static uint32_t wTimeTot;
static uint32_t oTime;
static uint32_t cTime;
static uint32_t sTime;
static uint32_t frameInterval;
uint8_t iSDbuffer[(RAMSIZE + CHUNK_HDR) * 2];
static size_t highPoint;
static File aviFile;
static char aviFileName[FILE_NAME_LEN];
static File playbackFile;
static char partName[FILE_NAME_LEN];
static size_t readLen;
static uint8_t recFPS;
static uint32_t recDuration;
static uint8_t saveFPS = 99;
bool doPlayback = false;
TaskHandle_t captureHandle = NULL;
TaskHandle_t playbackHandle = NULL;
static SemaphoreHandle_t readSemaphore;
static SemaphoreHandle_t playbackSemaphore;
SemaphoreHandle_t frameSemaphore[MAX_STREAMS] = {NULL};
SemaphoreHandle_t motionSemaphore = NULL;
SemaphoreHandle_t aviMutex = NULL;
static volatile bool isPlaying = false;
bool isCapturing = false;
bool stopPlayback = false;
bool timeLapseOn = false;
int dashCamOn = 0;
static bool pirVal = false;
#ifndef CONFIG_IDF_TARGET_ESP32C3
framesize_t maxFS = FRAMESIZE_SVGA;
static void IRAM_ATTR frameISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (isPlaying) xSemaphoreGiveFromISR (playbackSemaphore, &xHigherPriorityTaskWoken );
  if (captureHandle != NULL) vTaskNotifyGiveFromISR(captureHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}
void controlFrameTimer(bool restartTimer) {
  static hw_timer_t* frameTimer = NULL;
  if (frameTimer) {
    timerDetachInterrupt(frameTimer);
    timerEnd(frameTimer);
    frameTimer = NULL;
  }
  if (restartTimer) {
    frameTimer = timerBegin(OneMHz);
    if (frameTimer) {
      frameInterval = OneMHz / FPS;
      LOG_VRB("Интервал таймера кадра %uмс для FPS %u", frameInterval / 1000, FPS);
      timerAttachInterrupt(frameTimer, &frameISR);
      timerAlarm(frameTimer, frameInterval, true, 0);
    } else LOG_ERR("Не удалось настроить FrameTimer.");
  }
}
static void openAvi() {
  oTime = millis();
  dateFormat(partName, sizeof(partName), true);
  STORAGE.mkdir(partName);
  dateFormat(partName, sizeof(partName), false);
  aviFile = STORAGE.open(AVITEMP, FILE_WRITE);
  oTime = millis() - oTime;
  LOG_VRB("Время открытия файла: %uмс", oTime);
#if INCLUDE_AUDIO
  startAudioRecord();
#endif
#if INCLUDE_TELEM
  haveSrt = startTelemetry();
#endif
  startTime = millis();
  frameCnt = fTimeTot = wTimeTot = dTimeTot = vidSize = 0;
  highPoint = AVI_HEADER_LEN;
  prepAviIndex();
}
static inline bool doMonitor(bool capturing) {
  static uint16_t motionCnt = 0;
  uint16_t checkRate = (capturing) ? FPS * moveStopSecs : FPS / moveStartChecks;
  if (!checkRate) checkRate = 1;
  if (++motionCnt / checkRate) motionCnt = 0;
  return !(bool)motionCnt;
}
static void timeLapse(camera_fb_t* fb, bool tlStop = false) {
  static int frameCntTL, requiredFrames, intervalCnt = 0;
  static int intervalMark = tlSecsBetweenFrames * saveFPS;
  static File tlFile;
  static char TLname[FILE_NAME_LEN];
  if (tlStop) {
    intervalCnt = 0;
    requiredFrames = frameCntTL - 1;
  }
  if (timeLapseOn) {
    if (timeSynchronized) {
      if (!frameCntTL) {
        requiredFrames = tlDurationMins * 60 / tlSecsBetweenFrames;
        if (requiredFrames > maxFrames) {
          LOG_WRN("Количество кадров, необходимое для замедленной съемки %u, уменьшено до максимального предела %u", requiredFrames, maxFrames);
          requiredFrames = maxFrames;
        }
        dateFormat(partName, sizeof(partName), true);
        STORAGE.mkdir(partName);
        dateFormat(partName, sizeof(partName), false);
        int tlen = snprintf(TLname, FILE_NAME_LEN - 1, "%s_%s_%u_%u_T.%s",
                            partName, frameData[fsizePtr].frameSizeStr, tlPlaybackFPS, tlDurationMins, AVI_EXT);
        if (tlen > FILE_NAME_LEN - 1) LOG_WRN("имя файла усечено");
        if (STORAGE.exists(TLTEMP)) STORAGE.remove(TLTEMP);
        tlFile = STORAGE.open(TLTEMP, FILE_WRITE);
        tlFile.write(aviHeader, AVI_HEADER_LEN);
        prepAviIndex(true);
        LOG_INF("Начатый файл замедленной съемки %s, длительность %u мин, для кадров %u",  TLname, tlDurationMins, requiredFrames);
        frameCntTL++;
      }
#if INCLUDE_PERIPH
      if (nightTime && intervalCnt == intervalMark - (saveFPS / 2)) setLamp(lampLevel);
#endif
      if (intervalCnt > intervalMark) {
#if INCLUDE_PERIPH
        if (!lampNight) setLamp(0);
#endif
        uint8_t hdrBuff[CHUNK_HDR];
        memcpy(hdrBuff, dcBuf, 4);
        uint16_t filler = (4 - (fb->len & 0x00000003)) & 0x00000003;
        uint32_t jpegSize = fb->len + filler;
        memcpy(hdrBuff + 4, &jpegSize, 4);
        tlFile.write(hdrBuff, CHUNK_HDR);
        tlFile.write(fb->buf, jpegSize);
        buildAviIdx(jpegSize, true, true);
        frameCntTL++;
        intervalCnt = 0;
        intervalMark = tlSecsBetweenFrames * saveFPS;
      }
      intervalCnt++;
      if (frameCntTL > requiredFrames) {
        xSemaphoreTake(aviMutex, portMAX_DELAY);
        buildAviHdr(tlPlaybackFPS, fsizePtr, --frameCntTL, true);
        xSemaphoreGive(aviMutex);
        finalizeAviIndex(frameCntTL, true);
        size_t idxLen = 0;
        do {
          idxLen = writeAviIndex(iSDbuffer, RAMSIZE, true);
          tlFile.write(iSDbuffer, idxLen);
        } while (idxLen > 0);
        tlFile.seek(0, SeekSet);
        tlFile.write(aviHeader, AVI_HEADER_LEN);
        tlFile.close();
        STORAGE.rename(TLTEMP, TLname);
        frameCntTL = intervalCnt = 0;
        LOG_INF("Завершенный таймлапс: %s", TLname);
#if INCLUDE_FTP_HFS
        if (autoUpload) fsStartTransfer(TLname);
#endif
      }
    }
  } else frameCntTL = intervalCnt = 0;
}
void keepFrame(camera_fb_t* fb) {
  if (fb->len < maxFrameBuffSize && alertBuffer != NULL) {
    memcpy(alertBuffer, fb->buf, fb->len);
    alertBufferSize = fb->len;
  }
}
static void saveFrame(camera_fb_t* fb) {
  uint32_t fTime = millis();
  uint16_t filler = (4 - (fb->len & 0x00000003)) & 0x00000003;
  size_t jpegSize = fb->len + filler;
  memcpy(iSDbuffer + highPoint, dcBuf, 4);
  memcpy(iSDbuffer + highPoint + 4, &jpegSize, 4);
  highPoint += CHUNK_HDR;
  if (highPoint >= RAMSIZE) {
    highPoint -= RAMSIZE;
    aviFile.write(iSDbuffer, RAMSIZE);
    memcpy(iSDbuffer, iSDbuffer + RAMSIZE, highPoint);
  }
  size_t jpegRemain = jpegSize;
  uint32_t wTime = millis();
  while (jpegRemain >= RAMSIZE - highPoint) {
    memcpy(iSDbuffer + highPoint, fb->buf + jpegSize - jpegRemain, RAMSIZE - highPoint);
    aviFile.write(iSDbuffer, RAMSIZE);
    jpegRemain -= RAMSIZE - highPoint;
    highPoint = 0;
  }
  wTime = millis() - wTime;
  wTimeTot += wTime;
  LOG_VRB("Время хранения SD %u мс", wTime);
  memcpy(iSDbuffer + highPoint, fb->buf + jpegSize - jpegRemain, jpegRemain);
  highPoint += jpegRemain;
  buildAviIdx(jpegSize);
  vidSize += jpegSize + CHUNK_HDR;
  frameCnt++;
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  LOG_VRB("Время обработки кадра %u мс", fTime);
  LOG_VRB("============================");
}
static bool closeAvi() {
  uint32_t vidDuration = millis() - startTime;
  uint32_t vidDurationSecs = lround(vidDuration / 1000.0);
  logLine();
  LOG_VRB("Время захвата %u, мин секунд: %u ", vidDurationSecs, minSeconds);
  cTime = millis();
  aviFile.write(iSDbuffer, highPoint);
  size_t readLen = 0;
  bool haveWav = false;
#if INCLUDE_AUDIO
  finishAudioRecord(true);
  haveWav = haveWavFile();
  if (haveWav) {
    do {
      readLen = writeWavFile(iSDbuffer, RAMSIZE);
      aviFile.write(iSDbuffer, readLen);
    } while (readLen > 0);
  }
#endif
  finalizeAviIndex(frameCnt);
  do {
    readLen = writeAviIndex(iSDbuffer, RAMSIZE);
    if (readLen) aviFile.write(iSDbuffer, readLen);
  } while (readLen > 0);
  float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
  uint8_t actualFPSint = (uint8_t)(lround(actualFPS));
  xSemaphoreTake(aviMutex, portMAX_DELAY);
  buildAviHdr(actualFPSint, fsizePtr, frameCnt);
  xSemaphoreGive(aviMutex);
  aviFile.seek(0, SeekSet);
  aviFile.write(aviHeader, AVI_HEADER_LEN);
  aviFile.close();
  LOG_VRB("Окончательное время хранения SD-карты %lмс", millis() - cTime);
  uint32_t hTime = millis();
#if INCLUDE_MQTT
  if (mqtt_active) {
    sprintf(jsonBuff, "{\"RECORD\":\"OFF\", \"TIME\":\"%s\"}", esp_log_system_timestamp());
    mqttPublish(jsonBuff);
    mqttPublishPath("record", "off");
  }
#endif
  if (vidDurationSecs >= minSeconds) {
    int alen = snprintf(aviFileName, FILE_NAME_LEN - 1, "%s_%s_%u_%lu%s%s%s.%s",
                        partName, frameData[fsizePtr].frameSizeStr, actualFPSint, vidDurationSecs,
                        haveWav ? "_S" : "", haveSrt ? "_M" : "", dashCamOn ? "_C" : "", AVI_EXT);
    if (alen > FILE_NAME_LEN - 1) LOG_WRN("имя файла усечено");
    STORAGE.rename(AVITEMP, aviFileName);
    LOG_VRB("Время закрытия AVI %lмс", millis() - hTime);
    cTime = millis() - cTime;
#if INCLUDE_TELEM
    stopTelemetry(aviFileName);
#endif
    if (dashCamOn) forceRecord = true;
    else {
      LOG_INF("******** Статистика записи AVI ********");
      LOG_ALT("Записано %s", aviFileName);
      LOG_INF("Продолжительность AVI: %u сек.", vidDurationSecs);
      LOG_INF("Количество кадров: %u", frameCnt);
      LOG_INF("Требуемый FPS: %u", FPS);
      LOG_INF("Фактический FPS: %0.1f", actualFPS);
      LOG_INF("Размер файла: %s", fmtSize(vidSize));
      if (frameCnt) {
        LOG_INF("Средняя длина кадра: %u байт.", vidSize / frameCnt);
        LOG_INF("Среднее время контроля кадра: %u мс", dTimeTot / frameCnt);
        LOG_INF("Среднее время буферизации кадров: %u мс.", fTimeTot / frameCnt);
        LOG_INF("Среднее время хранения кадра: %u мс.", wTimeTot / frameCnt);
      }
      LOG_INF("Средняя скорость записи SD: %u кБ/с.", ((vidSize / wTimeTot) * 1000) / 1024);
      LOG_INF("Время открытия/завершения файла: %u мс/%u мс.", oTime, cTime);
      LOG_INF("Занято: %u%%", std::min(100 * (wTimeTot + fTimeTot + dTimeTot + oTime + cTime) / vidDuration, (uint32_t)100));
      checkMemory();
      LOG_INF("*************************************");
#if INCLUDE_FTP_HFS
      if (autoUpload) {
        if (deleteAfter) {
          dateFormat(partName, sizeof(partName), true);
          fsStartTransfer(partName);
        } else fsStartTransfer(aviFileName);
      }
#endif
#if INCLUDE_TGRAM
      if (tgramUse) tgramAlert(aviFileName, "");
#endif
    }
    if (!checkFreeStorage()) doRecording = forceRecord = false;
    return true;
  } else {
    STORAGE.remove(AVITEMP);
    LOG_INF("Недостаточная продолжительность захвата: %u сек.", vidDurationSecs);
    return false;
  }
}
static boolean processFrame() {
  static bool wasCapturing = false;
  static bool wasRecording = false;
  static bool captureMotion = false;
  bool res = true;
  uint32_t dTime = millis();
  bool finishRecording = false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == NULL || !fb->len || fb->len > maxFrameBuffSize) return false;
  timeLapse(fb);
  for (int i = 0; i < vidStreams; i++) {
    if (!streamBufferSize[i] && streamBuffer[i] != NULL) {
      memcpy(streamBuffer[i], fb->buf, fb->len);
      streamBufferSize[i] = fb->len;
      xSemaphoreGive(frameSemaphore[i]);
    }
  }
  if (doKeepFrame) {
    keepFrame(fb);
    doKeepFrame = false;
  }
  if (useMotion && doMonitor(isCapturing)) captureMotion = checkMotion(fb, isCapturing);
  if (!useMotion && doMonitor(true)) checkMotion(fb, false, true);
#if INCLUDE_PERIPH
  if (pirUse) {
    pirVal = getPIRval();
    if (pirVal && !isCapturing) {
      if (lampAuto && nightTime) setLamp(lampLevel);
      notifyMotion(fb);
    }
  }
#endif
  isCapturing = forceRecord | captureMotion | pirVal;
  if (forceRecord || wasRecording || doRecording) {
    if (forceRecord && !wasRecording) wasRecording = true;
    else if (!forceRecord && wasRecording) wasRecording = false;
    if (isCapturing && !wasCapturing) {
      stopPlaying();
      stopPlayback = true;
      if (dashCamOn == 0) LOG_ALT("Захват начат %s%s%s", captureMotion ? "Motion " : "", pirVal ? "PIR" : "", forceRecord ? "Button" : "");
#if INCLUDE_MQTT
      if (mqtt_active) {
        sprintf(jsonBuff, "{\"RECORD\":\"ON\", \"TIME\":\"%s\"}", esp_log_system_timestamp());
        mqttPublish(jsonBuff);
        mqttPublishPath("record", "on");
      }
#endif
#if INCLUDE_PERIPH
      buzzerAlert(true);
#endif
      openAvi();
      wasCapturing = true;
    }
    if (isCapturing && wasCapturing) {
      showProgress();
      if (frameCnt < frameLimit) {
        dTimeTot += millis() - dTime;
        saveFrame(fb);
        if (frameCnt == frameLimit) {
          forceRecord = finishRecording = false;
          if (dashCamOn == 0) {
            logLine();
            LOG_WRN("Автоматическое закрытие записи после %u кадров", frameLimit);
          }
        }
      }
#if INCLUDE_PERIPH
      if (buzzerUse && frameCnt / FPS >= buzzerDuration) buzzerAlert(false);
#endif
    }
    if (!isCapturing && wasCapturing) {
      finishRecording = true;
#if INCLUDE_PERIPH
      if (lampAuto) setLamp(0);
      buzzerAlert(false);
#endif
    }
    wasCapturing = isCapturing;
  }
  esp_camera_fb_return(fb);
  if (finishRecording) {
    if (stopPlayback) closeAvi();
    finishRecording = isCapturing = wasCapturing = stopPlayback = false;
  }
  return res;
}
static void captureTask(void* parameter) {
  uint32_t ulNotifiedValue;
  while (true) {
    ulNotifiedValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (ulNotifiedValue > FB_CNT) ulNotifiedValue = FB_CNT;
    while (ulNotifiedValue-- > 0) processFrame();
  }
  vTaskDelete(NULL);
}
uint8_t setFPS(uint8_t val) {
  if (val) {
    FPS = val;
    controlFrameTimer(true);
    saveFPS = FPS;
  }
  return FPS;
}
uint8_t setFPSlookup(uint8_t val) {
  fsizePtr = val;
  return setFPS(frameData[fsizePtr].defaultFPS);
}
static fnameStruct extractMeta(const char* fname) {
  fnameStruct fnameMeta;
  char fnameStr[FILE_NAME_LEN];
  strcpy(fnameStr, fname);
  replaceChar(fnameStr, '_', ' ');
  int items = sscanf(fnameStr, "%*s %*s %*s %hху %lн", &fnameMeta.recFPS, &fnameMeta.recDuration);
  if (items != 2) LOG_ERR("не удалось проанализировать %s, элементы %u", fname, items);
  return fnameMeta;
}
static void playbackFPS(const char* fname) {
  fnameStruct fnameMeta = extractMeta(fname);
  recFPS = fnameMeta.recFPS;
  if (recFPS < 1) recFPS = 1;
  recDuration = fnameMeta.recDuration;
  FPS = recFPS;
  controlFrameTimer(true);
}
static void readSD() {
  uint32_t rTime = millis();
  readLen = 0;
  if (!stopPlayback) {
    readLen = playbackFile.read(iSDbuffer + RAMSIZE + CHUNK_HDR, RAMSIZE);
    LOG_VRB("Время чтения SD %lмс", millis() - rTime);
  }
  wTimeTot += millis() - rTime;
  xSemaphoreGive(readSemaphore);
  delay(10);
}
void openSDfile(const char* streamFile) {
  if (stopPlayback) LOG_WRN("Воспроизведение отклонено – идет съемка");
  else {
    stopPlaying();
    strcpy(aviFileName, streamFile);
    LOG_INF("Играю %s", aviFileName);
    playbackFile = STORAGE.open(aviFileName, FILE_READ);
    playbackFile.seek(AVI_HEADER_LEN, SeekSet);
    playbackFPS(aviFileName);
    isPlaying = true;
    doPlayback = true;
    readSD();
  }
}
mjpegStruct getNextFrame(bool firstCall) {
  mjpegStruct mjpegData;
  static bool remainingBuff;
  static bool completedPlayback;
  static size_t buffOffset;
  static uint32_t hTimeTot;
  static uint32_t tTimeTot;
  static uint32_t hTime;
  static size_t remainingFrame;
  static size_t buffLen;
  const uint32_t dcVal = 0x63643030;
  if (firstCall) {
    sTime = millis();
    hTime = millis();
    remainingBuff = completedPlayback = false;
    frameCnt = remainingFrame = vidSize = buffOffset = 0;
    wTimeTot = fTimeTot = hTimeTot = tTimeTot = 1;
  }
  LOG_VRB("Время отправки http %lмс", millis() - hTime);
  hTimeTot += millis() - hTime;
  uint32_t mTime = millis();
  if (!stopPlayback) {
    if (!remainingBuff) {
      mTime = millis();
      memcpy(iSDbuffer, iSDbuffer + RAMSIZE, CHUNK_HDR);
      xSemaphoreTake(readSemaphore, portMAX_DELAY);
      buffLen = readLen;
      LOG_VRB("Время ожидания SD %lмс", millis() - mTime);
      wTimeTot += millis() - mTime;
      mTime = millis();
      memcpy(iSDbuffer + CHUNK_HDR, iSDbuffer + RAMSIZE + CHUNK_HDR, buffLen);
      LOG_VRB("memcpy потребовалось %lмс для %u байт", millis() - mTime, buffLen);
      fTimeTot += millis() - mTime;
      remainingBuff = true;
      if (buffOffset > RAMSIZE) buffOffset = 4;
      else buffOffset = frameCnt ? 0 : CHUNK_HDR;
      xTaskNotifyGive(playbackHandle);
    }
    mTime = millis();
    if (!remainingFrame) {
      uint32_t inVal;
      memcpy(&inVal, iSDbuffer + buffOffset, 4);
      if (inVal != dcVal) {
        mjpegData.buffLen = buffOffset;
        mjpegData.buffOffset = 0;
        mjpegData.jpegSize = 0;
        stopPlayback = completedPlayback = true;
        return mjpegData;
      } else {
        uint32_t jpegSize;
        memcpy(&jpegSize, iSDbuffer + buffOffset + 4, 4);
        remainingFrame = jpegSize;
        vidSize += jpegSize;
        buffOffset += CHUNK_HDR;
        mjpegData.jpegSize = jpegSize;
        mTime = millis();
        xSemaphoreTake(playbackSemaphore, portMAX_DELAY);
        LOG_VRB("ожидание таймера кадра %lмс", millis() - mTime);
        tTimeTot += millis() - mTime;
        frameCnt++;
        showProgress();
      }
    } else mjpegData.jpegSize = 0;
    if (buffOffset > RAMSIZE) mjpegData.buffLen = 0;
    else mjpegData.buffLen = (remainingFrame > buffLen - buffOffset) ? buffLen - buffOffset : remainingFrame;
    mjpegData.buffOffset = buffOffset;
    remainingFrame -= mjpegData.buffLen;
    buffOffset += mjpegData.buffLen;
    if (buffOffset >= buffLen) remainingBuff = false;
  } else {
    playbackFile.close();
    logLine();
    if (!completedPlayback) LOG_INF("Принудительно закрыть воспроизведение");
    uint32_t playDuration = (millis() - sTime) / 1000;
    uint32_t totBusy = wTimeTot + fTimeTot + hTimeTot;
    LOG_INF("******** Статистика воспроизведения AVI ********");
    LOG_INF("Воспроизведение %s", aviFileName);
    LOG_INF("Записано количество кадров в секунду %u, продолжительность %u сек.", recFPS, recDuration);
    LOG_INF("Частота воспроизведения при воспроизведении %0.1f, продолжительность %u сек.", (float)frameCnt / playDuration, playDuration);
    LOG_INF("Количество кадров: %u", frameCnt);
    if (frameCnt) {
      LOG_INF("Средняя скорость чтения SD: %u кБ/с.", ((vidSize / wTimeTot) * 1000) / 1024);
      LOG_INF("Среднее время чтения SD кадра: %u мс.", wTimeTot / frameCnt);
      LOG_INF("Среднее время обработки кадра: %u мс.", fTimeTot / frameCnt);
      LOG_INF("Среднее время задержки кадра: %u мс.", tTimeTot / frameCnt);
      LOG_INF("Среднее время HTTP-отправки: %u мс", hTimeTot / frameCnt);
      LOG_INF("Занято: %u%%", min(100 * totBusy / (totBusy + tTimeTot), (uint32_t)100));
    }
    checkMemory();
    LOG_INF("*************************************\n");
    setFPS(saveFPS);
    stopPlayback = isPlaying = false;
    mjpegData.buffLen = mjpegData.buffOffset = 0;
  }
  hTime = millis();
  delay(1);
  return mjpegData;
}
void stopPlaying() {
  if (isPlaying) {
    stopPlayback = true;
    uint32_t timeOut = millis();
    while (doPlayback && millis() - timeOut < MAX_FRAME_WAIT) delay(10);
    if (doPlayback) {
      logLine();
      LOG_WRN("Принудительно закрыть воспроизведение");
      doPlayback = false;
      setFPS(saveFPS);
      xSemaphoreGive(playbackSemaphore);
      xSemaphoreGive(readSemaphore);
      delay(200);
    }
    stopPlayback = false;
    isPlaying = false;
  }
}
static void playbackTask(void* parameter) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    readSD();
  }
  vTaskDelete(NULL);
}
static bool startSDtasks() {
  xTaskCreate(&playbackTask, "playbackTask", PLAYBACK_STACK_SIZE, NULL, PLAY_PRI, &playbackHandle);
  xTaskCreate(&captureTask, "captureTask", CAPTURE_STACK_SIZE, NULL, CAPTURE_PRI, &captureHandle);
  if (captureHandle == NULL) {
    OTAprereq();
    return false;
  }
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, (framesize_t)fsizePtr);
  setFPS(FPS);
  debugMemory("startSDtasks");
  return true;
}
bool prepRecording() {
  readSemaphore = xSemaphoreCreateBinary();
  playbackSemaphore = xSemaphoreCreateBinary();
  aviMutex = xSemaphoreCreateMutex();
  motionSemaphore = xSemaphoreCreateBinary();
  for (int i = 0; i < vidStreams; i++) frameSemaphore[i] = xSemaphoreCreateBinary();
  reloadConfigs();
  if (!startSDtasks()) return false;
#if INCLUDE_TINYML
  LOG_INF("%sИспользование TinyML", mlUse ? "" : "Not ");
#endif
  if ((fs::LittleFSFS*)&STORAGE == &LittleFS) {
    sdFreeSpaceMode = 0;
    sdMinCardFreeSpace = 0;
    doRecording = false;
    sdLog = false;
    useMotion = false;
    doRecording = false;
    LOG_WRN("Запись отключена из-за отсутствия SD-карты");
  } else {
    LOG_INF("Чтобы записать новый AVI, выполните одно из:");
    LOG_INF("- нажмите «Начать запись» на веб-странице");
#if INCLUDE_PERIPH
    if (pirUse) {
      LOG_INF("- прикрепите PIR к контакту %u", pirPin);
      LOG_INF("— поднять напряжение на выводе %u до 3,3 В.", pirPin);
    }
#endif
    if (useMotion) LOG_INF("- двигаться перед камерой");
  }
  logLine();
  debugMemory("prepRecording");
  return true;
}
void appShutdown() {
  timeLapse(NULL, true);
}
static void deleteTask(TaskHandle_t thisTaskHandle) {
  if (thisTaskHandle != NULL) vTaskDelete(thisTaskHandle);
  thisTaskHandle = NULL;
}
void endTasks() {
  for (int i = 0; i < numStreams; i++) deleteTask(sustainHandle[i]);
  deleteTask(captureHandle);
  deleteTask(playbackHandle);
#if INCLUDE_TELEM
  deleteTask(telemetryHandle);
#endif
#if INCLUDE_PERIPH
  deleteTask(DS18B20handle);
  deleteTask(servoHandle);
  deleteTask(stickHandle);
#endif
#if INCLUDE_SMTP
  deleteTask(emailHandle);
#endif
#if INCLUDE_FTP_HFS
  deleteTask(fsHandle);
#endif
#if INCLUDE_TGRAM
  deleteTask(telegramHandle);
#endif
#if INCLUDE_AUDIO
  deleteTask(audioHandle);
#endif
}
void OTAprereq() {
  doPlayback = forceRecord = false;
  controlFrameTimer(false);
#if INCLUDE_PERIPH
  setStickTimer(false);
#endif
  stopPing();
  endTasks();
  esp_camera_deinit();
  delay(100);
}
#ifdef CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3
#include "DFRobot_AXP313A.h"
DFRobot_AXP313A axp;
static bool camPower() {
  int pwrRetry = 5;
  while (pwrRetry) {
    if (axp.begin() == 0) {
      axp.enableCameraPower(axp.eOV2640);
      return true;
    } else {
      delay(1000);
      pwrRetry--;
    }
  }
  LOG_ERR("Не удалось включить камеру");
  return false;
}
#else
static bool camPower() {
  return true;
}
#endif
static esp_err_t changeXCLK(camera_config_t config) {
  if (config.xclk_freq_hz <= 20 * OneMHz) return ESP_OK;
  esp_err_t res = ESP_OK;
  ledc_stop(LEDC_LOW_SPEED_MODE, config.ledc_channel, 0);
  delay(5);
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_1_BIT,
    .timer_num = config.ledc_timer,
    .freq_hz = (uint32_t)config.xclk_freq_hz,
    .clk_cfg = LEDC_AUTO_CLK
  };
  res = ledc_timer_config(&ledc_timer);
  if (res != ESP_OK) {
    LOG_ERR("Не удалось настроить таймер %s.", espErrMsg(res));
    return res;
  }
  ledc_channel_config_t ledc_channel = {
    .gpio_num = XCLK_GPIO_NUM,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = config.ledc_channel,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = config.ledc_timer,
    .duty = 1,
    .hpoint = 0
  };
  res = ledc_channel_config(&ledc_channel);
  if (res != ESP_OK) {
    LOG_ERR("Не удалось настроить канал %s.", espErrMsg(res));
    return res;
  }
  delay(200);
  return res;
}
bool prepCam() {
  if (FRAMESIZE_INVALID != sizeof(frameData) / sizeof(frameData[0]))
    LOG_ERR("записи Framesize_t %d != записи FrameData %d", FRAMESIZE_INVALID, sizeof(frameData) / sizeof(frameData[0]));
  if (!camPower()) return false;
#if INCLUDE_I2C
  if (shareI2C(SIOD_GPIO_NUM, SIOC_GPIO_NUM)) {
    siodGpio = -1;
    siocGpio = -1;
  }
#endif
  bool res = false;
  maxFS = FRAMESIZE_SVGA;
  if (ESP.getPsramSize() > 5 * ONEMEG) maxFS = FRAMESIZE_QSXGA;
  else if (ESP.getPsramSize() > 3 * ONEMEG) maxFS = FRAMESIZE_UXGA;
#ifdef USE_PY260
  maxFS = (ESP.getPsramSize() > 5 * ONEMEG) ? FRAMESIZE_5MP : FRAMESIZE_HD;
#endif
  maxFrameBuffSize = maxAlertBuffSize = frameData[maxFS].frameWidth * frameData[maxFS].frameHeight / 5;
  LOG_INF("Максимальный размер кадра для %s PSRAM — %s. ", fmtSize(ESP.getPsramSize()), frameData[maxFS].frameSizeStr);
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_1;
  config.ledc_timer = LEDC_TIMER_1;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = siodGpio;
  config.pin_sccb_scl = siocGpio;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = xclkMhz * OneMHz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.frame_size = maxFS;
  config.jpeg_quality = 10;
  config.fb_count = FB_CNT;
  config.sccb_i2c_port = 0;
#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif
  esp_err_t err = ESP_FAIL;
  uint8_t retries = 2;
  while (retries && err != ESP_OK) {
    err = esp_camera_init(&config);
    if (err == ESP_OK) err = changeXCLK(config);
    if (err != ESP_OK) {
#if (defined(PWDN_GPIO_NUM)) && (PWDN_GPIO_NUM > -1)
      digitalWrite(PWDN_GPIO_NUM, 1);
      delay(100);
      digitalWrite(PWDN_GPIO_NUM, 0);
      delay(100);
#else
      delay(200);
#endif
      retries--;
    }
  }
  if (err != ESP_OK) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Ошибка инициализации камеры 0x%X:%s на %s", err, espErrMsg(err), CAM_BOARD);
  else {
    sensor_t* s = esp_camera_sensor_get();
    if (s == NULL) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Не удалось получить доступ к данным камеры %s.", CAM_BOARD);
    else {
      switch (s->id.PID) {
        case (OV2640_PID):
          strcpy(camModel, "OV2640");
          break;
        case (OV3660_PID):
          strcpy(camModel, "OV3660");
          break;
        case (OV5640_PID):
          strcpy(camModel, "OV5640");
          break;
        case (MEGA_CCM_PID):
          strcpy(camModel, "PY260");
          break;
        default:
          sprintf(camModel, "ПИД=0x%X", s->id.PID);
          break;
      }
      char fsizePtr[4];
      if (retrieveConfigVal("framesize", fsizePtr)) s->set_framesize(s, (framesize_t)(atoi(fsizePtr)));
      else s->set_framesize(s, FRAMESIZE_VGA);
      if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
      }
#if defined(CAMERA_MODEL_M5STACK_WIDE)
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
#endif
#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
#endif
#if defined(CAMERA_MODEL_ESP32S3_EYE)
      s->set_vflip(s, 1);
#endif
      res = true;
    }
  }
  if (res) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == NULL) {
      snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Не удалось получить кадр камеры. Проверьте аппаратное обеспечение камеры.");
    } else {
      esp_camera_fb_return(fb);
      fb = NULL;
      res = true;
      LOG_INF("Модель камеры %s готова, @ %u МГц", camModel, xclkMhz);
      if (timeLapseOn) dashCamOn = 0;
      if (dashCamOn) {
        timeLapseOn = useMotion = false;
        frameLimit = FPS * dashCamOn * 60;
        if (frameLimit > maxFrames) {
          frameLimit = maxFrames;
          LOG_WRN("Максимальный интервал непрерывной записи – %d мин.", frameLimit / FPS / 60);
        } else LOG_INF("Выполняйте непрерывную запись с интервалом %d мин.", dashCamOn);
        forceRecord = true;
      } else frameLimit = maxFrames;
    }
  }
  debugMemory("prepCam");
  return res;
}
#else
void appShutdown() {}
void OTAprereq() {}
uint8_t setFPSlookup(uint8_t val) {
  return 0;
}
#endif