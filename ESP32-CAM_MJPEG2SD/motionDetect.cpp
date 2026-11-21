#include "appGlobals.h"
#if INCLUDE_TINYML
#include TINY_ML_LIB
#endif
#define RESIZE_DIM 96
#define RESIZE_DIM_SQ (RESIZE_DIM * RESIZE_DIM)
#define INACTIVE_COLOR 96
#define JPEG_QUAL 80
int detectMotionFrames = 5;
int detectNightFrames = 10;
int detectNumBands = 10;
int detectStartBand = 3;
int detectEndBand = 8;
int detectChangeThreshold = 15;
uint8_t colorDepth;
static size_t stride;
bool mlUse = false;
float mlProbability = 0.8;
uint8_t lightLevel;
uint8_t nightSwitch = 20;
float motionVal = 8.0;
uint8_t* motionJpeg = NULL;
size_t motionJpegLen = 0;
static uint8_t* currBuff = NULL;
#ifndef CONFIG_IDF_TARGET_ESP32C3
#if INCLUDE_NEW_JPG
#include <esp_jpeg_dec.h>
#include <esp_jpeg_enc.h>
struct esp_jpeg_stream {
    jpeg_dec_handle_t       jpeg_dec;
    jpeg_dec_io_t*          jpeg_io;
    jpeg_dec_header_info_t* out_info;
    jpeg_pixel_format_t     output_type;
};
typedef struct esp_jpeg_stream* esp_jpeg_stream_handle_t;
static void jpgReduce(int inWidth, int inHeight, uint8_t downsize, int* outWidth, int* outHeight);
static bool jpg2rgbOpen(esp_jpeg_stream_handle_t jpegHandle, uint16_t width, uint16_t height);
static bool jpg2rgb(esp_jpeg_stream_handle_t jpegHandle, uint8_t* inputBuf, int inputLen, uint8_t* outputBuf);
static bool jpg2rgbClose(esp_jpeg_stream_handle_t jpegHandle);
static size_t rgb2jpg(uint8_t* rgb888, int width, int height, int qual, uint8_t* outputBuf);
#else
static bool jpg2rgb(const uint8_t* src, size_t src_len, uint8_t* out, uint8_t scale);
#endif
bool isNight(uint8_t nightSwitch) {
  static bool nightTime = false;
  static uint16_t nightCnt = 0;
  if (nightTime) {
    if (lightLevel > nightSwitch) {
      nightCnt--;
      if (nightCnt == 0) {
        nightTime = false;
        LOG_INF("Дневное время");
      }
    }
  } else {
    if (lightLevel < nightSwitch) {
      nightCnt++;
      if (nightCnt > detectNightFrames) {
        nightTime = true;
        LOG_INF("Ночное время");
      }
    }
  }
  return nightTime;
}
static void rescaleImage(const uint8_t* input, int inputWidth, int inputHeight, uint8_t* output, int outputWidth, int outputHeight) {
  float xRatio = (float)inputWidth / (float)outputWidth;
  float yRatio = (float)inputHeight / (float)outputHeight;
  for (int i = 0; i < outputHeight; ++i) {
    for (int j = 0; j < outputWidth; ++j) {
      int xL = (int)floor(xRatio * j);
      int yL = (int)floor(yRatio * i);
      int xH = (int)ceil(xRatio * j);
      int yH = (int)ceil(yRatio * i);
      float xWeight = xRatio * j - xL;
      float yWeight = yRatio * i - yL;
      for (int channel = 0; channel < colorDepth; ++channel) {
        uint8_t a = input[(yL * inputWidth + xL) * colorDepth + channel];
        uint8_t b = input[(yL * inputWidth + xH) * colorDepth + channel];
        uint8_t c = input[(yH * inputWidth + xL) * colorDepth + channel];
        uint8_t d = input[(yH * inputWidth + xH) * colorDepth + channel];
        float pixel = a * (1 - xWeight) * (1 - yWeight) + b * xWeight * (1 - yWeight)
                    + c * yWeight * (1 - xWeight) + d * xWeight * yWeight;
        output[(i * outputWidth + j) * colorDepth + channel] = (uint8_t)pixel;
      }
    }
  }
}
static void rgbToGray(uint8_t* buffer, int width, int height) {
  for (int i = 0; i < width * height; ++i) {
    int index = i * 3;
    buffer[i] = (uint8_t)(((77 * buffer[index]) + (150 * buffer[index + 1]) + (29 * buffer[index + 2])) >> 8);
  }
}
#if INCLUDE_TINYML
static int getImageData(size_t offset, size_t length, float *out_ptr) {
  size_t pixelPtr = offset * colorDepth;
  size_t out_ptr_idx = 0;
  while (out_ptr_idx < length) {
    out_ptr[out_ptr_idx++] = (colorDepth == RGB888_BYTES)
      ? (float)((currBuff[pixelPtr] << 16) + (currBuff[pixelPtr + 1] << 8) + currBuff[pixelPtr + 2])
      : (float)((currBuff[pixelPtr] << 16) + (currBuff[pixelPtr] << 8) + currBuff[pixelPtr]);
    pixelPtr += colorDepth;
  }
  return 0;
}
static bool tinyMLclassify() {
  bool out = false;
  uint32_t dTime = millis();
  if (RESIZE_DIM != EI_CLASSIFIER_INPUT_WIDTH) {
    uint8_t* tempBuff = (uint8_t*)ps_malloc(EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * colorDepth);
    rescaleImage(currBuff, RESIZE_DIM, RESIZE_DIM, tempBuff, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    memcpy(currBuff, tempBuff, EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * colorDepth);
    free(tempBuff);
  }
  signal_t features_signal;
  features_signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  features_signal.get_data = &getImageData;
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
  if (res == EI_IMPULSE_OK) {
    if (result.classification[0].value > mlProbability) {
      out = true;
      if (dbgVerbose) {
        LOG_VRB("Проблема: %0.2f, время: DSP %d мс, вывод %d мс, аномалия %d мс",
        result.classification[0].value, result.timing.dsp, result.timing.classification, result.timing.anomaly);
        char outcome[200] = {0};
        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
          sprintf(outcome + strlen(outcome), "%s: %.2f, ", ei_classifier_inferencing_categories[i], result.classification[i].value);
        LOG_VRB("Прогнозы – %s через %u мс", outcome, millis() - dTime);
      }
    }
  } else LOG_WRN("Не удалось запустить классификатор (%d).", res);
  return out;
}
#endif
bool checkMotion(camera_fb_t* fb, bool motionStatus, bool lightLevelOnly) {
  if (fsizePtr > FRAMESIZE_SXGA) return false;
  uint32_t dTime = millis();
  uint32_t lux = 0;
  static uint32_t motionCnt = 0;
  static uint8_t fsizePtrPrev = 255;
  static uint8_t scaling, downsize;
  static uint16_t reducer;
  static int sampleWidth = 0, sampleHeight = 0;
  static uint8_t* rgbBuf = (uint8_t*)heap_caps_aligned_calloc(16, 1, frameData[FRAMESIZE_SXGA].frameWidth * frameData[FRAMESIZE_SXGA].frameHeight * RGB888_BYTES / 8, MALLOC_CAP_SPIRAM);
 #if INCLUDE_NEW_JPG
  static struct esp_jpeg_stream jpegHandle = {0};
  static uint8_t* jpgBuf = (uint8_t*)ps_malloc(RESIZE_DIM_SQ * RGB888_BYTES);
#endif
  if (fsizePtr != fsizePtrPrev) {
    fsizePtrPrev = fsizePtr;
    scaling = frameData[fsizePtr].scaleFactor;
    reducer = frameData[fsizePtr].sampleRate;
    downsize = pow(2, scaling) * reducer;
    stride = (colorDepth == RGB888_BYTES) ? GRAYSCALE_BYTES : RGB888_BYTES;
    sampleWidth = frameData[fsizePtr].frameWidth / downsize;
    sampleHeight = frameData[fsizePtr].frameHeight / downsize;
#if INCLUDE_NEW_JPG
    jpg2rgbClose(&jpegHandle);
    jpgReduce(fb->width, fb->height, downsize, &sampleWidth, &sampleHeight);
    if (!jpg2rgbOpen(&jpegHandle, sampleWidth, sampleHeight)) return motionStatus;
#endif
  }
#if INCLUDE_NEW_JPG
  if (!jpg2rgb(&jpegHandle, fb->buf, fb->len, rgbBuf)) return motionStatus;
#else
  if (!jpg2rgb((uint8_t*)fb->buf, fb->len, rgbBuf, scaling)) return motionStatus;
#endif
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
  if (colorDepth == GRAYSCALE_BYTES) rgbToGray(rgbBuf, sampleWidth, sampleHeight);
#endif
  LOG_VRB("JPEG в растровое изображение с масштабированием %s, преобразование %u байт в %lums", colorDepth == RGB888_BYTES ? "color" : "grayscale", sampleWidth * sampleHeight * colorDepth, millis() - dTime);
  size_t resizeDimLen = RESIZE_DIM_SQ * colorDepth;
  if (motionJpeg == NULL) motionJpeg = (uint8_t*)ps_malloc(32 * 1024);
  if (currBuff == NULL) currBuff = (uint8_t*)ps_malloc(RESIZE_DIM_SQ * RGB888_BYTES);
  static uint8_t* prevBuff = (uint8_t*)ps_malloc(RESIZE_DIM_SQ * RGB888_BYTES);
  static uint8_t* changeMap = (uint8_t*)ps_malloc(RESIZE_DIM_SQ * RGB888_BYTES);
  dTime = millis();
  rescaleImage(rgbBuf, sampleWidth, sampleHeight, currBuff, RESIZE_DIM, RESIZE_DIM);
  LOG_VRB("Масштабирование растрового изображения до %u байт в %lums", resizeDimLen, millis() - dTime);
  dTime = millis();
  int changeCount = 0;
  uint16_t startPixel = (RESIZE_DIM*(detectStartBand-1)/detectNumBands) * RESIZE_DIM * colorDepth;
  uint16_t endPixel = (RESIZE_DIM*(detectEndBand)/detectNumBands) * RESIZE_DIM * colorDepth;
  int moveThreshold = ((endPixel-startPixel)/colorDepth) * (11-motionVal)/100;
  for (int i = 0; i < resizeDimLen; i += colorDepth) {
    uint16_t currPix = 0, prevPix = 0;
    for (int j = 0; j < colorDepth; j++) {
      currPix += currBuff[i + j];
      prevPix += prevBuff[i + j];
    }
    currPix /= colorDepth;
    prevPix /= colorDepth;
    lux += currPix;
    uint8_t pixVal = 255;
    if (dbgMotion) for (int j = 0; j < RGB888_BYTES; j++) changeMap[(i * stride) + j] = currPix;
    if (abs((int)currPix - (int)prevPix) > detectChangeThreshold) {
      if (i > startPixel && i < endPixel) changeCount++;
      else pixVal = 80;
      if (dbgMotion) {
        changeMap[(i * stride) + 2] = pixVal;
        for (int j = 0; j < RGB888_BYTES - 1; j++) changeMap[(i * stride) + j] = 0;
      }
    }
  }
  lightLevel = (lux*100)/(RESIZE_DIM_SQ*255);
  nightTime = isNight(nightSwitch);
  memcpy(prevBuff, currBuff, resizeDimLen);
  LOG_VRB("Обнаружено изменений: %u, пороговое значение %u, уровень освещенности %u, в %lмкм.", changeCount, moveThreshold, lightLevel, millis() - dTime);
  if (lightLevelOnly) return false;
  if (dbgMotion) {
    if (!motionJpegLen) {
      dTime = millis();
#if INCLUDE_NEW_JPG
      motionJpegLen = rgb2jpg(changeMap, RESIZE_DIM, RESIZE_DIM, JPEG_QUAL, jpgBuf);
      if (motionJpegLen == 0) LOG_WRN("motionDetect: ошибка кодирования()");
      memcpy(motionJpeg, jpgBuf, motionJpegLen);
#else
      uint8_t* jpg_buf = NULL;
      if (!fmt2jpg(changeMap, resizeDimLen, RESIZE_DIM, RESIZE_DIM, PIXFORMAT_RGB888, JPEG_QUAL, &jpg_buf, &motionJpegLen))
        LOG_WRN("motionDetect: fmt2jpg() не удалось");
      memcpy(motionJpeg, jpg_buf, motionJpegLen);
      free(jpg_buf);
      jpg_buf = NULL;
#endif
      xSemaphoreGive(motionSemaphore);
      LOG_VRB("Создано ChangeMap JPEG %d байт в %lums.", motionJpegLen, millis() - dTime);
    }
  } else {
    dTime = millis();
    if (!nightTime && changeCount > moveThreshold) {
      LOG_VRB("### Обнаружено изменение");
      motionCnt++;
      if (!motionStatus && motionCnt >= detectMotionFrames) {
        LOG_VRB("***** Движение - СТАРТ");
        motionStatus = true;
#if INCLUDE_TINYML
        if (mlUse) if (!tinyMLclassify()) motionCnt = 0;
#endif
        if (motionCnt) notifyMotion(fb);
        dTime = millis();
#if INCLUDE_MQTT
        if (mqtt_active && motionCnt) {
          sprintf(jsonBuff, "{\"MOTION\":\"ON\",\"TIME\":\"%s\"}", esp_log_system_timestamp());
          mqttPublish(jsonBuff);
          mqttPublishPath("motion", "on");
#if INCLUDE_HASIO
          mqttPublishPath("cmd", "still");
#endif
        }
#endif
      }
    } else motionCnt = 0;
    if (motionStatus && !motionCnt) {
      LOG_VRB("***** Движение - СТОП");
      motionStatus = false;
#if INCLUDE_MQTT
      if (mqtt_active) {
        sprintf(jsonBuff, "{\"MOTION\":\"OFF\",\"TIME\":\"%s\"}", esp_log_system_timestamp());
        mqttPublish(jsonBuff);
        mqttPublishPath("motion", "off");
      }
#endif
    }
    if (motionStatus) LOG_VRB("*** Движение – текущие %u кадров", motionCnt);
  }
  if (dbgVerbose) checkMemory();
  LOG_VRB("============================");
  return nightTime ? false : motionStatus;
}
void notifyMotion(camera_fb_t* fb) {
#if INCLUDE_SMTP
  if (smtpUse) {
    keepFrame(fb);
    char subjectMsg[50];
    snprintf(subjectMsg, sizeof(subjectMsg) - 1, "from %s", hostName);
    emailAlert("Оповещение о движении", subjectMsg);
  }
#endif
#if INCLUDE_TGRAM
  if (tgramUse) keepFrame(fb);
#endif
}
#if INCLUDE_NEW_JPG
static void jpgReduce(int inWidth, int inHeight, uint8_t downsize, int* outWidth, int* outHeight) {
  uint8_t roundTo8 = 8;
  inWidth /= downsize;
  inHeight /= downsize;
  float aspectRatio = (float)(inWidth) / inHeight;
  auto roundUpToMultiple = [](int n, int m) {
    return ((n + m - 1) / m) * m;
  };
  int newLarger = inWidth;
  int newSmaller = inHeight;
  if (inWidth < inHeight) {
    newLarger = inHeight;
    newSmaller = inWidth;
  }
  newLarger = roundUpToMultiple(inWidth, roundTo8);
  newSmaller = (int)(ceil((float)newLarger / aspectRatio));
  newSmaller = roundUpToMultiple(newSmaller, roundTo8);
  *outWidth = newLarger;
  *outHeight = newSmaller;
  if (inWidth < inHeight) {
    *outWidth = newSmaller;
    *outHeight = newLarger;
  }
}
static bool jpg2rgbOpen(esp_jpeg_stream_handle_t jpegHandle, uint16_t width, uint16_t height) {
  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB888;
  config.rotate = JPEG_ROTATE_0D;
  config.scale.width = width;
  config.scale.height = height;
  jpegHandle->output_type = JPEG_PIXEL_FORMAT_RGB888;
  jpeg_error_t ret = jpeg_dec_open(&config, &jpegHandle->jpeg_dec);
  if (ret != JPEG_ERR_OK) {
    LOG_ERR("Невозможно создать дескриптор декодера JPEG: %d", ret);
    return false;
  }
  jpegHandle->jpeg_io = (jpeg_dec_io_t*)calloc(1, sizeof(jpeg_dec_io_t));
  if (jpegHandle->jpeg_io == NULL) {
    LOG_ERR("Недостаточно памяти для создания дескриптора ввода");
    jpg2rgbClose(jpegHandle);
    return false;
  }
  jpegHandle->out_info = (jpeg_dec_header_info_t*)calloc(1, sizeof(jpeg_dec_header_info_t));
  if (jpegHandle->out_info == NULL) {
    LOG_ERR("Недостаточно памяти для создания дескриптора вывода");
    jpg2rgbClose(jpegHandle);
    return false;
  }
  return true;
}
static bool jpg2rgb(esp_jpeg_stream_handle_t jpegHandle, uint8_t* inputBuf, int inputLen, uint8_t* outputBuf) {
  jpegHandle->jpeg_io->inbuf = inputBuf;
  jpegHandle->jpeg_io->inbuf_len = inputLen;
  jpeg_error_t ret = jpeg_dec_parse_header(jpegHandle->jpeg_dec, jpegHandle->jpeg_io, jpegHandle->out_info);
  if (ret != JPEG_ERR_OK) {
    LOG_ERR("Не удалось проанализировать заголовок JPEG: %d.", ret);
    return false;
  }
  jpegHandle->jpeg_io->outbuf = outputBuf;
  ret = jpeg_dec_process(jpegHandle->jpeg_dec, jpegHandle->jpeg_io);
  if (ret != JPEG_ERR_OK) {
    LOG_ERR("Не удалось декодировать JPEG: %d", ret);
    return false;
  }
  return true;
}
static bool jpg2rgbClose(esp_jpeg_stream_handle_t jpegHandle) {
  jpeg_error_t ret = jpeg_dec_close(jpegHandle->jpeg_dec);
  if (jpegHandle->jpeg_io) free(jpegHandle->jpeg_io);
  if (jpegHandle->out_info) free(jpegHandle->out_info);
  return ret == JPEG_ERR_OK ? true : false;
}
size_t rgb2jpg(uint8_t* rgb888, int width, int height, int qual, uint8_t* outputBuf) {
  static bool firstCall = true;
  static jpeg_enc_handle_t jpeg_enc = NULL;
  static int bufLen = width * height * RGB888_BYTES;
  jpeg_error_t ret = JPEG_ERR_OK;
  if (firstCall) {
    firstCall = false;
    jpeg_enc_config_t jpeg_enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
    jpeg_enc_cfg.width = width;
    jpeg_enc_cfg.height = height;
    jpeg_enc_cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_enc_cfg.subsampling = JPEG_SUBSAMPLE_420;
    jpeg_enc_cfg.quality = qual;
    jpeg_enc_cfg.rotate = JPEG_ROTATE_0D;
    jpeg_enc_cfg.task_enable = false;
    jpeg_enc_cfg.hfm_task_priority = 13;
    jpeg_enc_cfg.hfm_task_core = 1;
    ret = jpeg_enc_open(&jpeg_enc_cfg, &jpeg_enc);
    if (ret != JPEG_ERR_OK) {
      LOG_ERR("Не удалось открыть декодер: %d.");
      return 0;
    }
  }
  int jpgLen = 0;
  ret = jpeg_enc_process(jpeg_enc, rgb888, bufLen, outputBuf, bufLen, &jpgLen);
  if (ret != JPEG_ERR_OK) LOG_ERR("Не удалось закодировать: %d", ret);
  return (size_t)jpgLen;
}
#else
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
static uint8_t work[3100];
static bool jpg2rgb(const uint8_t* src, size_t src_len, uint8_t* out, uint8_t scale) {
  esp_jpeg_image_cfg_t jpeg_cfg = {
      .indata = (uint8_t *)src,
      .indata_size = src_len,
      .outbuf = out,
      .outbuf_size = UINT32_MAX,
      .out_format = JPEG_IMAGE_FORMAT_RGB888,
      .out_scale = (esp_jpeg_image_scale_t)scale,
      .flags = {.swap_color_bytes = 0},
      .advanced = {
        .working_buffer = work,
        .working_buffer_size = sizeof(work)
      }
  };
  esp_jpeg_image_output_t output_img = {};
  esp_err_t res = esp_jpeg_decode(&jpeg_cfg, &output_img);
  if (res != ESP_OK) LOG_WRN("Ошибка jpg2rgb: %s", espErrMsg(res));
  return (res == ESP_OK) ? true : false;
}
#else
typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t data_offset;
  const uint8_t *input;
  uint8_t *output;
} rgb_jpg_decoder;
static bool _rgb_write(void * arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
  rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
  if (!data){
    if (x == 0 && y == 0) {
      jpeg->width = w;
      jpeg->height = h;
    }
    return true;
  }
  size_t jw = jpeg->width*RGB888_BYTES;
  size_t t = y * jw;
  size_t b = t + (h * jw);
  size_t l = x * RGB888_BYTES;
  uint8_t *out = jpeg->output+jpeg->data_offset;
  uint8_t *o = out;
  size_t iy, ix;
  w *= RGB888_BYTES;
  for (iy=t; iy<b; iy+=jw) {
    o = out+(iy+l)/stride;
    for (ix=0; ix<w; ix+=RGB888_BYTES) {
      if (colorDepth == RGB888_BYTES) {
        o[ix] = data[ix+2];
        o[ix+1] = data[ix+1];
        o[ix+2] = data[ix];
      } else {
        uint16_t grayscale = (data[ix+2]+data[ix+1]+data[ix])/RGB888_BYTES;
        o[ix/RGB888_BYTES] = (uint8_t)grayscale;
      }
    }
    data+=w;
  }
  return true;
}
static unsigned int _jpg_read(void * arg, size_t index, uint8_t *buf, size_t len) {
  rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
  if (buf) memcpy(buf, jpeg->input + index, len);
  return len;
}
static bool jpg2rgb(const uint8_t* src, size_t src_len, uint8_t* out, uint8_t scale) {
  rgb_jpg_decoder jpeg;
  jpeg.width = 0;
  jpeg.height = 0;
  jpeg.input = src;
  jpeg.output = out;
  jpeg.data_offset = 0;
  esp_err_t res = esp_jpg_decode(src_len, (jpg_scale_t)scale, _jpg_read, _rgb_write, (void*)&jpeg);
  if (res != ESP_OK) LOG_WRN("Ошибка jpg2rgb: %s", espErrMsg(res));
  return (res == ESP_OK) ? true : false;
}
#endif
#endif
#else
bool isNight(uint8_t nightSwitch) {return false;}
#endif