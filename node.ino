#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

// ===== Pin assignment =====
#define GAS_SENSOR_PIN    A3   // MQ2 analog
#define API_SENSOR_PIN    A5   // Flame analog
#define WIND_SENSOR_PIN   A0

#define DHT_PIN           8
#define DHT_TYPE          DHT11

#define LORA_SS           10
#define LORA_RST          9
#define LORA_DIO0         2

DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastSendTime = 0;
unsigned long interval = 5000; // kirim data tiap 10 detik
int baselineGas = 0; // Nilai MQ2 di udara bersih

void setup() {
  Serial.begin(9600);
  dht.begin();

  // ===== Kalibrasi Gas Sensor (MQ2) =====
  Serial.println("Kalibrasi sensor gas (MQ2)...");
  int total = 0;
  const int sampleCount = 100;
  for (int i = 0; i < sampleCount; i++) {
    total += analogRead(GAS_SENSOR_PIN);
    delay(50);
  }
  baselineGas = total / sampleCount;
  Serial.print("Baseline MQ2 (udara bersih): ");
  Serial.println(baselineGas);

  // ===== Setup LoRa =====
  Serial.println("Starting LoRa...");
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    while (true);
  }
  Serial.println("LoRa init OK!");
}

void loop() {
  if (millis() - lastSendTime > interval) {
    lastSendTime = millis();

    int gasValue = analogRead(GAS_SENSOR_PIN);
    int apiValue = analogRead(API_SENSOR_PIN);

    int windRaw = analogRead(WIND_SENSOR_PIN);
    int windDegree = map(windRaw, 0, 1023, 0, 360);
    if (windDegree >= 360) windDegree = 0;
    String arahAngin = getWindDirection(windDegree);

    float suhu = dht.readTemperature();
    float kelembaban = dht.readHumidity();

    if (isnan(suhu) || isnan(kelembaban)) {
      Serial.println("Gagal baca sensor DHT!");
      return;
    }

    int status = tentukanStatus(apiValue, gasValue, suhu, kelembaban);

    String dataToSend = arahAngin + "|" +
                        String(status) + "|" +
                        String((int)suhu) + "|" +
                        String((int)kelembaban) + "|" +
                        String(apiValue) + "|" +
                        String(gasValue);

    LoRa.beginPacket();
    LoRa.print(dataToSend);
    LoRa.endPacket();

    Serial.print("Wind Raw: ");
    Serial.print(windRaw);
    Serial.print(" -> Arah: ");
    Serial.println(arahAngin);

    Serial.println("Data Dikirim: " + dataToSend);
  }
}

// ===== Fuzzy Logic Status Penilaian =====
int tentukanStatus(int api, int asap, float suhu, float kelembaban) {
  // ===== Kategori Suhu =====
  String kategoriSuhu = "Normal";
  if (suhu <= 25) kategoriSuhu = "Rendah";
  else if (suhu <= 30) kategoriSuhu = "Sedang";
  else kategoriSuhu = "Tinggi";

  // ===== Kategori Kelembaban =====
  String kategoriKelembaban = "Normal";
  if (kelembaban <= 75) kategoriKelembaban = "Rendah";
  else if (kelembaban <= 80) kategoriKelembaban = "Sedang";
  else kategoriKelembaban = "Tinggi";

  // ===== Kategori API =====
  String kategoriAPI = "Rendah";
  if (api >= 701) kategoriAPI = "Rendah";
  else if (api >= 401) kategoriAPI = "Sedang";
  else if (api >= 1) kategoriAPI = "Tinggi";

  // ===== Kategori Asap (MQ2) =====
  String kategoriAsap = "Rendah";
  int deltaAsap = asap - baselineGas;
  if (deltaAsap < 0) deltaAsap = 0;

  if (deltaAsap >= 701) kategoriAsap = "Tinggi";
  else if (deltaAsap >= 401) kategoriAsap = "Sedang";
  else if (deltaAsap >= 1) kategoriAsap = "Rendah";

  // ===== Debug Monitor =====
  Serial.println("Kategori:");
  Serial.println("Suhu: " + kategoriSuhu);
  Serial.println("Kelembaban: " + kategoriKelembaban);
  Serial.println("API: " + kategoriAPI);
  Serial.println("Asap: " + kategoriAsap);
  Serial.print("Delta Asap: ");
  Serial.println(deltaAsap);

  // ===== Aturan Fuzzy Logic =====

  // 1) Api atau Asap Tinggi ➜ Bahaya
  if (kategoriAPI == "Tinggi" || kategoriAsap == "Tinggi") {
    return 3;
  }

  // 2) Api Sedang + Asap Sedang + Kelembaban tidak Tinggi ➜ Bahaya
  if (kategoriAPI == "Sedang" && kategoriAsap == "Sedang" && kategoriKelembaban != "Tinggi") {
    return 3;
  }

  // 3) Asap Tinggi/Sedang + Kelembaban Tinggi ➜ Aman
  if ((kategoriAsap == "Tinggi" || kategoriAsap == "Sedang") && kategoriKelembaban == "Tinggi") {
    return 1;
  }

  // 4) Suhu Tinggi + Api Rendah + Asap Rendah ➜ Waspada
  if (kategoriSuhu == "Tinggi" && kategoriAPI == "Rendah" && kategoriAsap == "Rendah") {
    return 2;
  }

  // 5) Suhu Tinggi + Kelembaban Rendah ➜ Waspada
  if (kategoriSuhu == "Tinggi" && kategoriKelembaban == "Rendah") {
    return 2;
  }

  // Default ➜ Aman
  return 1;
}

// ===== Fungsi Konversi Derajat ke Arah Mata Angin =====
String getWindDirection(int degree) {
  if (degree < 23 || degree >= 338) return "Utara";
  else if (degree < 68) return "Timur Laut";
  else if (degree < 113) return "Timur";
  else if (degree < 158) return "Tenggara";
  else if (degree < 203) return "Selatan";
  else if (degree < 248) return "Barat Daya";
  else if (degree < 293) return "Barat";
  else return "Barat Laut";
}
