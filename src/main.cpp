#include <Arduino.h>
#include <Wire.h>
#include <QN8027Radio.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// OLED显示屏设置
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// FM发射机
QN8027Radio radio = QN8027Radio();
uint8_t fsmStatus;
String stats[] = {"Resetting", "Recalibrating", "Idle", "TxReady", "PACalib", "Transmiting", "PA Off"};

// WiFi设置
const char* default_ssid = "xxxxxxxxxx";
const char* default_password = "xxxxxxxxx";
const char* ap_ssid = "FM_Transmitter_AP";
const char* ap_password = "12345678";

// Web服务器
AsyncWebServer server(80);

// 设置存储
Preferences preferences;

// 设置变量
float frequency = 88.0;
int txFreqDeviation = 150;
bool rdsEnabled = true;
String stationName = "QN8027FM";
String radioText = "Welcome to FM transmitter";
bool monoAudio = false;
int txPower = 75;
bool preEmphTime50 = true;

// 定时器
unsigned long lastDisplayUpdate = 0;
unsigned long lastRdsUpdate = 0;

// 功能声明
void setupWiFi();
void setupWebServer();
void setupOLED();
void updateDisplay();
void handleSerialCommands();
void loadSettings();
void saveSettings();

void setup() {
  Serial.begin(115200);
  
  // 初始化SPIFFS文件系统
  if(!SPIFFS.begin(true)) {
    Serial.println("SPIFFS初始化失败");
  }
  
  // 加载设置
  loadSettings();
  
  // 设置I2C针脚
  Wire.begin(8, 9);
  
  // 初始化OLED
  setupOLED();
  
  // FM发射机初始化
  radio.reset();
  radio.reCalibrate();
  
  // 应用设置
  radio.setFrequency(frequency);
  radio.setTxFreqDeviation(txFreqDeviation);
  radio.setTxPower(txPower);
  
  if(preEmphTime50) radio.setPreEmphTime50(ON);
  if(monoAudio) radio.MonoAudio(ON);
  
  radio.Switch(ON);
  
  // RDS设置
  if(rdsEnabled) {
    radio.RDS(ON);
  } else {
    radio.RDS(OFF);
  }
  
  // 设置WiFi和Web服务器
  setupWiFi();
  setupWebServer();
  
  Serial.println("FM发射机已启动");
  Serial.println("使用'help'命令查看可用指令");
  
  // 显示初始状态
  updateDisplay();
}

void loop() {
  // 处理串口命令
  handleSerialCommands();
  
  // 更新RDS信息
  if (millis() - lastRdsUpdate > 1000 && rdsEnabled) {
    radio.sendStationName(stationName);
    delay(100);
    radio.sendRadioText(radioText);
    lastRdsUpdate = millis();
  }
  
  // 更新显示屏
  if (millis() - lastDisplayUpdate > 1000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // 检查FM状态变化
  if(radio.getFSMStatus() != fsmStatus) {
    fsmStatus = radio.getFSMStatus();
    Serial.print("FSM模式已更改:");
    Serial.println(stats[fsmStatus]);
    updateDisplay();
  }
  
  delay(100);
}

void setupOLED() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306初始化失败");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("FM发射机初始化中...");
    display.display();
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // 频率显示
  display.setCursor(0, 0);
  display.print("FM: ");
  display.print(frequency);
  display.println(" MHz");
  
  // 电台名称
  display.setCursor(0, 16);
  display.print("Station: ");
  display.println(stationName);
  
  // 发射功率
  display.setCursor(0, 32);
  display.print("Power: ");
  display.print(txPower);
  display.println("%");
  
  // 状态
  display.setCursor(0, 48);
  display.print("Status: ");
  display.println(stats[radio.getFSMStatus()]);
  
  display.display();
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(default_ssid, default_password);
  
  Serial.println("正在连接到WiFi...");
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();
  
  // 等待10秒钟尝试连接
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  // 如果连接成功
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("已连接WiFi，IP地址: ");
    Serial.println(WiFi.localIP());
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Connected!");
    display.setCursor(0, 16);
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
    delay(2000);
  } else {
    // 如果连接失败，创建AP
    Serial.println("无法连接WiFi，创建接入点...");
    WiFi.disconnect();
    delay(1000);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    
    Serial.print("接入点已创建，IP: ");
    Serial.println(WiFi.softAPIP());
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("AP Created");
    display.setCursor(0, 16);
    display.print("SSID: ");
    display.println(ap_ssid);
    display.setCursor(0, 32);
    display.print("PWD: ");
    display.println(ap_password);
    display.setCursor(0, 48);
    display.print("IP: ");
    display.println(WiFi.softAPIP());
    display.display();
    delay(2000);
  }
}

void setupWebServer() {
  // 提供网页
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  // 提供CSS和JS文件
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });
  
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/script.js", "application/javascript");
  });
  
  // API端点 - 获取当前设置
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    doc["frequency"] = frequency;
    doc["txFreqDeviation"] = txFreqDeviation;
    doc["rdsEnabled"] = rdsEnabled;
    doc["stationName"] = stationName;
    doc["radioText"] = radioText;
    doc["monoAudio"] = monoAudio;
    doc["txPower"] = txPower;
    doc["preEmphTime50"] = preEmphTime50;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API端点 - 保存设置
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Settings updated");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, data, len);
    
    frequency = doc["frequency"];
    txFreqDeviation = doc["txFreqDeviation"];
    rdsEnabled = doc["rdsEnabled"];
    stationName = doc["stationName"].as<String>();
    radioText = doc["radioText"].as<String>();
    monoAudio = doc["monoAudio"];
    txPower = doc["txPower"];
    preEmphTime50 = doc["preEmphTime50"];
    
    // 应用设置
    radio.setFrequency(frequency);
    radio.setTxFreqDeviation(txFreqDeviation);
    radio.setTxPower(txPower);
    
    if(preEmphTime50) {
      radio.setPreEmphTime50(ON);
    } else {
      radio.setPreEmphTime50(OFF);
    }
    
    if(monoAudio) {
      radio.MonoAudio(ON);
    } else {
      radio.MonoAudio(OFF);
    }
    
    if(rdsEnabled) {
      radio.RDS(ON);
    } else {
      radio.RDS(OFF);
    }
    
    // 保存设置
    saveSettings();
    
    // 更新显示
    updateDisplay();
  });
  
  server.begin();
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    // 解析命令
    if (command.startsWith("freq ")) {
      float newFreq = command.substring(5).toFloat();
      if (newFreq >= 76.0 && newFreq <= 108.0) {
        frequency = newFreq;
        radio.setFrequency(frequency);
        Serial.println("频率已设置为: " + String(frequency) + " MHz");
        saveSettings();
      } else {
        Serial.println("频率必须在76-108 MHz范围内");
      }
    }
    else if (command.startsWith("power ")) {
      int newPower = command.substring(6).toInt();
      if (newPower >= 0 && newPower <= 100) {
        txPower = newPower;
        radio.setTxPower(txPower);
        Serial.println("发射功率已设置为: " + String(txPower) + "%");
        saveSettings();
      } else {
        Serial.println("功率必须在0-100%范围内");
      }
    }
    else if (command.startsWith("name ")) {
      stationName = command.substring(5);
      if (stationName.length() > 8) stationName = stationName.substring(0, 8);
      Serial.println("电台名称已设置为: " + stationName);
      saveSettings();
    }
    else if (command.startsWith("text ")) {
      radioText = command.substring(5);
      Serial.println("电台文本已设置为: " + radioText);
      saveSettings();
    }
    else if (command == "rds on") {
      rdsEnabled = true;
      radio.RDS(ON);
      Serial.println("RDS已启用");
      saveSettings();
    }
    else if (command == "rds off") {
      rdsEnabled = false;
      radio.RDS(OFF);
      Serial.println("RDS已禁用");
      saveSettings();
    }
    else if (command == "mono on") {
      monoAudio = true;
      radio.MonoAudio(ON);
      Serial.println("单声道模式已启用");
      saveSettings();
    }
    else if (command == "mono off") {
      monoAudio = false;
      radio.MonoAudio(OFF);
      Serial.println("单声道模式已禁用");
      saveSettings();
    }
    else if (command == "status") {
      Serial.println("FM发射机状态:");
      Serial.println("频率: " + String(frequency) + " MHz");
      Serial.println("功率: " + String(txPower) + "%");
      Serial.println("电台名称: " + stationName);
      Serial.println("电台文本: " + radioText);
      Serial.println("RDS: " + String(rdsEnabled ? "启用" : "禁用"));
      Serial.println("单声道: " + String(monoAudio ? "启用" : "禁用"));
      Serial.println("状态: " + stats[radio.getFSMStatus()]);
    }
    else if (command == "reset") {
      radio.reset();
      radio.reCalibrate();
      Serial.println("FM发射机已重置");
    }
    else if (command == "help") {
      Serial.println("可用命令:");
      Serial.println("freq <76-108> - 设置发射频率 (MHz)");
      Serial.println("power <0-100> - 设置发射功率 (%)");
      Serial.println("name <文本> - 设置电台名称 (最多8字符)");
      Serial.println("text <文本> - 设置RDS文本");
      Serial.println("rds on/off - 启用/禁用RDS");
      Serial.println("mono on/off - 启用/禁用单声道");
      Serial.println("status - 显示当前状态");
      Serial.println("reset - 重置FM发射机");
      Serial.println("help - 显示此帮助");
    }
    else {
      Serial.println("未知命令。使用'help'查看可用命令。");
    }
    
    updateDisplay();
  }
}

void loadSettings() {
  preferences.begin("fm_settings", false);
  frequency = preferences.getFloat("frequency", 88.0);
  txFreqDeviation = preferences.getInt("txFreqDev", 150);
  rdsEnabled = preferences.getBool("rdsEnabled", true);
  stationName = preferences.getString("stationName", "QN8027FM");
  radioText = preferences.getString("radioText", "Welcome to FM transmitter");
  monoAudio = preferences.getBool("monoAudio", false);
  txPower = preferences.getInt("txPower", 75);
  preEmphTime50 = preferences.getBool("preEmphTime50", true);
  preferences.end();
}

void saveSettings() {
  preferences.begin("fm_settings", false);
  preferences.putFloat("frequency", frequency);
  preferences.putInt("txFreqDev", txFreqDeviation);
  preferences.putBool("rdsEnabled", rdsEnabled);
  preferences.putString("stationName", stationName);
  preferences.putString("radioText", radioText);
  preferences.putBool("monoAudio", monoAudio);
  preferences.putInt("txPower", txPower);
  preferences.putBool("preEmphTime50", preEmphTime50);
  preferences.end();
}