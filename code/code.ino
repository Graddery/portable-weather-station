#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>  
#include <ESP8266HTTPClient.h> 
#include <ArduinoJson.h> 
#include <GyverPortal.h>
#include <NTPClient.h>   // Library for getting time from an NTP server
#include <WiFiUdp.h>     // Required for NTPClient
#include <EEPROM.h>

// Wi-Fi credentials
struct Data {
  char ssid[64] = "";        
  char password[64] = "";
  char city[64] = "Yekaterinburg";
};
Data data;

#define SSID_ADDR 0
#define PASSWORD_ADDR 64
#define CITY_ADDR 128

String inp_ssid = data.ssid;
String inp_pass = data.password;
String inp_city = data.city;

// Pins for display
#define TFT_CS     D8  
#define TFT_RST    D4  
#define TFT_DC     D3  

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 160

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_BME280 bme;

// OpenWeatherMap settings
const char* apiKey = "22d58716ed38e1e848505a9612dafdf4";

// GyverPortal instance
GyverPortal portal;

// NTP Client settings
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 5); // Timezone offset for Yekaterinburg (UTC+5)

// Global variables for weather data
float temperature = 0.0, humidity = 0.0, pressure = 0.0;
float lat = 0.0, lon = 0.0;
String weatherData = "";
String weatherForecast = "";
String forecastData[2]; // Массив для хранения прогноза
bool isAPMode = false; // Flag to track if in Access Point mode

void setup() {
  Serial.begin(115200);
  delay(100);
  // Initialize display
  tft.initR(INITR_BLACKTAB);  
  tft.fillScreen(ST77XX_BLACK); 
  Serial.println("Display initialized!");
  // Initialize BME280
  if (!bme.begin(0x76)) {  
    Serial.println("BME280 initialization failed!");
    while (1);
  }
  Serial.println("BME280 ready!");

  // Initialize EEPROM
  EEPROM.begin(512); // Initialize EEPROM with 512 bytes
  // Load saved data from EEPROM
  loadFromEEPROM();
  delay(100);
  
  // Initialize GyverPortal
  portal.attachBuild(buildPortal);
  portal.attach(onSubmit);
  portal.start();

  // Initialize NTP Client
  timeClient.begin();

  // Connect to Wi-Fi or start AP mode
  if (!connectWiFi()) {
    startAPMode();
  }
  getCoordinates(data.city);
  Serial.println("Setup complete!");
}

void loop() {
  portal.tick();
  timeClient.update();

  static unsigned long previousMillis = 0;
  const long interval = 10000;
  if (millis() - previousMillis >= interval) {
    previousMillis = millis();
    updateSensorData();
    weatherData = getWeatherData();
    //weatherForecast = getWeatherForecast(); // Fetch forecast
    getForecastData();
    updateDisplay();
  }
}

// Connect to Wi-Fi
bool connectWiFi() {
  WiFi.begin(data.ssid, data.password);
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(data.ssid);

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 10000; // 10 seconds timeout

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startAttemptTime >= timeout) {
      Serial.println("\nFailed to connect to Wi-Fi.");
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  isAPMode = false;
  return true;
}

// Start Access Point mode
void startAPMode() {
  portal.stop();
  delay(10);
  WiFi.softAP("Station_Setup", "12345678");
  Serial.println("Access Point started.");
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  isAPMode = true;
  portal.start();
}

// Update sensor data
void updateSensorData() {
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  pressure = bme.readPressure() / 100.0F;
  temperature = round(temperature * 10) / 10.0;
  humidity = round(humidity * 10) / 10.0;
}

// Fetch weather data
String getWeatherData() {
  WiFiClient client;
  HTTPClient http;
  String url = String("http://api.openweathermap.org/data/2.5/weather?q=") + data.city + "&appid=" + apiKey + "&units=metric&lang=ru";
  String weatherInfo = "";

  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    float temp = doc["main"]["temp"];
    const char* description = doc["weather"][0]["description"];
    weatherInfo = String(temp) + " C, " + String(description);
  } else {
    weatherInfo = "Ошибка получения данных";
  }
  http.end();
  return weatherInfo;
}

void getForecastData() {
  WiFiClient client;
  HTTPClient http;
  String url = String("http://api.openweathermap.org/data/2.5/forecast?q=") + data.city + "&appid=" + apiKey + "&units=metric&lang=ru";

  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    for (int i = 0; i < 3; i++) {
      float temp = doc["list"][i * 8]["main"]["temp"];
      const char* description = doc["list"][i * 8]["weather"][0]["description"];
      forecastData[i] = String(temp) + " C, " + String(description);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      forecastData[i] = "Ошибка";
    }
  }
  http.end();
}

// Update display
void updateDisplay() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  String currentTime = String(timeClient.getHours()) + ":" + (timeClient.getMinutes() < 10 ? "0" : "") + String(timeClient.getMinutes());
  int textWidth_time = currentTime.length() * 24; // Размер символа * кол-во символов
  int centerX_time = (SCREEN_WIDTH - textWidth_time) / 2;

  time_t rawTime = timeClient.getEpochTime(); // Получение времени в секундах с 1970
  struct tm *timeInfo = localtime(&rawTime);  // Преобразование в формат времени
  String currentDate = String(timeInfo->tm_mday) + "." +
                       String(timeInfo->tm_mon + 1) + "." +
                       String(timeInfo->tm_year + 1900);
  int textWidth_date = currentDate.length() * 6; // Размер символа * кол-во символов
  int centerX_date = (SCREEN_WIDTH - textWidth_date) / 2;

  int dropX = 5;   // Координата X начала капли
  int dropY = 80;  // Координата Y начала капли
  tft.fillTriangle(dropX + 8, dropY, dropX + 12, dropY + 12, dropX, dropY + 12, ST77XX_CYAN); // Нижняя часть капли
  tft.fillCircle(dropX + 8, dropY + 14, 8, ST77XX_CYAN); // Верхняя часть капли

  int thermX = 73;  // Координата X начала термометра
  int thermY = 80;  // Координата Y начала термометра
  
  // Рисуем корпус термометра
  tft.fillRect(thermX, thermY, 10, 17, ST77XX_WHITE); // Прямоугольник корпуса
  tft.fillCircle(thermX + 5, thermY + 17, 7, ST77XX_WHITE); // Круг внизу

  // Рисуем "ртуть" внутри термометра
  tft.fillRect(thermX + 3, thermY + 5, 4, 12, ST77XX_RED); // Прямоугольник ртути
  tft.fillCircle(thermX + 5, thermY + 17, 5, ST77XX_RED); // Круг ртути
  

  if (isAPMode) {
    // Display information in Access Point mode
    tft.setCursor(5, 10);
    tft.print("AP Mode");
    tft.setCursor(5, 30);
    tft.print("SSID: Station_Setup");
    tft.setCursor(5, 45);
    tft.print("IP: ");
    tft.print(WiFi.softAPIP());
    tft.setCursor(5, 60);
    tft.print("Pass: 12345678");
    tft.setTextSize(2);
    tft.setCursor(dropX + 20, dropY + 4);
    //tft.setCursor(5, 80);
    //tft.print(utf8rus("Влажность: "));
    //tft.printf("%.0f %%\n", humidity);
    tft.printf("%.0f%%\n", humidity);
    tft.setCursor(90, dropY + 4);
    //tft.print(utf8rus("Температура: "));
    //tft.printf("%.0f C \n ", temperature);
    tft.printf("%.0f\n", temperature);tft.fillCircle(117, dropY + 4, 2, ST77XX_WHITE); // Рисуем маленький круг (градус)
    tft.setTextSize(1);
    tft.setCursor(5, 110);
    tft.print(utf8rus("Давление: "));
    tft.printf("%.1f hPa\n", pressure);

  } else {
    // Display weather and sensor data in normal mode
    tft.setCursor(10, 5);
    tft.print("IP: ");
    tft.print(WiFi.localIP());
    tft.setCursor(centerX_time, 20);
    tft.setTextSize(4);
    tft.print(currentTime);
    tft.setTextSize(1);
    tft.setCursor(centerX_date, 55);
    tft.print(currentDate);
    tft.setTextSize(2);
    tft.setCursor(dropX + 20, dropY + 4);
    //tft.setCursor(5, 80);
    //tft.print(utf8rus("Влажность: "));
    //tft.printf("%.0f %%\n", humidity);
    tft.printf("%.0f%%\n", humidity);
    tft.setCursor(90, dropY + 4);
    //tft.print(utf8rus("Температура: "));
    //tft.printf("%.0f C \n ", temperature);
    tft.printf("%.0f\n", temperature);tft.fillCircle(117, dropY + 4, 2, ST77XX_WHITE); // Рисуем маленький круг (градус)
    tft.setTextSize(1);
    tft.setCursor(5, 120);
    tft.print(utf8rus("За окном: "));
    tft.print(utf8rus(weatherData));
    tft.setCursor(5, 140);
    tft.print(utf8rus("Давление: "));
    tft.printf("%.1f hPa\n", pressure);
  }
}

// Build GyverPortal interface
void buildPortal() {
  GP.BUILD_BEGIN(GP_DARK);
  GP.TITLE("Погодная станция");
  GP.HR();
  GP.LABEL("Температура:");
  GP.LABEL_BLOCK(String(temperature) + " C");
  GP.BREAK();
  GP.LABEL("Влажность:");
  GP.LABEL_BLOCK(String(humidity) + " %");
  GP.BREAK();
  GP.LABEL("Давление:");
  GP.LABEL_BLOCK(String(pressure) + " hPa");
  GP.BREAK();
  GP.LABEL("За окном:");
  GP.LABEL_BLOCK(weatherData);
  GP.HR();

  GP.TITLE("Прогноз на 2 дня вперед:");
  for (int i = 0; i < 2; i++) {
    GP.LABEL("Прогноз на день " + String(i + 1) + ":");
    GP.LABEL_BLOCK(forecastData[i]);
    GP.BREAK();
  }

  GP.FORM_BEGIN("/wifi_data");
  GP.TITLE("Данные по подключению к вайфай");
  GP.LABEL("SSID:");
  GP.TEXT("ssid", "Wi-Fi SSID", data.ssid);
  GP.BREAK();
  GP.LABEL("Пароль:");
  GP.TEXT("password", "Wi-Fi Password", data.password);
  GP.BREAK();
  GP.LABEL("Город:");
  GP.TEXT("city", "City", data.city);
  GP.SUBMIT("Сохранить");
  GP.FORM_END();
  GP.HR();

  GP.BUTTON("reboot", "Перезапустить");
  GP.BUILD_END();
}

// Handle form submissions
void onSubmit() {

  if (portal.form()){
    if(portal.form("/wifi_data")){

      inp_ssid = portal.getString("ssid");
      inp_ssid.toCharArray(data.ssid, sizeof(data.ssid));
      inp_pass = portal.getString("password");
      inp_pass.toCharArray(data.password, sizeof(data.password));
      inp_city.toCharArray(data.city, sizeof(data.city));

      //password = (portal.getString("password")).toCharArray(ssid, sizeof(ssid));
      Serial.println(data.ssid);
      Serial.println(data.password);
      Serial.println(data.city);

      saveToEEPROM();
    }
  }
 
  if (portal.click()) {

    if (portal.click("reboot")) {
      portal.stop();   // Останавливаем сервер
      delay(100);      // Ждем завершения текущих процессов
      ESP.restart();   // Перезагружаем устройство
    }
  }
}

void saveToEEPROM() {
  for (int i = 0; i < 64; i++) {
    EEPROM.write(SSID_ADDR + i, data.ssid[i]);
    EEPROM.write(PASSWORD_ADDR + i, data.password[i]);
    EEPROM.write(CITY_ADDR + i, data.city[i]);
  }
  EEPROM.commit(); // Save changes to EEPROM
  Serial.println("Сохранено");
}

// Load data from EEPROM
void loadFromEEPROM() {
  for (int i = 0; i < 64; i++) {
    data.ssid[i] = EEPROM.read(SSID_ADDR + i);
    data.password[i] = EEPROM.read(PASSWORD_ADDR + i);
    data.city[i] = EEPROM.read(CITY_ADDR + i);
  }
}

void getCoordinates(String city) {
  WiFiClient client;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + apiKey;
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    lat = doc["coord"]["lat"];
    lon = doc["coord"]["lon"];
    Serial.print("Latitude: ");
    Serial.println(lat);
    Serial.print("Longitude: ");
    Serial.println(lon);
  } else {
    Serial.println("Error getting coordinates.");
  }
  http.end();
}
