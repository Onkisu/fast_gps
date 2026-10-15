
#include <Arduino.h>
#include <TinyGPSPlus.h>

// =====================================================
// GPS
// =====================================================

TinyGPSPlus gps;
HardwareSerial GPS(0);

#define GPS_RX 4
#define GPS_TX 5

// =====================================================
// SIM800L
// =====================================================

HardwareSerial SIM800(1);

#define SIM_RX 6
#define SIM_TX 7

// =====================================================
// PINGGY TCP TUNNEL
// =====================================================

const char* HOST = "fdiqzeurya.a.pinggy.link";
const int PORT = 25249;
const char* PATH = "/gps";

// =====================================================
// TIMING
// =====================================================

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 60000UL; // 10 menit

int gpsFailCount = 0;
const int GPS_FAIL_LIMIT = 3;

bool dummySent = false;

// Data dummy
const float DUMMY_LAT = -6.853744;
const float DUMMY_LON = 108.015279;
const float DUMMY_SPEED = 0.0;
const float DUMMY_ALTITUDE = 370.5;

// =====================================================
// READ RESPONSE
// =====================================================

String readResponse(unsigned long timeout)
{
  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout)
  {
    while (SIM800.available())
    {
      char c = SIM800.read();
      response += c;
    }
  }

  return response;
}

// =====================================================
// SEND AT
// =====================================================

String sendAT(const char* cmd, unsigned long timeout)
{
  Serial.print("\n>> ");
  Serial.println(cmd);

  SIM800.println(cmd);

  String response = readResponse(timeout);

  if (response.length() > 0)
  {
    Serial.println(response);
  }

  return response;
}

// =====================================================
// SETUP GPRS
// =====================================================

void setupGPRS()
{
  sendAT("AT", 1000);
  sendAT("ATE0", 1000);
  sendAT("AT+CPIN?", 1000);
  sendAT("AT+CSQ", 1000);
  sendAT("AT+CREG?", 1000);
  sendAT("AT+CGATT?", 1000);

  sendAT("AT+CIPSHUT", 2000);

  // APN Telkomsel
  sendAT("AT+CSTT=\"internet\",\"\",\"\"", 1000);

  sendAT("AT+CIICR", 2000);
  sendAT("AT+CIFSR", 1000);
}

// =====================================================
// SEND GPS VIA TCP TUNNEL
// =====================================================

void sendGPS()
{
  float lat;
  float lon;
  float speed;
  float altitude;

  // ===================================================
  // GPS FIX CHECK / DUMMY DATA
  // ===================================================

  if (!gps.location.isValid())
  {
    gpsFailCount++;

    Serial.println();
    Serial.println("GPS BELUM FIX");

    Serial.print("Percobaan gagal: ");
    Serial.print(gpsFailCount);
    Serial.print("/");
    Serial.println(GPS_FAIL_LIMIT);

    if (gpsFailCount < GPS_FAIL_LIMIT || dummySent)
    {
      Serial.println("Pengiriman dilewati.");
      return;
    }

    // GPS gagal 3 kali: gunakan data dummy
    lat = DUMMY_LAT;
    lon = DUMMY_LON;
    speed = DUMMY_SPEED;
    altitude = DUMMY_ALTITUDE;

    dummySent = true;

    Serial.println("GPS gagal 3 kali.");
    Serial.println("MENGIRIM DATA DUMMY");
  }
  else
  {
    // Gunakan data GPS asli
    lat = gps.location.lat();
    lon = gps.location.lng();

    speed = gps.speed.isValid()
              ? gps.speed.kmph()
              : 0.0;

    altitude = gps.altitude.isValid()
                 ? gps.altitude.meters()
                 : 0.0;

    gpsFailCount = 0;
    dummySent = false;

    Serial.println("Menggunakan data GPS asli.");
  }

  // ===================================================
  // JSON
  // ===================================================

  String json =
    "{\"lat\":" + String(lat, 6) +
    ",\"lon\":" + String(lon, 6) +
    ",\"speed\":" + String(speed, 2) +
    ",\"altitude\":" + String(altitude, 2) +
    "}";

  Serial.println();
  Serial.println("================================");
  Serial.println("GPS JSON");
  Serial.println("================================");
  Serial.println(json);
  Serial.println("================================");

  // ===================================================
  // SSL MATI
  // ===================================================

  Serial.println("\nDisabling SSL...");

  sendAT("AT+CIPSSL=0", 3000);

  // ===================================================
  // CONNECT TCP TUNNEL
  // ===================================================

  Serial.println("\nConnecting to Pinggy TCP tunnel...");

  SIM800.print("AT+CIPSTART=\"TCP\",\"");
  SIM800.print(HOST);
  SIM800.print("\",");
  SIM800.println(PORT);

  String connectResponse = readResponse(3000);

  Serial.println("TCP RESPONSE:");
  Serial.println(connectResponse);

  // ===================================================
  // CEK CONNECT
  // ===================================================

  if (connectResponse.indexOf("CONNECT OK") == -1)
  {
    Serial.println("ERROR: TCP tunnel gagal terhubung.");

    sendAT("AT+CIPCLOSE", 5000);
    return;
  }

  Serial.println("TCP CONNECTED");

  // ===================================================
  // HTTP REQUEST
  // ===================================================

  String request =
    "POST " + String(PATH) + " HTTP/1.1\r\n"
    "Host: " + String(HOST) + "\r\n"
    "User-Agent: SIM800L-GPS\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " + String(json.length()) + "\r\n"
    "Connection: close\r\n"
    "\r\n" +
    json;

  Serial.println();
  Serial.println("================================");
  Serial.println("HTTP REQUEST");
  Serial.println("================================");
  Serial.println(request);
  Serial.println("================================");

  // ===================================================
  // CIPSEND
  // ===================================================

  Serial.println("\nWaiting for > prompt...");

  SIM800.println("AT+CIPSEND");

  unsigned long start = millis();
  bool prompt = false;

  while (millis() - start < 10000)
  {
    if (SIM800.available())
    {
      String response = SIM800.readString();

      Serial.println(response);

      if (response.indexOf(">") != -1)
      {
        prompt = true;
        break;
      }
    }
  }

  if (!prompt)
  {
    Serial.println("ERROR: Tidak mendapat prompt >");

    sendAT("AT+CIPCLOSE", 5000);
    return;
  }

  Serial.println("> Prompt received");

  // ===================================================
  // SEND HTTP REQUEST
  // ===================================================

  SIM800.print(request);

  delay(100);

  // CTRL+Z
  SIM800.write(0x1A);

  // ===================================================
  // WAIT FASTAPI RESPONSE
  // ===================================================

  Serial.println("\nWaiting FastAPI response...");

  String response = readResponse(5000);

  Serial.println();
  Serial.println("================================");
  Serial.println("FASTAPI RESPONSE");
  Serial.println("================================");
  Serial.println(response);
  Serial.println("================================");

  // ===================================================
  // CLOSE
  // ===================================================

  sendAT("AT+CIPCLOSE", 5000);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(2000);

  // GPS UART
  GPS.begin(
    9600,
    SERIAL_8N1,
    GPS_RX,
    GPS_TX
  );

  // SIM800L UART
  SIM800.begin(
    9600,
    SERIAL_8N1,
    SIM_RX,
    SIM_TX
  );

  delay(3000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 + GPS + SIM800L");
  Serial.println("GPS -> GPRS -> TCP -> PINGGY -> FASTAPI");
  Serial.println("GPS FAIL LIMIT: 3");
  Serial.println("SEND INTERVAL: 10 MINUTES");
  Serial.println("================================");

  setupGPRS();

  delay(3000);

  Serial.println("\nMenunggu GPS...");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // ===================================================
  // BACA GPS TERUS-MENERUS
  // ===================================================

  while (GPS.available())
  {
    gps.encode(GPS.read());
  }

  // ===================================================
  // DEBUG GPS
  // ===================================================

  static unsigned long lastGPSDebug = 0;

  if (millis() - lastGPSDebug >= 5000)
  {
    lastGPSDebug = millis();

    if (gps.location.isValid())
    {
      Serial.println();
      Serial.println("GPS FIX OK");

      Serial.print("Lat       : ");
      Serial.println(gps.location.lat(), 6);

      Serial.print("Lon       : ");
      Serial.println(gps.location.lng(), 6);

      Serial.print("Speed     : ");

      if (gps.speed.isValid())
      {
        Serial.print(gps.speed.kmph());
      }
      else
      {
        Serial.print("N/A");
      }

      Serial.println(" km/h");

      Serial.print("Altitude  : ");

      if (gps.altitude.isValid())
      {
        Serial.print(gps.altitude.meters());
      }
      else
      {
        Serial.print("N/A");
      }

      Serial.println(" m");
    }
    else
    {
      Serial.println("GPS belum FIX...");
    }
  }

  // ===================================================
  // KIRIM SETIAP 10 MENIT
  // ===================================================

  if (millis() - lastSend >= SEND_INTERVAL)
  {
    lastSend = millis();

    sendGPS();
  }
}
