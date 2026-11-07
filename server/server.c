#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAP_WIDTH 9
#define MAP_HEIGHT 7
#define PLAYER_COUNT 2
#define RELIC_COUNT 3
#define LOG_COUNT 8
#define REQUEST_CAPACITY 16384
#define RESPONSE_CAPACITY 8192

typedef struct {
    int x;
    int y;
    int hp;
    int score;
    int carried;
    int mana;
    int home_x;
    int home_y;
} Player;

typedef struct {
    int x;
    int y;
    int carrier;
    bool available;
} Relic;

typedef struct {
    Player players[PLAYER_COUNT];
    Relic relics[RELIC_COUNT];
    int current_player;
    int actions_left;
    int winner;
    char log[LOG_COUNT][128];
    int log_count;
} Game;

static const char *const map_rows[MAP_HEIGHT] = {
    "#########",
    "#.......#",
    "#.#.#...#",
    "#.......#",
    "#...#.#.#",
    "#.......#",
    "#########"
};

static const char *const player_names[PLAYER_COUNT] = {"Aster", "Bramble"};
static Game game;

static void add_log(const char *message) {
    if (game.log_count == LOG_COUNT) {
        memmove(game.log, game.log + 1, sizeof(game.log[0]) * (LOG_COUNT - 1));
        game.log_count--;
    }
    snprintf(game.log[game.log_count], sizeof(game.log[game.log_count]), "%s", message);
    game.log_count++;
}

static void reset_game(void) {
    memset(&game, 0, sizeof(game));
    game.players[0] = (Player){1, 1, 3, 0, 0, 1, 1, 1};
    game.players[1] = (Player){7, 5, 3, 0, 0, 1, 7, 5};
    game.relics[0] = (Relic){7, 1, -1, true};
    game.relics[1] = (Relic){1, 5, -1, true};
    game.relics[2] = (Relic){5, 3, -1, true};
    game.actions_left = 3;
    game.winner = -1;
    add_log("Aster and Bramble enter the labyrinth.");
}

static bool is_open(int x, int y) {
    return x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT && map_rows[y][x] == '.';
}

static int other_player(int player) {
    return 1 - player;
}

static void advance_turn(void) {
    game.current_player = other_player(game.current_player);
    game.actions_left = 3;
    game.players[game.current_player].mana = 1;
}

static void spend_action(void) {
    game.actions_left--;
    if (game.actions_left <= 0 && game.winner < 0) {
        advance_turn();
    }
}

static void escape_json(const char *input, char *output, size_t capacity) {
    size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)input; *cursor && used + 2 < capacity; cursor++) {
        if (*cursor == '"' || *cursor == '\\') {
            output[used++] = '\\';
        }
        if (*cursor >= 32) {
            output[used++] = (char)*cursor;
        }
    }
    output[used] = '\0';
}

static void make_state_json(char *output, size_t capacity) {
    size_t used = 0;
    int written = snprintf(output, capacity,
        "{\"map\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],\"players\":[",
        map_rows[0], map_rows[1], map_rows[2], map_rows[3], map_rows[4], map_rows[5], map_rows[6]);
    if (written < 0 || (size_t)written >= capacity) return;
    used = (size_t)written;

    for (int i = 0; i < PLAYER_COUNT; i++) {
        Player *player = &game.players[i];
        written = snprintf(output + used, capacity - used,
            "%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"hp\":%d,\"score\":%d,\"carried\":%d,\"mana\":%d,\"homeX\":%d,\"homeY\":%d}",
            i ? "," : "", player_names[i], player->x, player->y, player->hp,
            player->score, player->carried, player->mana, player->home_x, player->home_y);
        if (written < 0 || (size_t)written >= capacity - used) return;
        used += (size_t)written;
    }

    written = snprintf(output + used, capacity - used, "],\"relics\":[");
    if (written < 0 || (size_t)written >= capacity - used) return;
    used += (size_t)written;
    for (int i = 0; i < RELIC_COUNT; i++) {
        Relic *relic = &game.relics[i];
        written = snprintf(output + used, capacity - used,
            "%s{\"x\":%d,\"y\":%d,\"carrier\":%d,\"available\":%s}",
            i ? "," : "", relic->x, relic->y, relic->carrier, relic->available ? "true" : "false");
        if (written < 0 || (size_t)written >= capacity - used) return;
        used += (size_t)written;
    }

    written = snprintf(output + used, capacity - used,
        "],\"currentPlayer\":%d,\"actionsLeft\":%d,\"winner\":%d,\"log\":[",
        game.current_player, game.actions_left, game.winner);
    if (written < 0 || (size_t)written >= capacity - used) return;
    used += (size_t)written;
    for (int i = 0; i < game.log_count; i++) {
        char escaped[256];
        escape_json(game.log[i], escaped, sizeof(escaped));
        written = snprintf(output + used, capacity - used, "%s\"%s\"", i ? "," : "", escaped);
        if (written < 0 || (size_t)written >= capacity - used) return;
        used += (size_t)written;
    }
    snprintf(output + used, capacity - used, "]}");
}

static bool read_json_string(const char *json, const char *key, char *value, size_t capacity) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *cursor = strstr(json, needle);
    if (!cursor || !(cursor = strchr(cursor + strlen(needle), ':'))) return false;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor++ != '"') return false;
    size_t length = 0;
    while (*cursor && *cursor != '"' && length + 1 < capacity) {
        if (*cursor == '\\' && cursor[1]) cursor++;
        value[length++] = *cursor++;
    }
    value[length] = '\0';
    return *cursor == '"';
}

static void send_response(int client, int status, const char *status_text,
                          const char *content_type, const char *body, size_t body_length) {
    char header[512];
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        status, status_text, content_type, body_length);
    if (header_length <= 0 || (size_t)header_length >= sizeof(header)) return;
    (void)send(client, header, (size_t)header_length, 0);
    size_t sent = 0;
    while (sent < body_length) {
        ssize_t result = send(client, body + sent, body_length - sent, 0);
        if (result <= 0) break;
        sent += (size_t)result;
    }
}

static void send_json(int client, int status, const char *status_text, const char *body) {
    send_response(client, status, status_text, "application/json; charset=utf-8", body, strlen(body));
}

static void send_state(int client) {
    char body[RESPONSE_CAPACITY];
    make_state_json(body, sizeof(body));
    send_json(client, 200, "OK", body);
}

static bool serve_asset(int client, const char *path) {
    const char *filename = NULL;
    const char *content_type = NULL;
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        filename = "client/index.html";
        content_type = "text/html; charset=utf-8";
    } else if (strcmp(path, "/style.css") == 0) {
        filename = "style/style.css";
        content_type = "text/css; charset=utf-8";
    } else if (strcmp(path, "/game.js") == 0) {
        filename = "client/game.js";
        content_type = "text/javascript; charset=utf-8";
    } else {
        return false;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char *contents = malloc((size_t)length + 1);
    if (!contents) {
        fclose(file);
        return false;
    }
    size_t read_length = fread(contents, 1, (size_t)length, file);
    fclose(file);
    send_response(client, 200, "OK", content_type, contents, read_length);
    free(contents);
    return true;
}

static bool handle_action(const char *body, char *error, size_t error_capacity) {
    char action[32];
    char direction[16];
    if (!read_json_string(body, "type", action, sizeof(action))) {
        snprintf(error, error_capacity, "Action type is missing.");
        return false;
    }
    if (game.winner >= 0) {
        snprintf(error, error_capacity, "The game is over. Start a new game to play again.");
        return false;
    }

    int player_index = game.current_player;
    int opponent_index = other_player(player_index);
    Player *player = &game.players[player_index];
    Player *opponent = &game.players[opponent_index];

    if (strcmp(action, "end") == 0) {
        advance_turn();
        char message[128];
        snprintf(message, sizeof(message), "%s ends the turn.", player_names[player_index]);
        add_log(message);
        return true;
    }
    if (strcmp(action, "move") == 0 || strcmp(action, "cast") == 0) {
        if (!read_json_string(body, "direction", direction, sizeof(direction))) {
            snprintf(error, error_capacity, "Choose a direction.");
            return false;
        }
        int dx = 0;
        int dy = 0;
        if (strcmp(direction, "up") == 0) dy = -1;
        else if (strcmp(direction, "down") == 0) dy = 1;
        else if (strcmp(direction, "left") == 0) dx = -1;
        else if (strcmp(direction, "right") == 0) dx = 1;
        else {
            snprintf(error, error_capacity, "That direction is not available.");
            return false;
        }

        if (strcmp(action, "move") == 0) {
            int x = player->x + dx;
            int y = player->y + dy;
            if (!is_open(x, y) || (opponent->x == x && opponent->y == y)) {
                snprintf(error, error_capacity, "That tile is blocked.");
                return false;
            }
            player->x = x;
            player->y = y;
            char message[128];
            snprintf(message, sizeof(message), "%s moves %s.", player_names[player_index], direction);
            add_log(message);
            spend_action();
            return true;
        }

        if (player->mana < 1) {
            snprintf(error, error_capacity, "Arc Spark is spent until your next turn.");
            return false;
        }
        int distance = -1;
        for (int step = 1; step <= 3; step++) {
            int x = player->x + dx * step;
            int y = player->y + dy * step;
            if (!is_open(x, y)) break;
            if (opponent->x == x && opponent->y == y) {
                distance = step;
                break;
            }
        }
        if (distance < 0) {
            snprintf(error, error_capacity, "No opponent is in Arc Spark's clear line.");
            return false;
        }

        player->mana--;
        opponent->hp--;
        char message[128];
        snprintf(message, sizeof(message), "%s hits %s with Arc Spark.", player_names[player_index], player_names[opponent_index]);
        add_log(message);
        int pushed_x = opponent->x + dx;
        int pushed_y = opponent->y + dy;
        if (is_open(pushed_x, pushed_y) && !(player->x == pushed_x && player->y == pushed_y)) {
            opponent->x = pushed_x;
            opponent->y = pushed_y;
        }
        if (opponent->hp <= 0) {
            for (int i = 0; i < RELIC_COUNT; i++) {
                if (game.relics[i].carrier == opponent_index) {
                    game.relics[i].carrier = -1;
                    game.relics[i].x = opponent->x;
                    game.relics[i].y = opponent->y;
                    game.relics[i].available = true;
                }
            }
            opponent->carried = 0;
            opponent->hp = 3;
            opponent->x = opponent->home_x;
            opponent->y = opponent->home_y;
            snprintf(message, sizeof(message), "%s returns to their home after being struck down.", player_names[opponent_index]);
            add_log(message);
        }
        spend_action();
        return true;
    }

    if (strcmp(action, "collect") == 0) {
        if (player->carried >= 2) {
            snprintf(error, error_capacity, "You can carry at most two relics.");
            return false;
        }
        for (int i = 0; i < RELIC_COUNT; i++) {
            Relic *relic = &game.relics[i];
            if (relic->available && relic->x == player->x && relic->y == player->y) {
                relic->available = false;
                relic->carrier = player_index;
                player->carried++;
                char message[128];
                snprintf(message, sizeof(message), "%s gathers a relic.", player_names[player_index]);
                add_log(message);
                spend_action();
                return true;
            }
        }
        snprintf(error, error_capacity, "There is no relic on this tile.");
        return false;
    }

    if (strcmp(action, "deliver") == 0) {
        if (player->x != player->home_x || player->y != player->home_y || player->carried == 0) {
            snprintf(error, error_capacity, "Return to your home tile carrying a relic to deliver it.");
            return false;
        }
        int delivered = player->carried;
        for (int i = 0; i < RELIC_COUNT; i++) {
            if (game.relics[i].carrier == player_index) {
                game.relics[i].carrier = -1;
            }
        }
        player->carried = 0;
        player->score += delivered;
        char message[128];
        snprintf(message, sizeof(message), "%s delivers %d relic%s.", player_names[player_index], delivered, delivered == 1 ? "" : "s");
        add_log(message);
        if (player->score >= 2) {
            game.winner = player_index;
            snprintf(message, sizeof(message), "%s wins the labyrinth duel!", player_names[player_index]);
            add_log(message);
        } else {
            spend_action();
        }
        return true;
    }

    snprintf(error, error_capacity, "Unknown action.");
    return false;
}

static void handle_client(int client) {
    char request[REQUEST_CAPACITY];
    size_t received = 0;
    char *header_end = NULL;
    while (received + 1 < sizeof(request) && !header_end) {
        ssize_t count = recv(client, request + received, sizeof(request) - received - 1, 0);
        if (count <= 0) return;
        received += (size_t)count;
        request[received] = '\0';
        header_end = strstr(request, "\r\n\r\n");
    }
    if (!header_end) return;

    size_t header_size = (size_t)(header_end + 4 - request);
    size_t body_size = 0;
    char *length_header = strstr(request, "Content-Length:");
    if (length_header && length_header < header_end) {
        body_size = (size_t)strtoul(length_header + strlen("Content-Length:"), NULL, 10);
    }
    if (body_size > sizeof(request) - header_size - 1) {
        send_json(client, 413, "Content Too Large", "{\"error\":\"Request body is too large.\"}");
        return;
    }
    while (received < header_size + body_size) {
        ssize_t count = recv(client, request + received, sizeof(request) - received - 1, 0);
        if (count <= 0) return;
        received += (size_t)count;
        request[received] = '\0';
    }

    char method[8] = {0};
    char path[128] = {0};
    if (sscanf(request, "%7s %127s", method, path) != 2) {
        send_json(client, 400, "Bad Request", "{\"error\":\"Malformed request.\"}");
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/state") == 0) {
        send_state(client);
    } else if (strcmp(method, "GET") == 0) {
        if (!serve_asset(client, path)) {
            send_json(client, 404, "Not Found", "{\"error\":\"Not found.\"}");
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/reset") == 0) {
        reset_game();
        send_state(client);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/action") == 0) {
        char error[160];
        if (handle_action(header_end + 4, error, sizeof(error))) {
            send_state(client);
        } else {
            char escaped[320];
            char response[400];
            escape_json(error, escaped, sizeof(escaped));
            snprintf(response, sizeof(response), "{\"error\":\"%s\"}", escaped);
            send_json(client, 400, "Bad Request", response);
        }
    } else {
        send_json(client, 405, "Method Not Allowed", "{\"error\":\"Method or route not allowed.\"}");
    }
}

int main(int argc, char **argv) {
    int port = 8080;
    if (argc > 1) {
        char *end = NULL;
        long requested = strtol(argv[1], &end, 10);
        if (!end || *end != '\0' || requested < 1 || requested > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return 1;
        }
        port = (int)requested;
    }

    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    int reuse = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "Could not set loopback address.\n");
        close(server);
        return 1;
    }
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 16) < 0) {
        perror("bind/listen");
        close(server);
        return 1;
    }

    reset_game();
    printf("Arcane Labyrinth is ready at http://127.0.0.1:%d\n", port);
    fflush(stdout);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        handle_client(client);
        close(client);
    }
}