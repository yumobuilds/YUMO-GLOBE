# YUMO-GLOBE
YUMO GLOBE — Brass Wire Sculpture Cyber Desk Clock featuring an ESP32-C3 and 7-Segment Display.
Hand-Bent Brass Wire Sculpture, NTP Time Sync, SK6812 LEDs & Startup Animation

Fourth build from YUMO BUILDS.

This one started as a pile of 1.5mm brass rods and a wooden base — no 3D printing, no CNC, no moulds. Every angle is hand-bent.

Inside: an ESP32-C3 Super Mini drives a 4-digit 7-segment display (LTD-4608E) via hardware-multiplexed ISR at 333Hz, keeping the display rock-solid while everything else runs in the background. Five SK6812 addressable LEDs live inside the sculpture — four ring the base, one sits at the top — and they each do their own thing.

Full source code on GitHub — build your own!

────────────────────────
🔧 PARTS USED

-ESP32-C3 Super Mini
https://www.amazon.co.uk/dp/B0DDL7WNZX

-2-Digit 7-Segment Display
https://www.amazon.co.uk/s?k=2-Digit+7-Segment+Display

-SK6812 Addressable LEDs
https://thepihut.com/products/neopixel-rgb-5050-led-with-integrated-driver-chip-100-pack

-Brass Rods 1.5mm × 300mm
https://www.amazon.co.uk/s?k=brass+rods+1.5mm

-DC 3.7V 18650 Li-ion Battery
https://www.amazon.co.uk/s?k=18650+3.7v+rechargeable+battery

-18650 Li-ion Battery charger module
https://www.amazon.co.uk/dp/B0DXC1JSB5

-Dome glass stand
https://www.amazon.co.uk/dp/B0DWX567HV

────────────────────────

🚀 FEATURES

🕐 NTP Time Sync — Auto Timezone
Connects via WiFiManager captive portal — no hardcoded passwords.
Timezone auto-detected via IP geolocation. Syncs every hour with pool.ntp.org and time.google.com.

⚡ Hardware-Multiplexed Display
ISR-driven 4-digit mux at 333Hz. Zero flicker. Heavy WiFi + LED tasks run in parallel without touching the display.

🌈 SK6812 LED Animations — 5 Stages
Startup: all 5 LEDs sync with the display animation — warm spinner (red → orange → gold), cool fill (teal → cyan → blue), countdown rainbow, neon burst, and a white strobe finale.
After sync: the top LED solos — breathing through neon yellow, purple, blue, red, and white.
Every 30 seconds: the top LED triple-flashes.
Every 5 minutes: base and top LEDs alternate slow breathing in contrasting blue/gold/purple/white pairs.
Every hour: full neon show across all 5 LEDs.

🫲Hand-Bent Brass Wire Sculpture
The frame is entirely hand-bent from 1.5mm brass rod — no 3D printing, no laser cutting, no moulds. The wooden base is turned by hand.

📶 WiFiManager Portal
No hardcoded credentials. Captive portal on first boot — saved forever after that.

────────────────────────
📦 LIBRARIES USED
FastLED · WiFiManager · ArduinoJson · PlatformIO
────────────────────────

More YUMO BUILDS projects coming soon.
💬 Drop a comment — what should I add next?

#ESP32 #ESP32C3 #YumoBuilds #YumoClock #FastLED #SK6812 #NeoPixel #NTPClock #DIYClock #SmartClock #BrassWire #WireSculpture #WireArt #HandmadeTech #Maker #OpenSource #ArduinoProject #ElectronicsProject #DIYGadget #HomeMade
