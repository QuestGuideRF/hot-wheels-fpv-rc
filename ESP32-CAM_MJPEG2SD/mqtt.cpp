#include "appGlobals.h"
#if (INCLUDE_HASIO && !INCLUDE_MQTT)
#error "Требуется INCLUDE_MQTT true"
#endif
#if INCLUDE_MQTT
#ifndef CONFIG_MQTT_PROTOCOL_311
#define CONFIG_MQTT_PROTOCOL_311 1
#endif
#include "mqtt_client.h"
#if (!INCLUDE_CERTS)
const char* mqtt_rootCACertificate = "";
#endif
#if (INCLUDE_HASIO)
#define HASIO_AVAILABILITY "homeassistant/status"
void sendMqttHasDiscovery();
void sendMqttHasState();
#endif
char mqtt_broker[MAX_HOST_LEN] = "";
char mqtt_port[5] = "";
char mqtt_user[MAX_HOST_LEN] = "";
char mqtt_user_Pass[MAX_PWD_LEN] = "";
char mqtt_topic_prefix[FILE_NAME_LEN / 2] = "";
char image_topic[FILE_NAME_LEN] = "";
#define MQTT_LWT_QOS 2
#define MQTT_LWT_RETAIN 1
#define MQTT_RETAIN 0
#define MQTT_QOS 1
bool mqtt_active = false;
bool mqttRunning = false;
bool mqttConnected = false;
esp_mqtt_client_handle_t mqtt_client = nullptr;
TaskHandle_t mqttTaskHandle = NULL;
static char remoteQuery[FILE_NAME_LEN * 2] = "";
static char lwt_topic[FILE_NAME_LEN];
static char cmd_topic[FILE_NAME_LEN];
static int mqttTaskDelay = 0;
static char mqttPublishTopic[FILE_NAME_LEN] = "";
void mqtt_client_publish(const char* topic, const char* payload){
  if (!mqtt_client || !mqttConnected) return;
  int id = esp_mqtt_client_publish(mqtt_client, topic, payload, strlen(payload), MQTT_QOS, MQTT_RETAIN);
  LOG_VRB("Публикация Mqtt, тема:%s, идентификатор:%d, длина:%i", topic, id, strlen(payload));
  LOG_VRB("Публикация Mqtt, полезная нагрузка:%s", payload);
}
void mqttPublish(const char* payload) {
  if (!strlen(mqtt_topic_prefix)) return;
  if (!strlen(mqttPublishTopic)) snprintf(mqttPublishTopic, FILE_NAME_LEN, "%sдатчик/%s/состояние", mqtt_topic_prefix, hostName);
  mqtt_client_publish(mqttPublishTopic, payload);
}
void mqttPublishPath(const char* suffix, const char* payload, const char *device) {
  char topic[2 * FILE_NAME_LEN];
  if (!strlen(mqtt_topic_prefix)) return;
  snprintf(topic, 2 * FILE_NAME_LEN, "%s%s/%s/%s", mqtt_topic_prefix, device, hostName, suffix);
  mqtt_client_publish(topic, payload);
}
static void mqtt_connected_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  LOG_INF("Mqtt подключен");
  esp_mqtt_client_publish(mqtt_client, lwt_topic, "online", 0, MQTT_LWT_QOS, MQTT_LWT_RETAIN);
  mqttConnected = true;
  int id = esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
  if (id == -1){
    LOG_WRN("Mqtt не удалось подписаться: %s", cmd_topic );
    stopMqttClient();
    return;
  }
  else LOG_VRB("Mqtt подписался: %s", cmd_topic );
#if (INCLUDE_HASIO)
  sendMqttHasDiscovery();
  vTaskDelay(1000 / portTICK_RATE_MS);
  sendMqttHasState();
  id = esp_mqtt_client_subscribe(mqtt_client, HASIO_AVAILABILITY, 1);
  if (id == -1){
    LOG_WRN("Mqtt не удалось подписаться: %s", HASIO_AVAILABILITY );
  } else LOG_VRB("Mqtt подписался: %s", HASIO_AVAILABILITY );
#endif
}
static void mqtt_disconnected_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  LOG_INF("Отключение Mqtt");
  mqttConnected = false;
}
static void mqtt_data_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  LOG_VRB("Тема Mqtt=%.*s ", event->topic_len, event->topic);
  LOG_VRB("Данные Mqtt=%.*s ", event->data_len, event->data);
#if INCLUDE_HASIO
  if(strncmp(event->topic, HASIO_AVAILABILITY, event->topic_len) == 0){
    sendMqttHasDiscovery();
    vTaskDelay(1000 / portTICK_RATE_MS);
    sendMqttHasState();
    return;
  }
#endif
  if(strncmp(event->topic, cmd_topic, event->topic_len) == 0){
    if (strlen(remoteQuery) == 0) sprintf(remoteQuery, "%.*s", event->data_len, (char*)event->data);
    mqttConnected = true;
    LOG_VRB("Возобновление потока mqtt..");
    xTaskNotifyGive(mqttTaskHandle);
  }
}
static void mqtt_error_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  LOG_VRB("База событий=%s, event_id=%d", base, event_id);
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  LOG_VRB("Ошибка события Mqtt %i", event->msg_id);
  if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
    LOG_WRN("Последняя строка ошибки (%s)", strerror(event->error_handle->esp_transport_sock_errno));
    mqttConnected = false;
  }
}
void sendMqttImage(){
  uint32_t startTime = millis();
  if (!strlen(mqtt_topic_prefix)) return;
  doKeepFrame = true;
  while (doKeepFrame && millis() - startTime < 4 * MAX_FRAME_WAIT) delay(100);
  if (!doKeepFrame && alertBufferSize) {
     const char* picBuff = (const char*)(alertBuffer);
     int id = esp_mqtt_client_publish(mqtt_client, image_topic, picBuff, alertBufferSize, MQTT_QOS, 0);
     LOG_VRB("Отправлено изображение, размер: %lu", alertBufferSize );
  }else{
    LOG_WRN("Не удалось отправить изображение");
  }
}
void checkForRemoteQuery() {
  if (strlen(remoteQuery) > 0) {
    char* query = strtok(remoteQuery, ";");
    while (query != NULL) {
      char* value = strchr(query, '=');
      if (value != NULL) {
        *value = 0;
        value++;
        LOG_VRB("Mqtt exec q: %s v: %s", query, value);
        if (!strcmp(query, "clockUTC")) {
        } else {
#ifdef ISCAM
          if (!strcmp(query, "fps")) setFPS(atoi(value));
          else if (!strcmp(query, "framesize"))  setFPSlookup(fsizePtr);
#endif
          updateStatus(query, value);
        }
      } else {
        LOG_VRB("Выполнить cmd: %s", query);
        if (!strcmp(query, "reset")) {
            doRestart("Удаленный перезапуск Mqtt");
        }else if (!strcmp(query, "status")) {
          buildJsonString(false);
          mqttPublishPath("status", jsonBuff);
        } else if (!strcmp(query, "статус?q")) {
          buildJsonString(true);
          mqttPublishPath("status", jsonBuff);
#if (INCLUDE_HASIO)
        } else if (!strcmp(query, "still")) {
           sendMqttImage();
           sendMqttHasState();
        } else if (!strcmp(query, "state")) {
          sendMqttHasState();
        } else if (!strcmp(query, "disc")) {
          sendMqttHasDiscovery();
          vTaskDelay(1000 / portTICK_RATE_MS);
          sendMqttHasState();
#endif
        }
      }
      query = strtok(NULL, ";");
    }
    remoteQuery[0] = '\0';
  }
}
static void mqttTask(void* parameter) {
  LOG_VRB("Запуск задачи Mqtt");
  while (mqtt_active) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (mqttConnected) {
      checkForRemoteQuery();
      if (mqttTaskDelay > 0 ) vTaskDelay(mqttTaskDelay / portTICK_RATE_MS);
    } else {
      LOG_WRN("Отключено, подождите..");
      vTaskDelay(2000 / portTICK_RATE_MS);
    }
  }
  mqttRunning = false;
  LOG_VRB("Задача Mqtt завершается..");
  mqttTaskHandle = NULL;
  vTaskDelete(NULL);
}
void stopMqttClient() {
  if (mqtt_client == nullptr) return;
  if (mqttConnected){
    esp_mqtt_client_publish(mqtt_client, lwt_topic, "offline", 0, MQTT_LWT_QOS, MQTT_LWT_RETAIN);
    vTaskDelay(1000 / portTICK_RATE_MS);
  }
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_stop(mqtt_client));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_destroy(mqtt_client));
  LOG_VRB("Проверка задачи..%u", mqttTaskHandle);
  if ( mqttTaskHandle != NULL ) {
    LOG_VRB("Разблокировать задачу..");
    xTaskNotifyGive(mqttTaskHandle);
    vTaskDelay(1500 / portTICK_RATE_MS);
    LOG_VRB("Удалена задача..?");
  }
  LOG_VRB("Exiting..");
  mqttConnected = false;
  mqtt_client = nullptr;
}
void startMqttClient(void){
  if (!mqtt_active) {
    LOG_VRB("MQTT не активен..");
    return;
  }
  if (mqttConnected) {
    LOG_VRB("MQTT уже запущен. Выход");
    return;
  }
  if (!netIsConnected()) {
    mqttConnected = false;
    LOG_VRB("Сеть отключена. Повторите попытку mqtt при подключении.");
    return;
  }
  char mqtt_uri[FILE_NAME_LEN];
  sprintf(mqtt_uri, "mqtt://%s:%s", mqtt_broker, mqtt_port);
  snprintf(lwt_topic, FILE_NAME_LEN, "%sдатчик/%s/lwt", mqtt_topic_prefix, hostName);
  snprintf(cmd_topic, FILE_NAME_LEN, "%sсенсор/%s/cmd", mqtt_topic_prefix, hostName);
  snprintf(image_topic, FILE_NAME_LEN, "%sсенсор/%s/фото", mqtt_topic_prefix, hostName);
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
      .address = { .uri = mqtt_uri },
    },
    .credentials = {
      .username = mqtt_user,
      .client_id = hostName,
      .authentication = { .password = mqtt_user_Pass },
    },
    .session = {
      .last_will = {
        .topic = lwt_topic,
        .msg = "offline",
        .qos = MQTT_LWT_QOS,
        .retain = MQTT_LWT_RETAIN,
      },
    },
  };
  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  LOG_INF("Mqtt подключается к %s...", mqtt_uri);
  if (mqtt_client != NULL) {
    if ( mqttTaskHandle == NULL ) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_CONNECTED, mqtt_connected_handler, NULL));
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_DISCONNECTED, mqtt_disconnected_handler, NULL));
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_DATA, mqtt_data_handler, mqtt_client));
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_ERROR, mqtt_error_handler, mqtt_client));
    } else {
      vTaskDelete(mqttTaskHandle);
      mqttTaskHandle = NULL;
    }
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(mqtt_client)) != ESP_OK) {
      LOG_WRN("Запуск Mqtt не удался");
    } else {
      LOG_VRB("Mqtt запущен");
      BaseType_t xReturned = xTaskCreate(&mqttTask, "mqttTask", MQTT_STACK_SIZE, NULL, MQTT_PRI, &mqttTaskHandle);
      LOG_INF("Создана задача mqtt: %u.", xReturned );
      mqttRunning = true;
    }
  }
}
#if (INCLUDE_HASIO)
void sendHasEntities (const char *name, const char *displayName, const char *units = "",
                      const char *icon = "", const char *category = "", const char *topic = "",
                      const char *payload_on = "",const char *payload_off = ""){
  char* p = jsonBuff;
  *p++ = '{';
  p += sprintf(p, "\"имя\":\"%s\",", displayName);
  p += sprintf(p, "\"uniq_id\":\"%s_%012llX\",", name, ESP.getEfuseMac() );
  if(strlen(units)>0)
    p += sprintf(p, "\"unit_of_meas\":\"%s\",", units);
  if(strlen(icon)>0)
    p += sprintf(p, "\"ic\":\"%s\",", icon);
  if(strcmp(category, "camera") == 0 ){
    p += sprintf(p, "\"t\":\"%sдатчик/%s/%s\",", mqtt_topic_prefix, hostName, topic);
  }else{
    if(strlen(category)>0){
      p += sprintf(p, "\"ent_cat\":\"%s\",", category);
    }
    if(strlen(topic))
      p += sprintf(p, "\"stat_t\":\"%sдатчик/%s/%s\",", mqtt_topic_prefix, hostName, topic);
    else
      p += sprintf(p, "\"stat_t\":\"%sдатчик/%s/%s\",", mqtt_topic_prefix, hostName, name);
    if(strlen(payload_on) && strlen(payload_off) ){
      p += sprintf(p, "\"pl_on\":\"%s\",", payload_on);
      p += sprintf(p, "\"pl_off\":\"%s\",", payload_off);
      p += sprintf(p, "\"cmd_t\":\"%sдатчик/%s/%s\",", mqtt_topic_prefix, hostName, "cmd");
    }else if(strlen(payload_on) && !strlen(payload_off) ){
      p += sprintf(p, "\"pl_prs\":\"%s\",", payload_on);
      p += sprintf(p, "\"cmd_t\":\"%sдатчик/%s/%s\",", mqtt_topic_prefix, hostName, "cmd");
    }
  }
  if(strcmp(category, "diagnostic") != 0 && strlen(payload_on) == 0){
    p += sprintf(p, "\"avty_t\":\"%sдатчик/%s/%s\",", mqtt_topic_prefix, hostName, "lwt");
    p += sprintf(p, "\"pl_avail\":\"%s\",", "online");
    p += sprintf(p, "\"pl_not_avail\":\"%s\",", "offline");
  }
  p += sprintf(p, "\"устройство\":");
    *p++ = '{';
      p += sprintf(p, "\"имя\":\"%s\",", hostName);
      p += sprintf(p, "\"ids\":[\"%s-%s\"],", hostName, ESP.getChipModel());
      p += sprintf(p, "\"sw\":\"%s\",", APP_VER);
      p += sprintf(p, "\"cns\":[[ \"mac\",\"%s\"]],", netMacAddress().c_str() );
      p += sprintf(p, "\"mdl\":\"%s-%i\",", ESP.getChipModel(), ESP.getChipRevision());
      p += sprintf(p, "\"cu\":\"http://%s/\",", netLocalIP().toString().c_str());
      p += sprintf(p, "\"mf\":\"%s\"", "esp32cam");
    *p++ = '}';
  *p++ = '}';
  *p = 0;
  char suffix[FILE_NAME_LEN] = "";
  sprintf(suffix, "%s/конфигурация", name);
  if(strlen(payload_on) && strlen(payload_off) )
    mqttPublishPath(suffix, jsonBuff, "switch");
  else if(strlen(payload_on) && !strlen(payload_off))
    mqttPublishPath(suffix, jsonBuff, "button");
  else if(strcmp(category, "camera") == 0 )
    mqttPublishPath("config", jsonBuff, "camera");
  else
    mqttPublishPath(suffix, jsonBuff);
}
void sendMqttHasDiscovery(){
  sendHasEntities ("motion", "Motion", "", "mdi:motion-sensor");
  sendHasEntities ("record", "Record","", "mdi:video-check");
  sendHasEntities ("clock", "Часы с камерой", "", "mdi:clock-outline", "diagnostic", "clock");
  sendHasEntities ("up_time", "Время работы", "", "mdi:clock", "diagnostic", "up_time");
  sendHasEntities ("atemp", "Температура камеры", "C", "mdi:coolant-temperature", "diagnostic", "atemp");
  sendHasEntities ("wifi_rssi", "Уровень сигнала", "dBm", "mdi:wifi", "diagnostic", "wifi_rssi");
  sendHasEntities ("wifi_ip", "Wi-Fi IP", "", "mdi:wifi", "diagnostic", "wifi_ip");
  sendHasEntities ("free_heap",  "Бесплатная куча", "", "mdi:memory", "diagnostic", "free_heap");
  sendHasEntities ("free_psram", "Бесплатное ПСРАМ", "", "mdi:memory", "diagnostic", "free_psram");
  sendHasEntities ("free_bytes", "Бесплатная SD", "", "mdi:memory", "diagnostic", "free_bytes");
  sendHasEntities ("led", "Камера светодиодная", "", "mdi:led-on", "", "", "уровень лампы=15","уровень лампы=0");
  sendHasEntities ("forceRecord", "Начать запись", "", "mdi:video-check", "", "", "ForceRecord=1","ForceRecord=0");
  sendHasEntities ("still", "Получить изображение", "", "mdi:list-status", "config", "", "still");
  sendHasEntities ("state", "Получить диагностику", "", "mdi:list-status", "config", "", "state");
  sendHasEntities ("restart", "Перезагрузить устройство", "", "mdi:restart", "config", "", "reset");
  sendHasEntities (hostName, "cam", "", "mdi:video", "camera", "still");
  mqttPublishPath("cmd", "still");
  if (isCapturing) mqttPublishPath("record", "on");
  else mqttPublishPath("record", "off");
  mqttPublishPath("motion", "off");
}
void sendMqttHasState(){
  char* p = jsonBuff;
  char timeBuff[20];
  strftime(timeBuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&currEpoch));
  mqttPublishPath("clock", timeBuff);
  formatElapsedTime(timeBuff, millis());
  mqttPublishPath("up_time", timeBuff);
  float aTemp = readTemperature(true);
  if (aTemp > -127.0){
    sprintf(p, "%0.1f", aTemp);
    mqttPublishPath("atemp", p);
  }
  sprintf(p, "%i", netRSSI());
  mqttPublishPath("wifi_rssi", p);
  sprintf(p, "%s", netLocalIP().toString().c_str());
  mqttPublishPath("wifi_ip", p);
  sprintf(p, "%s", fmtSize(ESP.getFreeHeap()) );
  mqttPublishPath("free_heap", p);
  sprintf(p, "%s", fmtSize(ESP.getFreePsram()) );
  mqttPublishPath("free_psram", p);
  sprintf(p, "%s", fmtSize(STORAGE.totalBytes() - STORAGE.usedBytes()) );
  mqttPublishPath("free_bytes", p);
}
#endif
#endif