#include <FastLED.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==================== [KONFIGURACJA] =======================
#define LED_PIN     4
#define TONE_PIN    12 
#define NUM_LEDS    20
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

const int ledcChannel = 0;
const int freq = 50;
const int resolution = 8;

const char* ssid = "xxxx";
const char* password = "xxx";
const char* serverURL = "http://xx.xxx.xxx.xxx:8000/counter.php"; 

int counter = 1;
unsigned long lastRequestTime = 0;
unsigned long lastLogTime = 0; 
const unsigned long requestInterval = 5000; 

void firstScenario();
void secondScenario();
void thirdScenario();

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- START URZĄDZENIA ---");

  // Inicjalizacja LED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  FastLED.clear();
  FastLED.show();

  // Inicjalizacja dźwięku LEDC
  ledcSetup(ledcChannel, freq, resolution);
  ledcAttachPin(TONE_PIN, ledcChannel);

  Serial.print("Łączenie z WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nPołączono z WiFi!");
  Serial.print("Adres IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("!!! Utrata WiFi - ponawianie !!!");
    WiFi.reconnect();
  }

  unsigned long now = millis();

  // --- 1. SPRAWDZANIE SERWERA ---
  if (now - lastRequestTime >= requestInterval) {
    lastRequestTime = now;
    WiFiClient client;
    HTTPClient http;
    
    Serial.print("[HTTP] Wysyłanie zapytania... ");
    
    if (http.begin(client, serverURL)) {
      int httpCode = http.GET();
      Serial.print("Kod: ");
      Serial.println(httpCode);

      if (httpCode == 200) {
        String payload = http.getString();
        payload.trim();
        int val = payload.toInt();
        
        if (val >= 1 && val <= 3) {
          if (counter != val) {
             Serial.println("========================================");
             Serial.print(">>> ZMIANA SCENARIUSZA: ");
             Serial.print(counter);
             Serial.print(" -> ");
             Serial.println(val);
             Serial.println("========================================");
             counter = val;
          }
        } else {
          Serial.print("[HTTP] Otrzymano nieprawidłową wartość: ");
          Serial.println(payload);
        }
      }
      http.end();
    } else {
      Serial.println("[HTTP] Błąd połączenia z serwerem!");
    }
  }

  // --- 2. LOGOWANIE STANU (co 2 sekundy - heartbeat) ---
  if (now - lastLogTime > 2000) {
    lastLogTime = now;
    Serial.print("[STATUS] Aktualnie odtwarzany scenariusz: ");
    Serial.println(counter);
  }

  // --- 3. WYKONYWANIE SCENARIUSZA ---
  switch (counter) {
    case 1: firstScenario();  break;
    case 2: secondScenario(); break;
    case 3: thirdScenario();  break;
  }
}

// ==================== [SCENARIUSZE] =======================

// SCENARIUSZ 1: Spokojny Oddech
void firstScenario() {
  CRGB baseColor = CRGB(4, 61, 102);
  uint8_t breathLevel = beatsin8(12, 0, 255);

  CRGB currentColor = baseColor;
  currentColor.nscale8_video(breathLevel);
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.show();

  ledcWriteTone(ledcChannel, 50); 
  ledcWrite(ledcChannel, breathLevel / 4); 
}

// SCENARIUSZ 2: Niepokojący
void secondScenario() {
  // Kolory
  CRGB bordowy = CRGB(80, 0, 0);
  CRGB czerwony = CRGB(219, 0, 0);

  // --- Uderzenie 1 (LUB) ---
  fill_solid(leds, NUM_LEDS, czerwony);
  FastLED.show();
  ledcWriteTone(ledcChannel, 40); 
  ledcWrite(ledcChannel, 128);   
  delay(150);

  // Przerwa krótka
  fill_solid(leds, NUM_LEDS, bordowy);
  FastLED.show();
  ledcWrite(ledcChannel, 0);     
  delay(150);

  // --- Uderzenie 2 (DUB) ---
  fill_solid(leds, NUM_LEDS, czerwony);
  FastLED.show();
  ledcWriteTone(ledcChannel, 52); 
  ledcWrite(ledcChannel, 128);
  delay(100);

  // Przerwa długa
  fill_solid(leds, NUM_LEDS, bordowy);
  FastLED.show();
  ledcWrite(ledcChannel, 0);
  delay(800); 
}

// SCENARIUSZ 3: Smutny
void thirdScenario() {
  // --- CZĘŚĆ 1: Szloch ---
  for (int i = 0; i < 3; i++) {
    int randomLed = random(0, NUM_LEDS);
    ledcWriteTone(ledcChannel, 800); 
    ledcWrite(ledcChannel, 100);
    fill_solid(leds, NUM_LEDS, CRGB(93, 20, 1));
    leds[randomLed] = CRGB::Gray; 
    FastLED.show();
    delay(70);
    ledcWrite(ledcChannel, 0); 
    FastLED.clear();
    FastLED.show();
    delay(50);
  }

  // --- CZĘŚĆ 2: Wycie ---
  for (int freq = 1000; freq > 400; freq -= 10) {
    int jitter = random(-20, 20);
    ledcWriteTone(ledcChannel, freq + jitter);
    ledcWrite(ledcChannel, 80);

    if (freq % 30 == 0) {
      for(int j = 0; j < NUM_LEDS; j++) {
        leds[j] = CRGB(93, 20, 1);
        if(random(0, 10) > 7) leds[j] = CRGB::Black; 
      }
      FastLED.show();
    }
    delay(15); 
  }

  ledcWrite(ledcChannel, 0);
  fill_solid(leds, NUM_LEDS, CRGB(93, 20, 1));
  FastLED.show();
  delay(1000); 
}