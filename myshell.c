
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 100
#define MAX_CMDS 20
#define MAX_HISTORY 100

// Structure for storing commands
typedef struct
{
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int append;
} Command;

// Command history
char history[MAX_HISTORY][MAX_LINE];
int history_count = 0;

// Add command to history
void add_history(char *line)
{
    if (history_count < MAX_HISTORY)
    {
        strcpy(history[history_count], line);
        history_count++;
    }
    else
    {
        for (int i = 1; i < MAX_HISTORY; i++)
        {
            strcpy(history[i - 1], history[i]);
        }

        strcpy(history[MAX_HISTORY - 1], line);
    }
}

// Display history
void show_history()
{
    for (int i = 0; i < history_count; i++)
    {
        printf("%d. %s\n", i + 1, history[i]);
    }
}

// Parse command
int parse_command(char *line, Command cmds[],
                  int *background)
{
    char *token;
    int cmd_count = 0;
    int arg_count = 0;

    *background = 0;

    memset(cmds, 0, sizeof(Command) * MAX_CMDS);

    cmds[0].args[0] = NULL;

    token = strtok(line, " \t\n");

    while (token != NULL)
    {
        // Background execution
        if (strcmp(token, "&") == 0)
        {
            if (strtok(NULL, " \t\n") != NULL)
            {
                printf("Error: & must be at the end\n");
                return -1;
            }

            *background = 1;
            break;
        }

        // Pipe
        else if (strcmp(token, "|") == 0)
        {
            if (arg_count == 0 || cmd_count >= MAX_CMDS - 1)
            {
                printf("Invalid pipe\n");
                return -1;
            }

            cmds[cmd_count].args[arg_count] = NULL;

            cmd_count++;
            arg_count = 0;
        }

        // Input redirection
        else if (strcmp(token, "<") == 0)
        {
            token = strtok(NULL, " \t\n");

            if (token == NULL)
            {
                printf("Input file missing\n");
                return -1;
            }

            cmds[cmd_count].input_file = token;
        }

        // Output redirection
        else if (strcmp(token, ">") == 0)
        {
            token = strtok(NULL, " \t\n");

            if (token == NULL)
            {
                printf("Output file missing\n");
                return -1;
            }

            cmds[cmd_count].output_file = token;
            cmds[cmd_count].append = 0;
        }

        // Append redirection
        else if (strcmp(token, ">>") == 0)
        {
            token = strtok(NULL, " \t\n");

            if (token == NULL)
            {
                printf("Output file missing\n");
                return -1;
            }

            cmds[cmd_count].output_file = token;
            cmds[cmd_count].append = 1;
        }

        // Normal command arguments
        else
        {
            if (arg_count >= MAX_ARGS - 1)
            {
                printf("Too many arguments\n");
                return -1;
            }

            cmds[cmd_count].args[arg_count++] = token;
        }

        token = strtok(NULL, " \t\n");
    }

    if (arg_count == 0)
    {
        printf("Invalid or incomplete command\n");
        return -1;
    }

    cmds[cmd_count].args[arg_count] = NULL;

    return cmd_count + 1;
}

// Execute built-in commands
int builtin_command(Command cmd)
{
    if (strcmp(cmd.args[0], "cd") == 0)
    {
        char *path = cmd.args[1];

        if (path == NULL)
        {
            path = getenv("HOME");
        }

        if (path == NULL || chdir(path) != 0)
        {
            perror("cd");
        }

        return 1;
    }

    else if (strcmp(cmd.args[0], "history") == 0)
    {
        show_history();
        return 1;
    }

    else if (strcmp(cmd.args[0], "exit") == 0)
    {
        exit(0);
    }

    return 0;
}

// Execute external commands and pipelines
void execute_commands(Command cmds[],
                      int cmd_count,
                      int background)
{
    int pipes[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];

    // Create pipes
    for (int i = 0; i < cmd_count - 1; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            perror("pipe");
            return;
        }
    }

    // Create child processes
    for (int i = 0; i < cmd_count; i++)
    {
        pids[i] = fork();

        if (pids[i] < 0)
        {
            perror("fork");
            break;
        }

        if (pids[i] == 0)
        {
            // Input from previous pipe
            if (i > 0)
            {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Output to next pipe
            if (i < cmd_count - 1)
            {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Close all pipe descriptors
            for (int j = 0; j < cmd_count - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Input redirection
            if (cmds[i].input_file != NULL)
            {
                int fd = open(cmds[i].input_file, O_RDONLY);

                if (fd < 0)
                {
                    perror("Input file");
                    exit(1);
                }

                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            // Output redirection
            if (cmds[i].output_file != NULL)
            {
                int flags = O_WRONLY | O_CREAT;

                if (cmds[i].append)
                {
                    flags |= O_APPEND;
                }
                else
                {
                    flags |= O_TRUNC;
                }

                int fd = open(cmds[i].output_file,
                              flags, 0644);

                if (fd < 0)
                {
                    perror("Output file");
                    exit(1);
                }

                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            // Execute command
            execvp(cmds[i].args[0], cmds[i].args);

            perror("Command execution failed");
            exit(1);
        }
    }

    // Parent closes all pipe descriptors
    for (int i = 0; i < cmd_count - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for foreground processes
    if (!background)
    {
        for (int i = 0; i < cmd_count; i++)
        {
            if (pids[i] > 0)
            {
                waitpid(pids[i], NULL, 0);
            }
        }
    }
    else
    {
        printf("[Background process started]\n");
    }
}

// Main function
int main()
{
    char line[MAX_LINE];
    char original[MAX_LINE];

    Command cmds[MAX_CMDS];

    int cmd_count;
    int background;

    printf("\n");
    printf("====================================\n");
    printf("       WELCOME TO MYSHELL\n");
    printf("====================================\n");

    while (1)
    {
        // Reap completed background processes
        while (waitpid(-1, NULL, WNOHANG) > 0)
        {
        }

        // Display prompt
        printf("MyShell> ");
        fflush(stdout);

        // Read user command
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            printf("\n");
            break;
        }

        // Ignore empty commands
        if (line[0] == '\n')
        {
            continue;
        }

        // Save original command
        strcpy(original, line);

        // Add command to history
        add_history(original);

        // Parse command
        cmd_count = parse_command(line, cmds, &background);

        if (cmd_count <= 0)
        {
            continue;
        }

        // Execute built-in commands
        if (cmd_count == 1 && !background &&
            cmds[0].input_file == NULL &&
            cmds[0].output_file == NULL)
        {
            if (builtin_command(cmds[0]))
            {
                continue;
            }
        }

        // Execute external command
        execute_commands(cmds, cmd_count, background);
    }

    return 0;
}
