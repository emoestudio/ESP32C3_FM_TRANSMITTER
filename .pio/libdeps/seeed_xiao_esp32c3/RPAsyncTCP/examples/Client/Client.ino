#include <RPAsyncTCP.h>

// Network Configuration
const char* SSID = "your_ssid";
const char* PASSWORD = "12345678";
const IPAddress SERVER_IP(192, 168, 2, 128);
const uint16_t TCP_PORT = 5698;

// Timing Configuration
const unsigned long RECONNECT_CHECK_INTERVAL = 1000;  // 1 second
const unsigned long HEARTBEAT_INTERVAL = 10000;       // 10 seconds

// Client Configuration
AsyncClient* tcpClient = nullptr;
bool isConnected = false;
const uint8_t REPLY_BUFFER_SIZE = 64;

// Status Tracking
unsigned long lastHeartbeat = 0;
unsigned long lastReconnectAttempt = 0;

void printNetworkStatus() {
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
}

bool sendHeartbeat(AsyncClient* client) {
  if (!client || !client->canSend()) {
    Serial.println("Cannot send heartbeat");
    return false;
  }

  char message[REPLY_BUFFER_SIZE];
  snprintf(message, sizeof(message), "Client heartbeat from %s", WiFi.localIP().toString().c_str());
  
  size_t added = client->add(message, strlen(message));
  if (added == 0) {
    Serial.println("Failed to add heartbeat to buffer");
    return false;
  }

  if (!client->send()) {
    Serial.println("Failed to send heartbeat");
    return false;
  }

  return true;
}

void handleServerData(void* arg, AsyncClient* client, void* data, size_t len) {
  Serial.printf("\nReceived %d bytes from %s\n", len, client->remoteIP().toString().c_str());
  Serial.write(static_cast<uint8_t*>(data), len);
  
  // Immediate reply to server
  char ackMsg[] = "ACK";
  client->add(ackMsg, strlen(ackMsg));
  client->send();
}

void handleConnect(void* arg, AsyncClient* client) {
  Serial.printf("Connected to server %s:%d\n", SERVER_IP.toString().c_str(), TCP_PORT);
  isConnected = true;
  sendHeartbeat(client);  // Send initial heartbeat
}

void handleDisconnect(void* arg, AsyncClient* client) {
  Serial.println("Disconnected from server");
  isConnected = false;
}

bool connectToServer() {
  if (tcpClient) {
    delete tcpClient;
    tcpClient = nullptr;
  }

  tcpClient = new AsyncClient();
  if (!tcpClient) {
    Serial.println("Failed to create TCP client");
    return false;
  }

  // Setup event handlers
  tcpClient->onData(handleServerData);
  tcpClient->onConnect(handleConnect);
  tcpClient->onDisconnect(handleDisconnect);

  return tcpClient->connect(SERVER_IP, TCP_PORT);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);  // Wait for serial port

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    while (true);  // Halt if no WiFi
  }

  Serial.print("Connecting to: ");
  Serial.println(SSID);
  
  while (WiFi.begin(SSID, PASSWORD) != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  printNetworkStatus();
  connectToServer();
}

void loop() {
  unsigned long currentTime = millis();

  // Heartbeat logic: Send every HEARTBEAT_INTERVAL
  if (isConnected && (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)) {
    if (sendHeartbeat(tcpClient)) {
      lastHeartbeat = currentTime;
    }
  }

  // Reconnection logic
  if (!isConnected && (currentTime - lastReconnectAttempt >= RECONNECT_CHECK_INTERVAL)) {
    Serial.println("Attempting to reconnect...");
    connectToServer();
    lastReconnectAttempt = currentTime;
  }
}