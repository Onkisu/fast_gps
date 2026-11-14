 
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
// SENSOR GETAR
// =====================================================

#define VIBRATION_PIN 9

int lastVibrationState = LOW;

unsigned long lastVibrationTime = 0;
const unsigned long DEBOUNCE_TIME = 100;

// =====================================================
// SERVER HTTP
// =====================================================

const char* SERVER_URL =
  "http://103.217.144.117:8000/gps";

// =====================================================
// GPS DUMMY
// =====================================================

const double DUMMY_LAT = -6.853744;
const double DUMMY_LON = 108.015279;
const double DUMMY_SPEED = 0.0;
const double DUMMY_ALTITUDE = 370.5;

// =====================================================
// SEND AT COMMAND
// =====================================================

String sendAT(const char* cmd, unsigned long timeout)
{
  while (SIM800.available())
  {
    SIM800.read();
  }

  Serial.print("\n>> ");
  Serial.println(cmd);

  SIM800.println(cmd);

  String response;
  unsigned long start = millis();

  while (millis() - start < timeout)
  {
    while (SIM800.available())
    {
      char c = SIM800.read();
      response += c;
      Serial.write(c);
    }

    delay(1);
  }

  return response;
}

// =====================================================
// WAIT FOR MODEM RESPONSE
// =====================================================

bool waitResponse(
  const char* keyword,
  unsigned long timeout,
  String& response
)
{
  response = "";

  unsigned long start = millis();

  while (millis() - start < timeout)
  {
    while (SIM800.available())
    {
      char c = SIM800.read();

      response += c;
      Serial.write(c);

      if (response.indexOf(keyword) >= 0)
      {
        return true;
      }
    }

    delay(1);
  }

  return false;
}

// =====================================================
// SETUP GPRS
// =====================================================

bool setupGPRS()
{
  Serial.println("\n================================");
  Serial.println("SETUP GPRS SIM800L");
  Serial.println("================================");

  String response;

  // Cek modem
  response = sendAT("AT", 1500);

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Modem tidak merespons.");
    return false;
  }

  // Nonaktifkan echo
  sendAT("ATE0", 1000);

  // Periksa SIM card
  response = sendAT("AT+CPIN?", 1500);

  if (response.indexOf("READY") == -1)
  {
    Serial.println("SIM card belum siap.");
    return false;
  }

  // Periksa sinyal
  sendAT("AT+CSQ", 1500);

  // Periksa registrasi jaringan
  response = sendAT("AT+CREG?", 1500);

  if (response.indexOf(",1") == -1 &&
      response.indexOf(",5") == -1)
  {
    Serial.println("Belum terdaftar pada jaringan GSM.");
    return false;
  }

  // Periksa GPRS
  response = sendAT("AT+CGATT?", 1500);

  if (response.indexOf("+CGATT: 1") == -1)
  {
    Serial.println("GPRS belum terhubung.");
    return false;
  }

  // Tutup bearer lama jika ada
  sendAT("AT+SAPBR=0,1", 2000);

  // Konfigurasi bearer GPRS
  response = sendAT(
    "AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"",
    2000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Gagal mengatur CONTYPE.");
    return false;
  }

  // APN Telkomsel
  response = sendAT(
    "AT+SAPBR=3,1,\"APN\",\"internet\"",
    2000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Gagal mengatur APN.");
    return false;
  }

  sendAT(
    "AT+SAPBR=3,1,\"USER\",\"\"",
    1500
  );

  sendAT(
    "AT+SAPBR=3,1,\"PWD\",\"\"",
    1500
  );

  // Aktifkan GPRS
  response = sendAT(
    "AT+SAPBR=1,1",
    15000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Gagal mengaktifkan bearer GPRS.");
    return false;
  }

  // Periksa IP
  response = sendAT(
    "AT+SAPBR=2,1",
    3000
  );

  if (response.indexOf("+SAPBR: 1,1") == -1)
  {
    Serial.println("Bearer GPRS belum aktif.");
    return false;
  }

  Serial.println("\nGPRS BERHASIL TERHUBUNG.");

  return true;
}

// =====================================================
// INITIALIZE HTTP
// =====================================================

bool initHTTP()
{
  Serial.println("\nMenginisialisasi HTTP...");

  // Bersihkan sesi sebelumnya
  sendAT("AT+HTTPTERM", 1000);

  String response = sendAT(
    "AT+HTTPINIT",
    3000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("HTTPINIT gagal.");
    return false;
  }

  // Pilih bearer GPRS
  response = sendAT(
    "AT+HTTPPARA=\"CID\",1",
    2000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Gagal mengatur CID.");
    return false;
  }

  // URL server
  response = sendAT(
    "AT+HTTPPARA=\"URL\",\"http://103.217.144.117:8000/gps\"",
    3000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Gagal mengatur URL.");
    return false;
  }

  // Content-Type JSON
  response = sendAT(
    "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
    2000
  );

  if (response.indexOf("OK") == -1)
  {
    Serial.println("Gagal mengatur Content-Type.");
    return false;
  }

  Serial.println("HTTP berhasil diinisialisasi.");

  return true;
}

// =====================================================
// SEND HTTP POST
// =====================================================

bool sendHTTPPost(const String& json)
{
  Serial.println("\n================================");
  Serial.println("HTTP POST");
  Serial.println("================================");

  Serial.print("URL: ");
  Serial.println(SERVER_URL);

  Serial.print("JSON: ");
  Serial.println(json);

  // ---------------------------------------------------
  // HTTPDATA
  // ---------------------------------------------------

  Serial.println("\nMenyiapkan HTTPDATA...");

  SIM800.print("AT+HTTPDATA=");
  SIM800.print(json.length());
  SIM800.println(",10000");

  String response;

  if (!waitResponse(
    "DOWNLOAD",
    10000,
    response
  ))
  {
    Serial.println("ERROR: DOWNLOAD prompt tidak ditemukan.");
    return false;
  }

  // Kirim JSON
  Serial.println("\nMengirim JSON...");

  size_t sent = SIM800.write(
    (const uint8_t*)json.c_str(),
    json.length()
  );

  if (sent != json.length())
  {
    Serial.println("ERROR: Pengiriman JSON tidak lengkap.");
    return false;
  }

  // Tunggu modem menerima seluruh JSON
  if (!waitResponse(
    "\r\nOK",
    15000,
    response
  ))
  {
    Serial.println("ERROR: HTTPDATA gagal.");
    return false;
  }

  Serial.println("\nJSON berhasil diterima modem.");

  // ---------------------------------------------------
  // HTTPACTION POST
  // ---------------------------------------------------

  Serial.println("\nMenjalankan HTTP POST...");

  SIM800.println("AT+HTTPACTION=1");

  // Tunggu respons HTTPACTION
  if (!waitResponse(
    "+HTTPACTION:",
    60000,
    response
  ))
  {
    Serial.println("ERROR: HTTPACTION timeout.");
    return false;
  }

  // Pastikan seluruh baris respons sudah diterima
  unsigned long start = millis();

  while (millis() - start < 2000)
  {
    while (SIM800.available())
    {
      char c = SIM800.read();
      response += c;
      Serial.write(c);

      if (c == '\n')
      {
        goto HTTP_ACTION_DONE;
      }
    }

    delay(1);
  }

HTTP_ACTION_DONE:

  Serial.println("\n========== HTTP ACTION ==========");
  Serial.println(response);
  Serial.println("=================================");

  // Parse:
  // +HTTPACTION: 1,200,xx

  int firstComma = response.indexOf(',');
  int secondComma = response.indexOf(
    ',',
    firstComma + 1
  );

  if (firstComma < 0 || secondComma < 0)
  {
    Serial.println("Respons HTTPACTION tidak valid.");
    return false;
  }

  int httpStatus = response.substring(
    firstComma + 1,
    secondComma
  ).toInt();

  int dataLength = response.substring(
    secondComma + 1
  ).toInt();

  Serial.print("HTTP Status: ");
  Serial.println(httpStatus);

  Serial.print("Response Length: ");
  Serial.println(dataLength);

  // ---------------------------------------------------
  // HTTPREAD
  // ---------------------------------------------------

  Serial.println("\nMembaca respons server...");

  response = sendAT(
    "AT+HTTPREAD",
    10000
  );

  Serial.println("\n========== SERVER RESPONSE ==========");
  Serial.println(response);
  Serial.println("=====================================");

  // ---------------------------------------------------
  // CHECK STATUS
  // ---------------------------------------------------

  if (httpStatus >= 200 && httpStatus < 300)
  {
    Serial.println("\nBERHASIL: HTTP POST diterima server.");
    return true;
  }

  Serial.println("\nGAGAL: HTTP status bukan 2xx.");

  return false;
}

// =====================================================
// TERMINATE HTTP
// =====================================================

void terminateHTTP()
{
  Serial.println("\nMengakhiri sesi HTTP...");

  sendAT("AT+HTTPTERM", 2000);
}

// =====================================================
// SEND GPS
// =====================================================

void sendGPS()
{
  double lat;
  double lon;
  double speed;
  double altitude;

  // GPS asli
  if (gps.location.isValid() &&
      gps.location.age() < 10000)
  {
    lat = gps.location.lat();
    lon = gps.location.lng();

    speed = gps.speed.isValid()
      ? gps.speed.kmph()
      : 0.0;

    altitude = gps.altitude.isValid()
      ? gps.altitude.meters()
      : 0.0;

    Serial.println("\nMenggunakan GPS asli.");
  }
  else
  {
    lat = DUMMY_LAT;
    lon = DUMMY_LON;
    speed = DUMMY_SPEED;
    altitude = DUMMY_ALTITUDE;

    Serial.println("\nGPS belum valid.");
    Serial.println("Menggunakan GPS dummy.");
  }

  // ---------------------------------------------------
  // JSON
  // ---------------------------------------------------

  String json =
    "{\"lat\":" + String(lat, 6) +
    ",\"lon\":" + String(lon, 6) +
    ",\"speed\":" + String(speed, 2) +
    ",\"altitude\":" + String(altitude, 2) +
    "}";

  Serial.println("\n========== GPS JSON ==========");
  Serial.println(json);
  Serial.println("==============================");

  // ---------------------------------------------------
  // HTTP INITIALIZATION
  // ---------------------------------------------------

  if (!initHTTP())
  {
    Serial.println("HTTP initialization gagal.");
    terminateHTTP();
    return;
  }

  // ---------------------------------------------------
  // HTTP POST
  // ---------------------------------------------------

  bool success = sendHTTPPost(json);

  // ---------------------------------------------------
  // TERMINATE
  // ---------------------------------------------------

  terminateHTTP();

  if (success)
  {
    Serial.println("\nDATA GPS BERHASIL DIKIRIM.");
  }
  else
  {
    Serial.println("\nPENGIRIMAN GPS GAGAL.");
  }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  

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

  pinMode(VIBRATION_PIN, INPUT);

  lastVibrationState = digitalRead(VIBRATION_PIN);
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32 + GPS + SIM800L");
  Serial.println("SENSOR GETAR -> GPRS -> HTTP -> FASTAPI");
  Serial.println("SERVER");
  Serial.println("PORT: 8000");
  Serial.println("PROTOCOL: HTTP AT COMMAND");
  Serial.println("METHOD: POST");
  Serial.println("CONTENT-TYPE: APPLICATION/JSON");
  Serial.println("GPS TIDAK VALID: DATA DUMMY");
  Serial.println("========================================");

  // Setup GPRS
  if (!setupGPRS())
  {
    Serial.println("\nPERINGATAN: GPRS GAGAL.");
    Serial.println("Periksa SIM card, APN, sinyal, dan jaringan.");
  }

  Serial.println("\nMenunggu GPS dan sensor getar...");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // Baca GPS terus-menerus
  while (GPS.available())
  {
    gps.encode(GPS.read());
  }

  // ---------------------------------------------------
  // GPS DEBUG SETIAP 5 DETIK
  // ---------------------------------------------------

  static unsigned long lastGPSDebug = 0;

  if (millis() - lastGPSDebug >= 5000)
  {
    lastGPSDebug = millis();

    if (gps.location.isValid() &&
        gps.location.age() < 10000)
    {
      Serial.println("\nGPS FIX OK");

      Serial.print("Latitude : ");
      Serial.println(gps.location.lat(), 6);

      Serial.print("Longitude: ");
      Serial.println(gps.location.lng(), 6);

      Serial.print("Speed    : ");

      if (gps.speed.isValid())
      {
        Serial.print(gps.speed.kmph());
      }
      else
      {
        Serial.print("N/A");
      }

      Serial.println(" km/h");

      Serial.print("Altitude : ");

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
      Serial.println("GPS belum FIX, data dummy tersedia.");
    }
  }

  // ---------------------------------------------------
  // SENSOR GETAR LOW -> HIGH
  // ---------------------------------------------------

  int vibrationState = digitalRead(VIBRATION_PIN);

  if (vibrationState == HIGH &&
      lastVibrationState == LOW)
  {
    unsigned long now = millis();

    if (now - lastVibrationTime >= DEBOUNCE_TIME)
    {
      lastVibrationTime = now;

      Serial.println("\n================================");
      Serial.println("GETARAN TERDETEKSI!");
      Serial.println("Mengirim GPS melalui HTTP POST...");
      Serial.println("================================");

      sendGPS();
    }
  }

  lastVibrationState = vibrationState;

  delay(1);
}
