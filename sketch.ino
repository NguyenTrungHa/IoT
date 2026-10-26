
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================== CAU HINH CHAN ====================
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define POT_PIN       34
#define BUTTON_PIN    16

#define OLED_SDA      21
#define OLED_SCL      22
#define OLED_ADDR     0x3C
#define OLED_WIDTH    128
#define OLED_HEIGHT   64

// ==================== CAU HINH DO ====================
const unsigned long SAMPLE_INTERVAL_MS = 2000;
const unsigned long DEBOUNCE_MS = 40;
const int WINDOW_SIZE = 5;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(
  OLED_WIDTH, OLED_HEIGHT, &Wire, -1
);

// Lich su 5 mau hop le gan nhat
float tempHistory[WINDOW_SIZE];
float humHistory[WINDOW_SIZE];

int historyIndex = 0;
int historyCount = 0;

unsigned long startTime = 0;
unsigned long lastSampleTime = 0;
unsigned long lastButtonChangeTime = 0;
unsigned long lastDebounceTime = 0;

bool previousRawButton = HIGH;
bool stableButton = HIGH;
bool delayMode = false;
bool oledAvailable = false;

unsigned long sampleNumber = 0;

// ==================== QUET I2C ====================
void scanI2C() {
  Serial.println();
  Serial.println(F("=== I2C SCAN ==="));

  int found = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("Tim thay I2C: 0x"));
      if (address < 16) Serial.print('0');
      Serial.println(address, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println(F("Khong tim thay thiet bi I2C"));
  }

  Serial.println(F("=== END I2C SCAN ==="));
  Serial.println();
}

// ==================== NUT NHAN ====================
void handleButton() {
  unsigned long now = millis();
  bool rawButton = digitalRead(BUTTON_PIN);

  if (rawButton != previousRawButton) {
    lastDebounceTime = now;
    previousRawButton = rawButton;
  }

  if (now - lastDebounceTime >= DEBOUNCE_MS) {
    if (rawButton != stableButton) {
      stableButton = rawButton;

      // Nhấn nút: chuyển giữa millis() và delay()
      if (stableButton == LOW) {
        delayMode = !delayMode;

        Serial.print(F("# MODE,"));
        Serial.println(delayMode ? F("delay") : F("millis"));

        if (oledAvailable) {
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(0, 0);
          display.println(F("Che do moi:"));
          display.println(delayMode ? F("delay()") : F("millis()"));
          display.display();
        }
      }
    }
  }
}

// ==================== TINH TRUNG BINH TRUOT 5 MAU ====================
float movingAverage(const float values[], int count) {
  if (count == 0) {
    return NAN;
  }

  float sum = 0.0;

  for (int i = 0; i < count; i++) {
    sum += values[i];
  }

  return sum / count;
}

void addValidSample(float temperature, float humidity) {
  tempHistory[historyIndex] = temperature;
  humHistory[historyIndex] = humidity;

  historyIndex = (historyIndex + 1) % WINDOW_SIZE;

  if (historyCount < WINDOW_SIZE) {
    historyCount++;
  }
}

// ==================== HIEN THI OLED ====================
void updateOLED(
  float temperature,
  float humidity,
  float avgTemp,
  float avgHum,
  int adcValue,
  bool valid
) {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.println(delayMode ? F("Mode: delay()") : F("Mode: millis()"));

  if (valid) {
    display.print(F("T: "));
    display.print(temperature, 1);
    display.println(F(" C"));

    display.print(F("H: "));
    display.print(humidity, 1);
    display.println(F(" %"));
  } else {
    display.println(F("DHT22: LOI DOC"));
  }

  display.print(F("MA5 T: "));
  if (isnan(avgTemp)) display.println(F("--"));
  else {
    display.print(avgTemp, 1);
    display.println(F(" C"));
  }

  display.print(F("MA5 H: "));
  if (isnan(avgHum)) display.println(F("--"));
  else {
    display.print(avgHum, 1);
    display.println(F(" %"));
  }

  display.print(F("ADC: "));
  display.println(adcValue);

  display.print(F("Mau hop le: "));
  display.print(historyCount);
  display.print('/');
  display.println(WINDOW_SIZE);

  display.display();
}

// ==================== DOC VA IN DU LIEU ====================
void takeSample() {
  unsigned long now = millis();
  unsigned long elapsed = now - startTime;
  unsigned long intervalActual = (sampleNumber == 0)
    ? 0
    : now - lastSampleTime;

  lastSampleTime = now;
  sampleNumber++;

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int adcValue = analogRead(POT_PIN);

  bool valid = !isnan(temperature) &&
               !isnan(humidity) &&
               temperature >= -40.0 &&
               temperature <= 80.0 &&
               humidity >= 0.0 &&
               humidity <= 100.0;

  String errorReason = "";

  if (isnan(temperature) && isnan(humidity)) {
    errorReason = "DHT_READ_FAILED";
  } else if (isnan(temperature)) {
    errorReason = "TEMP_READ_FAILED";
  } else if (isnan(humidity)) {
    errorReason = "HUMIDITY_READ_FAILED";
  } else if (temperature < -40.0 || temperature > 80.0) {
    errorReason = "TEMP_OUT_OF_RANGE";
  } else if (humidity < 0.0 || humidity > 100.0) {
    errorReason = "HUMIDITY_OUT_OF_RANGE";
  }

  if (valid) {
    addValidSample(temperature, humidity);
  }

  // Tinh trung binh cua toi da 5 mau hop le gan nhat.
  // Khi chua du 5 mau, tinh trung binh cac mau hop le hien co.
  float avgTemp = movingAverage(tempHistory, historyCount);
  float avgHum = movingAverage(humHistory, historyCount);

  updateOLED(
    temperature, humidity, avgTemp, avgHum, adcValue, valid
  );

  // Mot dong CSV cho moi lan do.
  // Sao chep phan CSV tu Serial Monitor de luu thu cong.
  Serial.print(elapsed);
  Serial.print(',');
  Serial.print(sampleNumber);
  Serial.print(',');

  if (!isnan(temperature)) Serial.print(temperature, 2);
  Serial.print(',');

  if (!isnan(humidity)) Serial.print(humidity, 2);
  Serial.print(',');

  Serial.print(valid ? 1 : 0);
  Serial.print(',');
  Serial.print(errorReason);
  Serial.print(',');
  Serial.print(adcValue);
  Serial.print(',');

  if (!isnan(avgTemp)) Serial.print(avgTemp, 2);
  Serial.print(',');

  if (!isnan(avgHum)) Serial.print(avgHum, 2);
  Serial.print(',');

  Serial.print(intervalActual);
  Serial.print(',');
  Serial.println(delayMode ? F("delay") : F("millis"));
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  Wire.begin(OLED_SDA, OLED_SCL);
  dht.begin();

  oledAvailable = display.begin(
    SSD1306_SWITCHCAPVCC, OLED_ADDR
  );

  if (oledAvailable) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("TH02 - ESP32"));
    display.println(F("DHT22 + OLED"));
    display.println(F("Moving Average 5"));
    display.display();
  } else {
    Serial.println(F("# WARNING,OLED_NOT_FOUND"));
  }

  scanI2C();

  Serial.println(F("# TH02 - MANUAL CSV EXPORT"));
  Serial.println(F("# Khong tu dong luu file"));
  Serial.println(F("# Dung nut GPIO16 de doi che do"));
  Serial.println(F("# CSV_HEADER"));
  Serial.println(
    F("time_ms,sample,temp_c,humidity_pct,valid,error_reason,"
      "adc,avg5_temp_c,avg5_humidity_pct,interval_actual_ms,mode")
  );

  startTime = millis();
  lastSampleTime = startTime - SAMPLE_INTERVAL_MS;
  lastSampleTime = startTime;
}

// ==================== LOOP ====================
void loop() {
  handleButton();

  if (delayMode) {
    // Che do delay(): co the bo lo mot so thao tac trong luc cho.
    // Goi handleButton() trong cac khoang nho de nut nhan van hoat dong.
    static bool sampledThisCycle = false;

    if (!sampledThisCycle) {
      takeSample();
      sampledThisCycle = true;
    }

    static unsigned long delayCycleStart = 0;
    if (delayCycleStart == 0) {
      delayCycleStart = millis();
    }

    if (millis() - delayCycleStart >= SAMPLE_INTERVAL_MS) {
      delayCycleStart = millis();
      sampledThisCycle = false;
    }

    delay(5);
  } else {
    // Che do millis(): khong dung delay dai de lap lich lay mau.
    unsigned long now = millis();

    if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      takeSample();
    }
  }
}

