# wizardsgame

## Arcane Labyrinth

Build and run the local two-player browser game from the repository root:

```sh
make
./output/arcane-server
```

Open <http://127.0.0.1:8080>. The server accepts an optional port argument.

Project layout:

- `server/`: C HTTP server and game rules
- `client/`: browser HTML and JavaScript
- `style/`: browser stylesheet
- `output/`: compiled server binary
