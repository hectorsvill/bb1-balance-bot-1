# Web Interface

BB1 serves its own web page from the Pico W. No app, no internet needed.

## Connecting

### Own network (default)

1. On your phone or Mac, join the Wi-Fi network **BB1**. The password is in `firmware/bb1/secrets.h` (`bb1robot` unless you changed it).
2. Open **http://192.168.42.1**

While a Mac is on BB1 it has no internet. That's normal: BB1 isn't connected to anything else.

### Home network

1. On the page, in **Network**, type your home Wi-Fi name and password, press **Save and join**.
2. BB1 saves them and restarts. Its own network turns off.
3. Back on your home Wi-Fi, open **http://bb1.local** (or the IP address BB1 prints on serial; type `wifi` to see it).

If `bb1.local` doesn't open (some tools and older Android phones don't resolve `.local` names), use the IP address instead: type `wifi` on serial to see it. Your router may also let you reserve that IP for BB1 so it never changes.

If BB1 can't find the home network within 15 s at startup, it falls back to its own network and the page says why. To go back to the own network on purpose: **Use own network** on the page, or `wifi reset` over serial.

### Do the settings persist?

Yes. The home network name and password are saved in the Pico's flash (the last few KB, through the `EEPROM` library). They survive power-off and uploading new firmware. They're cleared by "Use own network", `wifi reset`, or erasing the whole flash.

The password is stored as plain text on the Pico. It never goes into git.

## The page

| Panel | Shows / does |
|---|---|
| Pitch | big pitch number and a 10 s chart: blue = filtered pitch, orange = accelerometer only |
| Live values | pitch rate, accel pitch, loop work time, tick, gyro bias, filter alpha, IMU status, uptime |
| Actions | **Calibrate gyro** (lay it still first), **Set alpha** |
| Network | which network BB1 is on; save a home network; switch back to the own network |

The top right says **connected** (green) or **connection lost** (red) if BB1 stops answering.

Coming next: Record / Download log (step 4), motor test (step 5), Arm / Disarm and PID gains (steps 6 and 7), drive pad (milestone 2).

## How it works

```text
browser                                  Pico W core 0                     core 1
  every 50 ms: GET /state  ----------->  newest LogRecord -> JSON  <----  writes a record every 5 ms
  button:      POST /cal   ----------->  requestCalibrate = true   ---->  calibrates between ticks
```

The page never talks to the IMU directly: it reads the same log records as the serial readout and `dump`, and asks core 1 for changes through the same flags as the serial commands. So the web page can't upset the 200 Hz loop.

### Endpoints

You can call these yourself with `curl` (on the same network as BB1):

| Request | Does |
|---|---|
| `GET /` | the page |
| `GET /state` | newest values as JSON |
| `POST /cal` | recalibrate the gyro |
| `POST /alpha` with `value=0.98` | set the filter weight |
| `GET /wifi` | network mode and the saved home network name (never the password) |
| `POST /wifi` with `ssid=...&password=...` | save a home network and restart |
| `POST /wifi/forget` | forget it and restart on the own network |

```bash
curl http://192.168.42.1/state
```

```bash
curl -X POST -d value=0.97 http://192.168.42.1/alpha
```

## Files

| File | What's in it |
|---|---|
| `firmware/bb1/wifi_setup.h` | own network / home network, saving settings to flash, fallback |
| `firmware/bb1/web.h` | the routes above, one small function each |
| `firmware/bb1/page.h` | the page: HTML, CSS and JavaScript in one C++ raw string |
| `firmware/bb1/secrets.h` | own-network name and password (not in git) |

### Changing the page

Edit `page.h`, compile, upload. Everything between `R"PAGE(` and `)PAGE"` is the page exactly as the browser gets it. To try layout changes quickly, copy that part into a `.html` file and open it in a browser (the numbers won't update without the robot).

### Adding an action

1. In `web.h`, write a handler and register it in `webBegin()` with `server.on(...)`.
2. If core 1 has to do something, add a request flag to `shared.h` and handle it at the top of `loop1()`.
3. Add a button in `page.h` that calls `post('/your-route', {...}, 'msgId')`.
4. Add the endpoint to the table above.

### Building on a new machine

`secrets.h` isn't in git, so create it from the template first:

```bash
cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h
```
