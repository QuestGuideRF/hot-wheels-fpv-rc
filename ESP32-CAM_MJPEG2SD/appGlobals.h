#pragma once
#include "globals.h"
#if defined(CONFIG_IDF_TARGET_ESP32)
#define CAMERA_MODEL_AI_THINKER
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define CAMERA_MODEL_ESP32_S3_CAM
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define AUXILIARY
#define NO_SD
#endif
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32) && !defined(AUXILIARY)
#error "Необходимо выбрать плату ESP32 или ESP32S3 для камеры."
#endif
#define INCLUDE_FTP_HFS false
#define INCLUDE_TGRAM false
#define INCLUDE_AUDIO false
#define INCLUDE_PERIPH false
#define INCLUDE_SMTP false
#define INCLUDE_MQTT false
#define INCLUDE_HASIO false
#define INCLUDE_CERTS false
#define INCLUDE_UART false
#define INCLUDE_TELEM false
#define INCLUDE_WEBDAV false
#define INCLUDE_EXTHB false
#define INCLUDE_PGRAM false
#define INCLUDE_MCPWM false
#define INCLUDE_RTSP false
#define INCLUDE_DS18B20 false
#define INCLUDE_NEW_JPG false
#define INCLUDE_I2C false
#define USE_SSD1306 false
#define USE_BMx280 false
#define USE_MPU6050 false
#define USE_MPU9250 false
#define USE_DS3231 false
#define USE_LCD1602 false
#define INCLUDE_TINYML false
#define TINY_ML_LIB "your_impulse_edge_library.h"
#define ALLOW_SPACES false
#define HTTP_PORT 80
#define HTTPS_PORT 443
#define USE_IP6 false
#ifndef AUXILIARY
#include "esp_camera.h"
#endif
#include "camera_pins.h"
#define STATIC_IP_OCTAL "133"
#define DEBUG_MEM false
#define FLUSH_DELAY 0
#define DBG_ON false
#define DBG_LVL ESP_LOG_ERROR
#define DOT_MAX 50
#define HOSTNAME_GRP 99
#define APP_VER "10.8.5"
#if defined(AUXILIARY)
#define APP_NAME "ESP-CAM_AUX"
#define INDEX_PAGE_PATH DATA_DIR "/Auxil" HTML_EXT
#define USE_UARTTASK
#elif defined(SIDE_ALARM)
#define APP_NAME "ESP-CAM_SIDE"
#define INDEX_PAGE_PATH DATA_DIR "/SideAl" HTML_EXT
#define NO_SD
#else
#define APP_NAME "ESP-CAM_MJPEG"
#define INDEX_PAGE_PATH DATA_DIR "/MJPEG2SD" HTML_EXT
#endif
#define HTTP_CLIENTS 2
#define MAX_STREAMS 4
#define FILE_NAME_LEN 64
#define IN_FILE_NAME_LEN (FILE_NAME_LEN * 2)
#define JSON_BUFF_LEN (32 * 1024)
#define MAX_CONFIGS 220
#define MIN_RAM 8
#define MAX_RAM 4096
#define TLS_HEAP (64 * 1024)
#define WARN_HEAP (32 * 1024)
#define WARN_ALLOC (16 * 1024)
#define MAX_FRAME_WAIT 1200
#define RGB888_BYTES 3
#define GRAYSCALE_BYTES 1
#define EXTHB_LEN 64
#ifdef NO_SD
#define STORAGE LittleFS
#else
#define STORAGE SD_MMC
#endif
#define GITHUB_PATH "/s60sc/ESP32-CAM_MJPEG2SD/master"
#define RAMSIZE (1024 * 8)
#define CHUNKSIZE (1024 * 4)
#define ISCAM
#define CFG_VER 34
#define AVI_EXT "avi"
#define CSV_EXT "csv"
#define SRT_EXT "srt"
#define AVI_HEADER_LEN 310
#define CHUNK_HDR 8
#define WAVTEMP "/current.wav"
#define AVITEMP "/current.avi"
#define TLTEMP "/current.tl"
#define TELETEMP "/current.csv"
#define SRTTEMP "/current.srt"
#define DMA_BUFF_LEN 512
#define DMA_BUFF_CNT 4
#define MIC_GAIN_CENTER 3
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define SERVER_STACK_SIZE (1024 * 8)
#define DS18B20_STACK_SIZE (1024 * 2)
#else
#define SERVER_STACK_SIZE (1024 * 4)
#define DS18B20_STACK_SIZE (1024)
#endif
#define STICK_STACK_SIZE (1024 * 4)
#define BATT_STACK_SIZE (1024 * 2)
#define CAPTURE_STACK_SIZE (1024 * 4)
#define EMAIL_STACK_SIZE (1024 * 6)
#define FS_STACK_SIZE (1024 * 4)
#define LOG_STACK_SIZE (1024 * 3)
#define AUDIO_STACK_SIZE (1024 * 4)
#define MICREM_STACK_SIZE (1024 * 2)
#define MQTT_STACK_SIZE (1024 * 4)
#define PING_STACK_SIZE (1024 * 5)
#define PLAYBACK_STACK_SIZE (1024 * 2)
#define SERVO_STACK_SIZE (1024)
#define SUSTAIN_STACK_SIZE (1024 * 4)
#define TGRAM_STACK_SIZE (1024 * 6)
#define TELEM_STACK_SIZE (1024 * 4)
#define HB_STACK_SIZE (1024 * 2)
#define UART_STACK_SIZE (1024 * 2)
#define INTERCOM_STACK_SIZE (1024 * 2)
#define CAPTURE_PRI 6
#define SUSTAIN_PRI 5
#define HTTP_PRI 5
#define STICK_PRI 5
#define AUDIO_PRI 5
#define INTERCOM_PRI 5
#define PLAY_PRI 4
#define TELEM_PRI 3
#define TGRAM_PRI 1
#define EMAIL_PRI 1
#define FTP_PRI 1
#define LOG_PRI 1
#define MQTT_PRI 1
#define LED_PRI 1
#define SERVO_PRI 1
#define HB_PRI 1
#define UART_PRI 1
#define DS18B20_PRI 1
#define BATT_PRI 1
struct mjpegStruct {
  size_t buffLen;
  size_t buffOffset;
  size_t jpegSize;
};
struct fnameStruct {
  uint8_t recFPS;
  uint32_t recDuration;
  uint16_t frameCnt;
};
enum audioAction {NO_ACTION, UPDATE_CONFIG, RECORD_ACTION, PLAY_ACTION, PASS_ACTION, WAV_ACTION, STOP_ACTION};
enum stepperModel {BYJ_48, BIPOLAR_8mm};
void applyFilters();
void applyVolume();
void appShutdown();
void browserMicInput(uint8_t* wsMsg, size_t wsMsgLen);
void buildAviHdr(uint8_t FPS, uint8_t frameType, uint16_t frameCnt, bool isTL = false);
void buildAviIdx(size_t dataSize, bool isVid = true, bool isTL = false);
size_t buildSubtitle(int srtSeqNo, uint32_t sampleInterval);
void buzzerAlert(bool buzzerOn);
int8_t checkPotVol(int8_t adjVol);
bool checkSDFiles();
void currentStackUsage();
void displayAudioLed(int16_t audioSample);
void finalizeAviIndex(uint16_t frameCnt, bool isTL = false);
void finishAudioRecord(bool isValid);
float* getBMx280();
float* getMPU9250();
mjpegStruct getNextFrame(bool firstCall = false);
int getInputPeripheral(uint8_t cmd);
bool getPIRval();
bool haveWavFile(bool isTL = false);
bool identifyBMx();
void intercom();
bool isNight(uint8_t nightSwitch);
void micTaskStatus();
void motorSpeed(int speedVal, bool leftMotor = true);
void openSDfile(const char* streamFile);
void prepAudio();
void prepAviIndex(bool isTL = false);
bool prepCam();
bool prepRecording();
void prepTelemetry();
void prepMic();
void prepMotors();
void prepRTSP();
void prepUart();
void setCamPan(int panVal);
void setCamTilt(int tiltVal);
uint8_t setFPS(uint8_t val);
uint8_t setFPSlookup(uint8_t val);
void setInputPeripheral(uint8_t cmd, uint32_t controlVal);
void setLamp(uint8_t lampVal);
void setLightsRC(bool lightsOn);
bool setOutputPeripheral(uint8_t cmd, uint32_t rxValue);
void setSteering(int steerVal);
void setStepperPin(uint8_t pinNum, uint8_t pinPos);
void setStickTimer(bool restartTimer, uint32_t interval = 0);
bool shareI2C(int sdaShare, int sclShare);
void startAudioRecord();
void startHeartbeat();
void startSustainTasks();
bool startTelemetry();
void stepperDone();
void stepperRun(float RPM, float revFraction, bool _clockwise, stepperModel thisStepper);
void stopPlaying();
void stopSustainTask(int taskId);
void stopTelemetry(const char* fileName);
void storeSensorData(bool fromStream);
void takePhotos(bool startPhotos);
void trackSteeering(int controlVal, bool steering);
size_t updateWavHeader();
size_t writeAviIndex(byte* clientBuf, size_t buffSize, bool isTL = false);
bool writeUart(uint8_t cmd, uint32_t outputData);
size_t writeWavFile(byte* clientBuf, size_t buffSize);
#ifndef CONFIG_IDF_TARGET_ESP32C3
bool checkMotion(camera_fb_t* fb, bool motionStatus, bool lightLevelOnly = false);
void keepFrame(camera_fb_t* fb);
void notifyMotion(camera_fb_t* fb);
#endif
extern int moveStartChecks;
extern int moveStopSecs;
extern int detectMotionFrames;
extern int detectNightFrames;
extern int detectNumBands;
extern int detectStartBand;
extern int detectEndBand;
extern int detectChangeThreshold;
extern bool mlUse;
extern float mlProbability;
extern int tlSecsBetweenFrames;
extern int tlDurationMins;
extern int tlPlaybackFPS;
extern const char* appConfig;
extern bool autoUpload;
extern bool dbgMotion;
extern bool doPlayback;
extern bool doRecording;
extern bool forceRecord;
extern bool forcePlayback;
extern uint8_t FPS;
extern uint8_t fsizePtr;
extern bool isCapturing;
extern uint8_t lightLevel;
extern uint8_t lampLevel;
extern int micGain;
extern int8_t ampVol;
extern uint8_t minSeconds;
extern float motionVal;
extern uint8_t nightSwitch;
extern bool nightTime;
extern bool stopPlayback;
extern bool useMotion;
extern uint8_t colorDepth;
extern bool timeLapseOn;
extern int dashCamOn;
extern int maxFrames;
extern uint8_t xclkMhz;
extern char camModel[];
extern bool doKeepFrame;
extern int alertMax;
extern bool streamVid;
extern bool streamAud;
extern bool streamSrt;
extern uint8_t numStreams;
extern uint8_t vidStreams;
#ifndef CONFIG_IDF_TARGET_ESP32C3
extern framesize_t maxFS;
#endif
extern uint8_t iSDbuffer[];
extern uint8_t aviHeader[];
extern const uint8_t dcBuf[];
extern const uint8_t wbBuf[];
extern byte* streamBuffer[];
extern size_t streamBufferSize[];
extern uint8_t* motionJpeg;
extern size_t motionJpegLen;
extern uint8_t* audioBuffer;
extern size_t audioBytes;
extern char srtBuffer[];
extern size_t srtBytes;
extern size_t maxFrameBuffSize;
extern size_t maxAlertBuffSize;
extern bool useUart;
extern int uartTxdPin;
extern int uartRxdPin;
extern bool pirUse;
extern bool lampAuto;
extern bool lampNight;
extern int lampType;
extern bool voltUse;
extern bool wakeUse;
extern bool buzzerUse;
extern int buzzerPin;
extern int buzzerDuration;
extern int relayPin;
extern bool relayMode;
extern int pirPin;
extern int lampPin;
extern int lightsPin;
extern bool teleUse;
extern int srtInterval;
extern int servoPanPin;
extern int servoTiltPin;
extern int ds18b20Pin;
extern int voltPin;
extern bool AudActive;
extern int micSckPin;
extern int micSWsPin;
extern int micSdPin;
extern bool micRem;
extern bool spkrRem;
extern int mampBckIo;
extern int mampSwsIo;
extern int mampSdIo;
extern volatile bool stopAudio;
extern volatile audioAction THIS_ACTION;
extern uint32_t SAMPLE_RATE;
extern int servoDelay;
extern int servoMinAngle;
extern int servoMaxAngle;
extern int servoMinPulseWidth;
extern int servoMaxPulseWidth;
extern int servoCenter;
extern bool SVactive;
extern int voltDivider;
extern float voltLow;
extern int voltInterval;
extern bool stepperUse;
extern uint8_t stepINpins[];
extern bool useBDC;
extern int motorRevPin;
extern int motorFwdPin;
extern int motorRevPinR;
extern int motorFwdPinR;
extern bool trackSteer;
extern int servoSteerPin;
extern int lightsRCpin;
extern char AuxIP[];
extern int pwmFreq;
extern int maxSteerAngle;
extern int maxTurnSpeed;
extern int maxDutyCycle;
extern int minDutyCycle;
extern bool allowReverse;
extern bool autoControl;
extern int waitTime;
extern int heartbeatRC;
extern bool stickUse;
extern int stickzPushPin;
extern int stickXpin;
extern int stickYpin;
extern bool external_heartbeat_active;
extern char external_heartbeat_domain[];
extern char external_heartbeat_uri[];
extern int external_heartbeat_port;
extern char external_heartbeat_token[];
extern bool PGactive;
extern bool clockWise;
extern uint8_t timeForFocus;
extern uint8_t timeForPhoto;
extern int pinShutter;
extern int pinFocus;
extern uint8_t photosDone;
extern float gearing;
extern uint8_t numberOfPhotos;
extern float tRPM;
extern bool extCam;
extern int quality;
extern bool rtspVideo;
extern bool rtspAudio;
extern bool rtspSubtitles;
extern int rtspPort;
extern uint16_t rtpVideoPort;
extern uint16_t rtpAudioPort;
extern uint16_t rtpSubtitlesPort;
extern char RTP_ip[];
extern uint8_t rtspMaxClients;
extern uint8_t rtpTTL;
extern char RTSP_Name[];
extern char RTSP_Pass[];
extern TaskHandle_t battHandle;
extern TaskHandle_t captureHandle;
extern TaskHandle_t DS18B20handle;
extern TaskHandle_t emailHandle;
extern TaskHandle_t fsHandle;
extern TaskHandle_t logHandle;
extern TaskHandle_t mqttTaskHandle;
extern TaskHandle_t playbackHandle;
extern esp_ping_handle_t pingHandle;
extern TaskHandle_t servoHandle;
extern TaskHandle_t stickHandle;
extern TaskHandle_t sustainHandle[];
extern TaskHandle_t telegramHandle;
extern TaskHandle_t telemetryHandle;
extern TaskHandle_t uartRxHandle;
extern TaskHandle_t audioHandle;
extern SemaphoreHandle_t frameSemaphore[];
extern SemaphoreHandle_t motionSemaphore;
struct frameStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
  const uint8_t scaleFactor;
  const uint8_t sampleRate;
};
const frameStruct frameData[] = {
  {"96X96", 96, 96, 30, 1, 1},
  {"QQVGA", 160, 120, 30, 1, 1},
  {"128X128", 128, 128, 30, 1, 1},
  {"QCIF", 176, 144, 30, 1, 1},
  {"HQVGA", 240, 176, 30, 2, 1},
  {"240X240", 240, 240, 30, 2, 1},
  {"QVGA", 320, 240, 30, 2, 1},
  {"320X320", 320, 320, 30, 2, 1},
  {"CIF", 400, 296, 30, 2, 1},
  {"HVGA", 480, 320, 30, 2, 1},
  {"VGA", 640, 480, 20, 3, 1},
  {"SVGA", 800, 600, 20, 3, 1},
  {"XGA", 1024, 768, 5, 3, 1},
  {"HD", 1280, 720, 5, 3, 1},
  {"SXGA", 1280, 1024, 5, 3, 1},
  {"UXGA", 1600, 1200, 5, 4, 1},
  {"FHD", 1920, 1080, 5, 3, 1},
  {"P_HD", 720, 1280, 5, 3, 1},
  {"P_3MP", 864, 1536, 5, 3, 1},
  {"QXGA", 2048, 1536, 5, 4, 1},
  {"QHD", 2560, 1440, 5, 4, 1},
  {"WQXGA", 2560, 1600, 5, 4, 1},
  {"P_FHD", 1080, 1920, 5, 4, 1},
  {"QSXGA", 2560, 1920, 4, 4, 1},
  {"5MP", 2592, 1944, 4, 4, 1}
};