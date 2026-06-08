#include <Arduino.h>
#include <BLEDevice.h>

#define RELAY_PIN   26
#define GREEN_LED   25
#define BUZZER_PIN  33

static BLEUUID serviceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");

// Yeni beacon eklemek için buraya adres ekle
const char* TARGETS[] = {
    "db:e0:00:de:b6:f8"
    // "aa:bb:cc:dd:ee:ff",
    // "11:22:33:44:55:66"
};

const int TARGET_COUNT = sizeof(TARGETS) / sizeof(TARGETS[0]);
int currentTargetIndex = 0;

BLEClient* client = nullptr;
BLERemoteCharacteristic* remoteChar = nullptr;

bool connected = false;
bool relayOn = false;

unsigned long lastReconnectTry = 0;
unsigned long relayOpenedAt = 0;
unsigned long lastButtonAt = 0;

const unsigned long RECONNECT_INTERVAL = 5000;
const unsigned long OPEN_DURATION = 2000;
const unsigned long BUTTON_DEBOUNCE = 1500;

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("Baglandi");
    }

    void onDisconnect(BLEClient* pclient) {
        Serial.println("Baglanti koptu");
        connected = false;
        remoteChar = nullptr;

        currentTargetIndex++;
        if (currentTargetIndex >= TARGET_COUNT) {
            currentTargetIndex = 0;
        }
    }
};

// LOW LEVEL TRIGGER PASIF BUZZER
// LOW  -> aktif darbe
// HIGH -> pasif
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
    toneLowTrigger(2500, 80);
}

void beepLongLow() {
    toneLowTrigger(900, 350);
}

void beepOpenDoor() {
    // 1 kısa net bip
    beepShort();
}

void beepCloseDoor() {
    // 2 kısa bip
    beepShort();
    delay(100);
    beepShort();
}

void beepConnected() {
    // 3 kısa bip
    beepShort();
    delay(80);
    beepShort();
    delay(80);
    beepShort();
}

void beepConnectionFailed() {
    // 1 uzun düşük bip
    beepLongLow();
}

void beepServiceError() {
    // kesik kesik hata sesi
    for (int i = 0; i < 5; i++) {
        toneLowTrigger(700, 60);
        delay(60);
    }
}

void openDoor() {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(GREEN_LED, HIGH);

    relayOn = true;
    relayOpenedAt = millis();

    Serial.println("KAPI 10 SN ACIK");

    beepOpenDoor();
}

void closeDoor() {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(GREEN_LED, LOW);

    relayOn = false;

    Serial.println("KAPI KAPANDI");

    beepCloseDoor();
}

void notifyCallback(
    BLERemoteCharacteristic* characteristic,
    uint8_t* data,
    size_t length,
    bool isNotify
) {
    if (length == 0) return;

    Serial.print("Notify: ");
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();

    if (data[0] == 0x01) {
        if (millis() - lastButtonAt > BUTTON_DEBOUNCE) {
            lastButtonAt = millis();
            openDoor();
        }
    }
}

bool connectToITag() {
    const char* addressText = TARGETS[currentTargetIndex];
    BLEAddress targetAddress(addressText);

    Serial.print("iTAG baglaniliyor: ");
    Serial.println(addressText);

    if (client != nullptr) {
        client->disconnect();
        delete client;
        client = nullptr;
    }

    client = BLEDevice::createClient();
    client->setClientCallbacks(new ClientCallbacks());

    if (!client->connect(targetAddress)) {
        Serial.println("Baglanti basarisiz");

        currentTargetIndex++;
        if (currentTargetIndex >= TARGET_COUNT) {
            currentTargetIndex = 0;
        }

        beepConnectionFailed();
        return false;
    }

    BLERemoteService* service = client->getService(serviceUUID);
    if (service == nullptr) {
        Serial.println("FFE0 service yok");
        client->disconnect();

        beepServiceError();
        return false;
    }

    remoteChar = service->getCharacteristic(charUUID);
    if (remoteChar == nullptr) {
        Serial.println("FFE1 characteristic yok");
        client->disconnect();

        beepServiceError();
        return false;
    }

    if (remoteChar->canNotify()) {
        remoteChar->registerForNotify(notifyCallback);
        Serial.println("Notify aktif");
    } else {
        Serial.println("Notify desteklemiyor");
        client->disconnect();

        beepServiceError();
        return false;
    }

    connected = true;

    Serial.print("Aktif beacon: ");
    Serial.println(addressText);

    beepConnected();

    return true;
}

void setup() {
    Serial.begin(115200);

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BUZZER_PIN, HIGH); // low-level trigger olduğu için HIGH = sessiz

    BLEDevice::init("ESP32-iTAG-Lock");

    Serial.println("Basladi");
}

void loop() {
    if (!connected && millis() - lastReconnectTry > RECONNECT_INTERVAL) {
        lastReconnectTry = millis();
        connectToITag();
    }

    if (relayOn && millis() - relayOpenedAt > OPEN_DURATION) {
        closeDoor();
    }

    delay(100);
}