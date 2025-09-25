/*
  ESP32 GPS Monitor with OLED Display & WiFi Dashboard
  
  Features:
  - OLED display showing GPS time, satellites, and coordinates
  - WiFi server for web-based GPS monitoring
  - Real-time GPS data from NEO-6M
  
  Hardware Connections:
  NEO-6M GPS:
    VCC -> 3.3V
    GND -> GND
    TX  -> GPIO 16 (RX2)
    RX  -> GPIO 17 (TX2)
  
  OLED Display (I2C):
    VCC -> 3.3V
    GND -> GND
    SDA -> GPIO 21 (default I2C SDA)
    SCL -> GPIO 22 (default I2C SCL)
  
  WiFi Access Point:
    SSID: ESP32_GPS_Monitor
    Password: gps123456
    IP: 192.168.4.1
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi credentials for Access Point
const char* ssid = "ESP32_GPS_Monitor";
const char* password = "gps123456";

// Create web server on port 80
WebServer server(80);

// Create a HardwareSerial object for GPS communication
HardwareSerial gpsSerial(2);

// GPS communication pins
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define LED_BUILTIN 2

// GPS data structure
struct GPSData {
  String time = "No Fix";
  String date = "No Fix";
  String latitude = "No Fix";
  String longitude = "No Fix";
  String altitude = "No Fix";
  String speed = "No Fix";
  String course = "No Fix";
  String satellites = "0";
  String fixQuality = "0";
  String fixStatus = "Invalid";
  bool hasValidFix = false;
  unsigned long lastUpdate = 0;
  
  // Parsed time components for OLED display
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  float latDecimal = 0.0;
  float lonDecimal = 0.0;
} gpsData;

// Variables for GPS data parsing
String currentSentence = "";
bool newGPSData = false;

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 GPS with OLED and WiFi Server Starting...");
  
  // Initialize LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  // Show startup message on OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("GPS Monitor Starting");
  display.println("Initializing...");
  display.display();
  delay(2000);
  
  // Initialize GPS
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS Serial initialized");
  
  // Setup WiFi Access Point
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Starting WiFi AP...");
  display.display();
  
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Access Point IP: ");
  Serial.println(IP);
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/style.css", handleCSS);
  
  server.begin();
  Serial.println("Web server started");
  
  // Show WiFi info on OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi: ESP32_GPS_Monitor");
  display.println("IP: 192.168.4.1");
  display.println("Waiting for GPS...");
  display.display();
  delay(3000);
  
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  server.handleClient();
  readGPSData();
  updateOLEDDisplay();
  
  // Update LED status based on GPS fix
  static unsigned long lastBlink = 0;
  unsigned long blinkInterval = gpsData.hasValidFix ? 1000 : 200;
  
  if (millis() - lastBlink > blinkInterval) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastBlink = millis();
  }
  
  // Check if GPS data is stale
  if (millis() - gpsData.lastUpdate > 5000) {
    gpsData.hasValidFix = false;
  }
  
  delay(100); // Small delay for stability
}

void updateOLEDDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  
  // Update OLED every 500ms for smooth display
  if (millis() - lastDisplayUpdate > 500) {
    display.clearDisplay();
    
    if (gpsData.hasValidFix) {
      // Display GPS time in large font
      display.setTextSize(2);
      display.setCursor(10, 0);
      display.printf("%02d:%02d:%02d", gpsData.hours, gpsData.minutes, gpsData.seconds);
      
      // Display satellite count
      display.setTextSize(1);
      display.setCursor(0, 20);
      display.printf("Sats: %s", gpsData.satellites.c_str());
      
      // Display coordinates
      display.setCursor(0, 30);
      display.printf("Lat: %.4f", gpsData.latDecimal);
      display.setCursor(0, 40);
      display.printf("Lon: %.4f", gpsData.lonDecimal);
      
      // Display date
      display.setCursor(0, 50);
      display.printf("Date: %s", gpsData.date.c_str());
      
      // GPS Fix indicator
      display.setCursor(100, 20);
      display.println("FIX");
      
    } else {
      // No GPS fix - show waiting message
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("GPS Status: No Fix");
      display.println("");
      display.printf("Satellites: %s\n", gpsData.satellites.c_str());
      display.println("");
      display.println("Waiting for signal...");
      display.println("Need clear sky view");
      display.println("");
      
      // Show animated waiting indicator
      static int waitDots = 0;
      display.setCursor(0, 56);
      display.print("Searching");
      for(int i = 0; i < (waitDots % 4); i++) {
        display.print(".");
      }
      waitDots++;
    }
    
    display.display();
    lastDisplayUpdate = millis();
  }
}

void readGPSData() {
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    
    if (c == '\n') {
      if (currentSentence.length() > 6) {
        parseNMEA(currentSentence);
        gpsData.lastUpdate = millis();
      }
      currentSentence = "";
    } else if (c != '\r') {
      currentSentence += c;
    }
    
    if (currentSentence.length() > 200) {
      currentSentence = "";
    }
  }
}

void parseNMEA(String sentence) {
  if (sentence.startsWith("$GPGGA") || sentence.startsWith("$GNGGA")) {
    parseGGA(sentence);
  } else if (sentence.startsWith("$GPRMC") || sentence.startsWith("$GNRMC")) {
    parseRMC(sentence);
  } else if (sentence.startsWith("$GPGSV") || sentence.startsWith("$GNGSV")) {
    parseGSV(sentence);
  }
}

void parseGGA(String sentence) {
  int commaIndex[15];
  int commaCount = 0;
  
  for (int i = 0; i < sentence.length() && commaCount < 15; i++) {
    if (sentence.charAt(i) == ',') {
      commaIndex[commaCount] = i;
      commaCount++;
    }
  }
  
  if (commaCount >= 14) {
    String time = sentence.substring(commaIndex[0] + 1, commaIndex[1]);
    String lat = sentence.substring(commaIndex[1] + 1, commaIndex[2]);
    String latDir = sentence.substring(commaIndex[2] + 1, commaIndex[3]);
    String lon = sentence.substring(commaIndex[3] + 1, commaIndex[4]);
    String lonDir = sentence.substring(commaIndex[4] + 1, commaIndex[5]);
    String quality = sentence.substring(commaIndex[5] + 1, commaIndex[6]);
    String sats = sentence.substring(commaIndex[6] + 1, commaIndex[7]);
    String alt = sentence.substring(commaIndex[8] + 1, commaIndex[9]);
    
    if (time.length() >= 6) {
      gpsData.time = formatTime(time);
      // Parse time components for OLED
      gpsData.hours = time.substring(0, 2).toInt();
      gpsData.minutes = time.substring(2, 4).toInt();
      gpsData.seconds = time.substring(4, 6).toInt();
    }
    
    if (lat.length() > 0 && lon.length() > 0) {
      gpsData.latitude = convertCoordinate(lat, latDir);
      gpsData.longitude = convertCoordinate(lon, lonDir);
      
      // Convert to decimal for OLED display
      gpsData.latDecimal = convertToDecimal(lat, latDir);
      gpsData.lonDecimal = convertToDecimal(lon, lonDir);
    }
    
    gpsData.fixQuality = quality;
    gpsData.satellites = sats;
    
    if (alt.length() > 0) {
      gpsData.altitude = alt + " m";
    }
    
    gpsData.hasValidFix = (quality.toInt() > 0);
  }
}

void parseRMC(String sentence) {
  int commaIndex[12];
  int commaCount = 0;
  
  for (int i = 0; i < sentence.length() && commaCount < 12; i++) {
    if (sentence.charAt(i) == ',') {
      commaIndex[commaCount] = i;
      commaCount++;
    }
  }
  
  if (commaCount >= 11) {
    String status = sentence.substring(commaIndex[1] + 1, commaIndex[2]);
    String speed = sentence.substring(commaIndex[6] + 1, commaIndex[7]);
    String course = sentence.substring(commaIndex[7] + 1, commaIndex[8]);
    String date = sentence.substring(commaIndex[8] + 1, commaIndex[9]);
    
    gpsData.fixStatus = (status == "A") ? "Valid" : "Invalid";
    
    if (speed.length() > 0) {
      float speedKnots = speed.toFloat();
      float speedKmh = speedKnots * 1.852;
      gpsData.speed = String(speedKmh, 1) + " km/h";
    }
    
    if (course.length() > 0) {
      gpsData.course = course + "°";
    }
    
    if (date.length() >= 6) {
      gpsData.date = formatDate(date);
    }
  }
}

void parseGSV(String sentence) {
  int commaIndex[20];
  int commaCount = 0;
  
  for (int i = 0; i < sentence.length() && commaCount < 20; i++) {
    if (sentence.charAt(i) == ',') {
      commaIndex[commaCount] = i;
      commaCount++;
    }
  }
  
  if (commaCount >= 3) {
    String totalSats = sentence.substring(commaIndex[2] + 1, commaIndex[3]);
    if (totalSats.length() > 0) {
      gpsData.satellites = totalSats;
    }
  }
}

String formatTime(String rawTime) {
  if (rawTime.length() >= 6) {
    String hours = rawTime.substring(0, 2);
    String minutes = rawTime.substring(2, 4);
    String seconds = rawTime.substring(4, 6);
    return hours + ":" + minutes + ":" + seconds + " UTC";
  }
  return "Invalid";
}

String formatDate(String rawDate) {
  if (rawDate.length() >= 6) {
    String day = rawDate.substring(0, 2);
    String month = rawDate.substring(2, 4);
    String year = "20" + rawDate.substring(4, 6);
    return day + "/" + month + "/" + year;
  }
  return "Invalid";
}

String convertCoordinate(String coord, String dir) {
  if (coord.length() < 4) return "Invalid";
  
  float value = coord.toFloat();
  int degrees = (int)(value / 100);
  float minutes = value - (degrees * 100);
  float decimal = degrees + (minutes / 60.0);
  
  String result = String(decimal, 6) + "° " + dir;
  return result;
}

float convertToDecimal(String coord, String dir) {
  if (coord.length() < 4) return 0.0;
  
  float value = coord.toFloat();
  int degrees = (int)(value / 100);
  float minutes = value - (degrees * 100);
  float decimal = degrees + (minutes / 60.0);
  
  if (dir == "S" || dir == "W") {
    decimal = -decimal;
  }
  
  return decimal;
}

void handleRoot() {
  String html = "<!DOCTYPE html>\n";
  html += "<html><head>\n";
  html += "<meta charset='UTF-8'>\n";
  html += "<title>ESP32 GPS Monitor</title>\n";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>\n";
  html += "<link rel='stylesheet' type='text/css' href='/style.css'>\n";
  html += "</head><body>\n";
  html += "<div class='container'>\n";
  html += "<h1>ESP32 GPS Monitor</h1>\n";
  html += "<div class='status-bar'>\n";
  html += "<div class='status-item'>\n";
  html += "<span class='status-label'>Status:</span>\n";
  html += "<span class='status-value' id='status'>Loading...</span>\n";
  html += "</div>\n";
  html += "<div class='status-item'>\n";
  html += "<span class='status-label'>Satellites:</span>\n";
  html += "<span class='status-value' id='satellites'>-</span>\n";
  html += "</div>\n";
  html += "</div>\n";
  
  html += "<div class='data-grid'>\n";
  html += "<div class='data-card'>\n";
  html += "<h3>Date & Time</h3>\n";
  html += "<p><strong>Date:</strong> <span id='date'>Loading...</span></p>\n";
  html += "<p><strong>Time:</strong> <span id='time'>Loading...</span></p>\n";
  html += "</div>\n";
  
  html += "<div class='data-card'>\n";
  html += "<h3>Position</h3>\n";
  html += "<p><strong>Latitude:</strong> <span id='latitude'>Loading...</span></p>\n";
  html += "<p><strong>Longitude:</strong> <span id='longitude'>Loading...</span></p>\n";
  html += "</div>\n";
  
  html += "<div class='data-card'>\n";
  html += "<h3>Measurements</h3>\n";
  html += "<p><strong>Altitude:</strong> <span id='altitude'>Loading...</span></p>\n";
  html += "<p><strong>Speed:</strong> <span id='speed'>Loading...</span></p>\n";
  html += "<p><strong>Course:</strong> <span id='course'>Loading...</span></p>\n";
  html += "</div>\n";
  
  html += "<div class='data-card'>\n";
  html += "<h3>Signal Info</h3>\n";
  html += "<p><strong>Fix Quality:</strong> <span id='fixQuality'>Loading...</span></p>\n";
  html += "<p><strong>Fix Status:</strong> <span id='fixStatus'>Loading...</span></p>\n";
  html += "<p><strong>Last Update:</strong> <span id='lastUpdate'>Loading...</span></p>\n";
  html += "</div>\n";
  html += "</div>\n";
  
  html += "<div class='footer'>\n";
  html += "<p>Data updates every 2 seconds | ESP32 GPS Monitor v1.0</p>\n";
  html += "</div>\n";
  html += "</div>\n";
  
  // JavaScript for auto-refresh
  html += "<script>\n";
  html += "function updateData() {\n";
  html += "  fetch('/data')\n";
  html += "    .then(response => response.json())\n";
  html += "    .then(data => {\n";
  html += "      document.getElementById('status').textContent = data.hasValidFix ? 'GPS Fix' : 'No Fix';\n";
  html += "      document.getElementById('status').className = 'status-value ' + (data.hasValidFix ? 'valid' : 'invalid');\n";
  html += "      document.getElementById('satellites').textContent = data.satellites;\n";
  html += "      document.getElementById('date').textContent = data.date;\n";
  html += "      document.getElementById('time').textContent = data.time;\n";
  html += "      document.getElementById('latitude').textContent = data.latitude;\n";
  html += "      document.getElementById('longitude').textContent = data.longitude;\n";
  html += "      document.getElementById('altitude').textContent = data.altitude;\n";
  html += "      document.getElementById('speed').textContent = data.speed;\n";
  html += "      document.getElementById('course').textContent = data.course;\n";
  html += "      document.getElementById('fixQuality').textContent = data.fixQuality;\n";
  html += "      document.getElementById('fixStatus').textContent = data.fixStatus;\n";
  html += "      document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();\n";
  html += "    })\n";
  html += "    .catch(err => console.log('Error:', err));\n";
  html += "}\n";
  html += "updateData();\n";
  html += "setInterval(updateData, 2000);\n";
  html += "</script>\n";
  html += "</body></html>\n";
  
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{\n";
  json += "  \"time\": \"" + gpsData.time + "\",\n";
  json += "  \"date\": \"" + gpsData.date + "\",\n";
  json += "  \"latitude\": \"" + gpsData.latitude + "\",\n";
  json += "  \"longitude\": \"" + gpsData.longitude + "\",\n";
  json += "  \"altitude\": \"" + gpsData.altitude + "\",\n";
  json += "  \"speed\": \"" + gpsData.speed + "\",\n";
  json += "  \"course\": \"" + gpsData.course + "\",\n";
  json += "  \"satellites\": \"" + gpsData.satellites + "\",\n";
  json += "  \"fixQuality\": \"" + gpsData.fixQuality + "\",\n";
  json += "  \"fixStatus\": \"" + gpsData.fixStatus + "\",\n";
  json += "  \"hasValidFix\": ";
  json += gpsData.hasValidFix ? "true" : "false";
  json += "\n}";
  
  server.send(200, "application/json", json);
}

void handleCSS() {
  String css = "body {\n";
  css += "  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n";
  css += "  margin: 0;\n";
  css += "  padding: 20px;\n";
  css += "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n";
  css += "  min-height: 100vh;\n";
  css += "}\n";
  
  css += ".container {\n";
  css += "  max-width: 1200px;\n";
  css += "  margin: 0 auto;\n";
  css += "  background: white;\n";
  css += "  border-radius: 15px;\n";
  css += "  box-shadow: 0 10px 30px rgba(0,0,0,0.3);\n";
  css += "  overflow: hidden;\n";
  css += "}\n";
  
  css += "h1 {\n";
  css += "  background: linear-gradient(135deg, #2c3e50, #34495e);\n";
  css += "  color: white;\n";
  css += "  text-align: center;\n";
  css += "  margin: 0;\n";
  css += "  padding: 30px;\n";
  css += "  font-size: 2.5em;\n";
  css += "  text-shadow: 2px 2px 4px rgba(0,0,0,0.3);\n";
  css += "}\n";
  
  css += ".status-bar {\n";
  css += "  display: flex;\n";
  css += "  justify-content: space-around;\n";
  css += "  background: #ecf0f1;\n";
  css += "  padding: 20px;\n";
  css += "  border-bottom: 3px solid #3498db;\n";
  css += "}\n";
  
  css += ".status-item {\n";
  css += "  text-align: center;\n";
  css += "}\n";
  
  css += ".status-label {\n";
  css += "  display: block;\n";
  css += "  font-weight: bold;\n";
  css += "  color: #2c3e50;\n";
  css += "  font-size: 1.1em;\n";
  css += "  margin-bottom: 5px;\n";
  css += "}\n";
  
  css += ".status-value {\n";
  css += "  display: block;\n";
  css += "  font-size: 1.5em;\n";
  css += "  font-weight: bold;\n";
  css += "  padding: 10px 20px;\n";
  css += "  border-radius: 25px;\n";
  css += "  min-width: 100px;\n";
  css += "}\n";
  
  css += ".status-value.valid {\n";
  css += "  background: #2ecc71;\n";
  css += "  color: white;\n";
  css += "}\n";
  
  css += ".status-value.invalid {\n";
  css += "  background: #e74c3c;\n";
  css += "  color: white;\n";
  css += "}\n";
  
  css += ".data-grid {\n";
  css += "  display: grid;\n";
  css += "  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));\n";
  css += "  gap: 20px;\n";
  css += "  padding: 30px;\n";
  css += "}\n";
  
  css += ".data-card {\n";
  css += "  background: linear-gradient(135deg, #f8f9fa, #e9ecef);\n";
  css += "  border-radius: 10px;\n";
  css += "  padding: 25px;\n";
  css += "  box-shadow: 0 5px 15px rgba(0,0,0,0.1);\n";
  css += "  transition: transform 0.3s ease;\n";
  css += "}\n";
  
  css += ".data-card:hover {\n";
  css += "  transform: translateY(-5px);\n";
  css += "}\n";
  
  css += ".data-card h3 {\n";
  css += "  color: #2c3e50;\n";
  css += "  margin: 0 0 20px 0;\n";
  css += "  font-size: 1.4em;\n";
  css += "  border-bottom: 2px solid #3498db;\n";
  css += "  padding-bottom: 10px;\n";
  css += "}\n";
  
  css += ".data-card p {\n";
  css += "  margin: 15px 0;\n";
  css += "  font-size: 1.1em;\n";
  css += "  color: #34495e;\n";
  css += "}\n";
  
  css += ".data-card strong {\n";
  css += "  color: #2c3e50;\n";
  css += "}\n";
  
  css += ".footer {\n";
  css += "  background: #2c3e50;\n";
  css += "  color: white;\n";
  css += "  text-align: center;\n";
  css += "  padding: 20px;\n";
  css += "  margin-top: 20px;\n";
  css += "}\n";
  
  css += "@media (max-width: 768px) {\n";
  css += "  .status-bar { flex-direction: column; gap: 15px; }\n";
  css += "  .data-grid { grid-template-columns: 1fr; }\n";
  css += "  h1 { font-size: 2em; padding: 20px; }\n";
  css += "  body { padding: 10px; }\n";
  css += "}\n";
  
  server.send(200, "text/css", css);
}