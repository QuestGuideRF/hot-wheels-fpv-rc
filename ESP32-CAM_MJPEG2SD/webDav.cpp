#include "appGlobals.h"
#if INCLUDE_WEBDAV
#define ALLOW "PROPPATCH,PROPFIND,ОПЦИИ,УДАЛЕНИЕ,ПЕРЕМЕСТИТЬ,КОПИРОВАТЬ,HEAD,POST,PUT,GET"
#define XML1 "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\">"
#define XML2 "<D:response xmlns:D=\"DAV:\"><D:href>"
#define XML3 "</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop>"
#define XML4 "</D:prop></D:propstat></D:response>"
#define XML5 "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktoken><D:href>"
#define XML6 "</D:href></D:locktoken></D:activelock></D:lockdiscovery></D:prop>"
static char pathName[IN_FILE_NAME_LEN];
static httpd_req_t* req;
static char formattedTime[80];
static const char* extensions[] = {"dummy", ".htm", ".css", ".txt", ".js", ".json", ".png", ".gif", ".jpg", ".ico", ".svg", ".xml", ".pdf", ".zip", ".gz"};
static const char* mimeTypes[] = {"application/octet-stream", "text/html", "text/html", "text/css", "text/plain", "application/javascript", "application/json", "image/png", "image/gif", "image/jpeg", "image/x-icon", "изображение/svg+xml", "text/xml", "application/pdf", "application/zip", "application/x-gzip"};
static int getMimeType(const char* path) {
  int mimePtr = 1;
  size_t len = strlen(path);
  for (const char* ext : extensions) {
    size_t slen = strlen(ext);
    if (!strncmp(path + len - slen, ext, slen)) return mimePtr;
    mimePtr++;
  }
  return 0;
}
static void formatTime(time_t t) {
  tm* timeinfo = gmtime(&t);
  strftime(formattedTime, sizeof(formattedTime), "%a, %d %b %Y %H:%M:%S по Гринвичу", timeinfo);
}
static bool haveResource(bool ignore = false) {
  if (STORAGE.exists(pathName)) return true;
  else if (!ignore) httpd_resp_send_404(req);
  return false;
}
static bool isFolder() {
  File root = STORAGE.open(pathName);
  bool res = root.isDirectory();
  root.close();
  return res;
}
static void sendContentProp(const char* prop, const char* value) {
  char propStr[strlen(prop) * 2 + strlen(value) + 15];
  sprintf(propStr, "<D:%s>%s</D:%s>", prop, value, prop);
  httpd_resp_sendstr_chunk(req, propStr);
  LOG_VRB("propStr %s", propStr);
}
static void sendPropResponse(File& file, const char* payload) {
  size_t encodeLen = 3 + strlen(file.path()) * 2;
  size_t maxLen = strlen(XML2) + encodeLen + strlen(XML3);
  char resp[maxLen + 1];
  snprintf(resp, maxLen, "%s%s%s", XML2, file.path(), XML3);
  httpd_resp_sendstr_chunk(req, resp);
  LOG_VRB("соответствующий XML: %s", resp);
  formatTime(file.getLastWrite());
  sendContentProp("getlastmodified", formattedTime);
  sendContentProp("creationdate", formattedTime);
  if (file.isDirectory()) sendContentProp("resourcetype", "<D:коллекция/>");
  else {
    char fsizeStr[15];
    sprintf(fsizeStr, "%u", file.size());
    sendContentProp("getcontentlength", fsizeStr);
    sendContentProp("getcontenttype", mimeTypes[getMimeType(file.path())]);
    httpd_resp_sendstr_chunk(req, "<тип ресурса/>");
  }
  sendContentProp("displayname", file.name());
  if (strlen(payload)) {
    if (strstr(payload, "quota-available-bytes") != NULL || strstr(payload, "quota-used-bytes") != NULL) {
      char numberStr[15];
      sprintf(numberStr, "%lлу", (uint64_t)STORAGE.totalBytes() - (uint64_t)STORAGE.usedBytes());
      sendContentProp("quota-available-bytes", numberStr);
      sprintf(numberStr, "%lлу", (uint64_t)STORAGE.usedBytes());
      sendContentProp("quota-used-bytes", numberStr);
    }
  }
  httpd_resp_sendstr_chunk(req, XML4);
}
static bool getPayload(char* payload) {
  int bytesRead = -1;
  size_t offset = 0;
  size_t psize = req->content_len;
  if (psize) {
    do {
      bytesRead = httpd_req_recv(req, payload + offset, psize - offset);
      if (bytesRead < 0) {
        if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
          delay(10);
          continue;
        } else {
          LOG_WRN("Запрос на перенос не выполнен со статусом %i", bytesRead);
          psize = 0;
          break;
        }
      } else offset += bytesRead;
    } while (bytesRead > 0);
    payload[psize] = 0;
    LOG_VRB("полезная нагрузка: %s\n", payload);
  }
  return bytesRead < 0 ? false : true;
}
static bool handleProp() {
  if (!haveResource()) return false;
  bool depth = false;
  char value[10];
  if (extractHeaderVal(req, "Depth", value) == ESP_OK) depth = (!strcmp(value, "0")) ? false : true;
  char payload[req->content_len + 1] = {0};
  if (req->content_len) getPayload(payload);
  httpd_resp_set_status(req, "207 Мультистатус");
  httpd_resp_set_type(req, "приложение/xml;кодировка=utf-8");
  httpd_resp_sendstr_chunk(req, XML1);
  File root = STORAGE.open(pathName);
  sendPropResponse(root, payload);
  if (depth && root.isDirectory()) {
    File entry = root.openNextFile();
    while (entry) {
      sendPropResponse(entry, "");
      entry.close();
      entry = root.openNextFile();
    }
  }
  root.close();
  httpd_resp_sendstr_chunk(req, "</D:мультистатус>");
  httpd_resp_sendstr_chunk(req, NULL);
  return true;
}
static bool handleOptions() {
  httpd_resp_sendstr(req, NULL);
  return true;
}
static bool handleGet() {
  if (!haveResource()) return false;
  if (isFolder()) {
    httpd_resp_send_404(req);
    return false;
  } else {
    httpd_resp_set_type(req, mimeTypes[getMimeType(pathName)]);
    strcpy(inFileName, pathName);
    esp_err_t res = fileHandler(req);
    return res == ESP_OK ? true : false;
  }
  return true;
}
static bool handleHead() {
  if (!haveResource()) return false;
  httpd_resp_sendstr(req, NULL);
  return true;
}
static bool handleLock() {
  const char* lockToken = "0123456789012345";
  httpd_resp_set_hdr(req, "Lock-Token", lockToken);
  char resp[strlen(XML5) + strlen(lockToken) + strlen(XML6) + 1];
  sprintf(resp, "%s%s%s", XML5, lockToken, XML6);
  httpd_resp_set_type(req, "приложение/xml;кодировка=utf-8");
  httpd_resp_sendstr(req, resp);
  return true;
}
static bool handleUnlock() {
  httpd_resp_set_status(req, "204 Нет содержания");
  httpd_resp_sendstr(req, NULL);
  return true;
}
static bool handlePut() {
  if (isFolder()) return false;
  if (!haveResource(true) || !req->content_len) {
    File file = STORAGE.open(pathName, FILE_WRITE);
    file.close();
    httpd_resp_set_status(req, "201 Создано");
    httpd_resp_sendstr(req, NULL);
  }
  if (req->content_len) {
    strcpy(inFileName, pathName);
    esp_err_t res = uploadHandler(req);
    return res == ESP_OK ? true : false;
  }
  return true;
}
static bool handleDelete() {
  if (!haveResource()) return false;
  deleteFolderOrFile(pathName);
  httpd_resp_sendstr(req, NULL);
  return true;
}
static bool handleMkdir() {
  if (haveResource(true)) return false;
  bool res = STORAGE.mkdir(pathName);
  if (res) httpd_resp_set_status(req, "201 Создано");
  else httpd_resp_set_status(req, "500 Внутренняя ошибка сервера");
  httpd_resp_sendstr(req, NULL);
  return res;
}
static bool checkSamePath(const char *source_path, const char *dest_path) {
  char source_dir[strlen(source_path) + 1];
  char dest_dir[strlen(dest_path) + 1];
  strncpy(source_dir, source_path, strrchr(source_path, '/') - source_path);
  source_dir[strrchr(source_path, '/') - source_path] = 0;
  strncpy(dest_dir, dest_path, strrchr(dest_path, '/') - dest_path);
  dest_dir[strrchr(dest_path, '/') - dest_path] = 0;
  return strcmp(source_dir, dest_dir) == 0;
}
static bool handleMove() {
  bool res = false;
  char dest[100];
  if (extractHeaderVal(req, "Destination", dest) == ESP_OK) {
    res = true;
    urlDecode(dest);
    char* pos = strstr(dest, WEBDAV);
    memmove(dest, pos + strlen(WEBDAV), strlen(dest));
    if (isFolder()) res = checkSamePath(pathName, dest);
    if (res) {
      res = STORAGE.rename(pathName, dest);
      if (res) httpd_resp_set_status(req, "201 Создано");
      else httpd_resp_set_status(req, "500 Внутренняя ошибка сервера");
      httpd_resp_sendstr(req, NULL);
      return true;
    }
  }
  httpd_resp_send_404(req);
  return false;
}
static bool handleCopy() {
  httpd_resp_send_404(req);
  return false;
}
bool handleWebDav(httpd_req_t* rreq) {
  req = rreq;
  sprintf(pathName, "%s", req->uri + strlen(WEBDAV));
  if (pathName[strlen(pathName) - 1] == '/') pathName[strlen(pathName) - 1] = 0;
  if (!strlen(pathName)) strcpy(pathName, "/");
  urlDecode(pathName);
  httpd_resp_set_hdr(req, "DAV", "1");
  httpd_resp_set_hdr(req, "Allow", ALLOW);
  switch(req->method) {
    case HTTP_PUT: return handlePut();
    case HTTP_PROPFIND: return handleProp();
    case HTTP_PROPPATCH: return handleProp();
    case HTTP_GET: return handleGet();
    case HTTP_HEAD: return handleHead();
    case HTTP_OPTIONS: return handleOptions();
    case HTTP_LOCK: return handleLock();
    case HTTP_UNLOCK: return handleUnlock();
    case HTTP_MKCOL: return handleMkdir();
    case HTTP_MOVE: return handleMove();
    case HTTP_DELETE: return handleDelete();
    case HTTP_COPY: return handleCopy();
    default: {
      LOG_ERR("Необработанный метод %s", HTTP_METHOD_STRING(req->method));
      httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Необработанный метод");
      return false;
    }
  }
  return true;
}
#endif