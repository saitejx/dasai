DASAI SMART - SETUP
1. Wiring (same as before): OLED SDA=21 SCL=22 VCC=3V3 GND=GND ; Touch OUT=33 VCC=3V3 GND=GND
2. Arduino IDE libraries: U8g2, ArduinoJson (7.x). Board: ESP32 Dev Module.
3. Open Dasai_Smart.ino (keep faces.h in the same folder). Edit WIFI_SSID, WIFI_PASS,
   LAT/LON and NOTIFY_KEY at the top.
4. Upload. NO esptool / 0x190000 flashing needed now - faces are inside the sketch.
5. Open Serial Monitor (115200) or watch the OLED at boot: it shows the ESP32 IP address.

PHONE NOTIFICATIONS (Android, same Wi-Fi):
 Use MacroDroid or Tasker: trigger "Notification Received", action "HTTP GET":
   http://<ESP_IP>/notify?key=dasai123&app={not_app_name}&text={not_text}
 (URL-encode the text if the app does not do it for you.)
 Test from a browser: http://<ESP_IP>/notify?key=dasai123&app=Test&text=Hello%20Dasai

TOUCH: tap = reaction, many fast taps = gets annoyed, hold ~1s = love + clears notifications.
After 30 s idle: temperature -> AQI -> time -> (notifications) -> back to faces.
