#include "appGlobals.h"
#if INCLUDE_TGRAM
#define TELEGRAM_HOST "api.telegram.org"
#define LONG_POLL 60
#define MAX_HTTP_MSG 2048
#define FORM_OFFSET 256
#define MAX_TGRAM_SIZE (50 * ONEMEG)
#define HTTP_VER "HTTP/1.1"
#define HTTP_CODE HTTP_VER " %d %*s\r"
#define POST_HDR "POST /bot%s/%s " HTTP_VER "\r\nHost: " TELEGRAM_HOST "\r\nДлина контента: %u\r\nТип контента: "
#define FORM_DATA "--" BOUNDARY_VAL "\r\nContent-disposition: данные формы; имя=\""
#define CONTENT_TYPE "\"; filename=\"%s\"\r\nContent-Type: \"%s\"\r\n\r\n"
#define MULTI_TYPE "multipart/форма-данные; граница=" BOUNDARY_VAL
#define JSON_TYPE "application/json"
#define GETUP_JSON "{\"limit\":1,\"timeout\":%d,\"offset\":%ld}"
#define POST_JSON "{\"chat_id\":%s,\"text\":\"%s\n\n%s%s\n\"}"
#define PARSE_MODE ",\"parse_mode\":\"%s\"}"
#define END_BOUNDARY "\r\n--" BOUNDARY_VAL "--\r\n"
#if (!INCLUDE_CERTS)
const char* telegram_rootCACertificate = "";
#endif
bool tgramUse = false;
char tgramToken[MAX_PWD_LEN] = "";
char tgramChatId[MAX_IP_LEN] = "";
char tgramHdr[FILE_NAME_LEN];
static char keyValue[100] = "";
static char* tgramBuff = NULL;
static int32_t lastUpdate = 0;
TaskHandle_t telegramHandle = NULL;
NetworkClientSecure tclient;
static inline bool connectTelegram() {
  return remoteServerConnect(tclient, TELEGRAM_HOST, HTTPS_PORT, telegram_rootCACertificate, TGRAMCONN);
}
static bool searchJsonResponse(const char* keyName) {
  char* keyPtr = strstr(tgramBuff, keyName);
  if (keyPtr == NULL) return false;
  char* startItem = keyPtr + strlen(keyName);
  char* endItem = strchr(startItem, ',');
  int valSize = endItem - startItem;
  if (valSize > sizeof(keyValue) - 1) {
    LOG_WRN("Значение JSON Telegram слишком длинное %d", valSize);
    valSize = sizeof(keyValue - 1);
  }
  strncpy(keyValue, startItem, valSize);
  keyValue[valSize] = 0;
  return true;
}
size_t getResponseHeader(NetworkClientSecure& sclient, const char* host, int waitSecs) {
  if (!waitSecs) waitSecs = responseTimeoutSecs;
  bool endOfHeader = false;
  size_t contentLen = 0;
  int httpCode = 0;
  uint32_t startTime = millis();
  if (sclient.available()) {
    while (!endOfHeader && millis() - startTime < waitSecs * 1000) {
      if (sclient.available()) {
        String tline = sclient.readStringUntil('\n');
        endOfHeader = tline.length() > 1 ? false : true;
        if (!httpCode) sscanf(tline.c_str(), HTTP_CODE, &httpCode);
        if (!contentLen) sscanf(tline.c_str(), "Длина содержимого: %d\r", &contentLen);
      } else delay(100);
    }
    if (!endOfHeader) {
      LOG_WRN("Время ожидания ответа от %s истекло.", host);
      return 0;
    }
  }
  return contentLen;
}
static bool getTgramResponse() {
  bool haveResponse = false;
  size_t readLen = 0;
  size_t contentLen = getResponseHeader(tclient, TELEGRAM_HOST, LONG_POLL);
  if (contentLen) {
    if (contentLen >= MAX_HTTP_MSG - 1) {
      LOG_WRN("contentLen %d превышает размер буфера", contentLen);
      contentLen = MAX_HTTP_MSG - 1;
    }
    while (contentLen - readLen > 0) {
      size_t availLen = tclient.available();
      if (availLen) readLen += tclient.readBytes((uint8_t*)tgramBuff + readLen, availLen);
      delay(50);
    }
    if (readLen != contentLen) LOG_WRN("Данные Telegram %d не равны contentLength %d", readLen, contentLen);
    tgramBuff[contentLen] = 0;
    removeChar(tgramBuff, '"');
    replaceChar(tgramBuff, '}', ',');
    if (searchJsonResponse("ok:")) {
      if (strcmp(keyValue, "true")) {
        if (searchJsonResponse("description:")) LOG_WRN("Ошибка телеграммы: %s", keyValue);
        else LOG_WRN("Ошибка Telegram, но описание не получено");
      } else if (searchJsonResponse("result:")) {
        if (strcmp(keyValue, "[]")) haveResponse = true;
      }
    }
    remoteServerClose(tclient);
  }
  return haveResponse;
}
static bool sendTgramHeader(const char* tmethod, const char* contentType, const char* dataType,
  size_t fileSize, const char* fileName, const char* caption) {
  if (connectTelegram()) {
    char* p = tgramBuff + FORM_OFFSET;
    bool isFile = dataType != NULL ? true : false;
    if (isFile) {
      p += sprintf(p, FORM_DATA "чат_id\"\r\n\r\n%s", tgramChatId);
      if (caption != NULL) p += sprintf(p, "\r\n" FORM_DATA "caption\"\r\n\r\n%s", caption);
      p += sprintf(p, "\r\n" FORM_DATA "%s", dataType);
      p += sprintf(p, CONTENT_TYPE, fileName, contentType);
    }
    size_t formLen = strlen(tgramBuff + FORM_OFFSET);
    p = tgramBuff;
    if (isFile) fileSize += formLen + strlen(END_BOUNDARY);
    p += sprintf(p, POST_HDR, tgramToken, tmethod, fileSize);
    isFile ? strcat(p, MULTI_TYPE) : strcat(p, JSON_TYPE);
    strcat(p, "\r\n\r\n");
    size_t reqLen = strlen(tgramBuff);
    if (formLen) {
      memmove(tgramBuff + reqLen, tgramBuff + FORM_OFFSET, formLen);
      tgramBuff[reqLen + formLen] = 0;
    }
    tclient.print(tgramBuff);
    return true;
  }
  return false;
}
static bool sendTgramBuff(uint8_t* buffData, size_t buffSize) {
  if (connectTelegram()) {
    for (size_t i = 0; i < buffSize; i += CHUNKSIZE) tclient.write(buffData + i, min((int)(buffSize - i), CHUNKSIZE));
    tclient.println(END_BOUNDARY);
    return true;
  }
  return false;
}
bool prepTelegram() {
  if (tgramUse) {
    if (strlen(tgramToken)) {
      if (tgramBuff == NULL) tgramBuff = psramFound() ? (char*)ps_malloc(MAX_HTTP_MSG) : (char*)malloc(MAX_HTTP_MSG);
      bool res = false;
      sendTgramHeader("getMe", NULL, NULL, 0, NULL, NULL);
      uint32_t startTime = millis();
      while (!res && (millis() - startTime < responseTimeoutSecs * 1000)) {
        if (getTgramResponse()) res = true;
        delay(200);
      }
      if (res) {
        if (searchJsonResponse("username:")) {
          LOG_INF("Подключено к дескриптору бота Telegram: %s", keyValue);
          xTaskCreate(appSpecificTelegramTask, "telegramTask", TGRAM_STACK_SIZE, NULL, TGRAM_PRI, &telegramHandle);
          debugMemory("setupTelegramTask");
          return true;
        } else LOG_WRN("Ответ getMe не анализируется %s", tgramBuff);
      } else LOG_WRN("Не удалось связаться с сервером Telegram.");
    } else LOG_WRN("Токен Telegram Bot не предоставлен.");
  } else LOG_INF("Телеграм не используется");
  return false;
}
bool getTgramUpdate(char* responseText) {
  if (tclient.connected()) {
    if (getTgramResponse()) {
      if (searchJsonResponse("update_id:")) {
        int32_t update_id = atoi(keyValue);
        if (lastUpdate < update_id) {
          lastUpdate = update_id;
          if (searchJsonResponse("чат:{id:")) {
            if (!strcmp(tgramChatId, keyValue)) {
              if (searchJsonResponse("text:")) {
                strncpy(responseText, keyValue, FILE_NAME_LEN - 1);
                return true;
              }
            } else LOG_WRN("Сообщение от неизвестного идентификатора чата: %s", keyValue);
          } else LOG_WRN("Идентификатор чата не найден");
        } else LOG_WRN("Старый update_id: %d", update_id);
      }
    }
  } else {
    char* t = tgramBuff + FORM_OFFSET;
    t += sprintf(t, GETUP_JSON, LONG_POLL, lastUpdate + 1);
    sendTgramHeader("getUpdates", NULL, NULL, strlen(tgramBuff + FORM_OFFSET), NULL, NULL);
  }
  return false;
}
bool sendTgramMessage(const char* info, const char* item, const char* parseMode) {
  char* t = tgramBuff + FORM_OFFSET;
  t += sprintf(t, POST_JSON, tgramChatId, tgramHdr, info, item);
  if (strlen(parseMode)) t += sprintf(t - 1, PARSE_MODE, parseMode);
  return sendTgramHeader("sendMessage", NULL, NULL, strlen(tgramBuff + FORM_OFFSET), NULL, NULL);
}
bool sendTgramPhoto(uint8_t* photoData, size_t photoSize, const char* caption) {
  if (sendTgramHeader("sendPhoto", "image/jpeg", "photo", photoSize, "frame.jpg", caption))
    return sendTgramBuff(photoData, photoSize);
  return false;
}
bool sendTgramFile(const char* fileName, const char* contentType, const char* caption) {
  if (connectTelegram()) {
    fs::FS fp = STORAGE;
    File df = fp.open(fileName);
    char errMsg[100] = "";
    if (df) {
      if (df.size() < MAX_TGRAM_SIZE) {
        sendTgramHeader("sendDocument", contentType, "document", df.size(), fileName, caption);
        uint8_t percentLoaded = 0;
        size_t chunksize = 0, totalSent = 0;
        while ((chunksize = df.read((uint8_t*)tgramBuff, MAX_HTTP_MSG))) {
          tclient.write((uint8_t*)tgramBuff, chunksize);
          totalSent += chunksize;
          if (calcProgress(totalSent, df.size(), 5, percentLoaded)) LOG_INF("Скачано %u%%", percentLoaded);
        }
        df.close();
        tclient.println(END_BOUNDARY);
      } else snprintf(errMsg, sizeof(errMsg) - 1, "Размер файла слишком велик: %s", fmtSize(df.size()));
    } else snprintf(errMsg, sizeof(errMsg) - 1, "Файл не существует или не может быть открыт: %s", fileName);
    if (strlen(errMsg)) {
      LOG_WRN("%s", errMsg);
      sendTgramMessage("ERROR: ", errMsg, "");
    }
  } else return false;
  return true;
}
#endif