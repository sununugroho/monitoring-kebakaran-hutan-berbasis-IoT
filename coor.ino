#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <LoRa.h>
#include <FirebaseESP32.h>

// ===== WiFi credentials =====
const char* ssid = "Sunu";
const char* password = "88888888";

// ===== ThingSpeak =====
String writeApiKey = "OOQDWQ9FEI612GMZ";
const char* readApiKey = "Y7NISYNOJUM5BD32";
const char* channelID = "2355586";

// ===== Firebase =====
#define FIREBASE_HOST "https://skirpsi-43d72-default-rtdb.firebaseio.com/"
#define FIREBASE_AUTH "shaUqMU2ud0dBu5exKeNeupmlJTrEOfR5cyMVwvO"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ===== LoRa Pin Mapping =====
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SS   5
#define LORA_RST  14
#define LORA_DIO0 26

// ===== LED dan Buzzer Pin =====
#define LED_HIJAU    4
#define LED_KUNING   22
#define LED_MERAH    32
#define BUZZER_PIN   27

unsigned long lastPacketTime = 0;
unsigned long checkInterval = 20000;
bool loraOffline = false;
int currentStatus = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_HIJAU, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_MERAH, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  allOff();
  connectWiFi();
  setupLoRa();
  setupFirebase();

  lastPacketTime = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) incoming += (char)LoRa.read();

    Serial.println("Data LoRa masuk: " + incoming);
    lastPacketTime = millis();
    loraOffline = false;

    handleIncomingData(incoming); 
    // updateLED sekarang dipanggil di handleIncomingData()
  }

  if (millis() - lastPacketTime > checkInterval) {
    if (!loraOffline) {
      Serial.println("Tidak ada data LoRa diterima! Reconnect LoRa...");
      reconnectLoRa();
      loraOffline = true;
    }
    warningMode();
  }
}

// ---------------------- Setup Function ----------------------

void setupLoRa() {
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa Gagal Start");
    while (1);
  }
  Serial.println("LoRa Initialized");
}

void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi Connected!" : "\nFailed to connect WiFi!");
}

void setupFirebase() {
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// ---------------------- Control Function ----------------------

void allOff() {
  digitalWrite(LED_HIJAU, LOW);
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_MERAH, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

void warningMode() {
  digitalWrite(LED_HIJAU, LOW);
  digitalWrite(LED_KUNING, HIGH);
  digitalWrite(LED_MERAH, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
}

void normalMode() {
  digitalWrite(BUZZER_PIN, LOW);
}

// ---------------------- Main Handler ----------------------

void handleIncomingData(String incoming) {
  int s1 = incoming.indexOf('|');
  int s2 = incoming.indexOf('|', s1 + 1);
  int s3 = incoming.indexOf('|', s2 + 1);
  int s4 = incoming.indexOf('|', s3 + 1);
  int s5 = incoming.indexOf('|', s4 + 1);

  if (s1 > 0 && s2 > 0 && s3 > 0 && s4 > 0 && s5 > 0) {
    String wind = incoming.substring(0, s1);
    String statusStr = incoming.substring(s1 + 1, s2);
    String suhu = incoming.substring(s2 + 1, s3);
    String rh = incoming.substring(s3 + 1, s4);
    String api = incoming.substring(s4 + 1, s5);
    String gas = incoming.substring(s5 + 1);

    // LANGSUNG update status dari LoRa tanpa tunggu ThingSpeak!
    currentStatus = statusStr.toInt();
    updateLED(currentStatus);

    // Upload tetap jalan tapi TIDAK MENGHAMBAT LED
    uploadToThingSpeak(wind, statusStr, suhu, rh, api, gas);
    uploadToFirebase(wind, statusStr, suhu, rh, api, gas);

  } else {
    Serial.println("Format data salah");
  }
}

void uploadToThingSpeak(String wind, String status, String suhu, String rh, String api, String gas) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = String("https://api.thingspeak.com/update?api_key=") + writeApiKey +
                 "&field1=" + wind +
                 "&field2=" + status +
                 "&field3=" + suhu +
                 "&field4=" + rh +
                 "&field5=" + api +
                 "&field6=" + gas;

    http.begin(client, url);
    int httpCode = http.GET();
    Serial.println(httpCode > 0 ? "Upload OK: " + String(httpCode) : "Upload Gagal: " + http.errorToString(httpCode));
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
}

void uploadToFirebase(String wind, String status, String suhu, String rh, String api, String gas) {
  if (Firebase.ready()) {
    Firebase.setString(fbdo, "/sensor/wind", wind);
    Firebase.setString(fbdo, "/sensor/status", status);
    Firebase.setString(fbdo, "/sensor/suhu", suhu);
    Firebase.setString(fbdo, "/sensor/rh", rh);
    Firebase.setString(fbdo, "/sensor/api", api);
    Firebase.setString(fbdo, "/sensor/gas", gas);
    Serial.println("Firebase upload done.");
  } else {
    Serial.println("Firebase not ready.");
  }
}

// ---------------------- Update LED & Buzzer ----------------------

void updateLED(int status) {
  normalMode();
  if (status == 1) {
    digitalWrite(LED_HIJAU, HIGH);
    digitalWrite(LED_KUNING, LOW);
    digitalWrite(LED_MERAH, LOW);
  } else if (status == 2) {
    digitalWrite(LED_HIJAU, LOW);
    digitalWrite(LED_KUNING, HIGH);
    digitalWrite(LED_MERAH, LOW);
  } else if (status == 3) {
    digitalWrite(LED_HIJAU, LOW);
    digitalWrite(LED_KUNING, LOW);
    digitalWrite(LED_MERAH, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    allOff();
  }
}

void reconnectLoRa() {
  LoRa.end();
  delay(1000);
  setupLoRa();
}
