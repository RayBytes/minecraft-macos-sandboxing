#define _DARWIN_C_SOURCE 1

#include <mach-o/dyld.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static bool unsafe_environment_key(const char *entry) {
    static const char *prefixes[] = {
        "DYLD_", "LD_", "PYTHON", "JAVA_TOOL_OPTIONS=", "JDK_JAVA_OPTIONS=", "_JAVA_OPTIONS=",
        "CLASSPATH=", "BASH_ENV=", "ENV=", "ZDOTDIR=", "RUBYOPT=", "PERL5OPT=", "NODE_OPTIONS=",
        "HTTP_PROXY=", "HTTPS_PROXY=", "ALL_PROXY=", "NO_PROXY=", "http_proxy=", "https_proxy=",
        "all_proxy=", "no_proxy=", "SSL_CERT_FILE=", "SSL_CERT_DIR=", "CURL_CA_BUNDLE=", "CURL_HOME=",
        "NETRC=", "SSLKEYLOGFILE=",
    };
    for (size_t index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++) {
        if (strncmp(entry, prefixes[index], strlen(prefixes[index])) == 0) {
            return true;
        }
    }
    return false;
}

static void sanitize_environment(void) {
    for (char **cursor = environ; *cursor != NULL;) {
        if (unsafe_environment_key(*cursor)) {
            char *equals = strchr(*cursor, '=');
            if (equals != NULL) {
                size_t size = (size_t)(equals - *cursor);
                char *key = strndup(*cursor, size);
                if (key == NULL) {
                    fputs("mcguard-entry: out of memory\n", stderr);
                    exit(70);
                }
                (void)unsetenv(key);
                free(key);
                cursor = environ;
                continue;
            }
        }
        cursor++;
    }
}

int main(int argc, char **argv) {
    uint32_t executable_size = PATH_MAX;
    char executable[PATH_MAX];
    if (_NSGetExecutablePath(executable, &executable_size) != 0) {
        fputs("mcguard-entry: executable path is too long\n", stderr);
        return 70;
    }
    char resolved[PATH_MAX];
    if (realpath(executable, resolved) == NULL) {
        fprintf(stderr, "mcguard-entry: cannot resolve executable path: %s\n", strerror(errno));
        return 70;
    }
    char *last_slash = strrchr(resolved, '/');
    if (last_slash == NULL) {
        return 70;
    }
    *last_slash = '\0';
    last_slash = strrchr(resolved, '/');
    if (last_slash == NULL) {
        return 70;
    }
    *last_slash = '\0';

    char script[PATH_MAX];
    int written = snprintf(script, sizeof(script), "%s/bin/mc-sandbox-wrapper", resolved);
    if (written < 0 || (size_t)written >= sizeof(script) || access(script, R_OK) != 0) {
        fputs("mcguard-entry: cannot locate policy wrapper\n", stderr);
        return 70;
    }

    char **python_args = calloc((size_t)argc + 3, sizeof(char *));
    if (python_args == NULL) {
        fputs("mcguard-entry: out of memory\n", stderr);
        return 70;
    }
    python_args[0] = "/usr/bin/python3";
    python_args[1] = "-I";
    python_args[2] = script;
    for (int index = 1; index < argc; index++) {
        python_args[index + 2] = argv[index];
    }
    python_args[argc + 2] = NULL;

    sanitize_environment();
    execve(python_args[0], python_args, environ);
    fprintf(stderr, "mcguard-entry: cannot start isolated policy wrapper: %s\n", strerror(errno));
    return 70;
}
