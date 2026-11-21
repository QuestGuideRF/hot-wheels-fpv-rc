#include "appGlobals.h"
#if INCLUDE_RTSP
#include <ESP32-RTSPServer.h>
RTSPServer rtspServer;
bool rtspVideo;
bool rtspAudio;
bool rtspSubtitles;
int rtspPort;
uint16_t rtpVideoPort;
uint16_t rtpAudioPort;
uint16_t rtpSubtitlesPort;
char RTP_ip[MAX_IP_LEN];
uint8_t rtspMaxClients;
uint8_t rtpTTL;
char RTSP_Name[MAX_HOST_LEN-1] = "";
char RTSP_Pass[MAX_PWD_LEN-1] = "";
bool useAuth;
IPAddress rtpIp;
char transportStr[30];
RTSPServer::TransportType determineTransportType() {
  if (rtspVideo && rtspAudio && rtspSubtitles) {
    strcpy(transportStr, "s: Видео, аудио и субтитры");
    return RTSPServer::VIDEO_AUDIO_SUBTITLES;
  } else if (rtspVideo && rtspAudio) {
    strcpy(transportStr, "с: Видео и аудио");
    return RTSPServer::VIDEO_AND_AUDIO;
  } else if (rtspVideo && rtspSubtitles) {
    strcpy(transportStr, "s: Видео и субтитры");
    return RTSPServer::VIDEO_AND_SUBTITLES;
  } else if (rtspAudio && rtspSubtitles) {
    strcpy(transportStr, "s: Аудио и субтитры");
    return RTSPServer::AUDIO_AND_SUBTITLES;
  } else if (rtspVideo) {
    strcpy(transportStr, ": Видео");
    return RTSPServer::VIDEO_ONLY;
  } else if (rtspAudio) {
    strcpy(transportStr, ": Аудио");
    return RTSPServer::AUDIO_ONLY;
  } else if (rtspSubtitles) {
    strcpy(transportStr, ": Субтитры");
    return RTSPServer::SUBTITLES_ONLY;
  } else {
    strcpy(transportStr, ": Никто!");
    return RTSPServer::NONE;
  }
}
#ifdef ISCAM
static void sendRTSPVideo(void* p) {
  uint8_t taskNum = 1;
  streamBufferSize[taskNum] = 0;
  while (true) {
    if (frameSemaphore[taskNum] != NULL) {
      if (xSemaphoreTake(frameSemaphore[taskNum], pdMS_TO_TICKS(MAX_FRAME_WAIT)) == pdTRUE) {
        if (streamBufferSize[taskNum] && rtspServer.readyToSendFrame()) {
          rtspServer.sendRTSPFrame(streamBuffer[taskNum], streamBufferSize[taskNum], quality, frameData[fsizePtr].frameWidth, frameData[fsizePtr].frameHeight);
        }
      }
      streamBufferSize[taskNum] = 0;
    } else delay(100);
  }
  vTaskDelete(NULL);
}
void sendRTSPSubtitles(void* arg) {
  char data[100];
  time_t currEpoch = getEpoch();
  size_t len = strftime(data, 12, "%H:%M:%S  ", localtime(&currEpoch));
  len += sprintf(data + len, "Частота кадров: %lу", rtspServer.rtpFps);
#if INCLUDE_TELEM
  if (teleUse) {
    storeSensorData(true);
    if (srtBytes) len += sprintf(data + len, "%s", (const char*)srtBuffer);
    srtBytes = 0;
  }
#endif
  rtspServer.sendRTSPSubtitles(data, len);
}
static void startRTSPSubtitles(void* arg) {
  rtspServer.startSubtitlesTimer(sendRTSPSubtitles);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  vTaskDelete(NULL);
}
#endif
static void sendRTSPAudio(void* p) {
#if INCLUDE_AUDIO
  audioBytes = 0;
  while (true) {
    if (micGain && audioBytes && rtspServer.readyToSendAudio()) {
      rtspServer.sendRTSPAudio((int16_t*)audioBuffer, audioBytes);
      audioBytes = 0;
    }
    delay(20);
  }
#endif
  vTaskDelete(NULL);
}
static void initRTSP() {
#ifdef ISVC
  rtspVideo = rtspSubtitles = false;
  rtspAudio = true;
  strcpy(RTP_ip, "239.255.0.1");
  rtspPort = 554;
  rtpAudioPort = 5432;
  rtpVideoPort = 0;
  rtpSubtitlesPort = 0;
  rtspMaxClients = 1;
  rtpTTL = 1;
#endif
}
void prepRTSP() {
  initRTSP();
  useAuth = rtspServer.setCredentials(RTSP_Name, RTSP_Pass);
  RTSPServer::TransportType transport = determineTransportType();
  rtpIp.fromString(RTP_ip);
  rtspServer.transport = transport;
#if INCLUDE_AUDIO
  rtspServer.sampleRate = SAMPLE_RATE;
#endif
  rtspServer.rtspPort = rtspPort;
  rtspServer.rtpVideoPort = rtpVideoPort;
  rtspServer.rtpAudioPort = rtpAudioPort;
  rtspServer.rtpSubtitlesPort = rtpSubtitlesPort;
  rtspServer.rtpIp = rtpIp;
  rtspServer.maxRTSPClients = rtspMaxClients;
  rtspServer.rtpTTL = rtpTTL;
  if (transport != RTSPServer::NONE) {
    if (rtspServer.init()) {
      LOG_INF("Сервер RTSP успешно запущен с транспортом%s", transportStr);
      LOG_INF("Подключиться к: rtsp://%s%s:%d%s", useAuth ? "<username>:<password>@" : "", netLocalIP().toString().c_str(),
        rtspServer.rtspPort, useAuth ? " (учетные данные не показаны по соображениям безопасности)" : "");
#ifdef ISCAM
      if (rtspVideo) xTaskCreate(sendRTSPVideo, "sendRTSPVideo", 1024 * 5, NULL, SUSTAIN_PRI, &sustainHandle[1]);
      if (rtspAudio) xTaskCreate(sendRTSPAudio, "sendRTSPAudio", 1024 * 5, NULL, SUSTAIN_PRI, &sustainHandle[2]);
      if (rtspSubtitles) xTaskCreate(startRTSPSubtitles, "startRTSPSubtitles", 1024 * 1, NULL, SUSTAIN_PRI, &sustainHandle[3]);
#endif
#ifdef ISVC
      xTaskCreate(sendRTSPAudio, "sendRTSPAudio", 1024 * 5, NULL, 5, NULL);
#endif
    } else LOG_ERR("Не удалось запустить RTSP-сервер.");
  } else LOG_WRN("RTSP-сервер не запущен, транспорт не выбран");
}
#endif