# Web Server Guide

This guide explains how to use the built-in web server for file transfer, device
settings, saved Wi-Fi networks, and SD-card font management.

The mDNS hostname and the hotspot SSID still carry the CrossPoint name; they are
inherited strings, not a different device.

## Overview

The web server is available while the device is in **File Transfer** mode. It
can:

- Upload, download, rename, move, and delete files on the SD card
- Create folders
- Edit many device settings from a browser
- Manage saved Wi-Fi networks
- Upload and delete `.cpfont` SD-card font families
- Accept WebDAV clients and WebSocket uploads
- Serve the study-data migration report (`GET /migration`)

The server does not require authentication. Use it only on trusted private
networks or in hotspot mode when you control who is connected.

## Starting File Transfer

1. Open **Settings**, go to the **System** tab, and select **File Transfer**.
2. Choose one of the available modes:

| Mode | Use when |
|------|----------|
| **Join Network** | You want the device to join an existing Wi-Fi network. |
| **Meeting Publications** | You want this week's meeting publications downloaded. This mode never starts the web server. |
| **Create Hotspot** | You want the device to create its own open Wi-Fi network. |

## Leaving File Transfer

The device has no Back button. Swipe in from the left edge (Back) or press Home
to leave.

- Backing out of the mode list returns to Settings, but only if you have not
  yet tried Join Network, Meeting Publications or Create Hotspot in this
  session.
- Once any of those has started Wi-Fi, every exit — from a running server, or
  backing out of the mode list afterwards — restarts the device silently to the
  launcher. The restart clears the heap fragmentation a Wi-Fi session leaves
  behind.

## Join Network Mode

1. Select **Join Network**.
2. If you have saved Wi-Fi credentials, the device first tries the last
   connected network, then other visible saved networks in signal-strength
   order. Swipe Back to cancel, or confirm to stop auto-connect and show the
   network list.
3. If the network list is shown, pick a 2.4 GHz Wi-Fi network from the scan
   results.
4. Enter the password if prompted.
5. Save credentials if you want the reader to reconnect automatically next time.

After connection, the reader shows:

- The connected SSID
- A QR code for the web URL
- The direct IP URL, for example `http://192.168.1.102/`
- The mDNS fallback URL, usually `http://berean.local/`

Use either URL from a phone, tablet, or computer on the same network.

## Create Hotspot Mode

1. Select **Create Hotspot**.
2. Connect your phone or computer to the open Wi-Fi network:

```text
bereanOS
```

3. Open the URL shown on the device. `http://berean.local/` is preferred
   when supported; the fallback IP is typically `http://192.168.4.1/`.

The device displays one QR code for joining the hotspot and another QR code for
opening the web interface.

## Meeting Publications Mode

Meeting Publications brings up its own station connection, resolves the current
week's publications and downloads them to the SD card. It never starts the web
server, so nothing is exposed on the network while it runs. When it finishes,
the device restarts to the launcher like any other Wi-Fi exit.

## Web Interface

The browser UI has four primary pages.

### Home

The Home page shows firmware status, network mode, IP address, device type,
uptime, and free heap.

### File Manager

The File Manager page can:

- Browse SD-card folders
- Upload files, using WebSocket upload when available and HTTP upload as a fallback
- Create folders
- Download files
- Rename files
- Move files into existing folders
- Delete one or more selected files or empty folders

Uploads never overwrite: an upload whose name already exists in the target
folder is refused. To side-load a second firmware build, delete the old
`firmware.bin` first (File Manager, or `POST /delete`) or upload under a new
name. When EPUB files are moved, renamed, or deleted through the web server,
the matching book cache is cleared so stale metadata is not reused.

### Protected paths

Nothing under a dot-named folder — `/.berean` (study data), `/.crosspoint`
(settings and Wi-Fi credentials), `/.fonts` — nor `System Volume Information`
or `XTCache` can be listed, downloaded, uploaded into, created, renamed, moved
or deleted, over HTTP, WebSocket or WebDAV. Every component of the path is
checked, so `..` is refused rather than resolved. This holds even with **Show
hidden files** on; that setting affects only the on-device file browser. The
migration report is readable through `GET /migration`, and fonts are managed
through the Fonts page.

### Settings

The Settings page exposes many firmware settings in the browser, including a few
that are hidden on the device — the publication download folder among them. It
also has a card for saved Wi-Fi networks.

Passwords are accepted when adding or editing entries, but saved passwords are
not returned by the API.

### Fonts

The Fonts page lists installed SD-card font families and lets you upload
`.cpfont` files. Upload files from one font family at a time. The server validates
the font family name, filename, and `.cpfont` magic bytes before accepting the
upload.

Installed fonts appear in **Settings > Reader > Font Family** after the font
registry refreshes.

## Command Line Use

Power users can use `curl`, WebDAV clients, or WebSocket clients while the web
server is running.

Endpoint details are documented in [webserver-endpoints.md](./webserver-endpoints.md).

## Security Notes

- The HTTP server runs on port 80.
- The WebSocket upload server runs on port 81.
- There is no authentication.
- Anyone on the same network can access the web interface while it is running.
- The server stops when you exit File Transfer.
- Hotspot mode creates an open network for connectivity fallback; disconnect when done.

## Tips

1. Use **Create Hotspot** when no trusted network is available.
2. Prefer `berean.local` when available, but keep the displayed IP address as a fallback.
3. Move closer to the router if upload progress stalls in Join Network mode.
4. Upload custom fonts through the Fonts page or copy them to `/.fonts/` or `/fonts/` on the SD card.
5. Exit File Transfer mode when finished to conserve battery.

## Related Documentation

- [User Guide](../USER_GUIDE.md)
- [Webserver Endpoints](./webserver-endpoints.md)
- [SD Card Fonts](./sd-card-fonts.md)
- [Troubleshooting](./troubleshooting.md)
