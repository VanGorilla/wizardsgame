# wizardsgame

## Arcane Labyrinth

Build and run the local two-player browser game from the repository root:

```sh
make
./output/arcane-server
```

Open <http://127.0.0.1:8080>. The server accepts an optional port argument.
Open the address in two separate browser profiles to join as both players; each
browser keeps its player assignment in a cookie.

The server listens on all network interfaces. To join from another device on
the same network, open `http://<server-computer-LAN-IP>:8080` in that device's
browser. If the connection is blocked, allow the server through the host
computer's firewall on the selected port.

Project layout:

- `server/`: C HTTP server and game rules
- `client/`: browser HTML and JavaScript
- `style/`: browser stylesheet
- `output/`: compiled server binary
