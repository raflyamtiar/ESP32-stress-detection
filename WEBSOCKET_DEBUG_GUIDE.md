# WebSocket Debugging Guide - June 12, 2026

## Issue Status

- **WS: OFF** masih persisten
- **HTTP POST Code: -1** tidak ada response dari server
- **Tidak ada WS connection logs** dalam serial output

## Hypothesis (dalam urutan probability)

### 1. ⚠️ **Possible: ngrok Domain Expired/Invalid**

- ngrok URL: `premedical-caryl-gawkishly.ngrok-free.dev`
- ngrok free tier tunnels **expire after 2 hours** of inactivity
- Postman works karena browser cache/keep-alive

### 2. ⚠️ **Possible: WebSocket Library Issue**

- `WebSocketsClient` mungkin tidak compatible dengan ngrok
- SSL certificate validation mungkin blocking connection
- Port 443 handshake timeout

### 3. ⚠️ **Possible: Network/Firewall Issue**

- ESP32 mungkin blocked dari port 443
- WiFi network mungkin restrict WebSocket

## Testing Steps (Updated)

### Step 1: Test HTTP Connectivity First

```
OUTPUT YANG DIHARAPKAN:
[SETUP] Testing HTTP connection...
[HTTP TEST] GET https://premedical-caryl-gawkishly.ngrok-free.dev/api/heartbeat -> Code: 200
[SETUP] HTTP Test Result: SUCCESS
```

**Jika HTTP fails:**

- ngrok tidak running atau domain expired
- Firewall blocking HTTPS port 443
- Network connectivity issue

**Jika HTTP success:**

- Network OK, lanjut ke WebSocket test

### Step 2: Monitor WebSocket Initialization

```
OUTPUT YANG DIHARAPKAN:
[WS] Connecting to ngrok: premedical-caryl-gawkishly.ngrok-free.dev
[WS] Port: 443
[WS] Path: /socket.io/?EIO=4&transport=websocket&type=esp32
[WS] Calling beginSSL()...
[WS] Setting event handler...
[WS] Setting reconnect interval to 5000ms...
[WS] beginSSL() completed - waiting for connection...
```

**Jika ini tidak muncul:**

- WiFi tidak connected
- Cek WiFi status di OLED

### Step 3: Monitor WebSocket Status Every 5 Seconds

```
OUTPUT YANG DIHARAPKAN:
[WS DEBUG] Started: 1 | Connected: 0 | WiFi: OK    (sebelum koneksi)
[WS] Connected to: (server info)                     (saat connected)
[WS] Socket.IO ready ✓                               (handshake berhasil)
[WS DEBUG] Started: 1 | Connected: 1 | WiFi: OK     (setelah handshake)
```

### Step 4: Connection Event Handler

```
EVENTS YANG MUNGKIN MUNCUL:
[WS] Connected to: ...           ← TCP connected
[WS] Handshake sent: 40          ← Socket.IO handshake
[WS] Received (1 bytes): 0       ← Server response
[WS] Socket.IO ready ✓           ← CONNECTED!

OR

[WS] Disconnected                ← Connection failed
[WS] Error (X bytes): ...        ← Error message
```

## Action Items

### Immediate Actions:

1. **Check if ngrok is still running**

   ```bash
   ngrok http 5000
   ```

   - Should see new ngrok URL
   - Note the URL, it might be different

2. **Verify ngrok URL in code matches**
   - Update `ws_host` if URL changed
   - Recompile and upload

3. **Check backend server status**
   ```bash
   curl https://premedical-caryl-gawkishly.ngrok-free.dev/api/heartbeat
   ```

   - Should return 200/404 (not -1 or timeout)

### If HTTP Test Fails:

- Ping ngrok: `ping premedical-caryl-gawkishly.ngrok-free.dev`
- Check WiFi connection on ESP32
- Restart ngrok tunnel
- Update ngrok URL in code

### If HTTP OK but WebSocket Fails:

- WebSocket library issue
- Try alternative: polling HTTP instead
- Check WebSocket logs in ngrok dashboard

## New Debug Output After Upload

Please share:

1. **Serial output in first 10 seconds after startup**
2. **[HTTP TEST] result**
3. **[WS DEBUG] output every 5 seconds**
4. **Any [WS] event messages**

## Possible Fixes

### Fix 1: Update ngrok URL

If ngrok domain expired, restart ngrok and update URL

### Fix 2: Use HTTP Polling

Replace WebSocket with periodic HTTP requests if WS fails

### Fix 3: Use Different WebSocket Library

Try `ESPWebSocket` or similar alternative

### Fix 4: Bypass SSL

Add fingerprint or use HTTP-only tunnel (non-HTTPS)

## Files Modified

- `Integration_WebSocket_oledButton.ino`
  - Added `testHTTPConnection()` function
  - Added HTTP connectivity test in setup()
  - Added detailed WS debug logging every 5s
  - Improved `startWebSocketIfOnline()` debug output
