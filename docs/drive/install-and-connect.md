# Connect to the board

The configurator is a web page. Open it, plug the vehicle in over USB, and the page talks to the
board directly — there is nothing to install and no server to run.

**[Open the configurator →](https://tropotek.github.io/betacrawler/app/)**

## You need a Chromium-based browser

Chrome, Edge, Brave and Opera all work. Firefox and Safari do not: talking to a USB serial device
from a page uses the Web Serial API, which only Chromium-based engines implement. The app checks
on load and tells you outright rather than failing halfway through a connection.

## Connecting to the board

![The configurator's Home page, connected to a board](../assets/screenshots/home.png)

1. Plug the board into a USB port.
2. Click **Connect**.
3. Pick the board in the browser's device picker. It appears as an STMicroelectronics virtual COM
   port — `/dev/ttyACM0` on Linux, `COM3` or similar on Windows.

The badge in the header reads **connected** once it is talking to the board. The **Help** page
shows what it found: the firmware version, the board it was built for, and when it was built.

The browser remembers a board you have picked before, so subsequent connections do not ask again.

## Pages that need a connection

**Configuration**, **Controller**, **Modes** and **Terminal** appear in the sidebar once a board
is connected, and the app returns you to Home if the connection drops.

**Firmware** is always available, on purpose — it is the tool you need when a board is in a bad
state, so it does not depend on that board working.

## Using it offline

The app installs like any other web app — Chrome offers an install button in the address bar —
and works with no network afterwards. The firmware images it flashes are cached along with it, so
a board can be recovered from a laptop with no internet at all.

## Running your own copy

You do not need to, but the app is a plain static site: any web server can host it, and the whole
tree is in `web-app/` in the repository.

```bash
cd web-app
python3 -m http.server 9091
```

Then open <http://localhost:9091>.

The one constraint is that the USB APIs require a **secure context**: `localhost` or HTTPS.
Serving the folder from a LAN address over plain HTTP loads the page but leaves it unable to
connect to anything. The repository's `docker-compose.yml` runs Caddy over the same folder if you
want that LAN deployment with a certificate.

Next: [First setup](first-setup.md).
