#include "appGlobals.h"
void setup() {
  logSetup();
  LOG_INF("Selected board %s", CAM_BOARD);
  if (startStorage()) {
    if (loadConfig()) {
#ifndef AUXILIARY
      if (psramFound()) {
        if (ESP.getPsramSize() > 1 * ONEMEG) prepCam();
        else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Insufficient PSRAM for app: %s", fmtSize(ESP.getPsramSize()));
      } else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Need PSRAM to be enabled");
#else
      LOG_INF("AUXILIARY mode without camera");
#endif
    }
  }
#ifdef DEV_ONLY
  devSetup();
#endif
  startNetwork();
  if (startWebServer()) {
#ifndef AUXILIARY
    startSustainTasks();
#endif
#if INCLUDE_SMTP
    prepSMTP();
#endif
#if INCLUDE_FTP_HFS
    prepUpload();
#endif
#if INCLUDE_UART
    prepUart();
#endif
#if INCLUDE_PERIPH
    prepPeripherals();
  #if INCLUDE_MCPWM
    prepMotors();
  #endif
#endif
#if INCLUDE_AUDIO
    prepAudio();
#endif
#if INCLUDE_TGRAM
    prepTelegram();
#endif
#if INCLUDE_I2C
    prepI2C();
  #if INCLUDE_TELEM
    prepTelemetry();
  #endif
#endif
#if INCLUDE_PERIPH
    startHeartbeat();
#endif
#ifndef AUXILIARY
 #if INCLUDE_RTSP
    prepRTSP();
 #endif
    if (!prepRecording()) {
      snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Insufficient memory, remove optional features");
      LOG_WRN("%s", startupFailure);
    }
#endif
    checkMemory();
  }
}
void loop() {
  LOG_INF("=============== Total tasks: %u ===============\n", uxTaskGetNumberOfTasks() - 1);
  delay(1000);
  vTaskDelete(NULL);
}