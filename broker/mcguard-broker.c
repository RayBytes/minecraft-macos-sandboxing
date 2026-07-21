#define _DARWIN_C_SOURCE 1

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define TOKEN_LIMIT 16384
#define REQUEST_LIMIT 512
#define MAX_JOIN_REQUESTS 8
#define MAX_KEYPAIR_REQUESTS 3
#define CERTIFICATE_LIMIT (64 * 1024)
#define JOIN_URL "https://sessionserver.mojang.com/session/minecraft/join"
#define CERTIFICATE_URL "https://api.minecraftservices.com/player/certificates"

struct options {
    int token_fd;
    int stdin_fd;
    const char *profile;
    const char *agent;
    const char *uuid;
    bool keep_profile;
    int command_index;
};

static volatile sig_atomic_t child_pid = -1;

static void wipe(void *data, size_t size) {
    if (data != NULL && size > 0) {
        volatile unsigned char *cursor = data;
        while (size-- > 0) {
            *cursor++ = 0;
        }
    }
}

static void die(const char *message) {
    fprintf(stderr, "mcguard-broker: %s\n", message);
    exit(70);
}

static void die_errno(const char *message) {
    fprintf(stderr, "mcguard-broker: %s: %s\n", message, strerror(errno));
    exit(70);
}

static void forward_signal(int signal_number) {
    pid_t pid = (pid_t)child_pid;
    if (pid > 0) {
        (void)kill(pid, signal_number);
    }
}

static struct options parse_options(int argc, char **argv) {
    struct options out = {.token_fd = -1, .stdin_fd = -1, .profile = NULL, .agent = NULL, .uuid = NULL,
                          .keep_profile = false, .command_index = -1};
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--") == 0) {
            out.command_index = index + 1;
            break;
        } else if (strcmp(argv[index], "--keep-profile") == 0) {
            out.keep_profile = true;
        } else if (index + 1 < argc && strcmp(argv[index], "--token-fd") == 0) {
            out.token_fd = atoi(argv[++index]);
        } else if (index + 1 < argc && strcmp(argv[index], "--stdin-fd") == 0) {
            out.stdin_fd = atoi(argv[++index]);
        } else if (index + 1 < argc && strcmp(argv[index], "--profile") == 0) {
            out.profile = argv[++index];
        } else if (index + 1 < argc && strcmp(argv[index], "--agent") == 0) {
            out.agent = argv[++index];
        } else if (index + 1 < argc && strcmp(argv[index], "--uuid") == 0) {
            out.uuid = argv[++index];
        } else {
            die("invalid arguments");
        }
    }
    if (out.token_fd < 3 || out.profile == NULL || out.agent == NULL || out.uuid == NULL
            || out.command_index < 0 || out.command_index >= argc) {
        die("missing required launch arguments");
    }
    return out;
}

static bool normalize_uuid(const char *input, char output[33]) {
    size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)input; *cursor != '\0'; cursor++) {
        if (*cursor == '-') {
            continue;
        }
        bool hex = (*cursor >= '0' && *cursor <= '9') || (*cursor >= 'a' && *cursor <= 'f')
            || (*cursor >= 'A' && *cursor <= 'F');
        if (!hex || used >= 32) {
            return false;
        }
        output[used++] = (*cursor >= 'A' && *cursor <= 'F') ? (char)(*cursor + ('a' - 'A')) : (char)*cursor;
    }
    output[used] = '\0';
    return used == 32;
}

static char *read_token(int fd, size_t *token_size) {
    char *token = calloc(TOKEN_LIMIT + 1, 1);
    if (token == NULL) {
        die("out of memory");
    }
    size_t used = 0;
    while (used < TOKEN_LIMIT) {
        ssize_t count = read(fd, token + used, TOKEN_LIMIT - used);
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            die_errno("cannot read credential pipe");
        }
        used += (size_t)count;
    }
    (void)close(fd);
    if (used == 0 || used == TOKEN_LIMIT || memchr(token, '\0', used) != NULL
            || memchr(token, '\n', used) != NULL || memchr(token, '\r', used) != NULL) {
        wipe(token, TOKEN_LIMIT + 1);
        free(token);
        die("invalid credential received");
    }
    *token_size = used;
    (void)mlock(token, TOKEN_LIMIT + 1);
    return token;
}

static bool valid_server_hash(const char *value) {
    size_t size = strlen(value);
    if (size == 0 || size > 41) {
        return false;
    }
    for (size_t index = 0; index < size; index++) {
        char character = value[index];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')
                || (index == 0 && character == '-'))) {
            return false;
        }
    }
    return true;
}

static char *json_escape(const char *value) {
    size_t size = strlen(value);
    char *escaped = calloc(size * 6 + 1, 1);
    if (escaped == NULL) {
        return NULL;
    }
    char *output = escaped;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        switch (*cursor) {
            case '"': *output++ = '\\'; *output++ = '"'; break;
            case '\\': *output++ = '\\'; *output++ = '\\'; break;
            case '\b': *output++ = '\\'; *output++ = 'b'; break;
            case '\f': *output++ = '\\'; *output++ = 'f'; break;
            case '\n': *output++ = '\\'; *output++ = 'n'; break;
            case '\r': *output++ = '\\'; *output++ = 'r'; break;
            case '\t': *output++ = '\\'; *output++ = 't'; break;
            default:
                if (*cursor < 0x20) {
                    (void)snprintf(output, 7, "\\u%04x", *cursor);
                    output += 6;
                } else {
                    *output++ = (char)*cursor;
                }
        }
    }
    *output = '\0';
    return escaped;
}

static size_t discard_response(char *data, size_t size, size_t count, void *context) {
    (void)data;
    (void)context;
    return size * count;
}

struct response_buffer {
    char *data;
    size_t size;
};

static size_t capture_response(char *data, size_t size, size_t count, void *context) {
    struct response_buffer *response = context;
    size_t incoming = size * count;
    if (incoming > CERTIFICATE_LIMIT - response->size) {
        return 0;
    }
    memcpy(response->data + response->size, data, incoming);
    response->size += incoming;
    return incoming;
}

static bool configure_secure_curl(CURL *curl) {
    return curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_USERAGENT, "MCGuard/0.1") == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_PROXY, "") == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK;
}

static bool authenticate_join(const char *token, const char *uuid, const char *server_hash) {
    char *escaped_token = json_escape(token);
    if (escaped_token == NULL) {
        return false;
    }
    size_t body_size = strlen(escaped_token) + strlen(uuid) + strlen(server_hash) + 96;
    char *body = calloc(body_size, 1);
    if (body == NULL) {
        wipe(escaped_token, strlen(escaped_token));
        free(escaped_token);
        return false;
    }
    (void)snprintf(body, body_size,
        "{\"accessToken\":\"%s\",\"selectedProfile\":\"%s\",\"serverId\":\"%s\"}",
        escaped_token, uuid, server_hash);

    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    bool ok = false;
    if (curl != NULL) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        (void)curl_easy_setopt(curl, CURLOPT_URL, JOIN_URL);
        (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        (void)curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        (void)configure_secure_curl(curl);
        (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
        CURLcode result = curl_easy_perform(curl);
        long status = 0;
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        ok = result == CURLE_OK && status == 204;
        if (!ok) {
            fprintf(stderr, "[MCGuard] Session authentication failed (HTTP %ld, curl %d); response discarded.\n",
                status, (int)result);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    wipe(body, body_size);
    wipe(escaped_token, strlen(escaped_token));
    free(body);
    free(escaped_token);
    return ok;
}

static bool fetch_key_pair(const char *token, char **json, size_t *json_size) {
    struct response_buffer response = {.data = calloc(CERTIFICATE_LIMIT + 1, 1), .size = 0};
    if (response.data == NULL) {
        return false;
    }
    size_t authorization_size = strlen(token) + 23;
    char *authorization = calloc(authorization_size, 1);
    if (authorization == NULL) {
        free(response.data);
        return false;
    }
    (void)snprintf(authorization, authorization_size, "Authorization: Bearer %s", token);

    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    bool ok = false;
    if (curl != NULL) {
        headers = curl_slist_append(headers, authorization);
        headers = curl_slist_append(headers, "Content-Type: application/json");
        (void)curl_easy_setopt(curl, CURLOPT_URL, CERTIFICATE_URL);
        (void)curl_easy_setopt(curl, CURLOPT_POST, 1L);
        (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        (void)curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        (void)configure_secure_curl(curl);
        (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_response);
        (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode result = curl_easy_perform(curl);
        long status = 0;
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        ok = result == CURLE_OK && status == 200 && response.size > 0;
        if (!ok) {
            fprintf(stderr, "[MCGuard] Profile certificate request failed (HTTP %ld, curl %d); response discarded.\n",
                status, (int)result);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    wipe(authorization, authorization_size);
    free(authorization);
    if (!ok) {
        wipe(response.data, CERTIFICATE_LIMIT + 1);
        free(response.data);
        return false;
    }
    *json = response.data;
    *json_size = response.size;
    return true;
}

static char *base64_encode(const unsigned char *input, size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_size = 4 * ((size + 2) / 3);
    char *output = calloc(output_size + 1, 1);
    if (output == NULL) {
        return NULL;
    }
    size_t in = 0;
    size_t out = 0;
    while (in < size) {
        uint32_t first = input[in++];
        uint32_t second = in < size ? input[in++] : 0;
        uint32_t third = in < size ? input[in++] : 0;
        uint32_t combined = (first << 16) | (second << 8) | third;
        output[out++] = alphabet[(combined >> 18) & 0x3f];
        output[out++] = alphabet[(combined >> 12) & 0x3f];
        output[out++] = alphabet[(combined >> 6) & 0x3f];
        output[out++] = alphabet[combined & 0x3f];
    }
    size_t remainder = size % 3;
    if (remainder > 0) {
        output[output_size - 1] = '=';
        if (remainder == 1) {
            output[output_size - 2] = '=';
        }
    }
    return output;
}

static void write_all(int fd, const char *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t count = write(fd, data + written, size - written);
        if (count > 0) {
            written += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static int create_listener(char directory[64], char socket_path[104]) {
    (void)strcpy(directory, "/private/tmp/mcguard.XXXXXX");
    if (mkdtemp(directory) == NULL) {
        die_errno("cannot create broker directory");
    }
    (void)chmod(directory, 0700);
    int written = snprintf(socket_path, 104, "%s/broker.sock", directory);
    if (written < 0 || written >= 104) {
        die("broker socket path is too long");
    }

    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) {
        die_errno("cannot create broker socket");
    }
    (void)fcntl(listener, F_SETFD, FD_CLOEXEC);
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    (void)strcpy(address.sun_path, socket_path);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 4) != 0) {
        die_errno("cannot listen on broker socket");
    }
    (void)chmod(socket_path, 0600);
    return listener;
}

static pid_t launch_game(const struct options *options, int argc, char **argv, const char *socket_path) {
    const char *attach_flag = "-XX:+DisableAttachMechanism";
    size_t agent_size = strlen(options->agent) + strlen(socket_path) + 13;
    char *agent_flag = calloc(agent_size, 1);
    if (agent_flag == NULL) {
        die("out of memory");
    }
    (void)snprintf(agent_flag, agent_size, "-javaagent:%s=%s", options->agent, socket_path);

    int command_count = argc - options->command_index;
    char **child_args = calloc((size_t)command_count + 7, sizeof(char *));
    if (child_args == NULL) {
        die("out of memory");
    }
    int output = 0;
    child_args[output++] = "/usr/bin/sandbox-exec";
    child_args[output++] = "-f";
    child_args[output++] = (char *)options->profile;
    child_args[output++] = argv[options->command_index];
    child_args[output++] = (char *)attach_flag;
    child_args[output++] = agent_flag;
    for (int index = options->command_index + 1; index < argc; index++) {
        child_args[output++] = argv[index];
    }
    child_args[output] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        die_errno("cannot launch protected JVM");
    }
    if (pid == 0) {
        if (options->stdin_fd >= 0 && dup2(options->stdin_fd, STDIN_FILENO) < 0) {
            fprintf(stderr, "mcguard-broker: cannot attach sanitized Prism protocol: %s\n", strerror(errno));
            _exit(70);
        }
        if (options->stdin_fd > STDERR_FILENO) {
            (void)close(options->stdin_fd);
        }
        execv(child_args[0], child_args);
        fprintf(stderr, "mcguard-broker: cannot execute sandboxed JVM: %s\n", strerror(errno));
        _exit(70);
    }
    free(child_args);
    free(agent_flag);
    return pid;
}

static bool peer_is_game(int client, pid_t expected_pid) {
    pid_t peer_pid = -1;
    socklen_t peer_pid_size = sizeof(peer_pid);
    return getsockopt(client, SOL_LOCAL, LOCAL_PEERPID, &peer_pid, &peer_pid_size) == 0
        && peer_pid == expected_pid;
}

static bool handle_client(int client, const char *token, const char *expected_uuid,
                          unsigned int *join_requests, unsigned int *keypair_requests) {
    struct timeval timeout = {.tv_sec = 25, .tv_usec = 0};
    (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char request[REQUEST_LIMIT + 1] = {0};
    size_t size = 0;
    while (size < REQUEST_LIMIT) {
        ssize_t count = read(client, request + size, REQUEST_LIMIT - size);
        if (count > 0) {
            size += (size_t)count;
            if (memchr(request, '\n', size) != NULL) {
                break;
            }
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
    const char *simple_response = "ERR\n";
    bool handled = false;
    if (size > 0 && size < REQUEST_LIMIT && request[size - 1] == '\n'
            && memchr(request, '\n', size - 1) == NULL) {
        request[size - 1] = '\0';
        if (strcmp(request, "KEYPAIR") == 0 && (*keypair_requests)++ < MAX_KEYPAIR_REQUESTS) {
            char *json = NULL;
            size_t json_size = 0;
            if (fetch_key_pair(token, &json, &json_size)) {
                char *encoded = base64_encode((const unsigned char *)json, json_size);
                if (encoded != NULL) {
                    write_all(client, "DATA\t", 5);
                    write_all(client, encoded, strlen(encoded));
                    write_all(client, "\n", 1);
                    wipe(encoded, strlen(encoded));
                    free(encoded);
                    handled = true;
                }
                wipe(json, CERTIFICATE_LIMIT + 1);
                free(json);
            }
        } else {
            char *save = NULL;
            char *verb = strtok_r(request, "\t", &save);
            char *uuid_input = strtok_r(NULL, "\t", &save);
            char *server_hash = strtok_r(NULL, "\t", &save);
            char *extra = strtok_r(NULL, "\t", &save);
            char uuid[33] = {0};
            if (verb != NULL && strcmp(verb, "JOIN") == 0 && (*join_requests)++ < MAX_JOIN_REQUESTS
                    && uuid_input != NULL && normalize_uuid(uuid_input, uuid)
                    && strcmp(uuid, expected_uuid) == 0 && server_hash != NULL
                    && valid_server_hash(server_hash) && extra == NULL) {
                simple_response = authenticate_join(token, expected_uuid, server_hash) ? "OK\n" : "ERR\n";
            }
        }
    }
    if (!handled) {
        write_all(client, simple_response, strlen(simple_response));
    }
    wipe(request, sizeof(request));
    return handled;
}

int main(int argc, char **argv) {
    struct options options = parse_options(argc, argv);
    char expected_uuid[33] = {0};
    if (!normalize_uuid(options.uuid, expected_uuid)) {
        die("invalid Minecraft profile UUID");
    }

    struct rlimit no_core = {.rlim_cur = 0, .rlim_max = 0};
    (void)setrlimit(RLIMIT_CORE, &no_core);
    size_t token_size = 0;
    char *token = read_token(options.token_fd, &token_size);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        die("cannot initialize HTTPS client");
    }

    char directory[64] = {0};
    char socket_path[104] = {0};
    int listener = create_listener(directory, socket_path);
    signal(SIGINT, forward_signal);
    signal(SIGTERM, forward_signal);
    pid_t game = launch_game(&options, argc, argv, socket_path);
    if (options.stdin_fd >= 0) {
        (void)close(options.stdin_fd);
    }
    child_pid = game;

    int status = 70 << 8;
    unsigned int join_requests = 0;
    unsigned int keypair_requests = 0;
    for (;;) {
        pid_t waited = waitpid(game, &status, WNOHANG);
        if (waited == game) {
            break;
        }
        if (waited < 0 && errno != EINTR) {
            break;
        }
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listener, &read_set);
        struct timeval wait_time = {.tv_sec = 0, .tv_usec = 250000};
        int selected = select(listener + 1, &read_set, NULL, NULL, &wait_time);
        if (selected > 0 && FD_ISSET(listener, &read_set)) {
            int client = accept(listener, NULL, NULL);
            if (client >= 0) {
                if (!peer_is_game(client, game)) {
                    (void)write(client, "ERR\n", 4);
                } else {
                    (void)handle_client(client, token, expected_uuid, &join_requests, &keypair_requests);
                }
                (void)close(client);
            }
        }
    }

    child_pid = -1;
    (void)close(listener);
    (void)unlink(socket_path);
    (void)rmdir(directory);
    if (!options.keep_profile) {
        (void)unlink(options.profile);
    }
    wipe(token, TOKEN_LIMIT + 1);
    (void)munlock(token, TOKEN_LIMIT + 1);
    free(token);
    wipe(expected_uuid, sizeof(expected_uuid));
    curl_global_cleanup();

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 70;
}
