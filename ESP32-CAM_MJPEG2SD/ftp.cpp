#include "appGlobals.h"
#if INCLUDE_FTP_HFS
#if (!INCLUDE_CERTS)
const char* hfs_rootCACertificate = "";
const char* ftps_rootCACertificate = "";
#endif
char fsServer[MAX_HOST_LEN];
uint16_t fsPort = 21;
char FS_Pass[MAX_PWD_LEN];
char fsWd[FILE_NAME_LEN];
static bool uploadInProgress = false;
uint8_t percentLoaded = 0;
TaskHandle_t fsHandle = NULL;
static char storedPathName[FILE_NAME_LEN];
static char folderPath[FILE_NAME_LEN];
static byte* fsChunk;
bool deleteAfter = false;
bool autoUpload = false;
bool fsUse = false;
#define CONTENT_TYPE "Тип контента: \"%s\"\r\n\r\n"
#define POST_HDR "POST /%s HTTP/1.1\r\nХост: %s\r\nContent-Length: %u\r\n" CONTENT_TYPE
#define MULTI_TYPE "multipart/форма-данные; граница=" BOUNDARY_VAL
#define JSON_TYPE "application/json"
#define BIN_TYPE "application/octet-stream"
#define FORM_DATA "--" BOUNDARY_VAL "\r\nContent-disposition: данные формы; name=\"%s%s\"\r\n" CONTENT_TYPE
#define END_BOUNDARY "\r\n--" BOUNDARY_VAL "--\r\n"
#define FILE_NAME "файл\"; имя_файла=\""
#define JSON_DATA "{\"pathname\":\"%s%s/%s\",\"passcode\":\"%s\"}"
#define FORM_OFFSET 256
NetworkClientSecure hclient;
char* fsBuff;
static void postHeader(const char* tmethod, const char* contentType, bool isFile,
  size_t fileSize, const char* fileName) {
  char* p = fsBuff + FORM_OFFSET;
  if (isFile) {
    p += sprintf(p, FORM_DATA, "json", "", JSON_TYPE);
    p += sprintf(p, JSON_DATA, fsWd, folderPath, fileName, FS_Pass);
    p += sprintf(p, "\r\n" FORM_DATA, FILE_NAME, fileName, BIN_TYPE);
  }
  size_t formLen = strlen(fsBuff + FORM_OFFSET);
  p = fsBuff;
  if (isFile) fileSize += formLen + strlen(END_BOUNDARY);
  p += sprintf(p, POST_HDR, tmethod, fsServer, fileSize, isFile ? MULTI_TYPE : JSON_TYPE);
  size_t reqLen = strlen(fsBuff);
  if (formLen) {
    memmove(fsChunk + reqLen, fsChunk + FORM_OFFSET, formLen);
    fsChunk[reqLen + formLen] = 0;
  }
  hclient.print(fsBuff);
}
static bool hfsStoreFile(File &fh) {
#ifdef ISCAM
  if (!strstr(fh.name(), AVI_EXT) && !strstr(fh.name(), CSV_EXT) && !strstr(fh.name(), SRT_EXT)) return false;
#else
  if (!strstr(fh.name(), FILE_EXT)) return false;
#endif
  LOG_INF("Загрузить файл: %s, размер: %s", fh.name(), fmtSize(fh.size()));
  postHeader("upload", BIN_TYPE, true, fh.size(), fh.name());
  uint8_t percentLoaded = 0;
  size_t chunksize = 0, totalSent = 0;
  while ((chunksize = fh.read((uint8_t*)fsChunk, CHUNKSIZE))) {
    hclient.write((uint8_t*)fsChunk, chunksize);
    totalSent += chunksize;
    if (calcProgress(totalSent, fh.size(), 5, percentLoaded)) LOG_INF("Загружено %u%%", percentLoaded);
  }
  percentLoaded = 100;
  hclient.println(END_BOUNDARY);
  return true;
}
bool useFtps = false;
char ftpUser[MAX_HOST_LEN];
static char rspBuf[256];
static char respCodeRx[4];
static fs::FS fp = STORAGE;
#define NO_CHECK "999"
NetworkClient rclient;
NetworkClient dclient;
static bool sendFtpCommand(const char* cmd, const char* param, const char* respCode, const char* respCode2 = NO_CHECK) {
  if (strlen(cmd)) {
    rclient.print(cmd);
    rclient.println(param);
  }
  LOG_VRB("Отправлен командный запрос: %s%s", cmd, param);
  uint32_t start = millis();
  while (!rclient.available() && millis() < start + (responseTimeoutSecs * 1000)) delay(1);
  if (!rclient.available()) {
    LOG_WRN("Тайм-аут ответа FTP-сервера");
    return false;
  }
  rclient.read((uint8_t*)respCodeRx, 3);
  respCodeRx[3] = 0;
  int readLen = rclient.read((uint8_t*)rspBuf, 255);
  rspBuf[readLen] = 0;
  while (rclient.available()) rclient.read();
  LOG_VRB("Код приема: %s, соответственно: %s", respCodeRx, rspBuf);
  if (strcmp(respCode, NO_CHECK) == 0) return true;
  if (strcmp(respCodeRx, respCode) != 0) {
    if (strcmp(respCodeRx, respCode2) != 0) {
      LOG_ERR("Команда %s получила неправильный ответ: %s %s", cmd, respCodeRx, rspBuf);
      return false;
    }
  }
  return true;
}
static bool ftpConnect() {
  if (rclient.connect(fsServer, fsPort)) {LOG_VRB("FTP подключен по адресу %s:%u", fsServer, fsPort);}
  else {
    LOG_WRN("Ошибка при открытии ftp-подключения к %s:%u.", fsServer, fsPort);
    return false;
  }
  if (!sendFtpCommand("", "", "220")) return false;
  if (useFtps) {
    if (sendFtpCommand("AUTH ", "TLS", "234")) {
    } else LOG_WRN("FTPS недоступен");
  }
  if (!sendFtpCommand("USER ", ftpUser, "331")) return false;
  if (!sendFtpCommand("PASS ", FS_Pass, "230")) return false;
  if (!sendFtpCommand("CWD ", fsWd, "250")) return false;
  if (!sendFtpCommand("Тип I", "", "200")) return false;
  return true;
}
static void ftpDisconnect() {
  rclient.println("QUIT");
  dclient.stop();
  rclient.stop();
}
static bool ftpCreateFolder(const char* folderName) {
  LOG_VRB("Проверьте папку %s", folderName);
  sendFtpCommand("CWD ", folderName, NO_CHECK);
  if (strcmp(respCodeRx, "550") == 0) {
    if (!sendFtpCommand("MKD ", folderName, "257")) return false;
    if (!sendFtpCommand("CWD ", folderName, "250")) return false;
  }
  return true;
}
static bool openDataPort() {
  if (!sendFtpCommand("PASV", "", "227")) return false;
  char* p = strchr(rspBuf, '(');
  int p1, p2;
  int items = sscanf(p, "(%*d,%*d,%*d,%*d,%d,%d)", &p1, &p2);
  if (items != 2) {
    LOG_ERR("Не удалось проанализировать порт данных");
    return false;
  }
  int dataPort = (p1 << 8) + p2;
  LOG_VRB("Порт данных: %i", dataPort);
  if (!dclient.connect(fsServer, dataPort)) {
    LOG_WRN("Соединение для передачи данных не удалось");
    return false;
  }
  return true;
}
static bool ftpStoreFile(File &fh) {
#ifdef ISCAM
  if (!strstr(fh.name(), AVI_EXT) && !strstr(fh.name(), CSV_EXT) && !strstr(fh.name(), SRT_EXT)) return false;
#else
  if (!strstr(fh.name(), FILE_EXT)) return false;
#endif
  char ftpSaveName[FILE_NAME_LEN];
  strcpy(ftpSaveName, fh.name());
  size_t fileSize = fh.size();
  LOG_INF("Загрузить файл: %s, размер: %s", ftpSaveName, fmtSize(fileSize));
  openDataPort();
  uint32_t writeBytes = 0;
  uint32_t uploadStart = millis();
  size_t readLen, writeLen;
  if (!sendFtpCommand("STOR ", ftpSaveName, "150", "125")) return false;
  do {
    readLen = fh.read(fsChunk, CHUNKSIZE);
    if (readLen) {
      writeLen = dclient.write((const uint8_t*)fsChunk, readLen);
      writeBytes += writeLen;
      if (writeLen == 0) {
        LOG_WRN("Загрузить файл на ftp не удалось");
        return false;
      }
      if (calcProgress(writeBytes, fileSize, 5, percentLoaded)) LOG_INF("Загружено %u%%", percentLoaded);
    }
  } while (readLen > 0);
  dclient.stop();
  percentLoaded = 100;
  bool res = sendFtpCommand("", "", "226");
  if (res) {
    LOG_ALT("Загружено %s через %u сек.", fmtSize(writeBytes), (millis() - uploadStart) / 1000);
  } else LOG_WRN("Передача файла не удалась");
  return res;
}
static bool getFolderName(const char* folderName) {
  strcpy(folderPath, folderName);
  int pos = 1;
  bool res = true;
  for (char* p = strchr(folderPath, '/'); (p = strchr(++p, '/')) != NULL; pos = p + 1 - folderPath) {
    *p = 0;
    if (!fsUse) res = ftpCreateFolder(folderPath + pos);
  }
  return res;
}
static bool uploadFolderOrFileFs(const char* fileOrFolder) {
  fsBuff = (char*)fsChunk;
  bool res = fsUse ? remoteServerConnect(hclient, fsServer, fsPort, hfs_rootCACertificate, FSFTP) : ftpConnect();
  if (!res) {
    LOG_WRN("Невозможно подключиться к серверу %s.", fsUse ? "HTTPS" : "FTP");
    return false;
  }
  res = false;
  const int saveRefreshVal = refreshVal;
  refreshVal = 1;
  File root = fp.open(fileOrFolder);
  if (!root.isDirectory()) {
    char fsSaveName[FILE_NAME_LEN];
    strcpy(fsSaveName, root.path());
    if (getFolderName(root.path())) res = fsUse ? hfsStoreFile(root) : ftpStoreFile(root);
#ifdef ISCAM
    if (res) {
      changeExtension(fsSaveName, CSV_EXT);
      if (fp.exists(fsSaveName)) {
        File csv = fp.open(fsSaveName);
        res = fsUse ? hfsStoreFile(csv) : ftpStoreFile(csv);
        csv.close();
      }
      changeExtension(fsSaveName, SRT_EXT);
      if (fp.exists(fsSaveName)) {
        File srt = fp.open(fsSaveName);
        res = fsUse ? hfsStoreFile(srt) : ftpStoreFile(srt);
        srt.close();
      }
    }
    if (!res) LOG_WRN("Не удалось загрузить: %s", fsSaveName);
#endif
  } else {
    LOG_INF("Папка для загрузки: ", root.name());
    strncpy(folderPath, root.name(), FILE_NAME_LEN - 1);
    res = fsUse ? true : ftpCreateFolder(root.name());
    if (!res) {
      refreshVal = saveRefreshVal;
      return false;
    }
    File fh = root.openNextFile();
    while (fh) {
      res = fsUse ? hfsStoreFile(fh) : ftpStoreFile(fh);
      if (!res) break;
      fh.close();
      fh = root.openNextFile();
    }
    if (fh) fh.close();
  }
  refreshVal = saveRefreshVal;
  root.close();
  fsUse ? remoteServerClose(hclient) : ftpDisconnect();
  return res;
}
static void fileServerTask(void* parameter) {
#ifdef ISCAM
  doPlayback = false;
#endif
  fsChunk = psramFound() ? (byte*)ps_malloc(CHUNKSIZE) : (byte*)malloc(CHUNKSIZE);
  if (strlen(storedPathName) >= 2) {
    File root = fp.open(storedPathName);
    if (!root) LOG_WRN("Не удалось открыть: %s", storedPathName);
    else {
      bool res = uploadFolderOrFileFs(storedPathName);
      if (res && deleteAfter) deleteFolderOrFile(storedPathName);
    }
  } else LOG_VRB("Root or null is not allowed %s", storedPathName);
  uploadInProgress = false;
  free(fsChunk);
  fsHandle = NULL;
  vTaskDelete(NULL);
}
bool fsStartTransfer(const char* fileFolder) {
  setFolderName(fileFolder, storedPathName);
  if (!uploadInProgress) {
    uploadInProgress = true;
    if (fsHandle == NULL) xTaskCreate(&fileServerTask, "fileServerTask", FS_STACK_SIZE, NULL, FTP_PRI, &fsHandle);
    debugMemory("fsStartTransfer");
    return true;
  } else LOG_WRN("Невозможно передать %s, так как выполняется еще одна передача.", storedPathName);
  return false;
}
void prepUpload() {
  LOG_INF("Для загрузки файлов будет использоваться сервер %s.", fsUse ? "HTTPS" : "FTP");
}
#endif