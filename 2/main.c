#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include "Vector/Vector.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct Cmd_ {
    char *argv[128];
    int last_elem;
    int background; // if 1 - is background
    int mode_write;
    char *write_to_file;
} typedef Cmd;

void init_cmd(Cmd *cmd) {
//    init_vector(&cmd->argv, sizeof(char *));
    cmd->background = 0;
    cmd->last_elem = 0;
    cmd->write_to_file = NULL;
    cmd->mode_write = 0;
}

enum NextCommand_ {
    PIPE,
    OR,
    AND,
    NONE
} typedef NextCommand;

struct LineCmd_ {
    Vector/*<Cmd>*/ cmds;
    Vector/*<NextCommand*>*/ nexts;
} typedef LineCmd;

void init_line_cmd(LineCmd *lineCmd) {
    init_vector(&lineCmd->cmds, sizeof(Cmd));
    init_vector(&lineCmd->nexts, sizeof(NextCommand *));
}

void write_to_string(char *string, char x) {
    size_t old_len = strlen(string);
    string[old_len] = x;
    string[old_len + 1] = '\0';
}

NextCommand *make_command(NextCommand n) {
    NextCommand *nextCommand = calloc(1, sizeof(NextCommand));
    *nextCommand = n;
    return nextCommand;
}

#define add_command_and_clear(cmd, lineCmd, string, first_command)      \
    if (write_to_file) {                                                \
        Cmd temp = *(Cmd*)get(&lineCmd.cmds, lineCmd.cmds.size - 1);    \
        temp.write_to_file = strdup(string);                            \
        temp.mode_write = write_to_file;                                \
        set(&lineCmd.cmds, lineCmd.cmds.size - 1, &temp);               \
    } else {                                                            \
        if (!was_space){                                                \
            cmd.argv[cmd.last_elem++] = strdup(string);                 \
        }                                                               \
        push_back(&lineCmd.cmds, (void*)&cmd);                          \
        init_cmd(&cmd);                                                 \
        first_command = 0;                                              \
        string[0] = '\0';                                               \
        was_space = 0;                                                  \
    }


#define reread_line(line)   \
i = -1;                     \
free(line);                 \
size_t sz = 0;              \
getline(&line, &sz, stdin);

LineCmd parse(char *line) {
    LineCmd lineCmd;
    init_line_cmd(&lineCmd);

    Cmd cmd;
    init_cmd(&cmd);

    char string[1024];
    string[0] = '\0';

    int first_command = 0;
    int was_space = 0;

    int single_quote_is_open = 0;
    int double_quote_is_open = 0;

    int write_to_file = 0; // 1 is >
    // 2 is >>

    int comment = 0;

    for (int i = 0; i < strlen(line); ++i) {
        if (comment) {
            break;
        }
        switch (line[i]) {
            case '#': {
                comment = 1;
                break;
            }
            case '&': {
                if (single_quote_is_open == 0 && double_quote_is_open == 0) {
                    if (i + 1 < strlen(line) && line[i + 1] == '&') { // &&
                        push_back(&lineCmd.nexts, (void *) make_command(AND));
                    } else { // &
                        cmd.background = 1;
                    }

                    add_command_and_clear(cmd, lineCmd, string, first_command);
                } else {
                    write_to_string(string, line[i]);
                }
                break;
            }
            case '|': {
                if (single_quote_is_open == 0 && double_quote_is_open == 0) {
                    if (i + 1 < strlen(line) && line[i + 1] == '|') { // ||
                        push_back(&lineCmd.nexts, (void *) make_command(OR));
                        ++i;
                    } else { // |
                        push_back(&lineCmd.nexts, (void *) make_command(PIPE));
                    }

                    add_command_and_clear(cmd, lineCmd, string, first_command);
                } else {
                    write_to_string(string, line[i]);
                }
                break;
            }
            case '>': {
                if (single_quote_is_open == 0 && double_quote_is_open == 0) {
                    if (i + 1 < strlen(line) && line[i + 1] == '>') { // >>
                        write_to_file = 2;
                        ++i;
                    } else { // >
                        write_to_file = 1;
                    }
                    first_command = 0;
                } else {
                    write_to_string(string, line[i]);
                }
                break;
            }
            case '\'': {
                if (double_quote_is_open == 0) {
                    single_quote_is_open = 1 - single_quote_is_open;
                } else write_to_string(string, line[i]);
                break;
            }
            case '\"': {
                if (single_quote_is_open == 0) {
                    double_quote_is_open = 1 - double_quote_is_open;
                } else write_to_string(string, line[i]);
                break;
            }
            case '\\': {
                if (i + 1 < strlen(line)) {
                    if (line[i + 1] == '\\' || line[i + 1] == ' ') {
                        write_to_string(string, line[++i]);
                    } else if (
                            single_quote_is_open == 1 && line[i + 1] == '\'' ||
                            double_quote_is_open == 1 && line[i + 1] == '\"') {
                        write_to_string(string, line[i++]);
                        write_to_string(string, line[i]);
                    } else if (
                            single_quote_is_open == 0 && double_quote_is_open == 0 && line[i + 1] == '\n'
                            ) {
                        reread_line(line);
                    }
                }
                break;
            }
            case ' ': {
                was_space = 1;
                if (single_quote_is_open == 1 || double_quote_is_open == 1) {
                    write_to_string(string, line[i]);
                } else if (first_command) {
                    cmd.argv[cmd.last_elem++] = strdup(string);
                    string[0] = '\0';
                }
                break;
            }
            case '\n': {
                if (single_quote_is_open || double_quote_is_open) {
                    write_to_string(string, line[i]);
                    reread_line(line);
                }
                break;
            }
            default: {
                was_space = 0;
                first_command = 1;
                write_to_string(string, line[i]);
                break;
            }
        }
    }

    if (write_to_file) {
        cmd.mode_write = write_to_file;
        cmd.write_to_file = strdup(string);
    } else if (first_command) {
        cmd.argv[cmd.last_elem++] = strdup(string);
    }

//    add_command_and_clear(cmd, lineCmd, string, first_command);
    push_back(&lineCmd.cmds, (void *) &cmd);
    push_back(&lineCmd.nexts, (void *) make_command(NONE));
    return lineCmd;
}

char *read_line() {
    char *line = NULL;
    size_t bufferSize = 0;

    if (getline(&line, &bufferSize, stdin) == -1) {
        exit(EXIT_FAILURE);
    }

//    line[strlen(line) - 1] = '\0';
    return line;
}

void print_line_cmd(LineCmd lineCmd) {
    for (int i = 0; i < lineCmd.cmds.size; ++i) {
        Cmd *cmd = (Cmd *) get(&lineCmd.cmds, i);
        printf("Name: %s\nArgv:\n", cmd->argv[0]);
        for (int j = 0; j < cmd->last_elem; ++j) {
            printf("!\t%s\n", cmd->argv[j]);
        }

        printf("Mode write to file: %d\n", cmd->mode_write);
        if (cmd->mode_write) {
            printf("Filename: %s\n", cmd->write_to_file);
        }
    }
}

void execute_chdir(Cmd cmd) {
    if (strcmp(cmd.argv[0], "cd") == 0) {
        char *path = cmd.argv[1];
        if (chdir(path) != 0) {
            printf("cd: %s: No such file or directory\n", path);
        }
        return;
    }

}

#define NEED_WRITE(current_cmd)                                 \
if (current_cmd->mode_write != 0) {                             \
    int flags = O_WRONLY | O_CREAT;                             \
    int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH; \
    if (current_cmd->mode_write == 1) {                         \
        flags |= O_TRUNC;                                       \
    } else {                                                    \
        flags |= O_APPEND;                                      \
    }                                                           \
    int filedf = open(                                          \
                    current_cmd->write_to_file,                 \
                    flags, mode);                               \
    dup2(filedf, STDOUT_FILENO);                                \
}

void execute_line_cmd(LineCmd lineCmd) {
    for (int i = 0; i < lineCmd.cmds.size; ++i) {
        NextCommand nextCommand = *(NextCommand *) get(&lineCmd.nexts, i);
        {
            Cmd *temp_cmd = (Cmd *) get(&lineCmd.cmds, i);
            execute_chdir(*temp_cmd);
        }
        if (nextCommand == PIPE) {
            int current = i;
            int end = i + 1;

//            int stdin_dup = dup(STDIN_FILENO);
//            int stdout_dup = dup(STDOUT_FILENO);

            while (end < lineCmd.nexts.size && *(NextCommand *) get(&lineCmd.nexts, end) == PIPE) ++end;

            int fd[2], prev_fd[2];
            pipe(fd);

            pid_t *pids = calloc(end - current + 1, sizeof(pid_t));
            for (; current <= end; ++current) {
                prev_fd[0] = fd[0];
                prev_fd[1] = fd[1];
                pipe(fd);

                if ((pids[current - i] = fork()) == 0) {
                    Cmd *current_cmd = (Cmd *) get(&lineCmd.cmds, current);

                    if (current == i) {
                        close(fd[0]);
                        dup2(fd[1], STDOUT_FILENO);

//                        close(fd[1]);
                        close(prev_fd[0]);
                        close(prev_fd[1]);
                    } else if (current == end) {
                        close(prev_fd[1]);
                        dup2(prev_fd[0], STDIN_FILENO);

//                        close(prev_fd[0]);
                        close(fd[0]);
                        close(fd[1]);
                        NEED_WRITE(current_cmd);
                    } else {
                        close(prev_fd[1]);
                        close(fd[0]);

                        dup2(prev_fd[0], STDIN_FILENO);
                        dup2(fd[1], STDOUT_FILENO);

//                        close(prev_fd[0]);
//                        close(fd[1]);
                    }

                    current_cmd->argv[current_cmd->last_elem] = NULL;
                    execvp(current_cmd->argv[0], current_cmd->argv);
                    exit(1);
                }
                close(prev_fd[0]);
                close(prev_fd[1]);
            }
            for (int j = 0; j <= end - i; ++j) {
                waitpid(pids[j], NULL, 0);
            }
            close(fd[0]);
            close(fd[1]);
            i = end;
//            dup2(stdin_dup, STDIN_FILENO);
//            dup2(stdout_dup, STDOUT_FILENO);

        } else if (nextCommand == NONE) {

            if (fork() == 0) {
                Cmd *current_cmd = (Cmd *) get(&lineCmd.cmds, i);

                NEED_WRITE(current_cmd);

                current_cmd->argv[current_cmd->last_elem] = NULL;
                execvp(current_cmd->argv[0], current_cmd->argv);
                exit(1);
            }
            wait(NULL);
        }
    }
}

int main() {

    while (1) {
//        printf("$> ");

        char *line = read_line();
        LineCmd lineCmd = parse(line);

//        print_line_cmd(lineCmd);
        execute_line_cmd(lineCmd);
    }
}
