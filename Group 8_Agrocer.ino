#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
//  WiFi
// ============================================================
#define WIFI_SSID     "Saharant"
#define WIFI_PASS     "gajahmelingkardikali"

// ============================================================
//  MQTT HiveMQ
// ============================================================
#define MQTT_SERVER   "broker.hivemq.com"
#define MQTT_PORT     1883
#define MQTT_CLIENT   "SmartGreenhouse_Seledri"

#define TOPIC_SUHU        "greenhouse/seledri/suhu"
#define TOPIC_KELEMBABAN  "greenhouse/seledri/kelembaban"
#define TOPIC_SOIL        "greenhouse/seledri/soil"
#define TOPIC_RELAY_SOIL  "greenhouse/seledri/relay/soil"
#define TOPIC_RELAY_SUHU  "greenhouse/seledri/relay/suhu"
#define TOPIC_STATUS      "greenhouse/seledri/status"

// ============================================================
//  PIN — tanpa LED
// ============================================================
#define DHTPIN        4
#define SOIL_PIN      34
#define RELAY_POMPA   26   // relay 1 → pompa dikontrol soil
#define RELAY_KIPAS   27   // relay 2 → kipas dikontrol suhu

// ============================================================
//  THRESHOLD
// ============================================================
#define SUHU_ON       25.0   // kipas nyala jika suhu >= ini
#define SUHU_OFF      15.0   // kipas mati jika suhu <= ini
#define SOIL_KERING   60     // pompa nyala jika persen tanah <= ini
#define SOIL_BASAH    70     // pompa mati jika persen tanah >= ini

const unsigned long INTERVAL_SENSOR = 2000;
const unsigned long INTERVAL_MQTT   = 5000;

DHTesp dht;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

float suhu        = 0;
float kelembaban  = 0;
int   soilRaw     = 0;
bool  relayPompa  = false;
bool  relayKipas  = false;

unsigned long lastRead     = 0;
unsigned long lastMqttSend = 0;

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_POMPA, OUTPUT);
  pinMode(RELAY_KIPAS,  OUTPUT);
  digitalWrite(RELAY_POMPA, HIGH);
  digitalWrite(RELAY_KIPAS,  HIGH);

  dht.setup(DHTPIN, DHTesp::DHT22);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0); lcd.print("Smart Greenhouse");
  lcd.setCursor(2, 1); lcd.print("Starting up...");
  delay(1500);

  koneksiWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  koneksiMQTT();
}

void loop() {
  if (!mqtt.connected()) koneksiMQTT();
  mqtt.loop();

  unsigned long now = millis();

  if (now - lastRead >= INTERVAL_SENSOR) {
    lastRead = now;
    bacaSensor();
    kontrolOtomatis();
    tampilLCD();
    serialLog();
  }

  if (now - lastMqttSend >= INTERVAL_MQTT) {
    lastMqttSend = now;
    kirimMQTT();
  }
}

void koneksiWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi");
  Serial.print("[WiFi] Menghubungkan ke ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int coba = 0;
  while (WiFi.status() != WL_CONNECTED && coba < 20) {
    delay(500);
    Serial.print(".");
    coba++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Terhubung!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\n[WiFi] GAGAL - Mode Offline");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi GAGAL");
    lcd.setCursor(0, 1); lcd.print("Mode Offline");
    delay(2000);
  }
}

void koneksiMQTT() {
  int coba = 0;
  while (!mqtt.connected() && coba < 5) {
    Serial.print("[MQTT] Menghubungkan...");
    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println(" Terhubung!");
      mqtt.publish(TOPIC_STATUS, "online");
    } else {
      Serial.print(" Gagal, rc=");
      Serial.print(mqtt.state());
      Serial.println(" Coba lagi...");
      delay(3000);
      coba++;
    }
  }
}

void kirimMQTT() {
  if (!mqtt.connected()) return;

  int soilPersen = map(soilRaw, 4095, 0, 0, 100);
  soilPersen = constrain(soilPersen, 0, 100);
  char buf[10];

  dtostrf(suhu, 4, 1, buf);
  mqtt.publish(TOPIC_SUHU, buf);

  dtostrf(kelembaban, 4, 1, buf);
  mqtt.publish(TOPIC_KELEMBABAN, buf);

  itoa(soilPersen, buf, 10);
  mqtt.publish(TOPIC_SOIL, buf);

  mqtt.publish(TOPIC_RELAY_SOIL, relayPompa ? "ON" : "OFF");
  mqtt.publish(TOPIC_RELAY_SUHU, relayKipas ? "ON" : "OFF");

  Serial.println("[MQTT] Data terkirim!");
}

void bacaSensor() {
  TempAndHumidity data = dht.getTempAndHumidity();
  if (!isnan(data.temperature)) suhu       = data.temperature;
  if (!isnan(data.humidity))    kelembaban = data.humidity;
  soilRaw = analogRead(SOIL_PIN);
}

void kontrolOtomatis() {
  // Hitung persentase tanah
  int soilPersen = map(soilRaw, 4095, 0, 0, 100);
  soilPersen = constrain(soilPersen, 0, 100);

  // Relay Pompa dikontrol berdasarkan persentase
  if (soilPersen <= SOIL_KERING && !relayPompa) {
    digitalWrite(RELAY_POMPA, LOW);
    relayPompa = true;
    Serial.println("[POMPA] ON  - tanah kering");
  } else if (soilPersen >= SOIL_BASAH && relayPompa) {
    digitalWrite(RELAY_POMPA, HIGH);
    relayPompa = false;
    Serial.println("[POMPA] OFF - tanah basah");
  }

  // Relay Kipas dikontrol suhu DHT22
  if (suhu >= SUHU_ON && !relayKipas) {
    digitalWrite(RELAY_KIPAS, LOW);
    relayKipas = true;
    Serial.println("[KIPAS] ON  - suhu tinggi");
  } else if (suhu <= SUHU_OFF && relayKipas) {
    digitalWrite(RELAY_KIPAS, HIGH);
    relayKipas = false;
    Serial.println("[KIPAS] OFF - suhu normal");
  }
}

void tampilLCD() {
  int soilPersen = map(soilRaw, 4095, 0, 0, 100);
  soilPersen = constrain(soilPersen, 0, 100);

  lcd.clear();

  // Baris 1: T:27.5C H:65%
  lcd.setCursor(0, 0);
  lcd.print("T:"); lcd.print(suhu, 1);
  lcd.print("C H:"); lcd.print((int)kelembaban); lcd.print("%");

  // Baris 2: S:80% P:ON K:OFF
  lcd.setCursor(0, 1);
  lcd.print("S:"); lcd.print(soilPersen);
  lcd.print("% P:"); lcd.print(relayPompa ? "ON " : "OFF");
  lcd.print(" K:"); lcd.print(relayKipas ? "ON" : "OFF");
}

void serialLog() {
  Serial.println("================================");
  Serial.print("Suhu         : "); Serial.print(suhu, 1);       Serial.println(" C");
  Serial.print("Kelembaban   : "); Serial.print(kelembaban, 1); Serial.println(" %");
  Serial.print("Soil ADC     : "); Serial.println(soilRaw);
  Serial.print("Pompa        : "); Serial.println(relayPompa ? "ON" : "OFF");
  Serial.print("Kipas        : "); Serial.println(relayKipas ? "ON" : "OFF");
  Serial.print("MQTT         : "); Serial.println(mqtt.connected() ? "Terhubung" : "Terputus");
}