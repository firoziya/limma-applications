// ESP8266 AutoHUB - Full working sketch
// Features:
// - AP (always on) + optional STA using saved credentials in EEPROM
// - 4 relays with live UI status updates (no page reload)
// - 2 servos controlled via web UI or /s1_angle and /s2_angle routes
// - DHT11 sensor (temp + humidity) and MQ analog -> simple AQI mapping
// - EEPROM save/load for STA credentials
// - mDNS responder (autohub.local)
// - JSON endpoint /get-sensor-data returns sensors, relays, servos
// Required libraries: ESP8266WiFi, ESP8266WebServer, EEPROM, ESP8266mDNS, DHT, Servo, ArduinoJson

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <DHT.h>
#include <Servo.h>
#include <ArduinoJson.h>

// EEPROM Configuration
#define EEPROM_SIZE 512
#define SSID_ADDR 0       // start address for SSID (max MAX_STR_LEN bytes)
#define PASS_ADDR 64      // start address for PASS
#define MAX_STR_LEN 64

// AP WiFi credentials (AP always active)
const char* ap_ssid = "AutoHUB";
const char* ap_password = "12345678";

// Station (user) credentials loaded from EEPROM
String sta_ssid = "";
String sta_password = "";
bool sta_connected = false;

ESP8266WebServer server(80);

// Relay pins (4 channels) - NodeMCU labels
const int relayPins[] = { D5, D6, D7, D8 }; // GPIO14,12,13,15
const int numRelays = 4;
bool relayStates[] = { false, false, false, false };

// Servo pins (2 servos)
const int servoPins[] = { D1, D2 }; // GPIO5,4
Servo servos[2];
int servoAngles[] = { 0, 0 };

// Sensor pins
const int dhtPin = D4; // GPIO2
const int mq135Pin = A0; // analog pin

// Sensor objects
DHT dht(dhtPin, DHT11);

// Timer variables
unsigned long lastSensorRead = 0;
const long sensorInterval = 2000;
float currentTemp = NAN;
float currentHumidity = NAN;
int currentAQI = 0;

// Forward declarations
void setupServerRoutes();
void handleRoot();
void handleStatus();
void handleWiFiSetup();
void handleSaveWiFi();
void handleGetSensorData();
void handleNotFound();
void handleServoDynamic();
void setRelay(int channel, bool state);
void initializePins();
void readSensors();
void startAPMode();
void connectToSTA();
void loadWiFiCredentials();
void saveWiFiCredentials();
String getHTMLHeader(String title);
String getHTMLFooter();

// EEPROM helpers (store fixed-length strings)
void writeStringToEEPROM(int addrOffset, const String &str) {
  int len = str.length();
  if (len > MAX_STR_LEN - 1) len = MAX_STR_LEN - 1;
  for (int i = 0; i < MAX_STR_LEN; i++) {
    if (i < len) {
      EEPROM.write(addrOffset + i, str[i]);
    } else {
      EEPROM.write(addrOffset + i, 0);
    }
  }
}

String readStringFromEEPROM(int addrOffset) {
  char data[MAX_STR_LEN];
  for (int i = 0; i < MAX_STR_LEN; i++) {
    data[i] = EEPROM.read(addrOffset + i);
  }
  data[MAX_STR_LEN - 1] = '\0';
  return String(data);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Load WiFi credentials from EEPROM
  loadWiFiCredentials();

  // Initialize pins and peripherals
  initializePins();

  // Initialize sensors
  dht.begin();

  // Start AP mode
  startAPMode();

  // Try connecting to STA if creds exist
  if (sta_ssid.length() > 0) {
    connectToSTA();
  }

  // Start mDNS
  if (!MDNS.begin("autohub")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started at autohub.local");
  }

  // Setup HTTP routes
  setupServerRoutes();

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorRead >= sensorInterval) {
    readSensors();
    lastSensorRead = millis();
  }

  // If STA credentials present but not connected, optionally reconnect
  if (sta_ssid.length() > 0 && WiFi.status() != WL_CONNECTED && sta_connected) {
    connectToSTA();
  }
}

void initializePins() {
  // Relay pins
  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW); // default off. If your relay is active LOW, change logic accordingly.
    relayStates[i] = false;
  }

  // Servos
  for (int i = 0; i < 2; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(0);
    servoAngles[i] = 0;
  }
}

void loadWiFiCredentials() {
  sta_ssid = readStringFromEEPROM(SSID_ADDR);
  sta_password = readStringFromEEPROM(PASS_ADDR);
  sta_ssid.trim();
  sta_password.trim();

  Serial.println("Loaded WiFi credentials:");
  Serial.println("SSID: " + sta_ssid);
  Serial.print("Password: ");
  Serial.println(sta_password.length() ? "***" : "Not set");
}

void saveWiFiCredentials() {
  writeStringToEEPROM(SSID_ADDR, sta_ssid);
  writeStringToEEPROM(PASS_ADDR, sta_password);
  EEPROM.commit();
  Serial.println("WiFi credentials saved to EEPROM");
}

void startAPMode() {
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("AP Mode Started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

void connectToSTA() {
  if (sta_ssid.length() == 0) return;

  Serial.print("Connecting to STA: ");
  Serial.println(sta_ssid);
  WiFi.begin(sta_ssid.c_str(), sta_password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    sta_connected = true;
    Serial.println("Connected to STA!");
    Serial.print("STA IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    sta_connected = false;
    Serial.println("Failed to connect to STA");
  }
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) currentTemp = t;
  if (!isnan(h)) currentHumidity = h;

  int mq = analogRead(mq135Pin); // 0 - 1023 on ESP8266 ADC
  // Very simple mapping to AQI range 0-500. For production use proper calibration & algorithm.
  currentAQI = map(mq, 0, 1023, 0, 500);
}

// ---- HTTP routes ----
void setupServerRoutes() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/wifi-setup", handleWiFiSetup);
  server.on("/save-wifi", HTTP_POST, handleSaveWiFi);
  server.on("/get-sensor-data", handleGetSensorData);

  // Relay control
  server.on("/ch01on", []() { setRelay(0, true); });
  server.on("/ch01off", []() { setRelay(0, false); });
  server.on("/ch02on", []() { setRelay(1, true); });
  server.on("/ch02off", []() { setRelay(1, false); });
  server.on("/ch03on", []() { setRelay(2, true); });
  server.on("/ch03off", []() { setRelay(2, false); });
  server.on("/ch04on", []() { setRelay(3, true); });
  server.on("/ch04off", []() { setRelay(3, false); });

  // Quick JSON ping route
  server.on("/ping", []() {
    server.send(200, "application/json", "{\"status\":\"pong\",\"device\":\"AutoHUB-ESP8266\"}");
  });

  // Servo dynamic routes handled via NotFound
  server.onNotFound(handleNotFound);
}

void handleRoot() {
  String html = getHTMLHeader("ESP8266 Controller");

  // Dashboard content (includes relay status badges and JS to update them)
  html += R"rawliteral(
    <div class="container">
      <div class="header">
        <h1>🏠 ESP8266 Smart Controller</h1>
        <div class="wifi-status">
          <span class="status-badge ap">AP: )rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</span>)rawliteral";
  html += (sta_connected ? "<span class='status-badge sta'>STA: Connected</span>" : "<span class='status-badge sta-disconnected'>STA: Disconnected</span>");
  html += R"rawliteral(
        </div>
      </div>

      <div class="grid-container">
        <div class="card">
          <h2>🔌 Relay Controls</h2>
          <div class="relay-grid">
  )rawliteral";

  // Generate relay items with status span and buttons (JS will update the status on click)
  for (int i = 0; i < numRelays; i++) {
    html += "<div class='relay-item'><div style='display:flex;align-items:center;gap:12px;'><span>Channel " + String(i + 1) + "</span><span id='relay-status-" + String(i+1) + "' class='relay-state'>" + (relayStates[i] ? "ON" : "OFF") + "</span></div><div class='btn-group'><button class='btn btn-on' onclick=\"controlRelay('ch0" + String(i+1) + "on'," + String(i+1) + ")\">ON</button><button class='btn btn-off' onclick=\"controlRelay('ch0" + String(i+1) + "off'," + String(i+1) + ")\">OFF</button></div></div>";
  }

  html += R"rawliteral(
          </div>
        </div>

        <div class="card">
          <h2>🔄 Servo Controls</h2>
          <div class="servo-controls">
            <div class="servo-item">
              <span>Servo 1: <span id="servo1-angle">)rawliteral";
  html += String(servoAngles[0]);
  html += R"rawliteral(</span>°</span>
              <input type="range" min="0" max="180" value=")rawliteral";
  html += String(servoAngles[0]);
  html += R"rawliteral(" onchange="setServo(1, this.value)">
            </div>
            <div class="servo-item">
              <span>Servo 2: <span id="servo2-angle">)rawliteral";
  html += String(servoAngles[1]);
  html += R"rawliteral(</span>°</span>
              <input type="range" min="0" max="180" value=")rawliteral";
  html += String(servoAngles[1]);
  html += R"rawliteral(" onchange="setServo(2, this.value)">
            </div>
          </div>
        </div>

        <div class="card">
          <h2>📊 Sensor Readings</h2>
          <div class="sensor-readings">
            <div class="sensor-item"><span>🌡️ Temperature</span><span id="temperature">--)rawliteral";
  html += R"rawliteral(</span></div>
            <div class="sensor-item"><span>💧 Humidity</span><span id="humidity">--</span></div>
            <div class="sensor-item"><span>🌫️ Air Quality</span><span id="aqi">--</span></div>
          </div>
        </div>
      </div>
    </div>

    <script>
      // controlRelay(command, channelIndex) -- updates UI right away based on server response
      function controlRelay(command, ch) {
        fetch('/' + command)
          .then(response => response.text())
          .then(msg => {
            showNotification(msg);
            // Update the specific relay badge based on server reply
            let statusSpan = document.getElementById('relay-status-' + ch);
            if (!statusSpan) return;
            if (msg.indexOf('ON') !== -1) {
              statusSpan.textContent = 'ON';
              statusSpan.style.color = 'green';
            } else if (msg.indexOf('OFF') !== -1) {
              statusSpan.textContent = 'OFF';
              statusSpan.style.color = 'red';
            } else {
              // fallback: fetch full status from server
              syncStates();
            }
          }).catch(err => {
            showNotification('Error: ' + err);
          });
      }

      function setServo(servoNum, angle) {
        // call dynamic route /s{n}_{angle}
        fetch('/s' + servoNum + '_' + angle)
          .then(response => response.text())
          .then(msg => {
            showNotification(msg);
            document.getElementById('servo' + servoNum + '-angle').textContent = angle;
          })
          .catch(err => showNotification('Error controlling servo'));
      }

      function updateSensorData() {
        fetch('/get-sensor-data')
          .then(response => response.json())
          .then(data => {
            document.getElementById('temperature').textContent = data.temperature + '°C';
            document.getElementById('humidity').textContent = data.humidity + '%';
            document.getElementById('aqi').textContent = data.aqi;
            // Update relay badges if needed (sync)
            for (let i = 0; i < data.relays.length; i++) {
              let el = document.getElementById('relay-status-' + (i+1));
              if (el) {
                el.textContent = data.relays[i] ? 'ON' : 'OFF';
                el.style.color = data.relays[i] ? 'green' : 'red';
              }
            }
            // Update servo angles
            if (data.servos && data.servos.length >= 2) {
              document.getElementById('servo1-angle').textContent = data.servos[0];
              document.getElementById('servo2-angle').textContent = data.servos[1];
            }
          })
          .catch(err => {
            // console.log('Sensor fetch err', err);
          });
      }

      function showNotification(message) {
        const n = document.createElement('div');
        n.className = 'notification';
        n.textContent = message;
        document.body.appendChild(n);
        setTimeout(() => n.remove(), 2500);
      }

      function syncStates() {
        // fetch full JSON to sync everything
        updateSensorData();
      }

      // periodic updates
      setInterval(updateSensorData, 2000);
      updateSensorData();
    </script>
  )rawliteral";

  html += getHTMLFooter();
  server.send(200, "text/html", html);
}

void handleStatus() {
  String html = getHTMLHeader("Device Status");
  html += R"rawliteral(
    <div class="container">
      <div class="header"><h1>📊 Device Status</h1><a href="/" class="back-btn">← Back</a></div>
      <div class="status-grid">
        <div class="status-card"><h3>🔌 Relay Status</h3><div id="relay-status-list">)rawliteral";

  for (int i = 0; i < numRelays; i++) {
    html += "<div class='status-item'>Channel " + String(i+1) + ": <span class='" + String(relayStates[i] ? "status-on'>ON" : "status-off'>OFF") + "</span></div>";
  }

  html += R"rawliteral(
        </div></div>
        <div class="status-card"><h3>🔄 Servo Status</h3><div id="servo-status">)rawliteral";

  for (int i = 0; i < 2; i++) {
    html += "<div class='status-item'>Servo " + String(i+1) + ": <span>" + String(servoAngles[i]) + "°</span></div>";
  }

  html += R"rawliteral(
        </div></div>
        <div class="status-card"><h3>📡 Network Status</h3>)rawliteral";

  html += "<div class='status-item'>AP IP: <span>" + WiFi.softAPIP().toString() + "</span></div>";
  html += "<div class='status-item'>STA: <span class='" + String(sta_connected ? "status-on'>Connected" : "status-off'>Disconnected") + "</span></div>";
  if (sta_connected) html += "<div class='status-item'>STA IP: <span>" + WiFi.localIP().toString() + "</span></div>";

  html += R"rawliteral(
        </div>
      </div>
    </div>
  )rawliteral";

  html += getHTMLFooter();
  server.send(200, "text/html", html);
}

void handleWiFiSetup() {
  String html = getHTMLHeader("WiFi Setup");
  html += R"rawliteral(
    <div class="container">
      <div class="header"><h1>📶 WiFi Setup</h1><a href="/" class="back-btn">← Back</a></div>
      <div class="card">
        <h2>Configure Station Mode</h2>
        <form action="/save-wifi" method="POST" class="wifi-form">
          <div class="form-group"><label for="ssid">WiFi SSID:</label>
            <input type="text" id="ssid" name="ssid" value=")rawliteral";
  html += sta_ssid;
  html += R"rawliteral(" required></div>
          <div class="form-group"><label for="password">WiFi Password:</label>
            <input type="password" id="password" name="password" placeholder="Enter WiFi password"></div>
          <button type="submit" class="btn btn-primary">Save & Connect</button>
        </form>
        <div class="wifi-info">
          <h3>Connection Info</h3>
          <p><strong>AP:</strong> )rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</p>
          <p><strong>mDNS:</strong> autohub.local</p>
          <p><strong>STA:</strong> )rawliteral";
  html += (sta_connected ? "Connected to " + sta_ssid : "Disconnected");
  html += R"rawliteral(</p>
        </div>
      </div>
    </div>
  )rawliteral";

  html += getHTMLFooter();
  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) {
    sta_ssid = server.arg("ssid");
    sta_password = server.arg("password");
    saveWiFiCredentials();
    connectToSTA();

    // Redirect to home
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Saved & Redirecting");
  } else {
    server.send(400, "text/plain", "Missing SSID parameter");
  }
}

void handleGetSensorData() {
  StaticJsonDocument<256> doc;
  doc["temperature"] = isnan(currentTemp) ? 0 : currentTemp;
  doc["humidity"] = isnan(currentHumidity) ? 0 : currentHumidity;
  doc["aqi"] = currentAQI;
  JsonArray rel = doc.createNestedArray("relays");
  for (int i = 0; i < numRelays; i++) rel.add(relayStates[i]);
  JsonArray sv = doc.createNestedArray("servos");
  sv.add(servoAngles[0]); sv.add(servoAngles[1]);

  String response;
  serializeJson(doc, response);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

void setRelay(int channel, bool state) {
  if (channel >= 0 && channel < numRelays) {
    // If your relay board is active LOW, invert state here:
    // digitalWrite(relayPins[channel], state ? LOW : HIGH);
    digitalWrite(relayPins[channel], state ? HIGH : LOW);
    relayStates[channel] = state;
    server.send(200, "text/plain", "CH" + String(channel + 1) + " turned " + (state ? "ON" : "OFF"));
    Serial.println("CH" + String(channel + 1) + " -> " + (state ? "ON" : "OFF"));
  } else {
    server.send(400, "text/plain", "Invalid channel");
  }
}

void handleNotFound() {
  String path = server.uri();
  if (path.startsWith("/s1_") || path.startsWith("/s2_")) {
    handleServoDynamic();
    return;
  }
  // show 404
  server.send(404, "text/plain", "Page not found: " + path);
}

void handleServoDynamic() {
  String path = server.uri(); // e.g. /s1_90
  Serial.println("Servo request: " + path);

  if (path.startsWith("/s1_") || path.startsWith("/s2_")) {
    int servoNum = path.substring(2, 3).toInt() - 1; // 1->index0
    String angleStr = path.substring(4);
    int angle = angleStr.toInt();
    if (servoNum >= 0 && servoNum < 2 && angle >= 0 && angle <= 180) {
      servos[servoNum].write(angle);
      servoAngles[servoNum] = angle;
      server.send(200, "text/plain", "Servo " + String(servoNum + 1) + " set to " + String(angle) + " degrees");
      Serial.println("Servo " + String(servoNum+1) + " -> " + String(angle));
    } else {
      server.send(400, "text/plain", "Invalid servo command. Use /s1_45 or /s2_90 (0-180)");
    }
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

String getHTMLHeader(String title) {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width,initial-scale=1">
      <title>)rawliteral";
  html += title;
  html += R"rawliteral(</title>
    <style>
      * { box-sizing: border-box; }
      body {
        font-family: 'Segoe UI', Arial, sans-serif;
        background: linear-gradient(135deg,#667eea 0%, #764ba2 100%);
        margin: 0;
        padding: 20px;
        color: #222;
      }
      .container { max-width: 1100px; margin: 0 auto; }
      .header { background: rgba(255,255,255,0.08); padding: 16px; border-radius: 12px; display:flex; justify-content:space-between; align-items:center; }
      .wifi-status { display:flex; gap:10px; align-items:center; }
      .status-badge { padding:6px 10px; border-radius:12px; color:#fff; font-weight:700; font-size:0.9em; }
      .ap { background: #4CAF50; }
      .sta { background: #2196F3; }
      .sta-disconnected { background: #ff9800; }
      .grid-container { display:grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap:18px; margin-top:18px; }
      .card { background:#fff; padding:18px; border-radius:12px; box-shadow:0 8px 20px rgba(0,0,0,0.12); }
      .relay-grid { display:flex; flex-direction:column; gap:10px; }
      .relay-item { display:flex; justify-content:space-between; align-items:center; padding:12px; background:#f8f9fa; border-radius:8px; }
      .btn-group { display:flex; gap:8px; }
      .btn { padding:8px 14px; border-radius:8px; border:none; cursor:pointer; font-weight:700; }
      .btn-on { background:#4CAF50; color:#fff; }
      .btn-off { background:#f44336; color:#fff; }
      .servo-controls { display:flex; flex-direction:column; gap:12px; }
      .sensor-readings { display:flex; flex-direction:column; gap:10px; }
      .sensor-item { display:flex; justify-content:space-between; padding:10px; background:#e3f2fd; border-radius:8px; font-weight:700; }
      .notification { position:fixed; top:18px; right:18px; background:#4CAF50; color:#fff; padding:12px 18px; border-radius:8px; z-index:999; box-shadow:0 6px 18px rgba(0,0,0,0.2); }
      .relay-state { font-weight:800; margin-left:6px; }
      .status-on { color:#4CAF50; font-weight:800; }
      .status-off { color:#f44336; font-weight:800; }
      .back-btn { color:#fff; text-decoration:none; padding:8px 12px; background:rgba(0,0,0,0.12); border-radius:8px; }
      @media (max-width: 700px) {
        .header { flex-direction: column; align-items:flex-start; gap:8px; }
      }
    </style>
    </head>
    <body>
  )rawliteral";
  return html;
}

String getHTMLFooter() {
  return R"rawliteral(
    </body>
    </html>
  )rawliteral";
}
