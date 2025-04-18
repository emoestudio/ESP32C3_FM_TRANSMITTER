#include <RPAsyncTCP.h>
#include <vector>
#include <algorithm>

// Network Configuration
const char* SSID = "your_ssid";
const char* PASSWORD = "12345678";
const uint16_t TCP_PORT = 5698;

// Server Configuration
AsyncServer* tcpServer = nullptr;
std::vector<AsyncClient*> connectedClients;
IPAddress serverIP;

// Server Response Configuration
const uint16_t RESPONSE_BUFFER_SIZE = 128;

void printNetworkStatus() {
  if (serverIP == INADDR_NONE) return; // Ensure IP is valid
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  serverIP = WiFi.localIP();
  Serial.print("Server IP: ");
  Serial.println(serverIP);
}

void handleClientDisconnect(AsyncClient* client) {
  Serial.printf("Client %s disconnected\n", client->remoteIP().toString().c_str());
  
  // Remove and delete client
  connectedClients.erase(std::remove(connectedClients.begin(), connectedClients.end(), client), connectedClients.end());
  delete client;
}

void sendWelcomeMessage(AsyncClient* client) {
  if (!client->canSend() || client->space() < RESPONSE_BUFFER_SIZE) return;

  char response[RESPONSE_BUFFER_SIZE];
  snprintf(response, sizeof(response), "Connected to RPAsyncTCP Server @ %s", serverIP.toString().c_str());
  
  size_t len = strlen(response);
  size_t added = client->add(response, len, ASYNC_WRITE_FLAG_COPY);
  if (added != len) {
    Serial.println("Failed to add all data to buffer");
    return;
  }

  if (!client->send()) {
    Serial.println("Failed to send data");
  }
}

// Client Event Handlers
void handleClientData(void* arg, AsyncClient* client, void* data, size_t length) {
  Serial.printf("\nReceived %zu bytes from %s\n", length, client->remoteIP().toString().c_str());
  Serial.write(static_cast<uint8_t*>(data), length);
  
  sendWelcomeMessage(client);
}

void handleClientError(void* arg, AsyncClient* client, err_t error) {
  Serial.printf("Client %s error: %s\n", client->remoteIP().toString().c_str(), client->errorToString(error));
  handleClientDisconnect(client);
}

void handleClientTimeout(void* arg, AsyncClient* client, uint32_t time) {
  Serial.printf("Client %s timeout after %ums\n", 
               client->remoteIP().toString().c_str(), time);
  handleClientDisconnect(client);
}

// Server Event Handler
void handleNewConnection(void* arg, AsyncClient* client) {
  Serial.printf("New client connected: %s\n", client->remoteIP().toString().c_str());
  
  // Setup client callbacks
  client->onData(handleClientData, nullptr);
  client->onError(handleClientError, nullptr);
  client->onDisconnect([](void*, AsyncClient* c) { handleClientDisconnect(c); }, nullptr);
  client->onTimeout(handleClientTimeout, nullptr);
  
  connectedClients.push_back(client);
  sendWelcomeMessage(client);
}

void initializeWiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found!");
    while(true);
  }

  Serial.print("Connecting to: ");
  Serial.println(SSID);
  
  while (WiFi.begin(SSID, PASSWORD) != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);

  initializeWiFi();
  printNetworkStatus();

  tcpServer = new AsyncServer(TCP_PORT);
  tcpServer->onClient(handleNewConnection, tcpServer);
  tcpServer->begin();

  Serial.printf("Server running at %s:%d\n", serverIP.toString().c_str(), TCP_PORT);
}

void loop() {
  delay(1); // Yield to background tasks
}