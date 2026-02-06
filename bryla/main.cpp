#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// --- KONFIGURACJA WIFI / MQTT ---
const char* ssid = "xxxx";
const char* password = "xxxx";
const char* mqtt_server = "192.168.0.126";
const char* device_id = "widget_motor_001";
const char* http_server_url = "http://xx.xxx.xxx.xxxx:8000/counter.php";

// --- PINY (ESP32-CAM) ---
#define I2C_SDA 15
#define I2C_SCL 14
#define MOT_AIN1 2
#define MOT_AIN2 13
#define MOT_PWM  4 

// --- USTAWIENIA SILNIKA ---
const int pwmFreq = 5000;
const int pwmChannel = 0;
const int pwmResolution = 8;

// --- OBIEKTY GLOBALNE ---
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_MPU6050 mpu;

// --- ZMIENNE STANU ---
volatile int current_scenario = 1; 
// Zmienna globalna do przekazywania pozycji Z do wątku silnika
volatile float global_az = 0.0; 

unsigned long lastHttpCheck = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastOfflineScenarioChange = 0;
unsigned long lastStatusLog = 0; 
unsigned long lastMqttRetry = 0;

bool mpuFound = false;
bool wifiAvailable = false; 

// --- DEKLARACJE FUNKCJI ---
void setup_wifi();
void reconnect_mqtt();
void motorTask(void * parameter);
void setMotorSpeed(int speed);
void check_http_scenario();

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- START URZĄDZENIA (SILNIK + MPU) ---");
  
  // 1. Inicjalizacja I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // 2. Inicjalizacja MPU6050
  if (!mpu.begin()) {
    Serial.println("[ERROR] Nie znaleziono MPU6050!");
    mpuFound = false;
  } else {
    Serial.println("[INIT] MPU6050 OK!");
    mpuFound = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // 3. Inicjalizacja Silnika
  pinMode(MOT_AIN1, OUTPUT);
  pinMode(MOT_AIN2, OUTPUT);
  ledcSetup(pwmChannel, pwmFreq, pwmResolution);
  ledcAttachPin(MOT_PWM, pwmChannel);

  // 4. WiFi i MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);

  // 5. Uruchomienie zadania silnika (rdzeń 1)
  xTaskCreatePinnedToCore(
    motorTask, "MotorTask", 4096, NULL, 1, NULL, 1
  );
}

void loop() {
  unsigned long now = millis();

  // --- 1. ODCZYT CZUJNIKA (Aktualizacja zmiennej globalnej dla silnika) ---
  if (mpuFound) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    // Aktualizuj globalną zmienną dla wątku silnika
    // Normalnie Z ~ 9.8. Do góry nogami Z ~ -9.8.
    global_az = a.acceleration.z; 
  }

  // --- 2. SPRAWDZANIE WIFI ---
  if (WiFi.status() != WL_CONNECTED) {
    wifiAvailable = false;
    if (now - lastWifiRetry > 10000) {
      lastWifiRetry = now;
      Serial.println("[WIFI] Utracono połączenie. Próba nawiązania...");
      WiFi.reconnect();
    }
  } else {
    if (!wifiAvailable) {
      Serial.println("[WIFI] Połączono ponownie!");
      wifiAvailable = true;
    }
  }

  // --- 3. LOGIKA STEROWANIA ---
  if (wifiAvailable) {
    // === TRYB ONLINE ===
    
    // A. HTTP (Priorytet sterowania)
    if (now - lastHttpCheck > 2000) { 
      lastHttpCheck = now;
      check_http_scenario();
    }

    // B. MQTT (Raportowanie)
    if (!client.connected()) {
      if (now - lastMqttRetry > 5000) {
        lastMqttRetry = now;
        reconnect_mqtt(); 
      }
    } else {
      client.loop();
      if (now - lastMqttPublish > 100) {
        lastMqttPublish = now;
        
        float ax = 0, ay = 0, roll = 0, pitch = 0;
        if (mpuFound) {
          sensors_event_t a, g, temp;
          mpu.getEvent(&a, &g, &temp);
          ax = a.acceleration.x; ay = a.acceleration.y;
          // Obliczanie kątów
          roll = atan2(ay, global_az) * 180.0 / PI;
          pitch = atan2(-ax, sqrt(ay * ay + global_az * global_az)) * 180.0 / PI;
        }

        char msg[250];
        snprintf(msg, sizeof(msg), 
          "{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"roll\":%.1f,\"pitch\":%.1f,\"scen\":%d}",
          ax, ay, global_az, roll, pitch, current_scenario
        );
        client.publish(String(String(device_id) + "/sensor").c_str(), msg);
      }
    }

  } else {
    // === TRYB OFFLINE ===
    if (now - lastOfflineScenarioChange > 15000) {
      lastOfflineScenarioChange = now;
      current_scenario++;
      if (current_scenario > 3) current_scenario = 1;
      Serial.printf("[OFFLINE] Auto-zmiana scenariusza na: %d\n", current_scenario);
    }
  }

  // --- 4. STATUS LOG ---
  if (now - lastStatusLog > 5000) {
    lastStatusLog = now;
    Serial.printf("[STATUS] Scen: %d | Az: %.2f | WiFi: %s | MQTT: %s\n", 
      current_scenario,
      global_az,
      wifiAvailable ? "OK" : "BRAK",
      client.connected() ? "OK" : "BRAK");
  }
}

// --- FUNKCJE POMOCNICZE ---

void check_http_scenario() {
  HTTPClient http;
  http.begin(http_server_url);
  http.setTimeout(1000); 
  
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();
    int val = payload.toInt();
    
    if (val >= 1 && val <= 3) {
      if(current_scenario != val) {
          Serial.printf(">>> [HTTP] Zmiana scenariusza: %d -> %d\n", current_scenario, val);
          current_scenario = val;
      }
    }
  }
  http.end();
}

void setMotorSpeed(int speed) {
  // Zabezpieczenie zakresu PWM
  if (speed > 255) speed = 255;
  if (speed < 0) speed = 0;

  if (speed > 0) {
    digitalWrite(MOT_AIN1, HIGH);
    digitalWrite(MOT_AIN2, LOW);
  } else {
    digitalWrite(MOT_AIN1, LOW);
    digitalWrite(MOT_AIN2, LOW);
    speed = 0;
  }
  ledcWrite(pwmChannel, speed);
}

void motorTask(void * parameter) {
  for(;;) {
    int mode = current_scenario; 

    // --- SCENARIUSZ 1: SPOKOJNY ODDECH ---
    // Płynne rozpędzanie i zwalnianie silnika
    if (mode == 1) {
      // Wdech (narastanie)
      for (int i = 40; i < 180; i+=2) { 
        setMotorSpeed(i);
        vTaskDelay(25 / portTICK_PERIOD_MS); // Wolniej = głębszy oddech
        if (current_scenario != 1) break;
      }
      // Wydech (opadanie)
      for (int i = 180; i > 40; i-=2) {
        setMotorSpeed(i);
        vTaskDelay(25 / portTICK_PERIOD_MS);
        if (current_scenario != 1) break;
      }
      vTaskDelay(200 / portTICK_PERIOD_MS); // Krótka pauza po wydechu
    } 
    
    // --- SCENARIUSZ 2: NIEPOKOJĄCE BICIE SERCA ---
    // LUB -> DUB -> Przerwa
    else if (mode == 2) {
      // Uderzenie 1 (LUB)
      setMotorSpeed(255);
      vTaskDelay(150 / portTICK_PERIOD_MS);
      
      // Przerwa krótka
      setMotorSpeed(0);
      vTaskDelay(150 / portTICK_PERIOD_MS);
      
      // Uderzenie 2 (DUB)
      setMotorSpeed(200);
      vTaskDelay(150 / portTICK_PERIOD_MS);
      
      // Przerwa długa 
      setMotorSpeed(0);
      vTaskDelay(800 / portTICK_PERIOD_MS);
    } 
    
    // --- SCENARIUSZ 3: SMUTEK (SZLOCH) + WARUNEK ODWRÓCENIA ---
    else if (mode == 3) {
        if (global_az < -5.0) {
            Serial.println(">>> [SCENARIUSZ 3] Wykryto odwrócenie! Uspokajanie...");
            setMotorSpeed(0);
            vTaskDelay(200 / portTICK_PERIOD_MS); 
            continue; // Pomiń resztę pętli, sprawdź znowu
        }

        // Losowe drgania
        int pulses = random(2, 5); 
        for(int k=0; k<pulses; k++) {
            int intensity = random(120, 220);
            setMotorSpeed(intensity);
            vTaskDelay(random(50, 150) / portTICK_PERIOD_MS); 
            
            setMotorSpeed(0);
            vTaskDelay(random(50, 100) / portTICK_PERIOD_MS); 
            
            if (current_scenario != 3) break;
        }
        
        // Dłuższa przerwa
        vTaskDelay(random(500, 1000) / portTICK_PERIOD_MS);
    }
    else {
        // Safe mode
        setMotorSpeed(0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

void setup_wifi() {
  Serial.print("Laczenie z WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Polaczono!");
    wifiAvailable = true;
  } else {
    Serial.println("\nBrak WiFi! Start w trybie OFFLINE.");
    wifiAvailable = false;
  }
}

void reconnect_mqtt() {
  if (client.connect(device_id)) {
    Serial.println("[MQTT] Polaczono!");
  }
}