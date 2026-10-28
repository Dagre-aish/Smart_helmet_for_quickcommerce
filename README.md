# Smart Helmet + Firebase live dashboard

This folder contains:
- `smart_helmet_firebase.ino` — ESP32 firmware adapted from your original sketch.
- `portal/index.html` — starter live dashboard for Rider `RIDER_001`.

## What the code preserves
- `IR_PIN = 19`; LOW means helmet detected.
- `BUZZER_PIN = 25`.
- `ALCOHOL_PIN = 34`.
- Rider ID `RIDER_001`.
- Buzzer warning starts after 20 seconds without the helmet.
- The 2-minute ban timer starts after that warning period.
- Wearing the helmet again clears the ban, matching the original sketch.
- Alcohol detection overrides the buzzer state.

The code uploads a Firebase record about once per second and keeps printing JSON to Serial Monitor. The MPU6500 is not included because the original uploaded sketch did not read it.

## A. Create Firebase project
1. Go to https://console.firebase.google.com/ and create a project.
2. Open **Build → Realtime Database** and create a database. Copy its exact database URL.
3. Open **Build → Authentication → Sign-in method** and enable **Anonymous**.
4. In **Project settings → General**, register a Web app if needed. Copy the `apiKey`, `authDomain`, `projectId`, `appId`, and database URL.
5. The Firebase API key and Realtime Database URL are preconfigured in the ESP32 sketch. Replace only `YOUR_PHONE_HOTSPOT_NAME` and `YOUR_PHONE_HOTSPOT_PASSWORD` with your 2.4 GHz hotspot credentials.
6. The Firebase web config in `portal/index.html` is preconfigured for this project. Enable Anonymous sign-in and publish the Realtime Database rules before testing.

Firebase database URLs may end in `firebasedatabase.app` rather than `firebaseio.com`; use the exact URL Firebase shows you.

## B. Prototype database rules
For a class demo, rules can require authentication. In Realtime Database → Rules, use:

```json
{
  "rules": {
    ".read": "auth != null",
    ".write": "auth != null"
  }
}
```

Publish the rules. This is suitable only for a short prototype: anonymous sign-in is easy for anyone to enable, and these rules let any authenticated user read/write all records. Do NOT use these rules for real riders or production. Production should restrict each device to its own rider path using a trusted backend/device identity, and dashboard users should have read-only access.

## C. Arduino IDE
1. Install Arduino IDE.
2. Install ESP32 board support from Boards Manager (Espressif Systems).
3. Install **ArduinoJson** by Benoit Blanchon from Library Manager.
4. Select your ESP32 board and its USB COM port.
5. Edit the four placeholders listed in section A.
6. Upload `smart_helmet_firebase.ino`.
7. Open Serial Monitor at **115200 baud**. Confirm Wi-Fi and Firebase messages, then check that JSON values continue printing.
8. In Firebase Realtime Database, look under `riders/RIDER_001`.

## D. Run the portal locally
The portal uses Firebase's browser SDK and needs to be served over HTTP (not opened as a `file://` URL).
1. Install Node.js if needed.
2. Open a terminal in the `portal` folder.
3. Run:
   ```bash
   npx serve .
   ```
4. Open the local URL printed by the command.
5. Confirm the portal shows `Live · Firebase connected` and sensor values.

## E. Deploy to Vercel
1. Create a folder/repository containing `portal/index.html` as the site's root `index.html` (or set the Vercel project root directory to `portal`).
2. Import the repository into Vercel and deploy as a static site.
3. Open the deployed URL. The dashboard will listen to Firebase in real time.

## F. Phone hotspot
- Use a hotspot name/password without unusual characters for initial testing.
- Ensure the phone hotspot provides 2.4 GHz Wi-Fi if your phone offers a band choice; many ESP32 boards do not support 5 GHz Wi-Fi.
- Keep the phone's hotspot enabled while testing.
- If Wi-Fi fails, verify SSID/password and watch Serial Monitor.

## Important security and hardware warnings
- The sketch uses `WiFiClientSecure.setInsecure()` to simplify the first prototype. This disables TLS certificate verification. Do not deploy it for production or use it for sensitive data; replace it with proper certificate validation.
- Never connect a 7.4 V LiPo directly to the ESP32 3.3 V pin. Use a correctly adjusted regulator and verify its output with a multimeter before connecting. Confirm your specific ESP32 board's VIN/5V input.
- MQ-3 modules typically use 5 V for their heater. Ensure the analog output going to GPIO34 never exceeds 3.3 V; use a suitable divider/conditioning circuit and common ground.
- Calibrate `ALCOHOL_THRESHOLD` for your sensor. Raw ADC readings are not a certified breath-alcohol measurement.
- Do not use the software ban as a real-world enforcement mechanism without a robust backend, validation, privacy safeguards, and a manual review path.
## Deploy the dashboard on GitHub Pages

The `portal` folder is published automatically by `.github/workflows/pages.yml` whenever changes are pushed to `main`. After the first successful run, open the repository's **Settings → Pages** to find the site URL. The dashboard needs Anonymous sign-in enabled and readable Realtime Database rules to connect.
