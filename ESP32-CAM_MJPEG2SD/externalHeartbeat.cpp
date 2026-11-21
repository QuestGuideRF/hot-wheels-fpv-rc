#include "appGlobals.h"
#if INCLUDE_EXTHB
char external_heartbeat_domain[MAX_HOST_LEN] = "";
char external_heartbeat_uri[64] = "";
int external_heartbeat_port;
char external_heartbeat_token[EXTHB_LEN] = "";
bool external_heartbeat_active = false;
void sendExternalHeartbeat() {
  char uri[64 + 10 + EXTHB_LEN] = "";
  strcpy(uri, external_heartbeat_uri);
  strcat(uri, "?токен=");
  strcat(uri, external_heartbeat_token);
  NetworkClientSecure hclient;
  buildJsonString(false);
  if (remoteServerConnect(hclient, external_heartbeat_domain, external_heartbeat_port, "", EXTERNALHB)) {
    HTTPClient https;
    int httpCode = HTTP_CODE_NOT_FOUND;
    if (https.begin(hclient, external_heartbeat_domain, external_heartbeat_port, uri, true)) {
      https.addHeader("Content-Type", "application/json");
      httpCode = https.POST(jsonBuff);
      if (httpCode == HTTP_CODE_OK) {
        LOG_INF("Внешний контрольный сигнал отправлен на адрес: %s%s", external_heartbeat_domain, uri);
      } else LOG_WRN("Не удалось выполнить внешний запрос Heartbeat, ошибка: %s", https.errorToString(httpCode).c_str());
      https.end();
    }
    remoteServerClose(hclient);
  }
}
#endif