#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>

#define LOGHUP_VERSION "0.1"

typedef struct {
    char *path;
    int path_fd;
    int pipe[2];
    int done;
} stdlog_t;

// Globals
stdlog_t out;
stdlog_t err;
int signo = SIGHUP;
pid_t child_pid = 0;
int hupped = 0;
int forward_sig = 0;

// Function prototypes
void parse_args(int argc, char ***argv);
void handle_hup();
int init_log(stdlog_t *log);
void deinit_log(stdlog_t *log);
void init_pipes(int is_child);
pid_t fork_child(char **argv);
int pipe_to_logs();
int read_pipe_into_log(stdlog_t *log);

// Entry point
int main(int argc, char **argv) {
    int wait_status;
    int exit_code;

    // Zero fill structs
    memset(&out, 0, sizeof(stdlog_t));
    memset(&err, 0, sizeof(stdlog_t));

    // Parse args and advance argv to positional args
    parse_args(argc, &argv);

    // Register signal handler
    if (SIG_ERR == signal(signo, handle_hup)) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    // Init logs
    if (!init_log(&out)) {
        fprintf(stderr, "Failed to init stdout log.\n");
        exit(EXIT_FAILURE);
    }
    if (!init_log(&err)) {
        fprintf(stderr, "Failed to init stderr log.\n");
        exit(EXIT_FAILURE);
    }

    // Fork child proc
    child_pid = fork_child(argv);

    // Pipe to files
    exit_code = pipe_to_logs();

    // Wait for child_pid to exit
    if (-1 != waitpid(child_pid, &wait_status, 0)) {
        exit_code |= WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 0;
    } else {
        perror("waitpid(child_pid)");
        exit_code |= 1;
    }
    return exit_code;
}

// Parse command line arguments, and advance argv to positional args
void parse_args(int argc, char ***argv) {
    int c;
    while (-1 != (c = getopt(argc, *argv, "ho:e:s:f"))) {
        switch (c) {
            case 'h':
                printf("loghup version %s\n\n", LOGHUP_VERSION);
                printf("Usage: loghup [options] -- <command>\n\n");
                printf("Options:\n");
                printf("    -h         Show this help\n");
                printf("    -o <path>  Log stdout to <path>\n");
                printf("    -e <path>  Log stderr to <path>\n");
                printf("    -s <sig#>  Trap <sig#> instead of SIGHUP (%d)\n", SIGHUP);
                printf("    -f         Forward signal to child process\n");
                exit(EXIT_SUCCESS);
                break;
            case 'o':
                out.path = optarg;
                break;
            case 'e':
                err.path = optarg;
                break;
            case 's':
                signo = atoi(optarg);
                break;
            case 'f':
                forward_sig = 1;
                break;
        }
    }
    if (!out.path || !err.path) {
        fprintf(stderr, "Expected at least -o and -e. Try -h for help.\n");
        exit(EXIT_FAILURE);
    }
    *argv += optind;
    if (!*argv[0]) {
        fprintf(stderr, "Expected command. Try -h for help.\n");
        exit(EXIT_FAILURE);
    }
}

// Handle SIGHUP (or specificed signal), optionally forwarding to child_pid
void handle_hup() {
    hupped = 1;
    if (forward_sig && child_pid && -1 == kill(child_pid, signo)) {
        perror("kill(child_pid)");
    }
}

// Close log if already open, (re)open log, and initialize pipe if not present.
// Return 1 on success, 0 on error.
int init_log(stdlog_t *log) {
    if (log->path_fd && 0 != close(log->path_fd)) {
        perror("close(log->path_fd)");
        return 0;
    }
    if (-1 == (log->path_fd = open(log->path, O_RDWR|O_CREAT|O_APPEND, 0644))) {
        log->path_fd = 0;
        perror("open(log->path)");
        return 0;
    }
    if (0 == log->pipe[0] && 0 == log->pipe[1] && -1 == pipe(log->pipe)) {
        perror("pipe(log->pipe)");
        return 0;
    }
    return 1;
}

// Close file and pipe if open, and mark done
void deinit_log(stdlog_t *log) {
    if (log->path_fd) {
        close(log->path_fd);
        log->path_fd = 0;
    }
    if (log->pipe[0]) {
        close(log->pipe[0]);
        log->pipe[0] = 0;
    }
    log->done = 1;
}

// Init pipes for parent and child procs
void init_pipes(int is_child) {
    if (is_child) {
        // Close read side of child pipes
        close(out.pipe[0]);
        close(err.pipe[0]);
        // Redirect streams to write side of child pipes
        dup2(out.pipe[1], STDOUT_FILENO);
        dup2(err.pipe[1], STDERR_FILENO);
    }
    // Close write side of pipes (already dup'd for child proc)
    close(out.pipe[1]);
    close(err.pipe[1]);
}

// Fork a child process and initialize pipes
pid_t fork_child(char **argv) {
    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (child_pid == 0) {
        // Child
        init_pipes(1);
        // Exec command
        if (-1 == execvp(argv[0], argv)) {
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    }
    // Parent
    init_pipes(0);
    return child_pid;
}

// Pipe stdout and stderr of child proc to logs
int pipe_to_logs() {
    int exit_code = EXIT_SUCCESS;
    fd_set readfds;
    int rv;
    int nfds;

    while (!out.done || !err.done) {
        // Reinit logs if HUP'd
        if (hupped) {
            init_log(&out);
            init_log(&err);
            hupped = 0;
        }

        // Select on read side of pipes
        FD_ZERO(&readfds);
        nfds = 0;
        if (!out.done) {
            FD_SET(out.pipe[0], &readfds);
            nfds = out.pipe[0] + 1;
        }
        if (!err.done) {
            FD_SET(err.pipe[0], &readfds);
            if (err.pipe[0] + 1 > nfds) {
                nfds = err.pipe[0] + 1;
            }
        }
        rv = select(nfds, &readfds, NULL, NULL, NULL);
        if (-1 == rv) {
            // Select error
            perror("select");
            exit_code = EXIT_FAILURE;
            break;
        } else if (0 == rv) {
            // Nothing ready to read. Retry?
            continue;
        }

        // Read pipes and write to logs
        if (!out.done && FD_ISSET(out.pipe[0], &readfds)) {
            if (!read_pipe_into_log(&out)) {
                exit_code = EXIT_FAILURE;
            }
        }
        if (!err.done && FD_ISSET(err.pipe[0], &readfds)) {
            if (!read_pipe_into_log(&err)) {
                exit_code = EXIT_FAILURE;
            }
        }
    }
    deinit_log(&out);
    deinit_log(&err);
    return exit_code;
}

// Read log->pipe[0] and write to log->path_fd. Return 1 on success, 0 on error.
int read_pipe_into_log(stdlog_t *log) {
    int ok = 1;
    ssize_t nbytes;
    char buf[PIPE_BUF];
    // TODO Try splice http://linux.die.net/man/2/splice
    nbytes = read(log->pipe[0], buf, PIPE_BUF);
    if (-1 == nbytes) {
        // Read error
        ok = 0;
        perror("read(log->pipe[0])");
        goto done;
    } else if (0 == nbytes) {
        // EOF
        goto done;
    } else if (nbytes != write(log->path_fd, buf, nbytes)) {
        // Write error
        ok = 0;
        perror("write(log->path_fd)");
        goto done;
    }
    return ok;
done:
    deinit_log(log);
    return ok;
}
