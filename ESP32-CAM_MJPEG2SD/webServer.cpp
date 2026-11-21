#include "appGlobals.h"
#define MAX_HANDLERS 12
char inFileName[IN_FILE_NAME_LEN];
static char variable[FILE_NAME_LEN];
static char value[FILE_NAME_LEN];
static char retainAction[2];
int refreshVal = 5000;
static httpd_handle_t httpServer = NULL;
static int fdWs = -1;
static httpd_handle_t sseSocketHD;
static int sseSocketFD;
bool useHttps = false;
bool useSecure = false;
bool heartBeatDone = false;
static fs::FS fp = STORAGE;
static byte* chunk;
esp_err_t sendChunks(File df, httpd_req_t *req, bool endChunking) {
  size_t chunksize = 0;
  esp_err_t res = ESP_OK;
  while ((chunksize = df.read(chunk, CHUNKSIZE))) {
    res = httpd_resp_send_chunk(req, (char*)chunk, chunksize);
    if (res != ESP_OK) break;
  }
  if (endChunking) {
    df.close();
    httpd_resp_sendstr_chunk(req, NULL);
  }
  if (res != ESP_OK) {
    snprintf(startupFailure, SF_LEN, "Не удалось отправить в браузер: %s, ошибка %s.", inFileName, espErrMsg(res));
    LOG_WRN("%s", startupFailure);
    checkMemory();
    OTAprereq();
  }
  return res;
}
esp_err_t fileHandler(httpd_req_t* req, bool download) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (!strcmp(inFileName, LOG_FILE_PATH)) flush_log(false);
  File df = fp.open(inFileName);
  if (!df) {
    LOG_WRN("Файл не существует или не может быть открыт: %s", inFileName);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  if (!df.size()) {
    df.close();
    httpd_resp_sendstr(req, NULL);
    return ESP_OK;
  }
  return (download) ? downloadFile(df, req) : sendChunks(df, req);
}
static void displayLog(httpd_req_t *req) {
  if (logType == 0) {
    int startPtr, endPtr;
    startPtr = endPtr = mlogEnd;
    httpd_resp_set_type(req, "text/plain");
    do {
      int maxChunk = startPtr < endPtr ? endPtr - startPtr : RAM_LOG_LEN - startPtr;
      size_t chunkSize = std::min(CHUNKSIZE, maxChunk);
      if (chunkSize > 0) httpd_resp_send_chunk(req, messageLog + startPtr, chunkSize);
      startPtr += chunkSize;
      if (startPtr >= RAM_LOG_LEN) startPtr = 0;
    } while (startPtr != endPtr);
    httpd_resp_sendstr_chunk(req, NULL);
  }
}
bool checkAuth(httpd_req_t* req) {
  if (strlen(Auth_Name)) {
    size_t credLen = strlen(Auth_Name) + strlen(Auth_Pass) + 2;
    char credentials[credLen];
    snprintf(credentials, credLen, "%s:%s", Auth_Name, Auth_Pass);
    size_t authLen = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (authLen) {
      char auth[authLen];
      httpd_req_get_hdr_value_str(req, "Authorization", auth, authLen);
      if (!strstr(auth, encode64(credentials))) authLen = 0;
    }
    if (!authLen) {
      httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic");
      httpd_resp_set_status(req, "401 Несанкционированный");
      httpd_resp_sendstr(req, NULL);
      return false;
    }
  }
  return true;
}
static esp_err_t indexHandler(httpd_req_t* req) {
  strcpy(inFileName, INDEX_PAGE_PATH);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (strlen(startupFailure)) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, failPageS_html);
    httpd_resp_sendstr_chunk(req, startupFailure);
    httpd_resp_sendstr_chunk(req, failPageE_html);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
  }
  if (!fp.exists(INDEX_PAGE_PATH) && WiFi.status() != WL_CONNECTED) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char*)setupPage_html_gz, setupPage_html_gz_len);
  } else if (!checkAuth(req)) return ESP_OK;
  return fileHandler(req);
}
esp_err_t extractHeaderVal(httpd_req_t *req, const char* variable, char* value) {
  esp_err_t res = ESP_FAIL;
  size_t hdrFieldLen = httpd_req_get_hdr_value_len(req, variable);
  if (!hdrFieldLen) return ESP_ERR_INVALID_ARG;
  else if (hdrFieldLen >= IN_FILE_NAME_LEN - 1) LOG_WRN("Значение поля %s слишком длинное (%d)", variable, hdrFieldLen);
  else {
    res = httpd_req_get_hdr_value_str(req, variable, value, hdrFieldLen + 1);
    if (res != ESP_OK) LOG_ERR("Не удалось получить значение для %s: %s.", variable, espErrMsg(res));
  }
  return res;
}
esp_err_t extractQueryKeyVal(httpd_req_t *req, char* variable, char* value) {
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, variable, queryLen);
  urlDecode(variable);
  char* endPtr = strchr(variable, '=');
  if (endPtr != NULL) {
    *endPtr = 0;
    strcpy(value, variable + strlen(variable) + 1);
  } else {
    LOG_ERR("Неверная строка запроса %s", variable);
    httpd_resp_set_status(req, "400 Неверная строка запроса");
    httpd_resp_sendstr(req, NULL);
    return ESP_FAIL;
  }
  return ESP_OK;
}
static esp_err_t webHandler(httpd_req_t* req) {
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, variable, queryLen);
  urlDecode(variable);
  if (!strcmp(variable, "OTA.htm")) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, otaPage_html);
  } else if (!strcmp(HTML_EXT, variable+(strlen(variable)-strlen(HTML_EXT)))) {
    httpd_resp_set_type(req, "text/html");
  } else if (!strcmp(JS_EXT, variable+(strlen(variable)-strlen(JS_EXT)))) {
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "общедоступный, максимальный возраст = 604800");
  } else if (!strcmp(CSS_EXT, variable+(strlen(variable)-strlen(CSS_EXT)))) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "максимальный возраст=604800");
  } else if (!strcmp(TEXT_EXT, variable+(strlen(variable)-strlen(TEXT_EXT)))) {
    httpd_resp_set_type(req, "text/plain");
  } else if (!strcmp(ICO_EXT, variable+(strlen(variable)-strlen(ICO_EXT)))) {
    httpd_resp_set_type(req, "image/x-icon");
  } else if (!strcmp(SVG_EXT, variable+(strlen(variable)-strlen(SVG_EXT)))) {
    httpd_resp_set_type(req, "изображение/svg+xml");
  } else LOG_WRN("Неизвестный тип файла %s", variable);
  int dlen = snprintf(inFileName, IN_FILE_NAME_LEN - 1, "%s/%s", DATA_DIR, variable);
  if (dlen >= IN_FILE_NAME_LEN) LOG_WRN("имя файла усечено");
  return fileHandler(req);
}
static esp_err_t controlHandler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (extractQueryKeyVal(req, variable, value) != ESP_OK) return ESP_FAIL;
  if (!strcmp(variable, "displayLog")) displayLog(req);
  else {
    strcpy(value, variable + strlen(variable) + 1);
    if (!strcmp(variable, "reset")) {
      httpd_resp_sendstr(req, NULL);
      doRestart("Пользователь запросил перезагрузку");
      return ESP_OK;
    }
    if (!strcmp(variable, "startOTA")) snprintf(inFileName, IN_FILE_NAME_LEN - 1, "%s/%s", DATA_DIR, value);
    else {
      if (appSpecificWebHandler(req, variable, value) == ESP_FAIL) updateStatus(variable, value);
    }
  }
  httpd_resp_sendstr(req, NULL);
  return ESP_OK;
}
static esp_err_t statusHandler(httpd_req_t *req) {
  uint8_t filter = (uint8_t)httpd_req_get_url_query_len(req);
  buildJsonString(filter);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, jsonBuff);
  return ESP_OK;
}
bool parseJson(int rxSize) {
  jsonBuff[rxSize - 1] = ',';
  jsonBuff[rxSize] = 0;
  char* ptr = jsonBuff + 1;
  size_t itemLen = 0;
  bool retAction = false;
  do {
    char* endItem = strchr(ptr += itemLen, ':');
    itemLen = endItem - ptr;
    memcpy(variable, ptr, itemLen);
    variable[itemLen] = 0;
    removeChar(variable, '"');
    ptr++;
    endItem = strchr(ptr += itemLen, ',');
    itemLen = endItem - ptr;
    memcpy(value, ptr, itemLen);
    value[itemLen] = 0;
    removeChar(value, '"');
    ptr++;
    if (!strcmp(variable, "action")) {
      strcpy(retainAction, value);
      retAction = true;
    } else updateStatus(variable, value);
  } while (ptr + itemLen - jsonBuff < rxSize);
  return retAction;
}
static esp_err_t sseHandler(httpd_req_t *req) {
  const char* sseHeader = "HTTP/1.1 200 ОК\r\n"
                          "Управление кэшем: no-store\r\n"
                          "Соединение: поддержание активности\r\n"
                          "Тип контента: текст/поток событий\r\n\r\n";
  sseSocketHD = req->handle;
  sseSocketFD = httpd_req_to_sockfd(req);
  httpd_socket_send(sseSocketHD, sseSocketFD, sseHeader, strlen(sseHeader), 0);
  sendSSE("событие: открыто\nданные: открыто" SSESEP);
  return ESP_OK;
}
void sendSSE(const char* statusData) {
  if (sseSocketFD > 0) {
    int res = httpd_socket_send(sseSocketHD, sseSocketFD, statusData, strlen(statusData), 0);
    if (res == HTTPD_SOCK_ERR_TIMEOUT) LOG_WRN("Тайм-аут/прервано при использовании сокета");
    if (res == HTTPD_SOCK_ERR_FAIL) LOG_WRN("Неустранимая ошибка при использовании сокета");
  } else LOG_WRN("SSE не инициирован");
}
static esp_err_t updateHandler(httpd_req_t *req) {
  size_t rxSize = min(req->content_len, (size_t)JSON_BUFF_LEN);
  int ret = 0;
  do {
    ret = httpd_req_recv(req, jsonBuff, rxSize);
    if (ret < 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
      else {
        LOG_WRN("Запрос на обновление не выполнен со статусом %i", ret);
      }
    }
  } while (ret > 0);
  httpd_resp_sendstr(req, NULL);
  if (ret >= 0 && parseJson(rxSize)) appSpecificWebHandler(req, "action", retainAction);
  return ret < 0 ? ESP_FAIL : ESP_OK;
}
void progress(size_t prg, size_t sz) {
  static uint8_t pcProgress = 0;
  if (calcProgress(prg, sz, 5, pcProgress)) LOG_INF("OTA загружено %d%%", pcProgress);
}
esp_err_t uploadHandler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  size_t fileSize = req->content_len;
  size_t rxSize = min(fileSize, (size_t)JSON_BUFF_LEN);
  int bytesRead = -1;
  LOG_INF("Загрузить файл %s", inFileName);
  if (strstr(inFileName, ".bin") != NULL) {
    LOG_INF("Обновление прошивки с использованием файла %s", inFileName);
    OTAprereq();
    if (fdWs >= 0) httpd_sess_trigger_close(httpServer, fdWs);
    int cmd = (strstr(inFileName, "spiffs") != NULL) ? U_SPIFFS : U_FLASH;
    if (cmd == U_SPIFFS) STORAGE.end();
    if (Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      do {
        bytesRead = httpd_req_recv(req, jsonBuff, rxSize);
        if (bytesRead < 0) {
          if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
            delay(10);
            continue;
          } else {
            LOG_WRN("Не удалось загрузить запрос со статусом %i.", bytesRead);
            break;
          }
        }
        Update.write((uint8_t*)jsonBuff, (size_t)bytesRead);
        Update.onProgress(progress);
        fileSize -= bytesRead;
      } while (bytesRead > 0);
      if (!fileSize) Update.end(true);
    }
    if (Update.hasError()) LOG_WRN("OTA не удалось выполнить с ошибкой: %s", Update.errorString());
    else LOG_INF("OTA-обновление для %s завершено", cmd == U_FLASH ? "Sketch" : "SPIFFS");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, Update.hasError() ? "ОТА-обновление не удалось, перезапуск..." : "ОТА-обновление завершено, перезапуск...");
    doRestart("Перезагрузка после ОТА");
  } else {
    File uf = fp.open(inFileName, FILE_WRITE);
    if (!uf) LOG_WRN("Не удалось открыть %s в хранилище.", inFileName);
    else {
      do {
        bytesRead = httpd_req_recv(req, jsonBuff, rxSize);
        if (bytesRead < 0) {
          if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
            delay(10);
            continue;
          } else {
            LOG_WRN("Не удалось загрузить запрос со статусом %i.", bytesRead);
            break;
          }
        }
        uf.write((const uint8_t*)jsonBuff, bytesRead);
      } while (bytesRead > 0);
      uf.close();
      res = bytesRead < 0 ? ESP_FAIL : ESP_OK;
      httpd_resp_sendstr(req, res == ESP_OK ? "Загруженный файл завершен." : "Не удалось загрузить файл, повторите попытку.");
      if (res == ESP_OK) LOG_INF("Загруженный файл %s", inFileName);
      else LOG_WRN("Не удалось загрузить файл %s.", inFileName);
    }
  }
  return res;
}
static esp_err_t setupHandler(httpd_req_t *req) {
  int w = (netMode == 0) ? WiFi.scanNetworks() : 0;
  char* p = jsonBuff;
  p += sprintf(p, "{\"сети\":[");
  for (int i = 0; i < w; ++i) {
    p += sprintf(p, "{\"ssid\":\"%s\",\"encryption\":\"%s\",\"strength\":\"%ld\"},", WiFi.SSID(i).c_str(), getEncType(i), WiFi.RSSI(i));
  }
  p += sprintf(p-1, "]}");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "ПОЛУЧИТЬ, ОТПРАВИТЬ, ОПЦИИ");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_sendstr(req, jsonBuff);
  return ESP_OK;
}
void showHttpHeaders(httpd_req_t *req) {
  LOG_DBG("HTTP: %s %s", HTTP_METHOD_STRING(req->method), req->uri);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
  size_t maxHdrLen = max(CONFIG_HTTPD_MAX_REQ_HDR_LEN, CONFIG_HTTPD_MAX_URI_LEN);
#else
  size_t maxHdrLen = max(HTTPD_MAX_REQ_HDR_LEN, HTTPD_MAX_URI_LEN);
#endif
  uint32_t req_hdrs_count = *((uint8_t*)req->aux + 4 + maxHdrLen + 1 + 3 + 4 + 4 + 4 + 1 + 3);
  char* header = (char*)req->aux + 4;
  while (req_hdrs_count--) {
    LOG_DBG("  %s", header);
    header += strlen(header) + 2;
  }
}
static esp_err_t sendCrossOriginHeader(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST,GET,HEAD,ОПЦИИ");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
  httpd_resp_set_status(req, "204");
  httpd_resp_sendstr(req, NULL);
  return ESP_OK;
}
bool wsAsyncSendText(const char* wsData) {
  if (fdWs >= 0) {
    httpd_ws_frame_t wsPkt;
    wsPkt.payload = (uint8_t*)wsData;
    wsPkt.len = strlen(wsData);
    wsPkt.type = HTTPD_WS_TYPE_TEXT;
    wsPkt.final = true;
    esp_err_t ret = httpd_ws_send_frame_async(httpServer, fdWs, &wsPkt);
    if (ret != ESP_OK) LOG_WRN("Ошибка отправки веб-сокета с %s", esp_err_to_name(ret));
    return ret == ESP_OK ? true : false;
  }
  return false;
}
void wsAsyncSendBinary(uint8_t* data, size_t len) {
  if (fdWs >= 0) {
    if (data == NULL || len == 0) {
      LOG_WRN("Неверные данные или длина: data=%p, len=%u.", data, len);
      return;
    }
    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
    wsPkt.type = HTTPD_WS_TYPE_BINARY;
    wsPkt.payload = data;
    wsPkt.len = len;
    esp_err_t ret = httpd_ws_send_frame_async(httpServer, fdWs, &wsPkt);
    if (ret != ESP_OK) LOG_WRN("Ошибка отправки веб-сокета с %s", esp_err_to_name(ret));
  }
}
static esp_err_t wsHandler(httpd_req_t *req) {
  esp_err_t ret = ESP_OK;
  if (req->method == HTTP_GET) {
    if (fdWs != -1) {
      if (fdWs != httpd_req_to_sockfd(req)) {
        LOG_WRN("закрытие соединения, как более новый Websocket на %u", httpd_req_to_sockfd(req));
        killSocket();
      }
    }
    fdWs = httpd_req_to_sockfd(req);
    if (fdWs < 0) {
      LOG_WRN("не удалось получить номер сокета");
      ret = ESP_FAIL;
    } else LOG_INF("Соединение через веб-сокет: %d", fdWs);
  } else {
    httpd_ws_frame_t wsPkt;
    uint8_t wsMsg[MAX_PAYLOAD_LEN];
    memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
    wsPkt.payload = wsMsg;
    ret = httpd_ws_recv_frame(req, &wsPkt, MAX_PAYLOAD_LEN);
    if (ret == ESP_OK) {
      if (wsPkt.len >= MAX_PAYLOAD_LEN) LOG_ERR("Полезная нагрузка веб-сокета слишком длинная %d", wsPkt.len);
      wsMsg[wsPkt.len] = 0;
      if (wsPkt.type == HTTPD_WS_TYPE_BINARY && wsPkt.len) appSpecificWsBinHandler(wsMsg, wsPkt.len);
      else if (wsPkt.type == HTTPD_WS_TYPE_TEXT) appSpecificWsHandler((const char*)wsMsg);
      else if (wsPkt.type == HTTPD_WS_TYPE_CLOSE) appSpecificWsHandler("X");
    } else LOG_ERR("Не удалось получить веб-сокет с %s", esp_err_to_name(ret));
  }
  return ret;
}
void killSocket(int skt) {
  if (skt == -99) {
    skt = fdWs;
    fdWs = -1;
  }
  if (skt >= 0) httpd_sess_trigger_close(httpServer, skt);
}
static esp_err_t customOrNotFoundHandler(httpd_req_t *req, httpd_err_code_t err) {
  if (req->method == HTTP_OPTIONS) sendCrossOriginHeader(req);
#if INCLUDE_WEBDAV
  if (strncmp(req->uri, WEBDAV, strlen(WEBDAV)) == 0) return handleWebDav(req) ? ESP_OK : ESP_FAIL;
#endif
  httpd_resp_send_404(req);
  return ESP_FAIL;
}
bool startWebServer() {
  esp_err_t res = ESP_FAIL;
  chunk = psramFound() ? (byte*)ps_malloc(CHUNKSIZE) : (byte*)malloc(CHUNKSIZE);
#if INCLUDE_CERTS
  if (useHttps) {
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32S3
    config.httpd.stack_size = SERVER_STACK_SIZE;
#endif
    config.prvtkey_pem = (const uint8_t*)serverCerts[0];
    config.prvtkey_len = strlen(serverCerts[0]) + 1;
    config.servercert = (const uint8_t*)serverCerts[1];
    config.servercert_len = strlen(serverCerts[1]) + 1;
    config.httpd.server_port = HTTPS_PORT;
    config.httpd.lru_purge_enable = true;
    config.httpd.max_uri_handlers = MAX_HANDLERS;
    config.httpd.max_open_sockets = HTTP_CLIENTS + MAX_STREAMS;
    config.httpd.task_priority = HTTP_PRI;
    res = httpd_ssl_start(&httpServer, &config);
  } else {
#else
  if (!useHttps) {
#endif
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_ESP32S3
    config.stack_size = SERVER_STACK_SIZE;
#endif
    config.server_port = HTTP_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = MAX_HANDLERS;
    config.max_open_sockets = HTTP_CLIENTS + MAX_STREAMS;
    config.task_priority = HTTP_PRI;
    res = httpd_start(&httpServer, &config);
  }
  httpd_uri_t indexUri = {.uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = NULL};
  httpd_uri_t webUri = {.uri = "/web", .method = HTTP_GET, .handler = webHandler, .user_ctx = NULL};
  httpd_uri_t controlUri = {.uri = "/control", .method = HTTP_GET, .handler = controlHandler, .user_ctx = NULL};
  httpd_uri_t updateUri = {.uri = "/update", .method = HTTP_POST, .handler = updateHandler, .user_ctx = NULL};
  httpd_uri_t statusUri = {.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = NULL};
  httpd_uri_t uploadUri = {.uri = "/upload", .method = HTTP_POST, .handler = uploadHandler, .user_ctx = NULL};
  httpd_uri_t wifiUri = {.uri = "/wifi", .method = HTTP_GET, .handler = setupHandler, .user_ctx = NULL};
  httpd_uri_t sseUri = {.uri = "/sse", .method = HTTP_GET, .handler = sseHandler, .user_ctx = NULL};
  httpd_uri_t wsUri = {.uri = "/ws", .method = HTTP_GET, .handler = wsHandler, .user_ctx = NULL, .is_websocket = true};
  httpd_uri_t sustainUri = {.uri = "/sustain", .method = HTTP_GET, .handler = appSpecificSustainHandler, .user_ctx = NULL};
  httpd_uri_t checkUri = {.uri = "/sustain", .method = HTTP_HEAD, .handler = appSpecificSustainHandler, .user_ctx = NULL};
  if (res == ESP_OK) {
    httpd_register_uri_handler(httpServer, &indexUri);
    httpd_register_uri_handler(httpServer, &webUri);
    httpd_register_uri_handler(httpServer, &controlUri);
    httpd_register_uri_handler(httpServer, &updateUri);
    httpd_register_uri_handler(httpServer, &statusUri);
    httpd_register_uri_handler(httpServer, &uploadUri);
    httpd_register_uri_handler(httpServer, &sseUri);
    httpd_register_uri_handler(httpServer, &wifiUri);
    httpd_register_uri_handler(httpServer, &wsUri);
    httpd_register_uri_handler(httpServer, &sustainUri);
    httpd_register_uri_handler(httpServer, &checkUri);
    httpd_register_err_handler(httpServer, HTTPD_404_NOT_FOUND, customOrNotFoundHandler);
    LOG_INF("Запуск веб-сервера на порту: %u", useHttps ? HTTPS_PORT : HTTP_PORT);
    LOG_INF("Сертификаты удаленного сервера %s проверены.", useSecure ? "are" : "not");
    if (DEBUG_MEM) {
      uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
      LOG_INF("Пространство стека httpServer задачи %u", freeStack);
    }
  } else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Не удалось запустить веб-сервер %s.",espErrMsg(res));
  if (!DBG_ON) esp_log_level_set("*", ESP_LOG_NONE);
  debugMemory("startWebserver");
  if (strlen(startupFailure)) {
    LOG_WRN("%s", startupFailure);
    return false;
  }
  return true;
}