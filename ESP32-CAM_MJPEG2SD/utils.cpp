#include "appGlobals.h"
bool dbgVerbose = false;
bool timeSynchronized = false;
bool monitorOpen = true;
bool dataFilesChecked = false;
char startupFailure[SF_LEN] = {0};
size_t alertBufferSize = 0;
size_t maxAlertBuffSize = 32 * 1024;
byte* alertBuffer = NULL;
RTC_NOINIT_ATTR uint32_t crashLoop;
RTC_NOINIT_ATTR char brownoutStatus;
static void initBrownout(void);
static void printPartitionTable();
int wakePin;
int wakeLevel;
bool wakeUse = false;
char* jsonBuff = NULL;
char portFwd[6] = "";
#include <esp_task_wdt.h>
char hostName[MAX_HOST_LEN] = "";
char ST_SSID[MAX_HOST_LEN]  = "";
char ST_Pass[MAX_PWD_LEN] = "";
char ST_ip[MAX_IP_LEN]  = "";
char ST_sn[MAX_IP_LEN]  = "";
char ST_gw[MAX_IP_LEN]  = "";
char ST_ns1[MAX_IP_LEN] = "";
char ST_ns2[MAX_IP_LEN] = "";
char AP_SSID[MAX_HOST_LEN] = "";
char AP_Pass[MAX_PWD_LEN] = "";
char AP_ip[MAX_IP_LEN]  = "";
char AP_sn[MAX_IP_LEN]  = "";
char AP_gw[MAX_IP_LEN]  = "";
int ethCS = -1;
int ethInt = -1;
int ethRst = -1;
int ethSclk = -1;
int ethMiso = -1;
int ethMosi = -1;
char Auth_Name[MAX_HOST_LEN] = "";
char Auth_Pass[MAX_PWD_LEN] = "";
int responseTimeoutSecs = 10;
bool allowAP = true;
uint32_t wifiTimeoutSecs = 30;
static bool APstarted = false;
esp_ping_handle_t pingHandle = NULL;
bool usePing = true;
static void startPing();
static void printGpioInfo();
static void boardInfo();
int netMode = 0;
#define ETH_PHY_ADDR  0
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN
static void setupMdnsHost() {
  char mdnsName[MAX_IP_LEN];
  snprintf(mdnsName, MAX_IP_LEN, "%.*s", MAX_IP_LEN - 1, hostName);
  if (MDNS.begin(mdnsName)) {
    useHttps ? MDNS.addService("https", "tcp", HTTPS_PORT) : MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("ws", "udp", 83);
    MDNS.addService("ftp", "tcp", 21);
    LOG_INF("Служба mDNS: http%s://%s.local", useHttps ? "s" : "", mdnsName);
  } else LOG_WRN("Хост mDNS: %s Ошибка.", mdnsName);
  debugMemory("setupMdnsHost");
}
static const char* wifiStatusStr(wl_status_t wlStat) {
  switch (wlStat) {
    case WL_NO_SHIELD: return "Wi-Fi не инициализирован";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "недоступен, используйте точку доступа";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "невозможно подключиться";
    case WL_STOPPED: return "Wi-Fi остановился";
    default: return "Неверный статус Wi-Fi.";
  }
}
const char* getEncType(int ssidIndex) {
  switch (WiFi.encryptionType(ssidIndex)) {
    case (WIFI_AUTH_OPEN): return "Open";
    case (WIFI_AUTH_WEP): return "WEP";
    case (WIFI_AUTH_WPA_PSK): return "WPA_PSK";
    case (WIFI_AUTH_WPA2_PSK): return "WPA2_PSK";
    case (WIFI_AUTH_WPA_WPA2_PSK): return "WPA_WPA2_PSK";
    case (WIFI_AUTH_WPA2_ENTERPRISE): return "WPA2_ENTERPRISE";
    case (WIFI_AUTH_MAX): return "AUTH_MAX";
    default: return "Нет в списке";
  }
}
static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_READY: break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE: break;
    case ARDUINO_EVENT_WIFI_STA_START: LOG_INF("Станция Wi-Fi запущена и подключается к: %s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_STA_STOP: LOG_INF("Станция Wi-Fi остановлена ​​%s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_AP_START: {
      if (strlen(AP_SSID) && !strcmp(WiFi.AP.SSID().c_str(), AP_SSID)) {
        LOG_INF("SSID точки доступа Wi-Fi: %s запущен, для подключения используйте http%s://%s", WiFi.AP.SSID().c_str(), useHttps ? "s" : "", WiFi.AP.localIP().toString().c_str());
        APstarted = true;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STOP: {
      if (!strcmp(WiFi.AP.SSID().c_str(), AP_SSID)) {
        LOG_INF("Точка доступа Wi-Fi остановлена: %s", AP_SSID);
        APstarted = false;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: LOG_INF("IP-адрес станции Wi-Fi. Для подключения используйте «%s://%s».", useHttps ? "https" : "http", WiFi.STA.localIP().toString().c_str()); break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: LOG_INF("Станция Wi-Fi потеряла IP"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: LOG_INF("Подключение станции Wi-Fi к %s с использованием имени хоста: %s", ST_SSID, hostName); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: LOG_INF("Станция Wi-Fi отключена"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: LOG_INF("Подключение клиента Wi-Fi AP"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: LOG_INF("Отключение клиента точки доступа Wi-Fi"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6: LOG_INF("AP-интерфейс V6. Предпочтителен IP-адрес."); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6: LOG_INF("Интерфейс станции: IP-адрес V6 является предпочтительным."); break;
    case ARDUINO_EVENT_ETH_START: LOG_INF("Ethernet запущен, скорость %uМГц", ETH.linkSpeed()); break;
    case ARDUINO_EVENT_ETH_CONNECTED: LOG_INF("Ethernet подключен, MAC: %s", ETH.macAddress().c_str()); break;
    case ARDUINO_EVENT_ETH_STOP: LOG_INF("Ethernet остановлен"); break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      LOG_INF("IP-адрес Ethernet, для подключения используйте «%s://%s».", useHttps ? "https" : "http", ETH.localIP().toString().c_str());
      if (netMode == 2) WiFi.AP.enableNAPT(true);
      break;
    }
    case ARDUINO_EVENT_ETH_DISCONNECTED: {
      LOG_INF("Ethernet отключен");
      if (netMode == 2) WiFi.AP.enableNAPT(false);
      break;
    }
    case ARDUINO_EVENT_ETH_LOST_IP: {
      LOG_INF("Ethernet потерял IP");
      if (netMode == 2) WiFi.AP.enableNAPT(false);
      break;
    }
    default: LOG_WRN("Необработанное сетевое событие %d", event); break;
  }
}
static void setWifiAP() {
  if (!APstarted) {
    WiFi.AP.begin();
    if (strlen(AP_ip) > 1) {
      LOG_INF("Установите статический IP-адрес точки доступа: %s, %s, %s", AP_ip, AP_gw, AP_sn);
      IPAddress _ip, _gw, _sn, _ns1, _ns2;
      _ip.fromString(AP_ip);
      _gw.fromString(AP_gw);
      _sn.fromString(AP_sn);
      WiFi.AP.config(_ip, _gw, _sn);
    }
    WiFi.AP.create(AP_SSID, AP_Pass);
    debugMemory("setWifiAP");
  }
}
static void setWifiSTA() {
  if (strlen(ST_ip) > 1) {
    IPAddress _ip, _gw, _sn, _ns1, _ns2;
    if (!_ip.fromString(ST_ip)) LOG_WRN("Не удалось проанализировать IP-адрес: %s.", ST_ip);
    else {
      _ip.fromString(ST_ip);
      _gw.fromString(ST_gw);
      _sn.fromString(ST_sn);
      _ns1.fromString(ST_ns1);
      _ns2.fromString(ST_ns2);
      WiFi.STA.config(_ip, _gw, _sn, _ns1);
      LOG_INF("Станция Wi-Fi установила статический IP-адрес");
    }
  } else LOG_INF("IP-адрес станции Wi-Fi от DHCP");
  WiFi.STA.enableIPv6(USE_IP6);
  WiFi.STA.begin();
  WiFi.STA.connect(ST_SSID, ST_Pass);
  debugMemory("setWifiSTA");
}
static void predefEthPins() {
#if defined(ETH_CS)
  char ethPin[3];
  sprintf(ethPin, "%d", ETH_CS);
  updateStatus("ethCS", ethPin);
  sprintf(ethPin, "%d", ETH_INT);
  updateStatus("ethInt", ethPin);
  sprintf(ethPin, "%d", ETH_RST);
  updateStatus("ethRst", ethPin);
  sprintf(ethPin, "%d", ETH_SCLK);
  updateStatus("ethSclk", ethPin);
  sprintf(ethPin, "%d", ETH_MISO);
  updateStatus("ethMiso", ethPin);
  sprintf(ethPin, "%d", ETH_MOSI);
  updateStatus("ethMosi", ethPin);
#endif
}
static bool startEth() {
  if (ethCS != -1) {
    ETH.end();
#if CONFIG_IDF_TARGET_ESP32S3
    if (!ETH.begin(ETH_PHY_W5500,
                   ETH_PHY_ADDR_AUTO,
                   ethCS,
                   ethInt,
                   ethRst,
                   SPI2_HOST,
                   ethSclk,
                   ethMiso,
                   ethMosi,
                   ETH_PHY_SPI_FREQ_MHZ)) {
      LOG_WRN("Ошибка инициализации Ethernet W5500");
      return false;
    }
#endif
#if CONFIG_IDF_TARGET_ESP32
 #ifdef ISCAM
    LOG_WRN("Недостаточно контактов для Ethernet на ESP32.");
    netMode = 0;
    return false;
 #else
    if (!ETH.begin(ETH_PHY_LAN8720,
                   ETH_PHY_ADDR,
                   ethCS,
                   ethInt,
                   ethRst,
                   ETH_CLK_MODE)) {
      LOG_WRN("Ошибка инициализации Ethernet LAN8720");
      return false;
    }
 #endif
#endif
    if (strlen(ST_ip) > 1) {
      IPAddress _ip, _gw, _sn, _ns1;
      if (_ip.fromString(ST_ip)) {
        _gw.fromString(ST_gw);
        _sn.fromString(ST_sn);
        _ns1.fromString(ST_ns1);
        ETH.config(_ip, _gw, _sn, _ns1);
        LOG_INF("Ethernet установил статический IP");
      } else LOG_WRN("Не удалось проанализировать статический IP-адрес Ethernet: %s.", ST_ip);
    }
  } else {
    LOG_WRN("Контакты Ethernet не определены");
    return false;
  }
  uint32_t startAttemptTime = millis();
  while (!ETH.linkUp() && millis() - startAttemptTime < 8000) delay(100);
  if (!ETH.linkUp()) LOG_WRN("Соединение Ethernet не работает");
  startAttemptTime = millis();
  while (!ETH.localIP() && millis() - startAttemptTime < 8000) delay(100);
  if (!ETH.localIP()) LOG_WRN("Ethernet пока без IP");
  setupMdnsHost();
  if (pingHandle == NULL) startPing();
  return ETH.linkUp();
}
static bool startWifi(bool firstcall = true) {
  if (firstcall) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.STA.setAutoReconnect(false);
    WiFi.AP.clear();
    WiFi.AP.end();
    WiFi.STA.setHostname(hostName);
    delay(100);
  }
  wl_status_t wlStat = WL_NO_SSID_AVAIL;
  if (netMode == 0) {
    setWifiSTA();
    uint32_t startAttemptTime = millis();
    wlStat = WL_NO_SSID_AVAIL;
    if (strlen(ST_SSID)) {
      while (wlStat = WiFi.STA.status(), wlStat != WL_CONNECTED && millis() - startAttemptTime < 5000)  {
        logPrint(".");
        delay(500);
      }
    }
    int numNetworks = WiFi.scanNetworks();
    for (int i=0; i < numNetworks; i++) {
      if (!strcmp(WiFi.SSID(i).c_str(), ST_SSID))
        LOG_INF("Статистика Wi-Fi для %s – уровень сигнала: %d дБм; Шифрование: %s; канал: %u",  ST_SSID, WiFi.RSSI(i), getEncType(i), WiFi.channel(i));
    }
    if (wlStat != WL_CONNECTED) LOG_WRN("SSID %s не подключен %s", ST_SSID, wifiStatusStr(wlStat));
  }
  if (wlStat == WL_NO_SSID_AVAIL || allowAP) setWifiAP();
#if CONFIG_IDF_TARGET_ESP32S3
  if (netMode == 0) setupMdnsHost();
#endif
  if (pingHandle == NULL) startPing();
  return wlStat == WL_CONNECTED ? true : false;
}
bool startNetwork(bool firstcall) {
  Network.onEvent(onNetEvent);
  predefEthPins();
  if (netMode > 0) {
    if (startEth()) {
      if (netMode == 1) {
        WiFi.mode(WIFI_OFF);
#ifdef APP_BT_ENABLED
        if (btStarted()) btStop();
#endif
        return true;
      }
    } else {
      LOG_WRN("Не удалось запустить Ethernet, возврат к Wi-Fi");
      ETH.end();
      WiFi.AP.enableNAPT(false);
      netMode = 0;
    }
  }
  if (netMode == 2) {
    WiFi.AP.enableNAPT(true);
    allowAP = true;
  }
  return startWifi(firstcall);
}
IPAddress netLocalIP() { return (netMode > 0) ? ETH.localIP() : WiFi.STA.localIP(); }
IPAddress netGatewayIP() { return (netMode > 0) ? ETH.gatewayIP() : WiFi.STA.gatewayIP(); }
String netMacAddress() { return (netMode > 0) ? ETH.macAddress().c_str() : WiFi.STA.macAddress().c_str(); }
int netRSSI() { return (netMode == 1) ? 0 : WiFi.STA.RSSI(); }
bool netIsConnected() { return (netMode > 0) ? (ETH.linkUp() && ETH.localIP()) : (WiFi.STA.status() == WL_CONNECTED); }
void resetWatchDog(int wdIndex, uint32_t wdTimeout) {
  static bool watchDogStarted[4] = {false, false, false, false};
  if (watchDogStarted[wdIndex]) esp_task_wdt_reset();
  else {
    esp_task_wdt_deinit();
    esp_task_wdt_config_t twdt_config = {
      .timeout_ms = wdTimeout,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
    };
    esp_task_wdt_init(&twdt_config);
    esp_task_wdt_add(NULL);
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      watchDogStarted[wdIndex] = true;
      esp_task_wdt_reset();
      LOG_INF("WatchDog запущен для выполнения задачи: %s", pcTaskGetName(NULL));
    } else LOG_ERR("WatchDog не удалось запустить задачу: %s ", pcTaskGetName(NULL));
  }
}
static void statusCheck() {
  doAppPing();
  if (!timeSynchronized) getLocalNTP();
  if (!dataFilesChecked) dataFilesChecked = checkDataFiles();
#if INCLUDE_MQTT
  if (mqtt_active) startMqttClient();
#endif
}
void resetCrashLoop() {
  crashLoop = 0;
}
static void pingSuccess(esp_ping_handle_t hdl, void *args) {
  if (DEBUG_MEM) {
    static uint32_t minStack = UINT32_MAX;
    uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if (freeStack < minStack) {
      minStack = freeStack;
      if (freeStack < MIN_STACK_FREE) LOG_WRN("Только пространство стека задачи ping: %u", freeStack);
      else LOG_INF("Объем стека задач ping уменьшен до: %u", freeStack);
    }
  }
  resetWatchDog(0, wifiTimeoutSecs * 1000 * 2);
  if (dataFilesChecked) resetCrashLoop();
  statusCheck();
}
static void pingTimeout(esp_ping_handle_t hdl, void *args) {
  resetWatchDog(0, wifiTimeoutSecs * 1000 * 2);
  if (netMode > 0) {
    if (usePing) {
      LOG_WRN("Не удалось выполнить проверку связи шлюза, перезапустите Ethernet...");
      startNetwork(false);
    } else {
      if (netIsConnected()) statusCheck();
      else {
        LOG_WRN("Отключено, перезапустите Ethernet...");
        startNetwork(false);
      }
    }
  } else {
    if (strlen(ST_SSID)) {
      wl_status_t wStat = WiFi.STA.status();
      if (wStat != WL_NO_SSID_AVAIL && wStat != WL_NO_SHIELD) {
        if (usePing) {
          LOG_WRN("Не удалось пропинговать шлюз, перезапустите Wi-Fi...");
          startWifi(false);
        } else {
          if (wStat == WL_CONNECTED) statusCheck();
          else {
            LOG_WRN("Отключено, перезапустите Wi-Fi...");
            startWifi(false);
          }
        }
      }
    }
  }
}
static void startPing() {
  IPAddress ipAddr = netGatewayIP();
  if (!ipAddr) return;
  ip_addr_t pingDest;
  IP_ADDR4(&pingDest, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  esp_ping_config_t pingConfig = ESP_PING_DEFAULT_CONFIG();
  pingConfig.target_addr = pingDest;
  pingConfig.count = ESP_PING_COUNT_INFINITE;
  pingConfig.interval_ms = wifiTimeoutSecs * 1000;
  pingConfig.timeout_ms = 5000;
  pingConfig.task_stack_size = PING_STACK_SIZE;
  pingConfig.task_prio = 1;
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = pingSuccess;
  cbs.on_ping_timeout = pingTimeout;
  cbs.on_ping_end = NULL;
  cbs.cb_args = NULL;
  esp_ping_new_session(&pingConfig, &cbs, &pingHandle);
  esp_ping_start(pingHandle);
  LOG_INF("Запущен мониторинг пинга – %s", usePing ? "On" : "Off");
  debugMemory("startPing");
}
void stopPing() {
  if (pingHandle != NULL) {
    esp_ping_stop(pingHandle);
    esp_ping_delete_session(pingHandle);
    pingHandle = NULL;
  }
}
#define EXT_IP_HOST "api.ipify.org"
char extIP[MAX_IP_LEN] = "Не назначено";
bool doGetExtIP = true;
void getExtIP() {
  if (doGetExtIP) {
    NetworkClientSecure hclient;
    if (remoteServerConnect(hclient, EXT_IP_HOST, HTTPS_PORT, "", GETEXTIP)) {
      HTTPClient https;
      int httpCode = HTTP_CODE_NOT_FOUND;
      if (https.begin(hclient, EXT_IP_HOST, HTTPS_PORT, "/", true)) {
        char newExtIp[MAX_IP_LEN] = "";
        httpCode = https.GET();
        if (httpCode == HTTP_CODE_OK) {
          strncpy(newExtIp, https.getString().c_str(), sizeof(newExtIp) - 1);
          if (strcmp(newExtIp, extIP)) {
            strncpy(extIP, newExtIp, sizeof(extIP) - 1);
            updateStatus("extIP", extIP);
            updateStatus("save", "0");
            externalAlert("Внешний IP изменен", extIP);
          } else LOG_INF("Внешний IP: %s", extIP);
        } else LOG_WRN("Не удалось выполнить запрос внешнего IP-адреса, ошибка: %s", https.errorToString(httpCode).c_str());
        if (httpCode != HTTP_CODE_OK) doGetExtIP = false;
        https.end();
      }
      remoteServerClose(hclient);
    }
  }
}
static uint8_t failCounts[REMFAILCNT] = {0};
void remoteServerClose(NetworkClientSecure& sclient) {
  if (sclient.available()) sclient.clear();
  if (sclient.connected()) sclient.stop();
}
bool remoteServerConnect(NetworkClientSecure& sclient, const char* serverName, uint16_t serverPort, const char* serverCert, uint8_t connIdx) {
  if (sclient.connected()) return true;
  else {
    if (failCounts[connIdx] >= MAX_FAIL) {
      if (failCounts[connIdx] == MAX_FAIL) {
        LOG_ERR("Отменить попытку подключения %s до следующего обновления", serverName);
        failCounts[connIdx] = MAX_FAIL + 1;
      }
    } else {
      if (ESP.getFreeHeap() > TLS_HEAP) {
        if (useSecure && strlen(serverCert)) sclient.setCACert(serverCert);
        else sclient.setInsecure();
        uint32_t startTime = millis();
        while (!sclient.connected()) {
          if (sclient.connect(serverName, serverPort)) break;
          if (millis() - startTime > responseTimeoutSecs * 1000) break;
          delay(2000);
        }
        if (sclient.connected()) {
          failCounts[connIdx] = 0;
          return true;
        }
        else {
          char errBuf[100] = "Неизвестная ошибка сервера";
          int errNum = sclient.lastError(errBuf, sizeof(errBuf));
          LOG_WRN("Время ожидания подключения к серверу: %s, ошибка: %d, %s", serverName, errNum, errBuf);
        }
      } else LOG_WRN("Недостаточно кучи %s для сеанса TLS %s.", fmtSize(ESP.getFreeHeap()), serverName);
      failCounts[connIdx]++;
    }
  }
  return false;
}
void remoteServerReset() {
  for (uint8_t i = 0; i < REMFAILCNT; i++) failCounts[i] = 0;
}
char timezone[FILE_NAME_LEN] = "GMT0";
char ntpServer[MAX_HOST_LEN] = "pool.ntp.org";
uint8_t alarmHour = 1;
time_t getEpoch() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder) {
  time_t currEpoch = getEpoch();
  if (isFolder) strftime(inBuff, inBuffLen, "/%Y%m%d", localtime(&currEpoch));
  else strftime(inBuff, inBuffLen, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));
}
static void showLocalTime(const char* timeSrc) {
  time_t currEpoch = getEpoch();
  char timeFormat[20];
  strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
  LOG_INF("Получено текущее время от %s: %s с tz: %s", timeSrc, timeFormat, timezone);
  timeSynchronized = true;
}
bool getLocalNTP() {
  LOG_INF("Использование NTP-сервера: %s", ntpServer);
  configTzTime(timezone, ntpServer);
  if (getEpoch() > 10000) {
    showLocalTime("NTP");
    return true;
  }
  else {
    LOG_WRN("Еще не синхронизировано с NTP");
    return false;
  }
}
void syncToBrowser(uint32_t browserUTC) {
  if (!timeSynchronized) {
    struct timeval tv;
    tv.tv_sec = browserUTC;
    settimeofday(&tv, NULL);
    setenv("TZ", timezone, 1);
    tzset();
    showLocalTime("browser");
  }
}
void formatElapsedTime(char* timeStr, uint32_t timeVal, bool noDays) {
  uint32_t secs = timeVal / 1000;
  uint32_t mins = secs / 60;
  uint32_t hours = mins / 60;
  uint32_t days = hours / 24;
  secs = secs - (mins * 60);
  mins = mins - (hours * 60);
  hours = hours - (days * 24);
  if (noDays) sprintf(timeStr, "%02lи:%02lи:%02lю", hours, mins, secs);
  else sprintf(timeStr, "%lu-%02lu:%02lu:%02lu", days, hours, mins, secs);
}
static time_t setAlarm(uint8_t alarmHour) {
  time_t currEpoch = getEpoch();
  struct tm* timeinfo = localtime(&currEpoch);
  int nextDay = 0;
  do {
    timeinfo->tm_mday += nextDay;
    timeinfo->tm_hour = alarmHour;
    timeinfo->tm_min = 0;
    timeinfo->tm_sec = 0;
    nextDay = 1;
  } while (mktime(timeinfo) < getEpoch());
  char inBuff[30];
  strftime(inBuff, sizeof(inBuff), "%d/%m/%Y %H:%M:%S", timeinfo);
  LOG_INF("Тревога запланирована на %s", inBuff);
  return mktime(timeinfo);
}
bool checkAlarm() {
  static time_t rolloverEpoch = 0;
  if (timeSynchronized && getEpoch() >= rolloverEpoch) {
    rolloverEpoch = setAlarm(alarmHour);
    return true;
  }
  return false;
}
bool changeExtension(char* fileName, const char* newExt) {
  size_t inNamePtr = strlen(fileName);
  while (inNamePtr > 0 && fileName[inNamePtr] != '.') inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(fileName + inNamePtr, newExt, extLen);
  fileName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}
void showProgress(const char* marker) {
  static uint8_t dotCnt = 0;
  logPrint(marker);
  if (++dotCnt >= DOT_MAX) {
    dotCnt = 0;
    logLine();
  }
}
bool calcProgress(int progressVal, int totalVal, int percentReport, uint8_t &pcProgress) {
  uint8_t percentage = (progressVal * 100) / totalVal;
  if (percentage >= pcProgress + percentReport) {
    pcProgress = percentage;
    return true;
  } else return false;
}
bool urlEncode(const char* inVal, char* encoded, size_t maxSize) {
  int encodedLen = 0;
  char hexTable[] = "0123456789ABCDEF";
  while (*inVal) {
    if (isalnum(*inVal) || strchr("$-_.+!*'(),:@~#", *inVal)) *encoded++ = *inVal;
    else {
      encodedLen += 3;
      if (encodedLen >= maxSize) return false;
      *encoded++ = '%';
      *encoded++ = hexTable[(*inVal) >> 4];
      *encoded++ = hexTable[*inVal & 0xf];
    }
    inVal++;
  }
  *encoded = 0;
  return true;
}
void urlDecode(char* inVal) {
  std::string decodeVal(inVal);
  std::string replaceVal = decodeVal;
  std::smatch match;
  while (regex_search(decodeVal, match, std::regex("(%)([0-9A-Fa-f]{2})"))) {
    std::string s(1, static_cast<char>(std::strtoul(match.str(2).c_str(),nullptr,16)));
    replaceVal = std::regex_replace(replaceVal, std::regex(match.str(0)), s);
    decodeVal = match.suffix().str();
  }
  strcpy(inVal, replaceVal.c_str());
}
void listBuff (const uint8_t* b, size_t len) {
  if (!len || !b) LOG_WRN("Нечего печатать");
  else {
    for (size_t i = 0; i < len; i += 16) {
      int linelen = (len - i) < 16 ? (len - i) : 16;
      for (size_t k = 0; k < linelen; k++) logPrint(" %02x", b[i+k]);
      puts(" ");
    }
  }
}
size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize) {
  size_t h = 0, n = 0;
  while (h < hSize && n < nSize) {
    if (haystack[h] == needle[n]) {
      h++;
      n++;
      if (n == nSize) return h;
    } else {
      h = h - n + 1;
      n = 0;
    }
  }
  return 0;
}
void removeChar(char* s, char c) {
  int writer = 0, reader = 0;
  while (s[reader]) {
    if (s[reader] != c) s[writer++] = s[reader];
    reader++;
  }
  s[writer] = 0;
}
void replaceChar(char* s, char c, char r) {
  int reader = 0;
  while (s[reader]) {
    if (s[reader] == c) s[reader] = r;
    reader++;
  }
}
char* fmtSize (uint64_t sizeVal) {
  static char returnStr[20];
  if (sizeVal < 50 * 1024) sprintf(returnStr, "%lв байтах", sizeVal);
  else if (sizeVal < ONEMEG) sprintf(returnStr, "%lлуКБ", sizeVal / 1024);
  else if (sizeVal < ONEMEG * 1024) sprintf(returnStr, "%0.1fМБ", (double)(sizeVal) / ONEMEG);
  else sprintf(returnStr, "%0.1fГБ", (double)(sizeVal) / (ONEMEG * 1024));
  return returnStr;
}
uint16_t smoothAnalog(int analogPin, int samples) {
  uint32_t level = 0;
  if (analogPin > 0) {
    for (int j = 0; j < samples; j++) level += analogRead(analogPin);
    level /= samples;
  }
  return level;
}
void setupADC() {
  analogSetAttenuation(ADC_ATTEN);
  analogReadResolution(ADC_BITS);
}
float smoothSensor(float latestVal, float smoothedVal, float alpha) {
  return (latestVal * alpha) + smoothedVal * (1.0 - alpha);
}
#if CONFIG_IDF_TARGET_ESP32
extern "C" {
uint8_t temprature_sens_read();
}
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#include "driver/temperature_sensor.h"
static temperature_sensor_handle_t temp_sensor = NULL;
#endif
static void prepInternalTemp() {
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  temperature_sensor_install(&temp_sensor_config, &temp_sensor);
  temperature_sensor_enable(temp_sensor);
#endif
}
float readInternalTemp() {
  float intTemp = NULL_TEMP;
#if CONFIG_IDF_TARGET_ESP32
  intTemp = (temprature_sens_read() - 32) / 1.8;
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
    temperature_sensor_get_celsius(temp_sensor, &intTemp);
#endif
  return intTemp;
}
#define MAX_OUT 200
static va_list arglist;
static char fmtBuf[MAX_OUT];
static char outBuf[MAX_OUT];
char alertMsg[MAX_OUT];
TaskHandle_t logHandle = NULL;
static SemaphoreHandle_t logSemaphore = NULL;
static SemaphoreHandle_t logMutex = NULL;
static int logWait = 100;
bool useLogColors = false;
bool wsLog = false;
#define WRITE_CACHE_CYCLE 5
bool sdLog = false;
int logType = 0;
static FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;
RTC_NOINIT_ATTR char messageLog[RAM_LOG_LEN];
RTC_NOINIT_ATTR uint16_t mlogEnd;
static RTC_NOINIT_ATTR uint32_t backtrace[60];
static RTC_NOINIT_ATTR size_t btLen;
static RTC_NOINIT_ATTR char btReason[50];
static RTC_NOINIT_ATTR int btCore;
static RTC_NOINIT_ATTR uint32_t haveTrace;
static void ramLogClear() {
  mlogEnd = 0;
  memset(messageLog, 0, RAM_LOG_LEN);
}
static void ramLogStore(size_t msgLen) {
  if (mlogEnd + msgLen >= RAM_LOG_LEN) {
    uint16_t firstPart = RAM_LOG_LEN - mlogEnd;
    memcpy(messageLog + mlogEnd, outBuf, firstPart);
    msgLen -= firstPart;
    memcpy(messageLog, outBuf + firstPart, msgLen);
    mlogEnd = 0;
  } else memcpy(messageLog + mlogEnd, outBuf, msgLen);
  mlogEnd += msgLen;
}
void flush_log(bool andClose) {
  if (log_remote_fp != NULL) {
    fsync(fileno(log_remote_fp));
    fflush(log_remote_fp);
    if (andClose) {
      LOG_INF("Закрытый SD-файл для логирования");
      fclose(log_remote_fp);
      log_remote_fp = NULL;
    } else delay(1000);
  }
}
static void remote_log_init_SD() {
#if !CONFIG_IDF_TARGET_ESP32C3
  STORAGE.mkdir(DATA_DIR);
  log_remote_fp = NULL;
  log_remote_fp = fopen("/sdcard" LOG_FILE_PATH, "a");
  if (log_remote_fp == NULL) {LOG_WRN("Не удалось открыть файл журнала SD %s.", LOG_FILE_PATH);}
  else {
    logPrint(" \n");
    LOG_INF("Открыт SD-файл для регистрации");
  }
#endif
}
void reset_log() {
  if (logType == 0) ramLogClear();
  if (logType == 2) {
    if (log_remote_fp != NULL) flush_log(true);
    STORAGE.remove(LOG_FILE_PATH);
    remote_log_init_SD();
  }
  if (logType != 1) LOG_INF("Файл журнала %s удален.", logType == 0 ? "RAM" : "SD");
}
void remote_log_init() {
  if (sdLog) {
    flush_log(false);
    remote_log_init_SD();
  } else flush_log(true);
}
static void logTask(void *arg) {
  while(true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vsnprintf(outBuf, MAX_OUT, fmtBuf, arglist);
    va_end(arglist);
    xSemaphoreGive(logSemaphore);
  }
}
void logPrint(const char *format, ...) {
  if (logMutex == NULL) logSetup();
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(logWait)) == pdTRUE) {
    strncpy(fmtBuf, format, MAX_OUT);
    va_start(arglist, format);
    vTaskPrioritySet(logHandle, uxTaskPriorityGet(NULL) + 1);
    xTaskNotifyGive(logHandle);
    outBuf[MAX_OUT - 2] = '\n';
    outBuf[MAX_OUT - 1] = 0;
    xSemaphoreTake(logSemaphore, portMAX_DELAY);
    size_t msgLen = strlen(outBuf);
    if (outBuf[msgLen - 2] == '~') {
      outBuf[msgLen - 2] = ' ';
      snprintf(alertMsg, MAX_OUT - 1, "событие: предупреждение\nданные: %s%s", outBuf, SSESEP);
      sendSSE(alertMsg);
      alertMsg[0] = 0;
    }
    ramLogStore(msgLen);
    if (monitorOpen) Serial.print(outBuf);
    else delay(10);
    if (sdLog) {
      if (log_remote_fp != NULL) {
        fwrite(outBuf, sizeof(char), msgLen, log_remote_fp);
        if (counter_write++ % WRITE_CACHE_CYCLE == 0) fsync(fileno(log_remote_fp));
      }
    }
    if (msgLen > 1) {
      outBuf[msgLen - 1] = 0;
      if (wsLog) wsAsyncSendText(outBuf);
    }
    xSemaphoreGive(logMutex);
  }
}
void logLine() {
  logPrint(" \n");
}
int vprintfRedirect(const char* format, va_list args) {
  char buffer[256];
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  logPrint("%s", buffer);
  return len;
}
void logSetup() {
  if (logMutex == NULL) {
    set_arduino_panic_handler(appPanicHandler, NULL);
    Serial.begin(115200);
    Serial.setDebugOutput(DBG_ON);
    printf("\n\n");
    if (DEBUG_MEM) printf("init > Свободно: куча %lu\n", ESP.getFreeHeap());
    if (DBG_ON) esp_log_level_set("*", DBG_LVL);
    else esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_set_vprintf(vprintfRedirect);
    if (crashLoop == MAGIC_NUM) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Обнаружен аварийный цикл, проверьте журнал %s", (brownoutStatus == 'B' || brownoutStatus == 'R') ? "(затухание)" : " ");
    crashLoop = MAGIC_NUM;
    logSemaphore = xSemaphoreCreateBinary();
    logMutex = xSemaphoreCreateMutex();
    xSemaphoreGive(logSemaphore);
    xSemaphoreGive(logMutex);
    xTaskCreate(logTask, "logTask", LOG_STACK_SIZE, NULL, LOG_PRI, &logHandle);
    if (mlogEnd >= RAM_LOG_LEN) ramLogClear();
    logPrint("\n\n=============== %s %s ===============\n", APP_NAME, APP_VER);
    LOG_INF("Настройка журнала в ОЗУ, размер %u, начиная с %u", RAM_LOG_LEN, mlogEnd);
    initBrownout();
    prepInternalTemp();
    boardInfo();
    if (DBG_ON) printPartitionTable();
    LOG_INF("Скомпилировано с помощью arduino-esp32 v%s.", ESP_ARDUINO_VERSION_STR);
    wakeupResetReason();
    if (alertBuffer == NULL) alertBuffer = psramFound() ? (byte*)ps_malloc(maxAlertBuffSize) : (byte*)malloc(maxAlertBuffSize);
    if (jsonBuff == NULL) jsonBuff = psramFound() ? (char*)ps_malloc(JSON_BUFF_LEN) : (char*)malloc(JSON_BUFF_LEN);
    if (!DBG_ON) esp_log_level_set("*", ESP_LOG_ERROR);
    debugMemory("logSetup");
  }
}
void formatHex(const char* inData, size_t inLen) {
  char formatted[(inLen * 3) + 1];
  for (int i=0; i<inLen; i++) sprintf(formatted + (i*3), "%02x ", inData[i]);
  formatted[(inLen * 3)] = 0;
  LOG_INF("Шестнадцатеричное: %s", formatted);
}
const char* espErrMsg(esp_err_t errCode) {
  static char errText[100];
  esp_err_to_name_r(errCode, errText, 100);
  return errText;
}
#define BASE64 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
const uint8_t* encode64chunk(const uint8_t* inp, int rem) {
  rem = 3 - rem;
  uint32_t buff = 0;
  static uint8_t b64[4];
  for (int i = 0; i < 3 - rem; i++) buff |= inp[i] << (8*(2-i));
  for (int i = 0; i < 4 - rem; i++) b64[i] = BASE64[buff >> (6*(3-i)) & 0x3F];
  for (int i = 0; i < rem; i++) b64[3-i] = '=';
  return b64;
}
const char* encode64(const char* inp) {
  static char encoded[121];
  encoded[0] = 0;
  int len = strlen(inp);
  if (len > 90) {
    LOG_WRN("Входная строка слишком длинная: %u символов.", len);
    len = 90;
  }
  for (int i = 0; i < len; i += 3)
    strncat(encoded, (char*)encode64chunk((uint8_t*)inp + i, min(len - i, 3)), 4);
  return encoded;
}
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 1, 0)
static const char* getTaskStateString(eTaskState state) {
  switch (state) {
    case eRunning: return "Running";
    case eReady: return "Ready";
    case eBlocked: return "Blocked";
    case eSuspended: return "Suspended";
    case eDeleted: return "Deleted";
    case eInvalid: return "Invalid";
    default: return "Unknown";
  }
}
static void statsTask(void *arg) {
  #define STATS_TASK_PRIO     10
  #define STATS_INTERVAL      30000
  #define ARRAY_SIZE_OFFSET   40
  static configRUN_TIME_COUNTER_TYPE prevRunCounter = 0;
  esp_err_t ret = ESP_OK;
  TaskStatus_t *statsArray = NULL;
  UBaseType_t statsArraySize;
  configRUN_TIME_COUNTER_TYPE runCounter;
  while (true) {
    delay(STATS_INTERVAL);
    do {
      statsArraySize = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
      statsArray = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * statsArraySize);
      if (statsArray == NULL) {
        ret = ESP_ERR_NO_MEM;
        break;
      }
      statsArraySize = uxTaskGetSystemState(statsArray, statsArraySize, &runCounter);
      if (statsArraySize == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        break;
      }
      if (runCounter - prevRunCounter == 0) {
        ret = ESP_ERR_INVALID_STATE;
        break;
      }
      logPrint("\nИнтервал статистики задач %lums на %u ядрах\n", (runCounter - prevRunCounter) / 1000, CONFIG_FREERTOS_NUMBER_OF_CORES);
      logPrint("\n| %-16s | %-10s | %-3s | %-4s | %-6s |\n", "Название задачи", "State", "Pri", "Core", "Основной%");
      logPrint("|------------------|------------|-----|------|--------|\n");
      for (int i = 0; i < statsArraySize; i++) {
        float percentage_time = ((float)statsArray[i].ulRunTimeCounter * 100.0) / runCounter;
        logPrint("| %-16s | %-10s | %*u | %*s | %*.1f%% |\n",
          statsArray[i].pcTaskName, getTaskStateString(statsArray[i].eCurrentState), 3, (int)statsArray[i].uxCurrentPriority, 4, "tbd", 5, percentage_time);
      }
      logPrint("|------------------|------------|-----|------|--------|\n");
    } while (false);
    prevRunCounter = runCounter;
    free(statsArray);
    if (ret != ESP_OK) LOG_WRN("Не удалось запустить мониторинг задачи %s.", espErrMsg(ret));
  }
}
void runTaskStats() {
  vTaskDelay(pdMS_TO_TICKS(100));
  xTaskCreatePinnedToCore(statsTask, "statsTask", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
}
#endif
void checkMemory(const char* source) {
  LOG_INF("%s Свободно: куча %u, блок: %u, мин.: %u, pSRAM %u", strlen(source) ? source : "Setup", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
  if (ESP.getFreeHeap() < WARN_HEAP) LOG_WRN("Только свободная куча %u, мин %u", ESP.getFreeHeap(), ESP.getMinFreeHeap());
  if (ESP.getMaxAllocHeap() < WARN_ALLOC) LOG_WRN("Максимальный выделяемый блок кучи составляет всего %u.", ESP.getMaxAllocHeap());
  if (!strlen(source) && DEBUG_MEM) {
    printGpioInfo();
    runTaskStats();
  }
}
uint32_t checkStackUse(TaskHandle_t thisTask, int taskIdx) {
  static uint32_t minStack[20];
  uint32_t freeStack = 0;
  if (thisTask != NULL) {
    freeStack = (uint32_t)uxTaskGetStackHighWaterMark(thisTask);
    if (!minStack[taskIdx]) {
      minStack[taskIdx] = freeStack;
      LOG_INF("Задача %s на ядре %d, начальное пространство стека %u", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
    }
    if (freeStack < minStack[taskIdx]) {
      minStack[taskIdx] = freeStack;
      if (freeStack < MIN_STACK_FREE) LOG_WRN("Задача %s на ядре %d, только пространство стека: %u", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
      else LOG_INF("Задача %s на ядре %d, пространство стека уменьшено до %u", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
    }
  }
  return freeStack;
}
void debugMemory(const char* caller) {
  if (DEBUG_MEM) {
    logPrint("%s > Свободно: куча %u, блок: %u, мин: %u, pSRAM %u\n", caller, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
    delay(FLUSH_DELAY);
  }
}
#include <esp_wifi.h>
#include <driver/gpio.h>
void doRestart(const char* restartStr) {
  LOG_ALT("Управляемый перезапуск: %s", restartStr);
#ifdef ISCAM
  appShutdown();
#endif
#if INCLUDE_MQTT
  if (mqtt_active) stopMqttClient();
#endif
  resetCrashLoop();
  flush_log(true);
  delay(2000);
  ESP.restart();
}
static esp_sleep_wakeup_cause_t printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : LOG_INF("Пробуждение по внешнему сигналу с использованием RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : LOG_INF("Пробуждение по внешнему сигналу с использованием RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : LOG_INF("Пробуждение по внутреннему таймеру"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : LOG_INF("Пробуждение по тачпаду"); break;
    case ESP_SLEEP_WAKEUP_ULP : LOG_INF("Пробуждение по программе ULP"); break;
    case ESP_SLEEP_WAKEUP_GPIO: LOG_INF("Пробуждение по GPIO"); break;
    case ESP_SLEEP_WAKEUP_UART: LOG_INF("Пробуждение по UART"); break;
    default : LOG_INF("Пробуждение по сбросу"); break;
  }
  return wakeup_reason;
}
void appPanicHandler(arduino_panic_info_t *info, void *arg) {
  strncpy(btReason, info->reason, sizeof(btReason) - 1);
  btCore = info->core;
  btLen = info->backtrace_len;
  for (int i = 0; i < info->backtrace_len; i++) backtrace[i] = info->backtrace[i];
  haveTrace = MAGIC_NUM;
  esp_rom_delay_us(PANIC_DELAY * 1000 * 1000);
}
static void showBacktrace() {
  if (haveTrace == MAGIC_NUM) {
    haveTrace = 0;
    char bt[220];
    sprintf(bt, "%s на ядре %d", btReason, btCore);
    LOG_WRN("%s", bt);
    bt[0] = 0;
    for (int i = 0; i < btLen; i++) {
      snprintf(bt + strlen(bt), sizeof(bt) - strlen(bt) - 11, "0x%08x ", (unsigned int)backtrace[i]);
    }
    LOG_WRN("Вставьте обратную трассировку ниже в декодер исключений Arduino:\n");
    logPrint("Обратная трассировка: %s\n\n", bt);
  }
}
static esp_reset_reason_t printResetReason() {
  esp_reset_reason_t bootReason = esp_reset_reason();
  switch (bootReason) {
    case ESP_RST_UNKNOWN: LOG_INF("Сброс по неизвестной причине"); break;
    case ESP_RST_POWERON: {
      LOG_INF("Сброс при включении питания");
      brownoutStatus = 0;
      messageLog[0] = 0;
      break;
    }
    case ESP_RST_EXT: LOG_INF("Сброс с внешнего контакта"); break;
    case ESP_RST_SW: LOG_INF("Программный сброс через esp_restart"); break;
    case ESP_RST_PANIC: LOG_INF("Сброс программного обеспечения из-за исключения/паники"); break;
    case ESP_RST_INT_WDT: LOG_INF("Сброс из-за прерывания сторожевого таймера"); break;
    case ESP_RST_TASK_WDT: LOG_INF("Сброс из-за сторожевого таймера задач"); break;
    case ESP_RST_WDT: LOG_INF("Сброс из-за других сторожевых таймеров"); break;
    case ESP_RST_DEEPSLEEP: LOG_INF("Сброс после выхода из режима глубокого сна"); break;
    case ESP_RST_BROWNOUT: LOG_INF("Программный сброс из-за отключения питания"); break;
    case ESP_RST_SDIO: LOG_INF("Сброс через SDIO"); break;
    default: LOG_WRN("Необработанная причина сброса"); break;
  }
  showBacktrace();
  return bootReason;
}
esp_sleep_wakeup_cause_t wakeupResetReason() {
  printResetReason();
  esp_sleep_wakeup_cause_t wakeupReason = printWakeupReason();
  return wakeupReason;
}
void goToSleep(bool deepSleep) {
#if !CONFIG_IDF_TARGET_ESP32C3
  LOG_INF("Переходим в режим сна %s", deepSleep ? "deep" : "light");
  delay(100);
  if (deepSleep) {
    if (wakePin >= 0) {
      pinMode(wakePin, wakeLevel == 0 ? INPUT_PULLUP : INPUT_PULLDOWN);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)wakePin, wakeLevel);
    }
    esp_deep_sleep_start();
  } else {
    esp_wifi_stop();
    if (wakePin >= 0) gpio_wakeup_enable((gpio_num_t)wakePin, wakeLevel == 0 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    esp_light_sleep_start();
  }
  LOG_INF("Легкое пробуждение для сна");
  esp_wifi_start();
#else
  LOG_WRN("Эта функция несовместима с ESP32-C3.");
#endif
}
#include "esp_private/system_internal.h"
#include "esp_private/rtc_ctrl.h"
#include "hal/brownout_ll.h"
#include "soc/rtc_periph.h"
#include "hal/brownout_hal.h"
#define BROWNOUT_DET_LVL 7
IRAM_ATTR static void notifyBrownout(void *arg) {
  esp_cpu_stall(!xPortGetCoreID());
  esp_reset_reason_set_hint(ESP_RST_BROWNOUT);
  brownoutStatus = 'B';
  esp_restart_noos();
}
static void initBrownout(void) {
  if (brownoutStatus == 'R') LOG_WRN("Предупреждение о снижении напряжения было получено ранее");
  else if (brownoutStatus == 'B') {
    LOG_WRN("Произошло отключение электроэнергии из-за недостаточного электроснабжения.");
    brownoutStatus = 'R';
  } else {
    brownout_hal_config_t cfg = {
      .threshold = BROWNOUT_DET_LVL,
      .enabled = true,
      .reset_enabled = false,
      .flash_power_down = true,
      .rf_power_down = true,
    };
    brownout_hal_config(&cfg);
    brownout_ll_intr_clear();
    rtc_isr_register(notifyBrownout, NULL, RTC_CNTL_BROWN_OUT_INT_ENA_M, RTC_INTR_FLAG_IRAM);
    brownout_ll_intr_enable(true);
    brownoutStatus = 0;
  }
}
void forceCrash() {
  delay(5000);
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
  printf("%u\n", 1/0);
#pragma GCC diagnostic warning "-Wdiv-by-zero"
}
static void boardInfo() {
  LOG_INF("Чип %s, ядра %d, частота %d МГц, частота вращения %d", ESP.getChipModel(), ESP.getChipCores(), ESP.getCpuFreqMHz(), ESP.getChipRevision() / 100);
  FlashMode_t ideMode = ESP.getFlashChipMode();
  LOG_INF("Вспышка %s, режим %s @ %d МГц", fmtSize(ESP.getFlashChipSize()), (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"), ESP.getFlashChipSpeed() / OneMHz);
#if defined(CONFIG_SPIRAM_MODE_OCT)
  const char* psramMode = "OPI";
#else
  const char* psramMode = "QSPI";
#endif
  char memInfo[100] = "none";
#if !CONFIG_IDF_TARGET_ESP32C3
  if (psramFound()) sprintf(memInfo, "%s, режим %s при %dМГц", fmtSize(ESP.getPsramSize()), psramMode, CONFIG_SPIRAM_SPEED);
#endif
  LOG_INF("ПСРАМ %s", memInfo);
}
#include "esp32-hal-periman.h"
static void printGpioInfo() {
  logPrint("Информация о назначенном GPIO:\n");
  for (uint8_t i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
    if (!perimanPinIsValid(i)) continue;
    peripheral_bus_type_t type = perimanGetPinBusType(i);
    if (type == ESP32_BUS_TYPE_INIT) continue;
#if defined(BOARD_HAS_PIN_REMAP)
    int dpin = gpioNumberToDigitalPin(i);
    if (dpin < 0) continue;
    else logPrint("  D%-3d|%4u : ", dpin, i);
#else
    logPrint("  %4u : ", i);
#endif
    const char *extra_type = perimanGetPinBusExtraType(i);
    if (extra_type) logPrint("%s", extra_type);
    else logPrint("%s", perimanGetTypeName(type));
    int8_t bus_number = perimanGetPinBusNum(i);
    if (bus_number != -1) logPrint("[%u]", bus_number);
    int8_t bus_channel = perimanGetPinBusChannel(i);
    if (bus_channel != -1) logPrint("[%u]", bus_channel);
    logLine();
  }
  logLine();
}
const char* partitionTypeToStr(uint8_t type) {
  switch (type) {
    case ESP_PARTITION_TYPE_APP: return "APP";
    case ESP_PARTITION_TYPE_DATA: return "DATA";
    default: return "UNKNOWN";
  }
}
const char* partitionSubtypeToStr(uint8_t type, uint8_t subtype) {
  if (type == ESP_PARTITION_TYPE_APP) {
    switch (subtype) {
      case ESP_PARTITION_SUBTYPE_APP_FACTORY: return "Factory";
      case ESP_PARTITION_SUBTYPE_APP_OTA_0: return "OTA_0";
      case ESP_PARTITION_SUBTYPE_APP_OTA_1: return "OTA_1";
      case ESP_PARTITION_SUBTYPE_APP_OTA_2: return "OTA_2";
      case ESP_PARTITION_SUBTYPE_APP_OTA_3: return "OTA_3";
      case ESP_PARTITION_SUBTYPE_APP_OTA_4: return "OTA_4";
      case ESP_PARTITION_SUBTYPE_APP_OTA_5: return "OTA_5";
      default: return "App_Other";
    }
  } else if (type == ESP_PARTITION_TYPE_DATA) {
    switch (subtype) {
      case ESP_PARTITION_SUBTYPE_DATA_OTA: return "OTA_Data";
      case ESP_PARTITION_SUBTYPE_DATA_PHY: return "PHY";
      case ESP_PARTITION_SUBTYPE_DATA_NVS: return "NVS";
      case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS: return "NVS_Keys";
      case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "SPIFFS";
      case ESP_PARTITION_SUBTYPE_DATA_FAT: return "FAT";
      default: return "Data_Other";
    }
  }
  return "Unknown";
}
static void printPartitionTable() {
  LOG_INF("%-12s %-6s %-12s %-10s %-12s %-10s", "Partition", "Type", "Subtype", "Address", "Size", "Encrypted");
  LOG_INF("-----------------------------------------------------------------");
  esp_partition_iterator_t iter = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  if (iter == NULL) {
    LOG_ERR("Разделы не найдены");
    return;
  }
  do {
    const esp_partition_t* part = esp_partition_get(iter);
    const char* typeStr = partitionTypeToStr(part->type);
    const char* subtypeStr = partitionSubtypeToStr(part->type, part->subtype);
    const char* label = part->label;
    LOG_INF("%-12s %-6s %-12s 0x%08lX %-12s %-10s",
      label, typeStr, subtypeStr, part->address, fmtSize(part->size), part->encrypted ? "Yes" : "No");
    iter = esp_partition_next(iter);
  } while (iter != NULL);
  esp_partition_iterator_release(iter);
}