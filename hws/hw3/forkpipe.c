// Homework number 2 - forkpipe for OSY
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

pid_t gen_pid = 0;

int pipe_fd[2];

void gen_handler(int sig) {
    const char msg[] = "GEN TERMINATED\n";
    write(STDERR_FILENO, msg, sizeof(msg)-1);

    _exit(0);
}

int main() {
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(2);
    }

    gen_pid = fork();
    if (gen_pid < 0) {
        perror("fork");
        exit(2);
    }

    if (gen_pid == 0) {
        signal(SIGTERM, gen_handler);

        close(pipe_fd[0]);

        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);

        srand(getpid());

        while (1) {
            int a = rand() % 4096;
            int b = rand() % 4096;
            fprintf(stdout, "%d %d\n", a, b);
            fflush(stdout);
            sleep(1);
        }
        _exit(0);
    }

    pid_t nsd_pid = fork();
    if (nsd_pid < 0) {
        perror("fork");
        exit(2);
    }

    if (nsd_pid == 0) {
        close(pipe_fd[1]);

        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);

        execl("./nsd", "nsd", NULL);

        perror("execl");
        exit(2);
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);

    sleep(5);

    kill(gen_pid, SIGTERM);

    int status_gen, status_nsd;
    waitpid(gen_pid, &status_gen, 0);
    waitpid(nsd_pid, &status_nsd, 0);

    if ((WIFEXITED(status_gen) && WEXITSTATUS(status_gen) != 0) ||
        (WIFEXITED(status_nsd) && WEXITSTATUS(status_nsd) != 0)) {
        fprintf(stdout, "ERROR\n");
        return 1;
    }

    // OK
    fprintf(stdout, "OK\n");
    return 0;
}