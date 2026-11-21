#include "appGlobals.h"
#if INCLUDE_SMTP
#if (!INCLUDE_CERTS)
const char* smtp_rootCACertificate = "";
#endif
char smtp_login[MAX_HOST_LEN];
char SMTP_Pass[MAX_PWD_LEN];
char smtp_email[MAX_HOST_LEN];
char smtp_server[MAX_HOST_LEN];
uint16_t smtp_port;
#define MIME_TYPE "image/jpg"
#define ATTACH_NAME "frame.jpg"
TaskHandle_t emailHandle = NULL;
static char rspBuf[256];
static char respCodeRx[4];
static char subject[50];
static char message[100];
bool smtpUse = false;
int emailCount = 0;
int alertMax = 10;
static bool sendSmtpCommand(NetworkClientSecure& client, const char* cmd, const char* respCode) {
  LOG_VRB("Команда: %s", cmd);
  if (strlen(cmd)) client.println(cmd);
	uint32_t start = millis();
  while (!client.available() && millis() < start + (responseTimeoutSecs * 1000)) delay(1);
  if (!client.available()) {
    LOG_WRN("Тайм-аут ответа SMTP-сервера");
    return false;
  }
  client.read((uint8_t*)respCodeRx, 3);
  respCodeRx[3] = 0;
  int readLen = client.read((uint8_t*)rspBuf, 255);
  rspBuf[readLen] = 0;
  while (client.available()) client.read();
  LOG_VRB("Код приема: %s, соответственно: %s", respCodeRx, rspBuf);
  if (strcmp(respCodeRx, respCode) != 0) {
    LOG_ERR("Команда %s получила неправильный ответ: %s", cmd, rspBuf);
    return false;
  }
	return true;
}
static bool emailSend(const char* mimeType = MIME_TYPE, const char* fileName = ATTACH_NAME) {
  char content[100];
  NetworkClientSecure client;
  bool res = remoteServerConnect(client, smtp_server, smtp_port, smtp_rootCACertificate, EMAILCONN);
  if (!res) return false;
  while (true) {
    res = false;
    if (!sendSmtpCommand(client, "", "220")) break;
    sprintf(content, "HELO %s: ", APP_NAME);
    if (!sendSmtpCommand(client, content, "250")) break;
    if (!sendSmtpCommand(client, "АВТОРИЗАЦИЯ ВХОД", "334")) break;
    if (!sendSmtpCommand(client, encode64(smtp_login), "334")) break;
    if (!sendSmtpCommand(client, encode64(SMTP_Pass), "235")) break;
    sprintf(content, "ПОЧТА ОТ: <%s>", APP_NAME);
    if (!sendSmtpCommand(client, content, "250")) break;
    sprintf(content, "RCPT КОМУ: <%s>", smtp_email);
    if (!sendSmtpCommand(client, content, "250")) break;
    if (!sendSmtpCommand(client, "DATA", "354")) break;
    sprintf(content, "От: \"%s\" <%s>", APP_NAME, smtp_login);
    client.println(content);
    sprintf(content, "Кому: <%s>", smtp_email);
    client.println(content);
    sprintf(content, "Тема: %s", subject);
    client.println(content);
    client.println("MIME-версия: 1.0");
    sprintf(content, "Тип контента: Многочастный/смешанный; граница=%s", BOUNDARY_VAL);
    client.println(content);
    sprintf(content, "--%s", BOUNDARY_VAL);
    client.println(content);
    client.println("Тип контента: текстовый/обычный; кодировка = UTF-8");
    client.println("Кодирование передачи контента: цитирование-печать");
    client.println("Расположение контента: встроенное");
    client.println();
    client.println(message);
    client.println();
    if (alertBufferSize) {
      client.println(content);
      sprintf(content, "Тип контента: %s", mimeType);
      client.println(content);
      client.println("Кодирование передачи контента: base64");
      sprintf(content, "Содержание-Диспозиция: вложение; filename=\"%s\"", fileName);
      client.println(content);
      size_t chunkSize = 3;
      for (size_t i = 0; i < alertBufferSize; i += chunkSize)
        client.write(encode64chunk(alertBuffer + i, min(alertBufferSize - i, chunkSize)), 4);
    }
    client.println("\n");
    if (!sendSmtpCommand(client, ".", "250")) break;
    if (!sendSmtpCommand(client, "QUIT", "221")) break;
    res = true;
    break;
  }
  remoteServerClose(client);
  alertBufferSize = 0;
  return res;
}
static void emailTask(void* parameter) {
  if (emailCount < alertMax) {
    if (emailSend()) LOG_ALT("Отправлено ежедневное письмо %u", emailCount + 1);
    else LOG_WRN("Не удалось отправить электронное письмо");
  }
  if (++emailCount >= alertMax) LOG_WRN("Дневной лимит электронной почты %u достигнут", alertMax);
  emailHandle = NULL;
  vTaskDelete(NULL);
}
void emailAlert(const char* _subject, const char* _message) {
  if (smtpUse) {
    if (alertBuffer != NULL) {
      if (emailHandle == NULL) {
        strncpy(subject, _subject, sizeof(subject)-1);
        snprintf(subject+strlen(subject), sizeof(subject)-strlen(subject), " from %s", hostName);
        strncpy(message, _message, sizeof(message)-1);
        xTaskCreate(&emailTask, "emailTask", EMAIL_STACK_SIZE, NULL, EMAIL_PRI, &emailHandle);
        debugMemory("emailAlert");
      } else LOG_WRN("Оповещение по электронной почте уже выполняется");
    } else LOG_WRN("Необходимо перезагрузить компьютер, чтобы настроить электронную почту");
  }
}
void prepSMTP() {
  if (smtpUse) {
    emailCount = 0;
    if (alertBuffer == NULL) alertBuffer = (byte*)ps_malloc(maxFrameBuffSize);
    LOG_INF("Оповещения по электронной почте активны");
  }
}
#endif