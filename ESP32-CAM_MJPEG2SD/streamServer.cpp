#include "appGlobals.h"
#define AUX_STRUCT_SIZE 2048
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;граница=" BOUNDARY_VAL
#define JPEG_BOUNDARY "\r\n--" BOUNDARY_VAL "\r\n"
#define JPEG_TYPE "Тип контента: image/jpeg\r\nДлина контента: %10u\r\n\r\n"
#define HDR_BUF_LEN 64
#define END_WAIT 100
static fs::FS fpv = STORAGE;
bool forcePlayback = false;
bool streamVid = false;
bool streamAud = false;
bool streamSrt = false;
static bool isStreaming[MAX_STREAMS] = {false};
size_t streamBufferSize[MAX_STREAMS] = {0};
byte* streamBuffer[MAX_STREAMS] = {NULL};
static char variable[FILE_NAME_LEN];
static char value[FILE_NAME_LEN];
uint16_t sustainId = 0;
uint8_t numStreams = 1;
uint8_t vidStreams = 1;
int srtInterval = 1;
#ifndef CONFIG_IDF_TARGET_ESP32C3
TaskHandle_t sustainHandle[MAX_STREAMS];
struct httpd_sustain_req_t {
  httpd_req_t* req = NULL;
  uint8_t taskNum;
  char activity[16];
  bool inUse = false;
};
httpd_sustain_req_t sustainReq[MAX_STREAMS];
#if INCLUDE_RTSP
static const bool includeRTSP = true;
#else
static const bool includeRTSP = false;
#endif
static void showPlayback(httpd_req_t* req) {
  esp_err_t res = ESP_OK;
  stopPlaying();
  forcePlayback = true;
  if (fpv.exists(inFileName)) {
    if (stopPlayback) LOG_WRN("Воспроизведение отклонено – идет съемка");
    else {
      LOG_INF("Воспроизведение включено (выбран SD-файл)");
      doPlayback = true;
    }
  } else LOG_WRN("Файл %s не существует, когда запрошено воспроизведение.", inFileName);
  if (doPlayback) {
    mjpegStruct mjpegData;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    char hdrBuf[HDR_BUF_LEN];
    openSDfile(inFileName);
    mjpegData = getNextFrame(true);
    while (doPlayback) {
      size_t jpgLen = mjpegData.buffLen;
      size_t buffOffset = mjpegData.buffOffset;
      if (!jpgLen && !buffOffset) {
        res = httpd_resp_sendstr_chunk(req, JPEG_BOUNDARY);
        doPlayback = false;
      } else {
        if (jpgLen) {
          if (mjpegData.jpegSize) {
            if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, JPEG_BOUNDARY);
            snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, mjpegData.jpegSize);
            if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, hdrBuf);
          }
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)iSDbuffer+buffOffset, jpgLen);
        }
        if (res == ESP_OK) mjpegData = getNextFrame();
        else {
          LOG_VRB("Воспроизведение прервано из-за ошибки: %s", espErrMsg(res));
          stopPlaying();
        }
      }
    }
    if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
    sustainId = currEpoch;
  }
}
static void showStream(httpd_req_t* req, uint8_t taskNum) {
  esp_err_t res = ESP_OK;
  size_t jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  uint32_t startTime = millis();
  uint32_t frameCnt = 0;
  uint32_t mjpegLen = 0;
  isStreaming[taskNum] = true;
  streamBufferSize[taskNum] = 0;
  if (!taskNum) motionJpegLen = 0;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  char hdrBuf[HDR_BUF_LEN];
  while (isStreaming[taskNum]) {
    if (xSemaphoreTake(frameSemaphore[taskNum], pdMS_TO_TICKS(MAX_FRAME_WAIT)) == pdFAIL) {
      streamBufferSize[taskNum] = 0;
      continue;
    }
    if (dbgMotion && !taskNum) {
      if (xSemaphoreTake(motionSemaphore, pdMS_TO_TICKS(MAX_FRAME_WAIT)) == pdFAIL) continue;
      jpgLen = motionJpegLen;
      if (!jpgLen) continue;
      jpgBuf = motionJpeg;
    } else {
      if (!streamBufferSize[taskNum]) continue;
      jpgLen = streamBufferSize[taskNum];
      jpgBuf = streamBuffer[taskNum];
    }
    if (res == ESP_OK) {
      res = httpd_resp_sendstr_chunk(req, JPEG_BOUNDARY);
      snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpgLen);
      if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, hdrBuf);
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpgBuf, jpgLen);
      frameCnt++;
    }
    mjpegLen += jpgLen;
    jpgLen = streamBufferSize[taskNum] = 0;
    if (dbgMotion && !taskNum) motionJpegLen = 0;
    if (res != ESP_OK) {
      LOG_VRB("Потоковая передача прервана из-за ошибки: %s", espErrMsg(res));
      isStreaming[taskNum] = false;
    }
  }
  if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
  uint32_t mjpegTime = millis() - startTime;
  float mjpegTimeF = float(mjpegTime) / 1000;
  LOG_INF("MJPEG: кадров: %u, всего %s за %0.1f с при %0.1f кадров в секунду", frameCnt, fmtSize(mjpegLen), mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
}
static void audioStream(httpd_req_t* req, uint8_t taskNum) {
#if INCLUDE_AUDIO
  if (micGain) {
    esp_err_t res = ESP_OK;
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    isStreaming[taskNum] = true;
    uint32_t totalSamples = 0;
    audioBytes = WAV_HDR_LEN;
    updateWavHeader();
    while (isStreaming[taskNum]) {
      if (audioBytes) {
        res = httpd_resp_send_chunk(req, (const char*)audioBuffer, audioBytes);
        audioBytes = 0;
      } else delay(20);
      if (res != ESP_OK) isStreaming[taskNum] = false;
      else totalSamples += audioBytes / 2;
    }
    audioBytes = 1;
    if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
    LOG_INF("WAV: отправлено %lвам образцы", totalSamples);
  } else LOG_WRN("Микрофон ESP не определен или микрофон выключен");
#else
  httpd_resp_sendstr(req, NULL);
#endif
}
static void srtStream(httpd_req_t* req, uint8_t taskNum) {
  esp_err_t res = ESP_OK;
  isStreaming[taskNum] = true;
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  int srtSeqNo = 0;
  uint32_t srtTime = 0;
  const uint32_t sampleInterval = 1000 * (srtInterval < 1 ? 1 : srtInterval);
  char srtHdr[100];
  char timeStr[10];
  while (isStreaming[taskNum]) {
    srtSeqNo++;
    uint32_t startTime = millis();
    formatElapsedTime(timeStr, srtTime, true);
    size_t srtPtr = sprintf(srtHdr, "%d\n%s --> ", srtSeqNo, timeStr);
    srtTime += sampleInterval;
    formatElapsedTime(timeStr, srtTime, true);
    srtPtr += sprintf(srtHdr + srtPtr, "%s\n", timeStr);
    time_t currEpoch = getEpoch();
    srtPtr += strftime(srtHdr + srtPtr, 12, "%H:%M:%S  ", localtime(&currEpoch));
    httpd_resp_send_chunk(req, (const char*)srtHdr, srtPtr);
#if INCLUDE_TELEM
    if (teleUse) {
      storeSensorData(true);
      if (srtBytes) res = httpd_resp_send_chunk(req, (const char*)srtBuffer, srtBytes);
      srtBytes = 0;
    }
#endif
    if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, "\n\n");
    if (res != ESP_OK) isStreaming[taskNum] = false;
    else while (isStreaming[taskNum] && millis() - sampleInterval < startTime) delay(50);
  }
  if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
  LOG_INF("SRT: отправлено %d субтитров", srtSeqNo);
}
void stopSustainTask(int taskId) {
  isStreaming[taskId] = false;
}
static void sustainTask(void* p) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint8_t i = *(uint8_t*)p;
    if (i == 0) {
      if (!strcmp(sustainReq[i].activity, "download")) fileHandler(sustainReq[i].req, true);
      else if (!strcmp(sustainReq[i].activity, "playback")) showPlayback(sustainReq[i].req);
      else if (!strcmp(sustainReq[i].activity, "stream")) showStream(sustainReq[i].req, i);
    }
    else if (i == 1) showStream(sustainReq[i].req, i);
    else if (i == 2) audioStream(sustainReq[i].req, i);
    else if (i == 3) srtStream(sustainReq[i].req, i);
    killSocket(httpd_req_to_sockfd(sustainReq[i].req));
    delay(END_WAIT);
    free(sustainReq[i].req->aux);
    sustainReq[i].req->~httpd_req_t();
    free(sustainReq[i].req);
    sustainReq[i].req = NULL;
    sustainReq[i].inUse = false;
  }
  vTaskDelete(NULL);
}
void startSustainTasks() {
  if (streamVid) numStreams = vidStreams = 2;
  if (streamAud) numStreams = 3;
  if (streamSrt) numStreams = 4;
  if (numStreams > MAX_STREAMS) {
    LOG_WRN("количество потоков %d превышает MAX_STREAMS %d", numStreams, MAX_STREAMS);
    numStreams = MAX_STREAMS;
  }
  if (maxFrameBuffSize * (vidStreams + 1) > ESP.getFreePsram()) {
    LOG_WRN("Недостаточно PSRAM для потоков NVR");
    vidStreams = 1;
    streamVid = streamAud = streamSrt = false;
  }
  for (int i = 0; i < vidStreams; i++)
    if (streamBuffer[i] == NULL) streamBuffer[i] = (byte*)ps_malloc(maxFrameBuffSize);
  for (int i = 0; i < numStreams; i++) {
    sustainReq[i].taskNum = i;
    if (includeRTSP && i > 0) continue;
    xTaskCreate(sustainTask, "sustainTask", SUSTAIN_STACK_SIZE, &sustainReq[i].taskNum, SUSTAIN_PRI, &sustainHandle[i]);
  }
  LOG_INF("Запущено %d задач поддержки", numStreams);
  debugMemory("startSustainTasks");
}
esp_err_t appSpecificSustainHandler(httpd_req_t* req) {
  esp_err_t res = ESP_FAIL;
  if (checkAuth(req)) {
    if (extractQueryKeyVal(req, variable, value) == ESP_OK) {
      uint8_t taskNum = 99;
      if (!strcmp(variable, "download")) taskNum = 0;
      else if (!strcmp(variable, "playback")) taskNum = 0;
      else if (!strcmp(variable, "stream")) taskNum = 0;
      else if (!strcmp(variable, "video")) taskNum = 1;
      else if (!strcmp(variable, "audio")) taskNum = 2;
      else if (!strcmp(variable, "srt")) taskNum = 3;
      if (includeRTSP && taskNum > 0) taskNum = 99;
      if (taskNum < numStreams) {
        if (taskNum == 0) {
          if (req->method == HTTP_HEAD) {
            if (sustainReq[taskNum].inUse) {
              if (!strcmp(variable, "stream")) {
                isStreaming[taskNum] = false;
                if (!taskNum) doPlayback = false;
                delay(END_WAIT + 100);
              }
            }
            if (sustainReq[taskNum].inUse) {
              LOG_WRN("Задача %d не бесплатна", taskNum);
              httpd_resp_set_status(req, "500 Нет свободного задания");
            }
            else {
              sustainId = currEpoch;
              res = ESP_OK;
            }
            httpd_resp_sendstr(req, NULL);
            return res;
          }
        } else {
          if (taskNum < MAX_STREAMS) {
            if (sustainReq[taskNum].inUse) {
              isStreaming[taskNum] = false;
              delay(END_WAIT + 100);
            }
          }
        }
        if (!sustainReq[taskNum].inUse) {
          uint8_t i = taskNum;
          sustainReq[i].inUse = true;
          sustainReq[i].req = static_cast<httpd_req_t*>(malloc(sizeof(httpd_req_t)));
          new (sustainReq[i].req) httpd_req_t(*req);
          sustainReq[i].req->aux = psramFound() ? ps_malloc(AUX_STRUCT_SIZE) : malloc(AUX_STRUCT_SIZE);
          memcpy(sustainReq[i].req->aux, req->aux, AUX_STRUCT_SIZE);
          strncpy(sustainReq[i].activity, variable, sizeof(sustainReq[i].activity) - 1);
          xTaskNotifyGive(sustainHandle[i]);
          return ESP_OK;
        } else httpd_resp_set_status(req, "500 Нет свободного задания");
      } else {
        if (taskNum < MAX_STREAMS) LOG_WRN("Задача не создана для потока: %s, numStreams %d", variable, numStreams);
        else LOG_WRN("Неверный идентификатор задачи: %s.", variable);
        httpd_resp_set_status(req, "400 Неверный URL-адрес");
      }
    } else httpd_resp_set_status(req, "400 Неверный URL-адрес");
    httpd_resp_sendstr(req, NULL);
  }
  return res;
}
#else
esp_err_t appSpecificSustainHandler(httpd_req_t* req) {return ESP_OK;}
#endif