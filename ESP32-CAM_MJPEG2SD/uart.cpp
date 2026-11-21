#include "appGlobals.h"
#if INCLUDE_UART
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 1, 0)
#error uart.cpp must be compiled with arduino-esp32 core v3.1.0 or higher
#endif
#include "driver/uart.h"
#define UART_RTS UART_PIN_NO_CHANGE
#define UART_CTS UART_PIN_NO_CHANGE
#define UART_BAUD_RATE 115200
#define BUFF_LEN (UART_HW_FIFO_LEN(0) * 2)
#define MSG_LEN 8
int uartTxdPin;
int uartRxdPin;
TaskHandle_t uartRxHandle = NULL;
static QueueHandle_t uartQueue = NULL;
static SemaphoreHandle_t responseMutex = NULL;
static SemaphoreHandle_t writeMutex = NULL;
static uart_event_t uartEvent;
static byte uartBuffTx[BUFF_LEN];
static byte uartBuffRx[BUFF_LEN];
static const char* uartErr[] = {"FRAME_ERR", "PARITY_ERR", "UART_BREAK", "DATA_BREAK",
  "BUFFER_FULL", "FIFO_OVF", "UART_DATA", "PATTERN_DET", "EVENT_MAX"};
static const uint16_t header = 0x55aa;
static uart_port_t uartId;
static bool readUart() {
  if (xQueueReceive(uartQueue, (void*)&uartEvent, (TickType_t)portMAX_DELAY)) {
    if (uartEvent.type != UART_DATA) {
      xQueueReset(uartQueue);
      uart_flush_input(uartId);
      LOG_WRN("Неожиданный тип события UART: %s", uartErr[uartEvent.type]);
      delay(1000);
      return false;
    } else {
      int msgLen = 0;
      while (msgLen < MSG_LEN) {
        uart_get_buffered_data_len(uartId, (size_t*)&msgLen);
        delay(10);
      }
      heartBeatDone = true;
      msgLen = uart_read_bytes(uartId, uartBuffRx, msgLen, pdMS_TO_TICKS(20));
      uint16_t* rxPtr = (uint16_t*)uartBuffRx;
      if (rxPtr[0] != header) {
        return false;
      }
      byte checkSum = 0;
      for (int i = 0; i < MSG_LEN - 1; i++) checkSum += uartBuffRx[i];
      if (checkSum != uartBuffRx[MSG_LEN - 1]) {
        LOG_WRN("Недопустимое сообщение проигнорировано, получена контрольная сумма %02x, ожидается %02x", uartBuffRx[MSG_LEN - 1], checkSum);
        return false;
      }
    }
  }
  return true;
}
bool writeUart(uint8_t cmd, uint32_t outputData) {
  xSemaphoreTake(writeMutex, portMAX_DELAY);
  memcpy(uartBuffTx, &header, 2);
  uartBuffTx[2] = cmd;
  memcpy(uartBuffTx + 3, &outputData, 4);
  uartBuffTx[MSG_LEN - 1] = 0;
  for (int i = 0; i < MSG_LEN - 1; i++) uartBuffTx[MSG_LEN - 1] += uartBuffTx[i];
  bool res = uart_write_bytes(uartId, uartBuffTx, MSG_LEN) > 0 ? true : false;
  xSemaphoreGive(writeMutex);
  return res;
}
static bool configureUart() {
  uart_config_t uart_config = {
    .baud_rate = UART_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
#if CONFIG_IDF_TARGET_ESP32
    .source_clk = UART_SCLK_REF_TICK,
#endif
  };
#if CONFIG_IDF_TARGET_ESP32C3
  uartId = UART_NUM_1;
#else
  uartId = UART_NUM_2;
#endif
  esp_err_t res = uart_driver_install(uartId, BUFF_LEN, BUFF_LEN, 20, &uartQueue, 0);
  if (res == ESP_OK) res = uart_param_config(uartId, &uart_config);
  if (res == ESP_OK) res = uart_set_pin(uartId, uartTxdPin, uartRxdPin, UART_RTS, UART_CTS);
  if (res != ESP_OK) LOG_WRN("Не удалось настроить UART: %s", espErrMsg(res));
  return (res == ESP_OK) ? true : false;
}
static void uartRxTask(void *arg) {
  while (true) {
    xSemaphoreTake(responseMutex, portMAX_DELAY);
    if (readUart()) {
      uint32_t receivedData;
      memcpy(&receivedData, uartBuffRx + 3, 4);
#ifdef AUXILIARY
      if (!setOutputPeripheral(uartBuffRx[2], receivedData)) {
        int receivedData = getInputPeripheral(uartBuffRx[2]);
        if (receivedData >= 0) writeUart(uartBuffRx[2], (uint32_t)receivedData);
      }
#else
      setInputPeripheral(uartBuffRx[2], receivedData);
#endif
    }
    xSemaphoreGive(responseMutex);
  }
}
void prepUart() {
  if (useUart) {
    if (uartTxdPin && uartRxdPin) {
      LOG_INF("Подготовьте UART на контактах Tx %d, Rx %d", uartTxdPin, uartRxdPin);
      responseMutex = xSemaphoreCreateMutex();
      writeMutex = xSemaphoreCreateMutex();
      if (configureUart()) {
#ifdef USE_UARTTASK
        xSemaphoreTake(responseMutex, portMAX_DELAY);
        xTaskCreate(uartRxTask, "uartRxTask", UART_STACK_SIZE, NULL, UART_PRI, &uartRxHandle);
#endif
        xSemaphoreGive(responseMutex);
        xSemaphoreGive(writeMutex);
      }
    } else LOG_WRN("По крайней мере один вывод UART не определен");
  }
}
#endif