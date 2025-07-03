#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define DHTPIN D5
#define DHTTYPE DHT22
#define BUTTON_PIN D3
#define BUTTON_PIN2 D4
#define BUZZER_PIN D7  // Buzzer conectado al pin D7

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BMP280 bmp;
DHT dht(DHTPIN, DHTTYPE);

const int mq5Pin = A0;
int pantallaActual = 0;
const int totalPantallas = 6;

const char* ssid = "A55";
const char* password = "asd123!@#";

ESP8266WebServer server(80);
WiFiClientSecure client;

float temperaturaLocal = 0.0;
float humedadLocal = 0.0;
float presionLocal = 0.0;
int mq5Value = 0;

float temperaturaClima = 0.0;
String viento = "--";
float lluviaMM = -1.0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -10800, 60000); // GMT-3 (Argentina)
String horaWeb = "--";

unsigned long tiempoUltimaConsulta = 0;

void setup() {
  Wire.begin(D2, D1);
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // Inicialmente apagado

  dht.begin();
  if (!bmp.begin(0x76)) {
    lcd.print("Error BMP280");
    while (1);
  }

  lcd.print("Sensores listos");
  delay(2000);
  lcd.clear();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Servidor web iniciado");

  client.setInsecure();
  timeClient.begin();
}

void loop() {
  server.handleClient();

  if (digitalRead(BUTTON_PIN) == LOW) {
    lcd.clear();
    delay(100);
  }

  if (digitalRead(BUTTON_PIN2) == LOW) {
    pantallaActual = (pantallaActual + 1) % totalPantallas;
    lcd.clear();
    delay(300);
  }

  temperaturaLocal = dht.readTemperature();
  humedadLocal = dht.readHumidity();
  presionLocal = bmp.readPressure() / 100.0F;
  mq5Value = analogRead(mq5Pin);

  // Activar buzzer si nivel de gas es alto
  if (mq5Value > 200) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  timeClient.update();
  horaWeb = timeClient.getFormattedTime();

  if (millis() - tiempoUltimaConsulta > 60000) {
    obtenerClimaActual();
    obtenerLluvia();
    tiempoUltimaConsulta = millis();
  }

  switch (pantallaActual) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Tloc:"); lcd.print(temperaturaLocal, 1); lcd.print("C H:"); lcd.print(humedadLocal, 0); lcd.print("%");
      lcd.setCursor(0, 1);
      lcd.print("MQ5:"); lcd.print(mq5Value); lcd.print(" P:"); lcd.print(presionLocal, 0);
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Bmp280:");
      lcd.setCursor(0, 1);
      lcd.print(temperaturaLocal, 1); lcd.print("C "); lcd.print(presionLocal, 0); lcd.print("hPa");
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("MQ5 Raw: "); lcd.print(mq5Value);
      lcd.setCursor(0, 1);
      lcd.print(mq5Value > 600 ? "ALERTA GAS!" : "Nivel Normal");
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("T clima:"); lcd.print(temperaturaClima, 1); lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Viento: "); lcd.print(viento);
      break;
    case 4:
      lcd.setCursor(0, 0);
      lcd.print("Lluvia mm:");
      lcd.setCursor(0, 1);
      if (lluviaMM >= 0) lcd.print(lluviaMM, 2);
      else lcd.print("--");
      break;
    case 5:
      lcd.setCursor(0, 0);
      lcd.print("Hora local:");
      lcd.setCursor(0, 1);
      lcd.print(horaWeb);
      break;
  }

  delay(200);
}

void obtenerClimaActual() {
  const char* host = "api.open-meteo.com";
  const int httpsPort = 443;
  String url = "/v1/forecast?latitude=-34.61&longitude=-58.38&current_weather=true";

  if (!client.connect(host, httpsPort)) {
    Serial.println("Error conexion clima");
    return;
  }

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String payload = "";
  while (client.available()) {
    payload += client.readStringUntil('\n');
  }

  Serial.println("Clima current_weather:");
  Serial.println(payload);

  int pos = payload.indexOf("\"current_weather\":");
  if (pos == -1) return;

  int start = payload.indexOf("{", pos);
  int end = payload.indexOf("}", start);
  String current = payload.substring(start + 1, end);

  int tpos = current.indexOf("\"temperature\":");
  if (tpos != -1) {
    int tstart = current.indexOf(":", tpos) + 1;
    int tend = current.indexOf(",", tstart);
    temperaturaClima = current.substring(tstart, tend).toFloat();
  }

  int wpos = current.indexOf("\"windspeed\":");
  if (wpos != -1) {
    int wstart = current.indexOf(":", wpos) + 1;
    int wend = current.indexOf(",", wstart);
    if (wend == -1) wend = current.length();
    viento = current.substring(wstart, wend) + " km/h";
  }
}

void obtenerLluvia() {
  const char* host = "api.open-meteo.com";
  const int httpsPort = 443;
  String url = "/v1/forecast?latitude=-34.61&longitude=-58.38&hourly=precipitation&timezone=America/Argentina/Buenos_Aires";

  if (!client.connect(host, httpsPort)) {
    Serial.println("Error conexion lluvia");
    return;
  }

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String payload = "";
  while (client.available()) {
    payload += client.readStringUntil('\n');
  }

  Serial.println("Precipitacion:");
  Serial.println(payload);

  int pos = payload.indexOf("\"precipitation\":");
  if (pos == -1) {
    lluviaMM = -1;
    return;
  }

  int startArray = payload.indexOf("[", pos);
  int endArray = payload.indexOf("]", startArray);
  if (startArray == -1 || endArray == -1) {
    lluviaMM = -1;
    return;
  }

  String arrayStr = payload.substring(startArray + 1, endArray);
  int lastComma = arrayStr.lastIndexOf(",");
  String lastValue = (lastComma == -1) ? arrayStr : arrayStr.substring(lastComma + 1);

  lluviaMM = lastValue.toFloat();
}

void handleData() {
  String json = "{\"temperatura_local\":";
  json += String(temperaturaLocal);
  json += ",\"humedad_local\":";
  json += String(humedadLocal);
  json += ",\"presion_local\":";
  json += String(presionLocal);
  json += ",\"mq5\":";
  json += String(mq5Value);
  json += ",\"temperatura_clima\":";
  json += String(temperaturaClima);
  json += ",\"lluvia_mm\":";
  json += String(lluviaMM);
  json += ",\"viento\":\"" + viento + "\"";
  json += ",\"hora\":\"" + horaWeb + "\"}";

  Serial.println("JSON enviado:");
  Serial.println(json);
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Monitor IoT</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
  body { font-family: 'Segoe UI', sans-serif; background: #121212; color: #eee; padding: 20px; }
  header { text-align: center; margin-bottom: 30px; }
  .contenedor {
    display: grid;
    gap: 20px;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  }
  .tarjeta {
    background: #1e1e1e;
    padding: 15px;
    border-radius: 12px;
    text-align: center;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  }
  .tarjeta span {
    font-size: 1.8em;
    color: #00BFFF;
  }
  canvas { background: #fff; border-radius: 10px; padding: 10px; margin-top: 20px; }
</style>
</head>
<body>
<header><h1>🌡️ Monitor IoT</h1></header>
<div class="contenedor">
  <div class="tarjeta">🌞 Temp local: <span id="temp_local">--</span> °C</div>
  <div class="tarjeta">💧 Humedad local: <span id="hum_local">--</span> %</div>
  <div class="tarjeta">🌬️ Presión local: <span id="pres_local">--</span> hPa</div>
  <div class="tarjeta">🔥 MQ5: <span id="mq5">--</span></div>
  <div class="tarjeta">🌞 Temp clima: <span id="temp_clima">--</span> °C</div>
  <div class="tarjeta">🌧️ Lluvia mm: <span id="lluvia">--</span></div>
  <div class="tarjeta">💨 Viento: <span id="viento">--</span></div>
  <div class="tarjeta">🕒 Hora local: <span id="hora">--</span></div>
</div>

<canvas id="grafTemp"></canvas>
<canvas id="grafHum"></canvas>
<canvas id="grafPres"></canvas>

<script>
  const chartTemp = new Chart(document.getElementById('grafTemp'), {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Temp local °C', data: [], borderColor: 'red', fill: false }] },
    options: { responsive: true }
  });
  const chartHum = new Chart(document.getElementById('grafHum'), {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Humedad local %', data: [], borderColor: 'blue', fill: false }] },
    options: { responsive: true }
  });
  const chartPres = new Chart(document.getElementById('grafPres'), {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Presión local hPa', data: [], borderColor: 'green', fill: false }] },
    options: { responsive: true }
  });

  function actualizarDatos() {
    fetch("/data").then(r => r.json()).then(data => {
      let t = new Date().toLocaleTimeString();
      document.getElementById("temp_local").textContent = data.temperatura_local.toFixed(1);
      document.getElementById("hum_local").textContent = data.humedad_local.toFixed(1);
      document.getElementById("pres_local").textContent = data.presion_local.toFixed(1);
      document.getElementById("mq5").textContent = data.mq5;
      document.getElementById("temp_clima").textContent = data.temperatura_clima.toFixed(1);
      document.getElementById("lluvia").textContent = data.lluvia_mm >= 0 ? data.lluvia_mm.toFixed(2) : "--";
      document.getElementById("viento").textContent = data.viento;
      document.getElementById("hora").textContent = data.hora;

      if (chartTemp.data.labels.length > 20) {
        chartTemp.data.labels.shift(); chartTemp.data.datasets[0].data.shift();
        chartHum.data.labels.shift(); chartHum.data.datasets[0].data.shift();
        chartPres.data.labels.shift(); chartPres.data.datasets[0].data.shift();
      }

      chartTemp.data.labels.push(t);
      chartTemp.data.datasets[0].data.push(data.temperatura_local);
      chartHum.data.labels.push(t);
      chartHum.data.datasets[0].data.push(data.humedad_local);
      chartPres.data.labels.push(t);
      chartPres.data.datasets[0].data.push(data.presion_local);

      chartTemp.update(); chartHum.update(); chartPres.update();
    });
  }

  setInterval(actualizarDatos, 5000);
  actualizarDatos();
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

