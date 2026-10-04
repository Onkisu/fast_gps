
from fastapi import FastAPI
from pydantic import BaseModel
import requests

app = FastAPI()

# =========================
# TELEGRAM CONFIG
# =========================

BOT_TOKEN = "8816106296:AAFBHX48tr81flCUx2ykj3dHQT6dueoX3Rc"

CHAT_IDS = [
    "998365076"
]


# =========================
# GPS DATA
# =========================

class GPSData(BaseModel):
    lon: float
    lat: float
    speed: float
    altitude: float


latest_gps = None


# =========================
# SEND TELEGRAM
# =========================

def send_telegram(data: GPSData):

    message = (
        "📍 Smart GPS\n\n"
        f"Latitude  : {data.lat}\n"
        f"Longitude : {data.lon}\n"
        f"Speed     : {data.speed} km/h\n"
        f"Altitude  : {data.altitude} m"
    )

    url = f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage"

    for chat_id in CHAT_IDS:

        response = requests.post(
            url,
            json={
                "chat_id": chat_id,
                "text": message
            },
            timeout=10
        )

        response.raise_for_status()


# =========================
# POST GPS
# =========================

@app.post("/gps")
def receive_gps(data: GPSData):

    global latest_gps

    latest_gps = data

    # Kirim data GPS ke Telegram
    send_telegram(data)

    return {
        "status": "success",
        "message": "GPS data received and sent to Telegram",
        "data": data
    }


# =========================
# GET GPS
# =========================

@app.get("/gps")
def get_gps():

    if latest_gps is None:
        return {
            "status": "empty",
            "message": "No GPS data available"
        }

    return {
        "status": "success",
        "data": latest_gps
    }

