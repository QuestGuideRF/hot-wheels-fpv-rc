#include "appGlobals.h"
#include "ff.h"
#include "vfs_fat_internal.h"
int sdMinCardFreeSpace = 100;
int sdFreeSpaceMode = 1;
bool formatIfMountFailed = true;
static bool use1bitMode = true;
static fs::FS fp = STORAGE;
#if (!CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S2)
static int sdmmcFreq = BOARD_MAX_SDMMC_FREQ;
#endif
static std::vector<std::string> fileVec;
static auto currentDir = "/~текущий";
static auto previousDir = "/~предыдущий";
static char fsType[10] = {0};
static void infoSD() {
#if (!CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S2)
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) LOG_WRN("SD-карта не подключена");
  else {
    char typeStr[8] = "UNKNOWN";
    if (cardType == CARD_MMC) strcpy(typeStr, "MMC");
    else if (cardType == CARD_SD) strcpy(typeStr, "SDSC");
    else if (cardType == CARD_SDHC) strcpy(typeStr, "SDHC");
    LOG_INF("Тип SD-карты %s, размер: %s, битовый режим %d при %u МГц", typeStr, fmtSize(SD_MMC.cardSize()), use1bitMode ? 1 : 4, sdmmcFreq / 1000);
  }
#endif
}
static bool prepSD_MMC() {
  bool res = false;
#if (!CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S2)
  if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM);
  fileVec.reserve(1000);
  if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
#if CONFIG_IDF_TARGET_ESP32S3
#if !defined(SD_MMC_CLK)
  LOG_WRN("Контакты SD-карты не определены");
  return false;
#else
 #if defined(SD_MMC_D1)
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0, SD_MMC_D1, SD_MMC_D2, SD_MMC_D3);
  use1bitMode = false;
 #else
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
 #endif
#endif
#endif
  res = SD_MMC.begin("/sdcard", use1bitMode, formatIfMountFailed, sdmmcFreq);
#if defined(CAMERA_MODEL_AI_THINKER)
  pinMode(4, OUTPUT);
  digitalWrite(4, 0);
#endif
  if (res) {
    fp.mkdir(DATA_DIR);
    infoSD();
  } else LOG_WRN("Монтирование SD-карты не удалось");
#endif
  return res;
}
static void listFolder(const char* rootDir) {
  LOG_INF("Размер эскиза %s", fmtSize(ESP.getSketchSize()));
  File root = fp.open(rootDir);
  File file = root.openNextFile();
  while (file) {
    LOG_INF("Файл: %s, размер: %s", file.path(), fmtSize(file.size()));
    file = root.openNextFile();
  }
  char totalBytes[20];
  strcpy(totalBytes, fmtSize(STORAGE.totalBytes()));
  LOG_INF("%s: %s используется из %s", fsType, fmtSize(STORAGE.usedBytes()), totalBytes);
}
bool startStorage() {
  bool res = false;
#if (!CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S2)
  if ((fs::SDMMCFS*)&STORAGE == &SD_MMC) {
    strcpy(fsType, "SD_MMC");
    res = prepSD_MMC();
    if (res) listFolder(DATA_DIR);
    else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Проверьте, вставлена ​​ли SD-карта");
    debugMemory("startStorage");
    return res;
  }
#endif
  if (!strlen(fsType)) {
#ifdef _SPIFFS_H_
    if ((fs::SPIFFSFS*)&STORAGE == &SPIFFS) {
      strcpy(fsType, "SPIFFS");
      res = SPIFFS.begin(formatIfMountFailed);
    }
#endif
#ifdef _LITTLEFS_H_
    if ((fs::LittleFSFS*)&STORAGE == &LittleFS) {
      strcpy(fsType, "LittleFS");
      res = LittleFS.begin(formatIfMountFailed);
      if (res) LittleFS.mkdir(DATA_DIR);
    }
#endif
    if (res) {
      const char* rootDir = !strcmp(fsType, "LittleFS") ? DATA_DIR : "/";
      listFolder(rootDir);
    }
  } else {
    snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Не удалось смонтировать %s.", fsType);
    dataFilesChecked = true;
  }
  debugMemory("startStorage");
  return res;
}
static void getOldestDir(char* oldestDir) {
  File root = fp.open("/");
  File file = root.openNextFile();
  if (file) strcpy(oldestDir, file.path());
  while (file) {
    if (file.isDirectory() && strstr(file.name(), "System") == NULL
        && strstr(DATA_DIR, file.name()) == NULL) {
      if (strcmp(oldestDir, file.path()) > 0) strcpy(oldestDir, file.path());
    }
    file = root.openNextFile();
  }
}
void inline getFileDate(File& file, char* fileDate) {
  time_t writeTime = file.getLastWrite();
  struct tm lt;
  localtime_r(&writeTime, &lt);
  strftime(fileDate, sizeof(fileDate), "%Y-%m-%d %H:%M:%S", &lt);
}
bool checkFreeStorage() {
  bool res = false;
  size_t freeSize = (size_t)((STORAGE.totalBytes() - STORAGE.usedBytes()) / ONEMEG);
  if (!sdFreeSpaceMode && freeSize < sdMinCardFreeSpace)
    LOG_WRN("Оставшееся пространство %uМБ меньше минимального %uМБ", freeSize, sdMinCardFreeSpace);
  else {
    while (freeSize < sdMinCardFreeSpace) {
      char oldestDir[FILE_NAME_LEN];
      getOldestDir(oldestDir);
      LOG_WRN("Удаление самой старой папки: %s %s", oldestDir, sdFreeSpaceMode == 2 ? "после загрузки" : "");
#if INCLUDE_FTP_HFS
      if (sdFreeSpaceMode == 2) fsStartTransfer(oldestDir);
#endif
      deleteFolderOrFile(oldestDir);
      freeSize = (size_t)((STORAGE.totalBytes() - STORAGE.usedBytes()) / ONEMEG);
    }
    LOG_INF("Свободное место в хранилище: %s", fmtSize(STORAGE.totalBytes() - STORAGE.usedBytes()));
    res = true;
  }
  return res;
}
void setFolderName(const char* fname, char* fileName) {
  char partName[FILE_NAME_LEN];
  if (strchr(fname, '~') != NULL) {
    if (!strcmp(fname, currentDir)) {
      dateFormat(partName, sizeof(partName), true);
      strcpy(fileName, partName);
      LOG_INF("Текущий каталог установлен на %s.", fileName);
    }
    else if (!strcmp(fname, previousDir)) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      struct tm* tm = localtime(&tv.tv_sec);
      tm->tm_mday -= 1;
      time_t prev = mktime(tm);
      strftime(partName, sizeof(partName), "/%Y%m%d", localtime(&prev));
      strcpy(fileName, partName);
      LOG_INF("Предыдущий каталог установлен на %s.", fileName);
    } else strcpy(fileName, "");
  } else strcpy(fileName, fname);
}
bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension) {
  bool hasExtension = false;
  char partJson[200];
  bool noEntries = true;
  char fileName[FILE_NAME_LEN];
  setFolderName(fname, fileName);
  if (strstr(fileName, extension) != NULL) {
    hasExtension = true;
    noEntries = true;
    strcpy(jsonBuff, "{}");
  } else {
    bool returnDirs = strlen(fileName) > 1 ? (strchr(fileName+1, '/') == NULL ? false : true) : true;
    File root = fp.open(fileName);
    if (strlen(fileName)) {
      if (!root) LOG_WRN("Не удалось открыть каталог %s.", fileName);
      else if (!root.isDirectory()) LOG_WRN("Не каталог %s", fileName);
      LOG_VRB("Получение %s в %s", returnDirs ? "folders" : "files", fileName);
    }
    strcpy(jsonBuff, returnDirs ? "{" : "{\"/\":\".. [ Up ]\",");
    File file = root.openNextFile();
    if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM);
    while (file) {
      if (returnDirs && file.isDirectory() && strstr(DATA_DIR, file.name()) == NULL) {
        sprintf(partJson, "\"%s\":\"%s\",", file.path(), file.name());
        fileVec.push_back(std::string(partJson));
        noEntries = false;
      }
      if (!returnDirs && !file.isDirectory()) {
        if (strstr(file.name(), extension) != NULL) {
          sprintf(partJson, "\"%s\":\"%s %s\",", file.path(), file.name(), fmtSize(file.size()));
          fileVec.push_back(std::string(partJson));
          noEntries = false;
        }
      }
      file = root.openNextFile();
    }
    if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
  }
  if (noEntries && !hasExtension) sprintf(jsonBuff, "{\"/\":\"List folders\",\"%s\":\"Go to current (today)\",\"%s\":\"Go to previous (yesterday)\"}", currentDir, previousDir);
  else {
    sort(fileVec.begin(), fileVec.end(), std::greater<std::string>());
    for (auto fileInfo : fileVec) {
      if (strlen(jsonBuff) + strlen(fileInfo.c_str()) < jsonBuffLen) strcat(jsonBuff, fileInfo.c_str());
      else {
        LOG_WRN("Слишком много папок/файлов для перечисления %u+%u в %u байт.", strlen(jsonBuff), strlen(partJson), jsonBuffLen);
        break;
      }
    }
    jsonBuff[strlen(jsonBuff)-1] = '}';
  }
  fileVec.clear();
  return hasExtension;
}
static void deleteOthers(const char* baseFile) {
#ifdef ISCAM
  char otherDeleteName[FILE_NAME_LEN];
  strcpy(otherDeleteName, baseFile);
  changeExtension(otherDeleteName, CSV_EXT);
  if (STORAGE.remove(otherDeleteName)) LOG_INF("Файл %s удален.", otherDeleteName);
  changeExtension(otherDeleteName, SRT_EXT);
  if (STORAGE.remove(otherDeleteName)) LOG_INF("Файл %s удален.", otherDeleteName);
#endif
}
void deleteFolderOrFile(const char* deleteThis) {
  char fileName[FILE_NAME_LEN];
  setFolderName(deleteThis, fileName);
  File df = fp.open(fileName);
  if (!df) {
    LOG_WRN("Не удалось открыть %s.", fileName);
    return;
  }
  if (df.isDirectory() && (strstr(fileName, "System") != NULL
      || strstr("/", fileName) != NULL)) {
    df.close();
    LOG_WRN("Удаление %s запрещено.", fileName);
    delay(1000);
    return;
  }
  LOG_INF("Удаление: %s", fileName);
  if (df.isDirectory() || ((!strcmp(fsType, "SPIFFS")) && strstr("/", fileName) != NULL)) {
    LOG_INF("Содержимое папки %s", fileName);
    File file = df.openNextFile();
    while (file) {
      char filepath[FILE_NAME_LEN];
      strcpy(filepath, file.path());
      if (file.isDirectory()) LOG_INF("  КАТАЛОГ: %s", filepath);
      else {
        size_t fSize = file.size();
        file.close();
        LOG_INF("  ФАЙЛ: %s Размер: %s %sудален", filepath, fmtSize(fSize), STORAGE.remove(filepath) ? "" : "not ");
        deleteOthers(filepath);
      }
      file = df.openNextFile();
    }
    if (df.isDirectory()) LOG_ALT("Папка %s %s удалена.", fileName, STORAGE.rmdir(fileName) ? "" : "not ");
    else df.close();
  } else {
    df.close();
    LOG_ALT("Файл %s %s удален.", deleteThis, STORAGE.remove(deleteThis) ? "" : "not ");
    deleteOthers(deleteThis);
  }
}
#define BLOCKSIZE 512
static esp_err_t writeHeader(File& inFile, httpd_req_t* req) {
  char tarHeader[BLOCKSIZE] = {0};
  strncpy(tarHeader, inFile.name(), 99);
  sprintf(tarHeader + 100, "0000666");
  sprintf(tarHeader + 124, "%011o", inFile.size());
  memcpy(tarHeader + 148, "        ", 8);
  tarHeader[156] = '0';
  strcpy(tarHeader + 257, "ustar");
  memcpy(tarHeader + 263, "00", 2);
  uint32_t checksum = 0;
  for (const auto& ch : tarHeader) checksum += ch;
  sprintf(tarHeader + 148, "%06lо", checksum);
  return httpd_resp_send_chunk(req, tarHeader, BLOCKSIZE);
}
esp_err_t downloadFile(File& df, httpd_req_t* req) {
  esp_err_t res = ESP_OK;
  bool needZip = false;
  char downloadName[FILE_NAME_LEN];
  strcpy(downloadName, df.name());
  size_t downloadSize = df.size();
  char fsSavePath[FILE_NAME_LEN];
  strcpy(fsSavePath, inFileName);
#ifdef ISCAM
  changeExtension(fsSavePath, CSV_EXT);
  needZip = STORAGE.exists(fsSavePath);
  const char* extensions[3] = {AVI_EXT, CSV_EXT, SRT_EXT};
  if (needZip) {
    downloadSize = 0;
    for (const auto& ext : extensions) {
      changeExtension(fsSavePath, ext);
      File inFile = STORAGE.open(fsSavePath, FILE_READ);
      if (inFile) {
        downloadSize += (((inFile.size() + BLOCKSIZE - 1) / BLOCKSIZE) * BLOCKSIZE) + BLOCKSIZE;
        strcpy(downloadName, inFile.name());
        inFile.close();
      }
    }
    downloadSize += BLOCKSIZE * 2;
    changeExtension(downloadName, "zip");
  }
#endif
  LOG_INF("Скачать файл: %s, размер: %s", downloadName, fmtSize(downloadSize));
  httpd_resp_set_type(req, "application/octet-stream");
  char contentDisp[IN_FILE_NAME_LEN + 50];
  snprintf(contentDisp, sizeof(contentDisp) - 1, "вложение; имя_файла=%s", downloadName);
  httpd_resp_set_hdr(req, "Content-Disposition", contentDisp);
  char contentLength[10];
  snprintf(contentLength, sizeof(contentLength) - 1, "%i", downloadSize);
  httpd_resp_set_hdr(req, "Content-Length", contentLength);
  if (needZip) {
#ifdef ISCAM
    for (const auto& ext : extensions) {
      changeExtension(fsSavePath, ext);
      File inFile = STORAGE.open(fsSavePath, FILE_READ);
      if (inFile) {
        res = writeHeader(inFile, req);
        if (res == ESP_OK) res = sendChunks(inFile, req, false);
        if (res == ESP_OK) {
          size_t remainingBytes = inFile.size() % BLOCKSIZE;
          if (remainingBytes) {
            char zeroBlock[BLOCKSIZE - remainingBytes] = {};
            res = httpd_resp_send_chunk(req, zeroBlock, sizeof(zeroBlock));
          }
          inFile.close();
        }
      }
    }
    char zeroBlock[BLOCKSIZE] = {};
    res = httpd_resp_send_chunk(req, zeroBlock, BLOCKSIZE);
    res = httpd_resp_send_chunk(req, zeroBlock, BLOCKSIZE);
    res = httpd_resp_sendstr_chunk(req, NULL);
#endif
  } else res = sendChunks(df, req);
  return res;
}
bool formatSDcard() {
  LOG_INF("Отформатируйте SD-карту, подождите...");
  char drv[3] = {'0', ':', 0};
  const size_t workbuf_size = 4096;
  void* workbuf = NULL;
  size_t allocation_unit_size = 4 * 1024;
  int sector_size_default = 512;
  workbuf = ff_memalloc(workbuf_size);
  if (workbuf == NULL) {
    LOG_ERR("память рабочей буферной памяти не выделена");
    return false;
  }
  size_t alloc_unit_size = esp_vfs_fat_get_allocation_unit_size(
      sector_size_default, allocation_unit_size);
  const MKFS_PARM opt = {(BYTE)FM_ANY, 0, 0, 0, alloc_unit_size};
  FRESULT res = f_mkfs(drv, &opt, workbuf, workbuf_size);
  ff_memfree(workbuf);
  if (res != FR_OK) LOG_ERR("Не удалось отформатировать SD-карту");
  else LOG_INF("SD-карта отформатирована с размером единицы размещения %d", alloc_unit_size);
  return res != FR_OK ? false : true;
}