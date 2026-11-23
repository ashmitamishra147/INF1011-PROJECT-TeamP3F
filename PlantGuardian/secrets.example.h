// Template for secrets.h (which is git-ignored so passwords stay off GitHub).
//
// Setup: copy this file to "secrets.h" in the same folder, then fill in your own values.
//   cp PlantGuardian/secrets.example.h PlantGuardian/secrets.h

#define WIFI_SSID     "YOUR_WIFI_NAME"        // 2.4 GHz network (the WINC1500 has no 5 GHz). A phone hotspot works well.
                                              // Names with a curly apostrophe (iPhone: Nyu's) need it as octal: "Nyu\342\200\231s iPhone"
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"

// The computer running "node dashboard/serve.js". Use its IPv4 address on that same network
// (serve.js prints it as "Phone (same WiFi): http://<ip>:8000"). Commas, not dots!
#define SERVER_IP     192,168,1,100
#define SERVER_PORT   8000

// Any secret string. It must match DEVICE_TOKEN=... in dashboard/.env, so strangers on the network
// can't feed the dashboard fake readings.
#define DEVICE_TOKEN  "pick-a-random-string"
