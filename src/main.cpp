#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

#define AP_SSID "GarageDoor"
#define AP_PASS "garage123"

// BLE bağlantı denemesi 30-35s bloklayabiliyor
#define WDT_TIMEOUT_S 60

#define RELAY_PIN   26
#define GREEN_LED   25
#define BUZZER_PIN  33

static BLEUUID serviceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");

const char* DEFAULT_TARGET = "db:e0:00:de:b6:f8";
const int MAX_TARGETS = 5;

Preferences prefs;
String storedTargets[MAX_TARGETS];
int storedCount = 0;

bool scanning = false;

WebServer server(80);

const int RSSI_OPEN_THRESHOLD = -75;

const unsigned long OPEN_DURATION = 700;
const unsigned long BUTTON_DEBOUNCE = 1500;
const unsigned long PRESENCE_TIMEOUT = 30000;
const unsigned long SCAN_INTERVAL = 1200;
const unsigned long CONNECT_INTERVAL = 5000;

BLEClient* client = nullptr;
BLERemoteCharacteristic* remoteChar = nullptr;
BLEScan* scanner = nullptr;

bool connected = false;
bool beaconVisible = false;
bool autoOpenedThisPresence = false;
bool connectGaveUpThisPresence = false;
bool justDisconnected = false;
bool relayOn = false;

int activeTargetIndex = -1;

unsigned long lastScanAt = 0;
unsigned long lastSeenAt = 0;
unsigned long relayOpenedAt = 0;
unsigned long lastButtonAt = 0;
unsigned long lastConnectAt = 0;

int rssiHistory[2] = {-999, -999};
int rssiIdx = 0;

void printStatus(const char* eventName, int rssi = -999) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] ");

  Serial.print(eventName);

  Serial.print(" | beaconVisible=");
  Serial.print(beaconVisible ? "YES" : "NO");

  Serial.print(" | connected=");
  Serial.print(connected ? "YES" : "NO");

  Serial.print(" | autoOpened=");
  Serial.print(autoOpenedThisPresence ? "YES" : "NO");

  Serial.print(" | relayOn=");
  Serial.print(relayOn ? "YES" : "NO");

  if (rssi != -999) {
    Serial.print(" | RSSI=");
    Serial.print(rssi);
  }

  Serial.println();
}

void saveTargets() {
  prefs.begin("garage", false);
  prefs.putInt("count", storedCount);
  for (int i = 0; i < storedCount; i++) {
    prefs.putString(("mac" + String(i)).c_str(), storedTargets[i]);
  }
  prefs.end();
}

void loadTargets() {
  prefs.begin("garage", true);
  int count = prefs.getInt("count", 0);
  prefs.end();

  if (count == 0) {
    storedTargets[0] = DEFAULT_TARGET;
    storedCount = 1;
    saveTargets();
    Serial.println("NVS empty, loaded default target.");
    return;
  }

  prefs.begin("garage", true);
  storedCount = constrain(count, 0, MAX_TARGETS);
  for (int i = 0; i < storedCount; i++) {
    storedTargets[i] = prefs.getString(("mac" + String(i)).c_str(), "");
  }
  prefs.end();
}

void listTargets() {
  Serial.println("--- Beacon list ---");
  for (int i = 0; i < storedCount; i++) {
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] ");
    Serial.println(storedTargets[i]);
  }
  Serial.println("-------------------");
}

void handleSerial(String cmd) {
  cmd.trim();

  if (cmd == "LIST") {
    listTargets();
    return;
  }

  if (cmd.startsWith("ADD ")) {
    String mac = cmd.substring(4);
    mac.trim();
    mac.toLowerCase();

    if (mac.length() != 17) {
      Serial.println("ERROR: invalid MAC (expected xx:xx:xx:xx:xx:xx)");
      return;
    }
    if (storedCount >= MAX_TARGETS) {
      Serial.println("ERROR: max 5 beacons");
      return;
    }
    for (int i = 0; i < storedCount; i++) {
      if (storedTargets[i] == mac) {
        Serial.println("Already exists.");
        return;
      }
    }
    storedTargets[storedCount++] = mac;
    saveTargets();
    Serial.print("Added: ");
    Serial.println(mac);
    return;
  }

  if (cmd.startsWith("REMOVE ")) {
    String mac = cmd.substring(7);
    mac.trim();
    mac.toLowerCase();

    for (int i = 0; i < storedCount; i++) {
      if (storedTargets[i] == mac) {
        for (int j = i; j < storedCount - 1; j++) {
          storedTargets[j] = storedTargets[j + 1];
        }
        storedCount--;
        saveTargets();
        Serial.print("Removed: ");
        Serial.println(mac);
        return;
      }
    }
    Serial.println("Not found.");
    return;
  }

  Serial.println("Commands: LIST | ADD <mac> | REMOVE <mac>");
}

String buildPage() {
  String h = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Garaj Kapisi</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 16px}"
    "h2{color:#222}h3{color:#444;margin-top:20px}"
    ".st{background:#f0f0f0;padding:10px;border-radius:6px;margin-bottom:16px;font-size:14px}"
    ".row{display:flex;justify-content:space-between;align-items:center;"
          "padding:8px 10px;background:#f8f8f8;margin:4px 0;border-radius:4px}"
    ".mac{font-family:monospace;font-size:13px}"
    "button{border:none;padding:6px 14px;border-radius:4px;cursor:pointer;color:#fff}"
    ".rm{background:#e74c3c}.add{background:#27ae60}"
    ".af{display:flex;gap:8px;margin-top:8px}"
    ".af input{flex:1;padding:8px;border:1px solid #ccc;border-radius:4px;font-family:monospace}"
    "</style></head><body>"
    "<h2>&#x1F6AA; Garaj Kapisi</h2>";

  h += "<div class='st'>Beacon: <b>" + String(beaconVisible ? "Gorunur" : "Yok") + "</b>"
       " &nbsp; BLE: <b>" + String(connected ? "Bagli" : "Bagli degil") + "</b></div>";

  h += "<h3>Beacon Listesi (" + String(storedCount) + "/" + String(MAX_TARGETS) + ")</h3>";
  for (int i = 0; i < storedCount; i++) {
    h += "<div class='row'><span class='mac'>" + storedTargets[i] + "</span>"
         "<form method='POST' action='/remove' style='margin:0'>"
         "<input type='hidden' name='mac' value='" + storedTargets[i] + "'>"
         "<button class='rm'>Sil</button></form></div>";
  }
  if (storedCount == 0) h += "<p style='color:#aaa'>Kayitli beacon yok.</p>";

  h += "<h3>Beacon Ekle</h3>"
       "<form class='af' method='POST' action='/add'>"
       "<input name='mac' placeholder='xx:xx:xx:xx:xx:xx' maxlength='17'>"
       "<button class='add'>Ekle</button></form>";

  h += "<h3>Yakinlardaki BLE Cihazlar</h3>"
       "<button class='scan-btn' onclick='taraClick()' style='background:#2980b9;margin-bottom:10px'>Tara</button>"
       "<div id='sonuclar'></div>"
       "<script>"
       "function taraClick(){"
         "document.getElementById('sonuclar').innerHTML='<i>Taranıyor...</i>';"
         "fetch('/scan').then(r=>r.json()).then(list=>{"
           "if(!list.length){document.getElementById('sonuclar').innerHTML='<p style=\"color:#aaa\">Cihaz bulunamadi.</p>';return;}"
           "let html='';"
           "list.forEach(d=>{"
             "html+='<div class=\"row\">';"
             "html+='<span><span class=\"mac\">'+d.mac+'</span>';"
             "html+=(d.name?'<br><small style=\"color:#888\">'+d.name+'</small>':'');"
             "html+='<br><small>RSSI: '+d.rssi+'</small></span>';"
             "html+='<form method=\"POST\" action=\"/add\" style=\"margin:0\">';"
             "html+='<input type=\"hidden\" name=\"mac\" value=\"'+d.mac+'\">';"
             "html+='<button class=\"add\">Ekle</button></form>';"
             "html+='</div>';"
           "});"
           "document.getElementById('sonuclar').innerHTML=html;"
         "}).catch(()=>document.getElementById('sonuclar').innerHTML='<p style=\"color:red\">Hata.</p>');"
       "}"
       "</script>"
       "</body></html>";
  return h;
}

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

void handleScan() {
  if (scanning || connected) {
    server.send(200, "application/json", "[]");
    return;
  }

  scanning = true;
  BLEScanResults results = scanner->start(2, false);
  scanning = false;

  String json = "[";
  bool first = true;
  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice device = results.getDevice(i);
    if (!first) json += ",";
    json += "{\"mac\":\"" + String(device.getAddress().toString().c_str()) + "\","
            "\"name\":\"" + String(device.haveName() ? device.getName().c_str() : "") + "\","
            "\"rssi\":" + String(device.getRSSI()) + "}";
    first = false;
  }
  scanner->clearResults();
  json += "]";
  server.send(200, "application/json", json);
}

void handleAdd() {
  if (server.hasArg("mac")) {
    String mac = server.arg("mac");
    mac.trim();
    mac.toLowerCase();
    if (mac.length() == 17 && storedCount < MAX_TARGETS) {
      bool exists = false;
      for (int i = 0; i < storedCount; i++) {
        if (storedTargets[i] == mac) { exists = true; break; }
      }
      if (!exists) { storedTargets[storedCount++] = mac; saveTargets(); }
    }
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleRemove() {
  if (server.hasArg("mac")) {
    String mac = server.arg("mac");
    mac.trim();
    mac.toLowerCase();
    for (int i = 0; i < storedCount; i++) {
      if (storedTargets[i] == mac) {
        for (int j = i; j < storedCount - 1; j++) storedTargets[j] = storedTargets[j + 1];
        storedCount--;
        saveTargets();
        break;
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void setupWifi() {
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP: " AP_SSID " → http://");
  Serial.println(WiFi.softAPIP());

  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/scan",   HTTP_GET,  handleScan);
  server.on("/add",    HTTP_POST, handleAdd);
  server.on("/remove", HTTP_POST, handleRemove);
  server.begin();
}

int updateRssi(int raw) {
  rssiHistory[rssiIdx] = raw;
  rssiIdx = (rssiIdx + 1) % 2;

  int sum = 0, count = 0;
  for (int i = 0; i < 2; i++) {
    if (rssiHistory[i] != -999) { sum += rssiHistory[i]; count++; }
  }
  return count > 0 ? sum / count : raw;
}

void resetRssi() {
  for (int i = 0; i < 2; i++) rssiHistory[i] = -999;
  rssiIdx = 0;
}


// LOW trigger passive buzzer
void toneLowTrigger(unsigned int freq, unsigned int durationMs) {
  unsigned long periodUs = 1000000UL / freq;
  unsigned long halfPeriodUs = periodUs / 2;
  unsigned long start = millis();

  while (millis() - start < durationMs) {
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(halfPeriodUs);

    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(halfPeriodUs);
  }

  digitalWrite(BUZZER_PIN, HIGH);
}

void beepShort() {
  toneLowTrigger(2500, 70);
}

void beepBeaconFound() {
  beepShort();
}

void beepAutoOpen() {
  beepShort();
  delay(80);
  beepShort();
}

void beepButtonOpen() {
  beepShort();
  delay(80);
  beepShort();
  delay(80);
  beepShort();
}

void beepConnected() {
  for (int i = 0; i < 4; i++) {
    toneLowTrigger(3000, 45);
    delay(45);
  }
}

void beepError() {
  for (int i = 0; i < 5; i++) {
    toneLowTrigger(700, 60);
    delay(60);
  }
}

void triggerDoor(bool fromButton) {
  digitalWrite(RELAY_PIN, HIGH);

  relayOn = true;
  relayOpenedAt = millis();

  printStatus(fromButton ? "OPEN BY BUTTON" : "OPEN BY PROXIMITY");

  if (fromButton) {
    beepButtonOpen();
  } else {
    beepAutoOpen();
  }
}

void stopRelay() {
  digitalWrite(RELAY_PIN, LOW);
  relayOn = false;

  printStatus("RELAY OFF");
}

void checkRelay() {
  if (relayOn && millis() - relayOpenedAt > OPEN_DURATION) {
    stopRelay();
  }
}

void updateLed() {
  digitalWrite(GREEN_LED, beaconVisible ? HIGH : LOW);
}

class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    printStatus("BLE CONNECTED");
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    remoteChar = nullptr;
    justDisconnected = true;

    printStatus("BLE DISCONNECTED");
  }
};

static ClientCallbacks clientCallbacks;

void notifyCallback(
  BLERemoteCharacteristic* characteristic,
  uint8_t* data,
  size_t length,
  bool isNotify
) {
  if (length == 0) return;

  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] NOTIFY DATA: ");

  for (size_t i = 0; i < length; i++) {
    Serial.printf("%02X ", data[i]);
  }

  Serial.println();

  if (data[0] == 0x01) {
    if (millis() - lastButtonAt > BUTTON_DEBOUNCE) {
      lastButtonAt = millis();
      triggerDoor(true);
    } else {
      printStatus("BUTTON IGNORED DEBOUNCE");
    }
  }
}

esp_ble_addr_type_t guessAddrType(const String& mac) {
  int firstByte = strtol(mac.substring(0, 2).c_str(), nullptr, 16);
  return ((firstByte & 0xC0) == 0xC0) ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC;
}

bool connectForButton() {
  // Relay pulse tamamlanmadan connect'e girme
  if (relayOn) {
    unsigned long elapsed = millis() - relayOpenedAt;
    if (elapsed < OPEN_DURATION) delay(OPEN_DURATION - elapsed + 10);
    stopRelay();
  }

  if (activeTargetIndex < 0) {
    printStatus("CONNECT SKIPPED NO ACTIVE TARGET");
    return false;
  }

  if (connected) {
    return true;
  }

  const char* addressText = storedTargets[activeTargetIndex].c_str();

  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] CONNECTING FOR BUTTON: ");
  Serial.println(addressText);

  if (client != nullptr) {
    client->disconnect();
    delete client;
    client = nullptr;
  }

  client = BLEDevice::createClient();
  client->setClientCallbacks(&clientCallbacks);

  BLEAddress targetAddress(addressText);
  esp_ble_addr_type_t addrType = guessAddrType(storedTargets[activeTargetIndex]);

  esp_task_wdt_reset();
  if (!client->connect(targetAddress, addrType)) {
    checkRelay();
    connectGaveUpThisPresence = true;
    printStatus("BUTTON CONNECTION FAILED");
    return false;
  }

  checkRelay();

  BLERemoteService* service = client->getService(serviceUUID);

  if (service == nullptr) {
    printStatus("FFE0 SERVICE NOT FOUND");
    client->disconnect();
    beepError();
    return false;
  }

  remoteChar = service->getCharacteristic(charUUID);

  if (remoteChar == nullptr) {
    printStatus("FFE1 CHARACTERISTIC NOT FOUND");
    client->disconnect();
    beepError();
    return false;
  }

  if (!remoteChar->canNotify()) {
    printStatus("NOTIFY NOT SUPPORTED");
    client->disconnect();
    beepError();
    return false;
  }

  remoteChar->registerForNotify(notifyCallback);
  connected = true;

  printStatus("BUTTON NOTIFY ACTIVE");
  beepConnected();

  return true;
}

void scanBeacon() {
  scanning = true;
  BLEScanResults results = scanner->start(1, false);
  scanning = false;
  checkRelay();

  bool found = false;
  int bestRssi = -999;
  int bestTargetIdx = -1;

  int count = results.getCount();
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice device = results.getDevice(i);
    int rssi = device.getRSSI();
    String addr = device.getAddress().toString().c_str();
    addr.toLowerCase();

    for (int j = 0; j < storedCount; j++) {
      if (addr == storedTargets[j] && rssi > bestRssi) {
        found = true;
        bestRssi = rssi;
        bestTargetIdx = j;
        break;
      }
    }
  }

  // Kapı açıldıktan sonra hedefi değiştirme
  if (bestTargetIdx >= 0 && !autoOpenedThisPresence) activeTargetIndex = bestTargetIdx;

  scanner->clearResults();

  if (!found) {
    // Beacon görünmüyorsa disconnect sonrası uzaklaşıldı say
    if (justDisconnected) {
      justDisconnected = false;
      autoOpenedThisPresence = false;
      connectGaveUpThisPresence = false;
      resetRssi();
    }
    return;
  }

  lastSeenAt = millis();

  int smoothed = updateRssi(bestRssi);

  // Disconnect sonrası threshold altında görünüyorsa uzaklaşmış sayılır
  if (justDisconnected) {
    justDisconnected = false;
    if (smoothed < RSSI_OPEN_THRESHOLD) {
      autoOpenedThisPresence = false;
      connectGaveUpThisPresence = false;
      resetRssi();
    }
  }

  if (!beaconVisible) {
    beaconVisible = true;
    autoOpenedThisPresence = false;
    connectGaveUpThisPresence = false;

    printStatus("BEACON FOUND", smoothed);
    beepBeaconFound();
  }

  if (!autoOpenedThisPresence && smoothed >= RSSI_OPEN_THRESHOLD) {
    autoOpenedThisPresence = true;
    triggerDoor(false);
    checkRelay();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BUZZER_PIN, HIGH);

  BLEDevice::init("ESP32-iTAG-Lock");

  scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(80);

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  loadTargets();
  listTargets();
  setupWifi();

  Serial.println("ESP32 iTAG LOCK STARTED");
}

void loop() {
  unsigned long now = millis();

  if (!connected && now - lastScanAt > SCAN_INTERVAL) {
    lastScanAt = now;
    scanBeacon();
    now = millis();
  }

  if (connected) {
    lastSeenAt = now;
  }

  if (beaconVisible && !connected && now - lastSeenAt > PRESENCE_TIMEOUT) {
    beaconVisible = false;
    autoOpenedThisPresence = false;
    connectGaveUpThisPresence = false;
    activeTargetIndex = -1;
    resetRssi();

    printStatus("BEACON LOST");
  }

  if (beaconVisible && !connected && autoOpenedThisPresence && !connectGaveUpThisPresence && now - lastConnectAt > CONNECT_INTERVAL) {
    lastConnectAt = now;
    connectForButton();
    checkRelay();
  }

  if (relayOn && now - relayOpenedAt > OPEN_DURATION) {
    stopRelay();
  }

  updateLed();

  server.handleClient();

  if (Serial.available()) {
    handleSerial(Serial.readStringUntil('\n'));
  }

  esp_task_wdt_reset();
  delay(50);
}