#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define SIZE 8
#define MAX_REQUEST 8192

typedef struct {
    int x, y;
    int hp;
    int score;
    int has_treasure;
    int active;
} Player;

static Player players[2];
static int turn = 0;
static int game_over = 0;
static char message[128] = "Waiting for players.";

static const int treasure_x[2] = {1, 6};
static const int treasure_y[2] = {1, 6};

static void reset_game(void) {
    memset(players, 0, sizeof(players));
    players[0] = (Player){0, 0, 3, 0, 0, 1};
    players[1] = (Player){7, 7, 3, 0, 0, 1};
    turn = 0;
    game_over = 0;
    snprintf(message, sizeof(message), "Game started.");
}

static void send_response(int fd, int status, const char *status_text,
                          const char *body) {
    char header[512];
    int body_len = (int)strlen(body);
    int header_len = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, status_text, body_len);
    send(fd, header, (size_t)header_len, 0);
    send(fd, body, (size_t)body_len, 0);
}

static void send_state(int fd) {
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"turn\":%d,\"game_over\":%s,\"message\":\"%s\","
        "\"players\":["
        "{\"x\":%d,\"y\":%d,\"hp\":%d,\"score\":%d,\"has_treasure\":%s},"
        "{\"x\":%d,\"y\":%d,\"hp\":%d,\"score\":%d,\"has_treasure\":%s}],"
        "\"treasures\":[{\"x\":%d,\"y\":%d},{\"x\":%d,\"y\":%d}]}\n",
        turn, game_over ? "true" : "false", message,
        players[0].x, players[0].y, players[0].hp, players[0].score,
        players[0].has_treasure ? "true" : "false",
        players[1].x, players[1].y, players[1].hp, players[1].score,
        players[1].has_treasure ? "true" : "false",
        treasure_x[0], treasure_y[0], treasure_x[1], treasure_y[1]);
    send_response(fd, 200, "OK", body);
}

/* Read a small integer field from a JSON request body. */
static int json_int(const char *body, const char *key, int *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(body, pattern);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    char *end;
    long value = strtol(p, &end, 10);
    if (end == p || value < -1000 || value > 1000) return 0;
    *out = (int)value;
    return 1;
}

static void handle_action(int fd, const char *body) {
    int player, dx = 0, dy = 0;
    char action[24] = {0};

    if (!json_int(body, "player", &player) || player < 0 || player > 1) {
        send_response(fd, 400, "Bad Request", "{\"error\":\"player must be 0 or 1\"}\n");
        return;
    }

    const char *a = strstr(body, "\"action\"");
    if (a && (a = strchr(a, ':'))) {
        a++;
        while (*a && isspace((unsigned char)*a)) a++;
        if (*a == '"') {
            a++;
            size_t i = 0;
            while (*a && *a != '"' && i < sizeof(action) - 1)
                action[i++] = *a++;
        }
    }

    if (game_over) {
        send_response(fd, 409, "Conflict", "{\"error\":\"game is over\"}\n");
        return;
    }
    if (!players[1].active) {
        send_response(fd, 409, "Conflict", "{\"error\":\"waiting for second player\"}\n");
        return;
    }
    if (player != turn) {
        send_response(fd, 409, "Conflict", "{\"error\":\"not your turn\"}\n");
        return;
    }

    if (strcmp(action, "move") == 0) {
        if (!json_int(body, "dx", &dx) || !json_int(body, "dy", &dy) ||
            abs(dx) + abs(dy) != 1) {
            send_response(fd, 400, "Bad Request",
                          "{\"error\":\"move needs dx,dy of one orthogonal step\"}\n");
            return;
        }
        int nx = players[player].x + dx;
        int ny = players[player].y + dy;
        if (nx < 0 || nx >= SIZE || ny < 0 || ny >= SIZE) {
            send_response(fd, 400, "Bad Request", "{\"error\":\"outside board\"}\n");
            return;
        }
        int enemy = 1 - player;
        if (players[enemy].active && players[enemy].x == nx && players[enemy].y == ny) {
            send_response(fd, 400, "Bad Request", "{\"error\":\"square occupied\"}\n");
            return;
        }
        players[player].x = nx;
        players[player].y = ny;
        snprintf(message, sizeof(message), "Player %d moved.", player);
    } else if (strcmp(action, "attack") == 0) {
        int enemy = 1 - player;
        int distance = abs(players[player].x - players[enemy].x) +
                       abs(players[player].y - players[enemy].y);
        if (distance != 1) {
            send_response(fd, 400, "Bad Request",
                          "{\"error\":\"target must be adjacent\"}\n");
            return;
        }
        players[enemy].hp--;
        snprintf(message, sizeof(message), "Player %d hit player %d.", player, enemy);
        if (players[enemy].hp <= 0) {
            players[enemy].active = 0;
            players[player].score++;
            snprintf(message, sizeof(message), "Player %d wins by knockout.", player);
            game_over = 1;
        }
    } else if (strcmp(action, "pickup") == 0) {
        int x = players[player].x, y = players[player].y;
        if (players[player].has_treasure ||
            (x != treasure_x[player] || y != treasure_y[player])) {
            send_response(fd, 400, "Bad Request",
                          "{\"error\":\"no available treasure here\"}\n");
            return;
        }
        players[player].has_treasure = 1;
        snprintf(message, sizeof(message), "Player %d picked up treasure.", player);
    } else if (strcmp(action, "score") == 0) {
        int x = players[player].x, y = players[player].y;
        if (!players[player].has_treasure || x != (player ? 7 : 0) ||
            y != (player ? 7 : 0)) {
            send_response(fd, 400, "Bad Request",
                          "{\"error\":\"return your treasure to your starting square\"}\n");
            return;
        }
        players[player].has_treasure = 0;
        players[player].score++;
        snprintf(message, sizeof(message), "Player %d scored.", player);
        if (players[player].score >= 2) {
            snprintf(message, sizeof(message), "Player %d wins!", player);
            game_over = 1;
        }
    } else {
        send_response(fd, 400, "Bad Request",
                      "{\"error\":\"action must be move, attack, pickup, or score\"}\n");
        return;
    }

    if (!game_over) turn = 1 - turn;
    send_state(fd);
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(server_fd, 16) < 0) {
        perror("bind/listen");
        close(server_fd);
        return 1;
    }

    printf("Listening on http://localhost:%d\n", PORT);

    for (;;) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd < 0) continue;

        char req[MAX_REQUEST + 1];
        ssize_t n = recv(fd, req, MAX_REQUEST, 0);
        if (n <= 0) { close(fd); continue; }
        req[n] = '\0';

        if (!strncmp(req, "OPTIONS ", 8)) {
            send_response(fd, 204, "No Content", "");
        } else if (!strncmp(req, "GET /state ", 11)) {
            send_state(fd);
        } else if (!strncmp(req, "POST /join ", 11)) {
            if (!players[1].active) {
                players[1].active = 1;
                snprintf(message, sizeof(message), "Second player joined.");
                send_state(fd);
            } else {
                send_response(fd, 409, "Conflict", "{\"error\":\"game is full\"}\n");
            }
        } else if (!strncmp(req, "POST /reset ", 12)) {
            reset_game();
            players[1].active = 0;
            send_state(fd);
        } else if (!strncmp(req, "POST /action ", 13)) {
            char *body = strstr(req, "\r\n\r\n");
            if (!body) {
                send_response(fd, 400, "Bad Request", "{\"error\":\"missing body\"}\n");
            } else {
                handle_action(fd, body + 4);
            }
        } else {
            send_response(fd, 404, "Not Found",
                          "{\"error\":\"use GET /state, POST /join, POST /action, or POST /reset\"}\n");
        }
        close(fd);
    }
}
