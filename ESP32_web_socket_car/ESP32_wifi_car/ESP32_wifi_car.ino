#include<WiFi.h>
#include<WebServer.h>
#include<WebSocketsServer.h>
#include <FS.h>
#include "SPIFFS.h"
#include<Servo.h>
const char* ssid = "xxxx";
const char* password = "xxxxxxx";
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
int motor1EnablePin = 14;
int motor1pin1 = 27;
int motor1pin2 = 26;
int motor2EnablePin = 32;
int motor2pin1 = 33;
int motor2pin2 = 25;
const int freq = 30000;
const int motor1pwmChannel = 2;
const int motor2pwmChannel = 3;
const int resolution = 8;
int dutyCycle = 0;
Servo steeringServo;
int steeringServoPin = 18;
void webSocketEvent(uint8_t num, WStype_t type, uint8_t*payload, size_t length){
    Serial.printf("webSocketEvent(%d,%d,...)\r\n", num, type);
    switch(type){
      case WStype_DISCONNECTED:
        Serial.printf("[%u]Disconnected!\r\n",num);
        break;
      case WStype_CONNECTED:{
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u]Connected from %d.%d.%d.%d url:%s\r\n",num,ip[0],ip[1],ip[2],ip[3],payload);
      }
        break;
      case WStype_TEXT:
        Serial.printf("[%u]get Text: %s\r\n",num,payload);
        if(payload[0] == 'F'){
            String strPayload = String((const char*) payload);
            strPayload.remove(0,1);
          FrontAndBackMoveDirectionControl(strPayload);
        }
        if(payload[0] == 'L'){
            String strPayload = String((const char*) payload);
            strPayload.remove(0,1);
            steeringDirectionControl(strPayload);
        }
        break;
      case WStype_BIN:
        Serial.printf("[%u]get binary length: %u\r\n",num,length);
        break;
    }
}
void moveForward(){
  digitalWrite(motor1pin1, LOW);
  digitalWrite(motor1pin2, HIGH);
  digitalWrite(motor2pin1, LOW);
  digitalWrite(motor2pin2, HIGH);
}
void moveBackward(){
  digitalWrite(motor1pin1, HIGH);
  digitalWrite(motor1pin2, LOW);
  digitalWrite(motor2pin1, HIGH);
  digitalWrite(motor2pin2, LOW);
}
void moveStop(){
  digitalWrite(motor1pin1, LOW);
  digitalWrite(motor1pin2, LOW);
  digitalWrite(motor2pin1, LOW);
  digitalWrite(motor2pin2, LOW);
}
void FrontAndBackMoveDirectionControl(String directionData){
    uint8_t Direction = directionData.toInt();
    if(Direction > 31){
      moveBackward();
      dutyCycle = map(Direction, 31,60,170,255);
      ledcWrite(motor1pwmChannel, dutyCycle);
      ledcWrite(motor2pwmChannel, dutyCycle);
      Serial.print("move backward ->");
      Serial.println(dutyCycle);
    }
    else if(Direction < 29){
      moveForward();
      dutyCycle = map(Direction, 29,0,170,255);
      ledcWrite(motor1pwmChannel, dutyCycle);
      ledcWrite(motor2pwmChannel, dutyCycle);
       Serial.print("move forward ->");
       Serial.println(dutyCycle);
    }
    else{
      Serial.println("move stop");
        moveStop();
    }
}
int steeringAngle = 0;
void steeringDirectionControl(String SteeringData){
  Serial.print("servo direction");
  Serial.println(SteeringData);
  int Steering = SteeringData.toInt();
     if(Steering > 30){
      Serial.println("move right");
      Serial.println(90+Steering-15);
      steeringAngle = map(Steering, 30,60,90,135);
      steeringServo.write(steeringAngle);
    }
    else if(Steering < 30){
      Serial.println("move left");
      Serial.println(90-(35-Steering));
      steeringAngle = map(Steering, 30,0,90,55);
      steeringServo.write(steeringAngle);
    }
}
void setup() {
  Serial.begin(115200);
   steeringServo.attach(steeringServoPin);
  pinMode(motor1EnablePin, OUTPUT);
  pinMode(motor1pin1, OUTPUT);
  pinMode(motor1pin2, OUTPUT);
  pinMode(motor2EnablePin, OUTPUT);
  pinMode(motor2pin1, OUTPUT);
  pinMode(motor2pin2, OUTPUT);
  ledcSetup(motor1pwmChannel,freq, resolution);
  ledcSetup(motor2pwmChannel,freq, resolution);
  ledcAttachPin(motor1EnablePin, motor1pwmChannel);
  ledcAttachPin(motor2EnablePin, motor2pwmChannel);
  ledcWrite(motor1pwmChannel, dutyCycle);
  ledcWrite(motor2pwmChannel, dutyCycle);
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  IPAddress myIP = WiFi.localIP();
  if(!SPIFFS.begin(true)){
    Serial.println("Error while mounting SPIFFS");
    return;
  }
  SPIFFS.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  server.begin();
  Serial.println("HTTP server started");
}
void loop() {
  webSocket.loop();
  server.handleClient();
}
String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
bool handleFileRead(String path){
  Serial.println(path);
  #ifdef DEBUG
    Serial.println("handleFileRead: " + path);
  #endif
  if(path.endsWith("/")) path += "car.html";
  if(SPIFFS.exists(path)){
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, getContentType(path));
    file.close();
    return true;
  }
  return false;
}