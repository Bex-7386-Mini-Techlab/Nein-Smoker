#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <MQ135.h>

// =====================================================
// Nein-Smoker v2
// ESP8266 Generic Module
// - AP mode dashboard
// - Corrected PPM
// - Gauge percentage 1-100%
// - Status color by condition
// - LEDs + buzzer outputs
// =====================================================

// -------- Pins --------
#define PIN_MQ135    A0
#define PIN_BUZZER   13   // GPIO13
#define PIN_WARNLED  14   // GPIO14
#define PIN_DEADLED  15   // GPIO15
#define PIN_SAFELED  12   // GPIO12

// -------- WiFi AP --------
const char* AP_SSID = "Nein-Smoker";
const char* AP_PASS = "nein1234";   // must be at least 8 chars

ESP8266WebServer server(80);
MQ135 mq135_sensor(PIN_MQ135);

// -------- Environmental correction --------
// Replace with DHT22 later if you want better calibration
float temperature = 21.0;
float humidity    = 81.0;

// -------- Thresholds --------
const float SAFE_LIMIT    = 350000.0;
const float WARNING_LIMIT = 550000.0;

// -------- Timing --------
const unsigned long READ_INTERVAL_MS = 500;
const unsigned long WARN_BLINK_MS     = 700;
const unsigned long DEAD_BLINK_MS     = 150;
const unsigned long BUZZER_BEEP_MS    = 200;

// -------- State --------
unsigned long lastReadTime  = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastBeepTime  = 0;

bool blinkState = false;
bool beepState  = false;

float correctedPPM = 0.0;
int ppmPercent = 0;

enum SmokeState {
  SAFE_STATE,
  WARNING_STATE,
  DEATH_STATE
};

SmokeState currentState = SAFE_STATE;

// =====================================================
// Helpers
// =====================================================
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  if (in_max - in_min == 0.0) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

String getStatusText(SmokeState state) {
  switch (state) {
    case SAFE_STATE:    return "SAFE";
    case WARNING_STATE: return "WARNING";
    case DEATH_STATE:   return "DEATH";
    default:            return "UNKNOWN";
  }
}

String getStatusColor(SmokeState state) {
  switch (state) {
    case SAFE_STATE:    return "#22c55e"; // green
    case WARNING_STATE: return "#facc15"; // yellow
    case DEATH_STATE:   return "#ef4444"; // red
    default:            return "#94a3b8"; // gray
  }
}

String getStatusBg(SmokeState state) {
  switch (state) {
    case SAFE_STATE:    return "#071a10";
    case WARNING_STATE: return "#1d1805";
    case DEATH_STATE:   return "#220707";
    default:            return "#0f172a";
  }
}

void allOff() {
  digitalWrite(PIN_SAFELED, LOW);
  digitalWrite(PIN_WARNLED, LOW);
  digitalWrite(PIN_DEADLED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

void applyStateOutputs(SmokeState state, unsigned long now) {
  switch (state) {
    case SAFE_STATE:
      digitalWrite(PIN_SAFELED, HIGH);
      digitalWrite(PIN_WARNLED, LOW);
      digitalWrite(PIN_DEADLED, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      blinkState = false;
      beepState  = false;
      break;

    case WARNING_STATE:
      digitalWrite(PIN_SAFELED, LOW);
      digitalWrite(PIN_DEADLED, LOW);

      if (now - lastBlinkTime >= WARN_BLINK_MS) {
        lastBlinkTime = now;
        blinkState = !blinkState;
        digitalWrite(PIN_WARNLED, blinkState ? HIGH : LOW);
      }

      if (now - lastBeepTime >= BUZZER_BEEP_MS) {
        lastBeepTime = now;
        beepState = !beepState;
        digitalWrite(PIN_BUZZER, beepState ? HIGH : LOW);
      }
      break;

    case DEATH_STATE:
      digitalWrite(PIN_SAFELED, LOW);
      digitalWrite(PIN_WARNLED, LOW);

      if (now - lastBlinkTime >= DEAD_BLINK_MS) {
        lastBlinkTime = now;
        blinkState = !blinkState;
        digitalWrite(PIN_DEADLED, blinkState ? HIGH : LOW);
      }

      if (now - lastBeepTime >= DEAD_BLINK_MS) {
        lastBeepTime = now;
        beepState = !beepState;
        digitalWrite(PIN_BUZZER, beepState ? HIGH : LOW);
      }
      break;
  }
}

void updateSensorAndState() {
  correctedPPM = mq135_sensor.getCorrectedPPM(temperature, humidity);

  if (correctedPPM < SAFE_LIMIT) {
    currentState = SAFE_STATE;
  } else if (correctedPPM <= WARNING_LIMIT) {
    currentState = WARNING_STATE;
  } else {
    currentState = DEATH_STATE;
  }

  // Generalized 1-100% gauge based on 0..WARNING_LIMIT
  float pct = mapFloat(correctedPPM, 0.0, WARNING_LIMIT, 1.0, 100.0);
  if (pct < 1.0) pct = 1.0;
  if (pct > 100.0) pct = 100.0;
  ppmPercent = (int)round(pct);
}

// =====================================================
// Web dashboard
// =====================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Nein-Smoker</title>
  <style>
    :root{
      --bg:#071a10;
      --accent:#22c55e;
      --card:#0f172a;
      --text:#e5e7eb;
      --muted:#94a3b8;
    }
    *{box-sizing:border-box}
    body{
      margin:0;
      min-height:100vh;
      font-family:Arial,Helvetica,sans-serif;
      background:linear-gradient(180deg,var(--bg),#020617);
      color:var(--text);
      display:flex;
      align-items:center;
      justify-content:center;
      padding:20px;
      transition:background .35s ease;
    }
    .wrap{
      width:min(520px,100%);
      text-align:center;
    }
    .header{
      font-size:38px;
      font-weight:800;
      letter-spacing:.5px;
      margin-bottom:8px;
    }
    .tagline{
      color:var(--muted);
      margin-bottom:22px;
      font-size:15px;
    }
    .card{
      background:rgba(15,23,42,.88);
      border:1px solid rgba(148,163,184,.16);
      border-radius:26px;
      padding:24px;
      box-shadow:0 18px 50px rgba(0,0,0,.35);
      backdrop-filter:blur(8px);
    }
    .gauge{
      width:230px;
      height:230px;
      margin:0 auto 18px;
      border-radius:50%;
      background:conic-gradient(var(--accent) 0deg, #263042 0deg);
      position:relative;
      display:grid;
      place-items:center;
      transition:background .35s ease;
    }
    .gauge::before{
      content:'';
      position:absolute;
      width:170px;
      height:170px;
      background:#0b1220;
      border-radius:50%;
      box-shadow:inset 0 0 0 1px rgba(255,255,255,.05);
    }
    .center{
      position:relative;
      z-index:1;
      display:flex;
      flex-direction:column;
      align-items:center;
      gap:6px;
    }
    .percent{
      font-size:44px;
      font-weight:800;
      line-height:1;
    }
    .ppm{
      font-size:14px;
      color:var(--muted);
    }
    .status{
      display:inline-block;
      margin-top:6px;
      padding:10px 18px;
      border-radius:999px;
      font-weight:800;
      letter-spacing:1px;
      border:1px solid color-mix(in srgb, var(--accent) 70%, white 0%);
      color:var(--accent);
      background:color-mix(in srgb, var(--accent) 10%, transparent);
    }
    .meta{
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:12px;
      margin-top:18px;
      text-align:left;
    }
    .box{
      padding:14px;
      border-radius:18px;
      background:rgba(2,6,23,.55);
      border:1px solid rgba(148,163,184,.12);
    }
    .label{
      font-size:12px;
      color:var(--muted);
      margin-bottom:6px;
    }
    .value{
      font-size:18px;
      font-weight:700;
      word-break:break-word;
    }
    @media (max-width:420px){
      .header{font-size:30px}
      .gauge{width:200px;height:200px}
      .gauge::before{width:150px;height:150px}
      .percent{font-size:38px}
      .meta{grid-template-columns:1fr}
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="header">Nein-Smoker</div>
    <div class="tagline">Watch your smoke or you will die^^</div>

    <div class="card">
      <div class="gauge" id="gauge">
        <div class="center">
          <div class="percent" id="percent">0%</div>
          <div class="ppm" id="ppm">0 ppm</div>
        </div>
      </div>

      <div class="status" id="status">SAFE</div>

      <div class="meta">
        <div class="box">
          <div class="label">Corrected PPM</div>
          <div class="value" id="ppmBox">0</div>
        </div>
        <div class="box">
          <div class="label">Threshold</div>
          <div class="value">305k / 340k</div>
        </div>
      </div>
    </div>
  </div>

<script>
async function updateData(){
  try{
    const res = await fetch('/data', { cache: 'no-store' });
    const data = await res.json();

    const pct = Math.max(1, Math.min(100, Number(data.percent) || 1));
    const ppm = Number(data.ppm) || 0;
    const status = data.status || 'UNKNOWN';
    const color = data.color || '#94a3b8';
    const bg = data.bg || '#0f172a';

    document.documentElement.style.setProperty('--accent', color);
    document.documentElement.style.setProperty('--bg', bg);
    document.body.style.background = `linear-gradient(180deg, ${bg}, #020617)`;

    const angle = pct * 3.6;
    document.getElementById('gauge').style.background =
      `conic-gradient(${color} 0deg ${angle}deg, #263042 ${angle}deg 360deg)`;

    document.getElementById('percent').textContent = pct + '%';
    document.getElementById('ppm').textContent = ppm.toFixed(0) + ' ppm';
    document.getElementById('ppmBox').textContent = ppm.toFixed(0);
    document.getElementById('status').textContent = status;
    document.getElementById('status').style.color = color;
    document.getElementById('status').style.borderColor = color;
    document.getElementById('status').style.background = color + '1A';
  }catch(e){
    console.log(e);
  }
}
updateData();
setInterval(updateData, 600);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  String statusText = getStatusText(currentState);
  String statusColor = getStatusColor(currentState);
  String statusBg = getStatusBg(currentState);

  String json = "{";
  json += "\"ppm\":" + String(correctedPPM, 2) + ",";
  json += "\"percent\":" + String(ppmPercent) + ",";
  json += "\"status\":\"" + statusText + "\",";
  json += "\"color\":\"" + statusColor + "\",";
  json += "\"bg\":\"" + statusBg + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// =====================================================
// Setup / Loop
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_WARNLED, OUTPUT);
  pinMode(PIN_DEADLED, OUTPUT);
  pinMode(PIN_SAFELED, OUTPUT);

  allOff();
  digitalWrite(PIN_SAFELED, HIGH);

  Serial.println();
  Serial.println("=== Nein-Smoker v2 Booting ===");

  WiFi.mode(WIFI_AP);
  bool apOK = WiFi.softAP(AP_SSID, AP_PASS);

  if (apOK) {
    Serial.println("AP mode started successfully");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("AP mode failed");
  }

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Serial.println("Web server started");
}

void loop() {
  unsigned long now = millis();

  server.handleClient();

  if (now - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = now;
    updateSensorAndState();

    Serial.print("Corrected PPM: ");
    Serial.print(correctedPPM, 2);
    Serial.print(" | Percent: ");
    Serial.print(ppmPercent);
    Serial.print("% | State: ");
    Serial.println(getStatusText(currentState));
  }

  applyStateOutputs(currentState, now);
}