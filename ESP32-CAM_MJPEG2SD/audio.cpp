#include "appGlobals.h"
#if INCLUDE_AUDIO
#include <ESP_I2S.h>
I2SClass I2Spdm;
I2SClass I2Sstd;
i2s_port_t MIC_CHAN = I2S_NUM_1;
i2s_port_t AMP_CHAN = I2S_NUM_0;
static bool micUse = false;
bool micRem = false;
static bool ampUse = false;
bool spkrRem = false;
bool volatile stopAudio = false;
static bool micRecording = false;
bool I2Smic;
int micSckPin = -1;
int micSWsPin = -1;
int micSdPin = -1;
int mampBckIo = -1;
int mampSwsIo = -1;
int mampSdIo = -1;
int ampTimeout = 1000;
uint32_t SAMPLE_RATE = 16000;
int micGain = 0;
int8_t ampVol = 0;
TaskHandle_t audioHandle = NULL;
static int totalSamples = 0;
static const uint8_t sampleWidth = sizeof(int16_t);
const size_t sampleBytes = DMA_BUFF_LEN * sampleWidth;
int16_t* sampleBuffer = NULL;
static uint8_t* wsBuffer = NULL;
static size_t wsBufferLen = 0;
uint8_t* audioBuffer = NULL;
size_t audioBytes = 0;
static const char* micLabels[2] = {"PDM", "I2S"};
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define psramMax (ONEMEG * 6)
#else
#define psramMax (ONEMEG * 2)
#endif
#ifdef ISCAM
bool AudActive = false;
static File wavFile;
#endif
#ifdef ISVC
uint8_t* recAudioBuffer = NULL;
size_t recAudioBytes = 0;
#endif
static uint8_t wavHeader[WAV_HDR_LEN] = {
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
};
void applyVolume() {
  int8_t adjVol = ampVol * 2;
#ifdef ISVC
  adjVol = checkPotVol(adjVol);
#endif
  if (adjVol) {
    adjVol = adjVol > 5 ? adjVol - 5 : adjVol - 7;
    for (int i = 0; i < DMA_BUFF_LEN; i++) {
      sampleBuffer[i] = adjVol < 0 ? sampleBuffer[i] / abs(adjVol) : constrain((int32_t)sampleBuffer[i] * adjVol, SHRT_MIN, SHRT_MAX);
    }
  }
}
static bool setupMic() {
  bool res;
  if (I2Smic) {
    I2Sstd.setPins(micSckPin, micSWsPin, mampSdIo, micSdPin, -1);
    res = I2Sstd.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  } else {
    I2Spdm.setPinsPdmRx(micSWsPin, micSdPin);
    res = I2Spdm.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  }
  return res;
}
static bool setupAmp() {
  bool res = true;
  if (!micUse || !I2Smic) {
    I2Sstd.setPins(mampBckIo, mampSwsIo, mampSdIo, -1, -1);
    res = I2Sstd.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  }
  return res;
}
void closeI2S() {
  I2Sstd.end();
  I2Spdm.end();
}
static void applyMicGain(size_t bytesRead) {
  uint8_t gainFactor = pow(2, micGain - MIC_GAIN_CENTER);
  for (int i = 0; i < bytesRead / sampleWidth; i++) {
    sampleBuffer[i] = constrain(sampleBuffer[i] * gainFactor, SHRT_MIN, SHRT_MAX);
  }
}
static size_t espMicInput() {
  size_t bytesRead = 0;
  if (micUse) {
    bytesRead = I2Smic ? I2Sstd.readBytes((char*)sampleBuffer, sampleBytes) : I2Spdm.readBytes((char*)sampleBuffer, sampleBytes);
    applyMicGain(bytesRead);
  }
  return bytesRead;
}
size_t updateWavHeader() {
  uint32_t dataBytes = totalSamples * sampleWidth;
  uint32_t wavFileSize = dataBytes ? dataBytes + WAV_HDR_LEN - 8 : 0;
  memcpy(wavHeader+4, &wavFileSize, 4);
  memcpy(wavHeader+24, &SAMPLE_RATE, 4);
  uint32_t byteRate = SAMPLE_RATE * sampleWidth;
  memcpy(wavHeader+28, &byteRate, 4);
  memcpy(wavHeader+WAV_HDR_LEN-4, &dataBytes, 4);
  memcpy(audioBuffer, wavHeader, WAV_HDR_LEN);
  return dataBytes;
}
#ifdef ISVC
#if !INCLUDE_RTSP
bool rtspAudio = false;
#endif
static size_t micInput() {
  size_t bytesRead = (micRem) ? wsBufferLen : espMicInput();
  if (bytesRead && micRem) {
    memcpy(sampleBuffer, wsBuffer, bytesRead);
    wsBufferLen = 0;
    applyMicGain(bytesRead);
  } else if (micRem) delay(20);
  return bytesRead;
}
void browserMicInput(uint8_t* wsMsg, size_t wsMsgLen) {
  if (micRem && !wsBufferLen) {
    wsBufferLen = wsMsgLen;
    memcpy(wsBuffer, wsMsg, wsMsgLen);
  }
}
static void ampOutput(size_t bytesRead = sampleBytes) {
  applyFilters();
  if (spkrRem) wsAsyncSendBinary((uint8_t*)sampleBuffer, bytesRead);
  else if (ampUse) I2Sstd.write((uint8_t*)sampleBuffer, bytesRead);
  if (!audioBytes) {
    memcpy(audioBuffer, sampleBuffer, bytesRead);
    audioBytes = bytesRead;
  }
  displayAudioLed(sampleBuffer[0]);
}
static void passThru() {
  size_t bytesRead = micInput();
  if (bytesRead) ampOutput(bytesRead);
}
static void makeRecording() {
  if (psramFound()) {
    LOG_INF("Запись...");
    recAudioBytes = WAV_HDR_LEN;
    wsBufferLen = 0;
    while (recAudioBytes < psramMax) {
      size_t bytesRead = micInput();
      if (bytesRead) {
        memcpy(recAudioBuffer + recAudioBytes, sampleBuffer, bytesRead);
        recAudioBytes += bytesRead;
      }
      if (stopAudio) break;
    }
    if (!stopAudio) wsJsonSend("stopRec", "1");
    totalSamples = (recAudioBytes  - WAV_HDR_LEN) / sampleWidth;
    LOG_INF("%s запись сэмплов %d", stopAudio ? "Stopped" : "Finished",  totalSamples);
    stopAudio = true;
  } else LOG_WRN("PSRAM необходим для записи и воспроизведения");
}
static void playRecording() {
  if (psramFound()) {
    LOG_INF("Воспроизводится семплов: %d, начальная громкость: %d.", totalSamples, ampVol);
    for (int i = WAV_HDR_LEN; i < totalSamples * sampleWidth; i += sampleBytes) {
      memcpy(sampleBuffer, recAudioBuffer+i, sampleBytes);
      ampOutput();
      if (stopAudio) break;
    }
    if (!stopAudio) wsJsonSend("stopPlay", "1");
    LOG_INF("%s воспроизведение сэмплов %d", stopAudio ? "Stopped" : "Finished", totalSamples);
    stopAudio = true;
  } else LOG_WRN("PSRAM необходим для записи и воспроизведения");
}
static void VCactions() {
  stopAudio = false;
  closeI2S();
  prepAudio();
  setupFilters();
  switch (THIS_ACTION) {
    case RECORD_ACTION:
      if (micRem) wsAsyncSendText("#M1");
      if (micUse || micRem) makeRecording();
    break;
    case PLAY_ACTION:
      if (ampUse || spkrRem || rtspAudio) playRecording();
    break;
    case PASS_ACTION:
      if (ampUse || spkrRem || rtspAudio) {
        if (micRem) wsAsyncSendText("#M1");
        LOG_INF("Passthru начался");
        wsBufferLen = 0;
        while (!stopAudio) passThru();
        LOG_INF("Пасстру остановлен");
      }
    break;
    default:
    break;
  }
  displayAudioLed(0);
  xSemaphoreGive(audioSemaphore);
}
#endif
#ifdef ISCAM
void browserMicInput(uint8_t* wsMsg, size_t wsMsgLen) {
  if (micRem && !wsBufferLen) {
    wsBufferLen = wsMsgLen;
    memcpy(wsBuffer, wsMsg, wsMsgLen);
    int8_t adjVol = ampVol * 2;
    if (adjVol) {
      adjVol = adjVol > 5 ? adjVol - 5 : adjVol - 7;
      int16_t* wsPtr = (int16_t*) wsBuffer;
      for (int i = 0; i < wsBufferLen / sizeof(int16_t); i++) {
        wsPtr[i] = adjVol < 0 ? wsPtr[i] / abs(adjVol) : constrain((int32_t)wsPtr[i] * adjVol, SHRT_MIN, SHRT_MAX);
      }
    }
    I2Sstd.write(wsBuffer, wsBufferLen);
    wsBufferLen = 0;
  }
}
void startAudioRecord() {
  if (micUse && micGain) {
      wavFile = STORAGE.open(WAVTEMP, FILE_WRITE);
      wavFile.write(wavHeader, WAV_HDR_LEN);
      micRecording = true;
      totalSamples = 0;
  } else {
    micRecording = false;
    LOG_WRN("Микрофон ESP не определен или микрофон выключен");
  }
}
void finishAudioRecord(bool isValid) {
  if (micRecording) {
    micRecording = false;
    if (isValid) {
      size_t dataBytes = updateWavHeader();
      wavFile.seek(0, SeekSet);
      wavFile.write(wavHeader, WAV_HDR_LEN);
      wavFile.close();
      LOG_INF("Записано %d образцов аудио с коэффициентом усиления %i.", totalSamples, micGain - MIC_GAIN_CENTER);
      LOG_INF("Сохранено %s на SD для %s.", fmtSize(dataBytes + WAV_HDR_LEN), WAVTEMP);
    }
  }
}
static void camActions() {
  while (true) {
    size_t bytesRead = 0;
    if (micRecording || !audioBytes || spkrRem) bytesRead = espMicInput();
    if (bytesRead) {
      if (micRecording) {
        wavFile.write((uint8_t*)sampleBuffer, bytesRead);
        totalSamples += bytesRead / sampleWidth;
      }
      if (!audioBytes) {
        memcpy(audioBuffer, sampleBuffer, bytesRead);
        audioBytes = bytesRead;
      }
      if (spkrRem) wsAsyncSendBinary((uint8_t*)sampleBuffer, bytesRead);
    } else delay(20);
  }
}
#endif
void setI2Schan(int whichChan) {
  if (whichChan) {
    MIC_CHAN = I2S_NUM_1;
    AMP_CHAN = I2S_NUM_0;
  } else {
    MIC_CHAN = I2S_NUM_0;
    AMP_CHAN = I2S_NUM_1;
  }
}
static void predefPins() {
char audPin[3];
#if defined(I2S_SD)
  sprintf(audPin, "%d", I2S_SD);
  updateStatus("micSdPin", audPin);
  sprintf(audPin, "%d", I2S_WS);
  updateStatus("micSWsPin", audPin);
  sprintf(audPin, "%d", I2S_SCK);
  updateStatus("micSckPin", audPin);
#endif
#if defined(I2S_BCLK)
  sprintf(audPin, "%d", I2S_BCLK);
  updateStatus("mampBckIo", audPin);
  sprintf(audPin, "%d", I2S_LRCLK);
  updateStatus("mampSwsIo", audPin);
  sprintf(audPin, "%d", I2S_DIN);
  updateStatus("mampSdIo", audPin);
#endif
  I2Smic = micSckPin == -1 ? false : true;
#ifdef CONFIG_IDF_TARGET_ESP32S3
  MIC_CHAN = I2S_NUM_0;
#endif
}
static void audioTask(void* parameter) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef ISCAM
    camActions();
#endif
#ifdef ISVC
    VCactions();
#endif
  }
  vTaskDelete(NULL);
}
void prepAudio() {
#ifdef ISCAM
  predefPins();
#endif
  if (MIC_CHAN == I2S_NUM_1 && !I2Smic) LOG_WRN("На I2S_NUM_1 поддерживаются только устройства I2S.");
  else {
    if (micSdPin <= 0) LOG_WRN("Контакты микрофона не определены");
    else {
      micUse = setupMic();
      if (micUse) LOG_INF("Запись звука доступна с помощью микрофона %s на I2S%i с усилением %d.", micLabels[I2Smic], MIC_CHAN, micGain);
      else LOG_WRN("Невозможно запустить микрофон ESP");
    }
    if (mampSdIo <= 0) LOG_WRN("Контакты усилителя не определены");
    else {
      ampUse = setupAmp();
      if (ampUse) LOG_INF("Выход динамика доступен при использовании усилителя I2S на I2S%i с громкостью %d.", AMP_CHAN, ampVol);
      else LOG_WRN("Не могу запустить усилитель ESP.");
    }
  }
  if (sampleBuffer == NULL) sampleBuffer = (int16_t*)malloc(sampleBytes);
  if (wsBuffer == NULL) wsBuffer = (uint8_t*)malloc(MAX_PAYLOAD_LEN);
  if (audioBuffer == NULL && psramFound()) audioBuffer = (uint8_t*)ps_malloc(sampleBytes);
#ifdef ISVC
  if (recAudioBuffer == NULL && psramFound()) recAudioBuffer = (uint8_t*)ps_malloc(psramMax + (sizeof(int16_t) * DMA_BUFF_LEN));
  if (!micUse && !ampUse) LOG_WRN("Можно использовать только микрофон и динамик браузера.");
#endif
#ifdef ISCAM
  wsBufferLen = 0;
  if (!micUse) return;
#endif
  if (audioHandle == NULL) xTaskCreate(audioTask, "audioTask", AUDIO_STACK_SIZE, NULL, AUDIO_PRI, &audioHandle);
#ifdef ISCAM
  xTaskNotifyGive(audioHandle);
#endif
  debugMemory("prepAudio");
}
#endif