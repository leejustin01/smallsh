#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include "smallsh.h"

static bool fgOnly = false; // Foreground only mode flag
static pid_t processes[MAX_BG_PROCESSES]; // array of background process ID's
struct sigaction sa_SIGTSTP, sa_SIGINT; // sigaction structs
pid_t fgpid = -1; // process ID of the current foreground process

// main program loop
void shell() {
    char commandLine[2048];
    Command cmd;
    int exitStatus = 0;

    initprocesses();
    initSignalHandlers();

    while (true) {
        printf(":");
        fflush(stdout);

        // Read the command line
        if (fgets(commandLine, sizeof(commandLine), stdin) == NULL) {
            perror("Error reading input");
            break;
        }

        // remove the new line character at the end so we can parse the string
        size_t length = strlen(commandLine);
        if (length == 0) {
            continue;
        } else if (length > 0 && commandLine[length - 1] == '\n') {
            commandLine[length - 1] = '\0';
        }

        // process the input
        populateCommand(&cmd, commandLine);
        expandPID(&cmd);

        // perform different commands
        if (cmd.name == NULL) {
            reap();
            continue;
        } else if (strcmp(cmd.name, "exit") == 0) {
            exitShell();
        } else if (strcmp(cmd.name, "cd") == 0) {
            cd(cmd.args);
        } else if (strcmp(cmd.name, "status") == 0) {
            // print either exit status or terminating signal
            if (exitStatus != 0 && exitStatus != 1) {
                printf("terminated by signal %d\n", exitStatus);
            } else {
                printf("exit value %d\n", exitStatus);
            }
        } else {
            exitStatus = execCMD(&cmd);
        }

        reap();
    }
}


// Parse the command line and populating the command struct
void populateCommand(Command *cmd, char commandLine[2048]){
    char *token;
    int argIndex = 0;

    cmd->name = NULL;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->background = false;

    if (commandLine[0] == '#') { // handles comments
        return;
    }

    token = strtok(commandLine, " ");
    if (token) {
        cmd->name = strdup(token);
        cmd->args[argIndex++] = cmd->name;
    } else {
        return;
    }

    // tokenize the command line
    while ((token = strtok(NULL, " ")) != NULL) {
        if (strcmp("<", token) == 0) { // check for input file redirection
            token = strtok(NULL, " ");
            if (token)
                cmd->input_file = strdup(token);
        } else if (strcmp(">", token) == 0) { // check for output file redirection
            token = strtok(NULL, " ");
            if (token)
                cmd->output_file = strdup(token);
        } else {
            cmd->args[argIndex++] = strdup(token);
        }
    }

    if (argIndex > 0 && strcmp(cmd->args[argIndex - 1], "&") == 0) { // check the last argument for '&' to set background
        cmd->background = true;
        free(cmd->args[--argIndex]); 
        cmd->args[argIndex] = NULL; 
    } else {
        cmd->args[argIndex] = NULL;
    }
}

// Clean up and exit the shell
void exitShell() {
    int i;
    int exitStatus = 0;
    int wstatus;

    if (fgpid != -1) { // check if there is a foreground process
        if (kill(fgpid, SIGTERM) == -1) { // try killing the foreground process gracefully
            kill(fgpid, SIGKILL);
            exitStatus = 1;
        }
        waitpid(fgpid, &wstatus, 0); // wait on the process so it doesn't become a zombie
        fgpid = -1;
    }

    for (i = 0; i < MAX_BG_PROCESSES; i++) { // loop through all background processes
        if (processes[i] != -1) {
            if (kill(processes[i], SIGTERM) == -1) { // try killing the process gracefully
                kill(processes[i], SIGKILL);
                exitStatus = 1;
            }
            waitpid(processes[i], &wstatus, 0); // wait on the process so it doesn't become a zombie
            processes[i] = -1;
        }
    }

    exit(exitStatus);
}

// Handle the cd command
void cd(char *args[MAX_ARGS + 1]) {
    int result;
    if (args[1] != NULL) { // if a directory was given then try to cd into it
        result = chdir(args[1]);
    } else { // otherwise try to cd into home directory
        char *home_dir = getenv("HOME");
        if (home_dir == NULL) {
            perror("cd");
            return;
        }
        result = chdir(home_dir);
    }

    if (result != 0) {
        perror("cd");
        
    }
}

// Execute all other commands
int execCMD(Command *cmd) {
    pid_t pid = fork(); // fork a child
    int wstatus;
    int options = 0;

    if (pid == -1) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) { // child process

        redirect(&*cmd); // I/O redirection

        if (fgOnly) { // set background to false if the shell is in foreground only mode
            cmd->background = false;
        } 

        // tell foreground processes to use our handler for SIGINT
        // tell background processes to ignore SIGINT
        if (!cmd->background) {
            sa_SIGINT.sa_handler = handleSIGINT;
            sigaction(SIGINT, &sa_SIGINT, NULL);
        } else {
            sa_SIGINT.sa_handler = SIG_IGN;
            sigaction(SIGINT, &sa_SIGINT, NULL);
        }

        // if process is to be ran in the background and no input/output redirection is given, then redirect to /dev/null
        if (cmd->background) {
            if (!cmd->input_file) {
                int devNull = open("/dev/null", O_RDONLY);
                if (devNull == -1 || dup2(devNull, STDIN_FILENO) == -1) {
                    perror("Failed to redirect stdin to /dev/null");
                    
                    exit(1);
                }
                close(devNull);
            }
            if (!cmd->output_file) {
                int devNull = open("/dev/null", O_WRONLY);
                if (devNull == -1 || dup2(devNull, STDOUT_FILENO) == -1) {
                    perror("Failed to redirect stdout to /dev/null");
                    
                    exit(1);
                }
                close(devNull);
            }
        }

        execvp(cmd->name, cmd->args); // execute the command with the args
        perror("exec");
        exit(1);
    } else { // parent process
        if (cmd->background && !fgOnly) { // if command is running in the background and the shell is not in foreground mode
            printf("background pid is %d\n", pid);
            addbg(pid); // add the child pid to the background processes array

        } else { // if the command is not running in the background
            fgpid = pid; // hold the pid for the duration that it is running
            waitpid(pid, &wstatus, options);
            fgpid = -1;
            
            // return the status
            if (WIFEXITED(wstatus)) {
                return WEXITSTATUS(wstatus);
            } else if (WIFSIGNALED(wstatus)) {
                fprintf(stderr, "terminated by signal %d\n", WTERMSIG(wstatus));
                return WTERMSIG(wstatus);
            }
        }

    }
    return 0;
}

// Expand $$ into the pid
int expandPID(Command *cmd) {
    char pid[10];
    int len = sprintf(pid, "%d", getpid());  // Convert PID to string
    
    int curArg = 0;
    char *pos;
    
    while (curArg < 512 && cmd->args[curArg] != NULL) { // loop through all the arguments
        while ((pos = strstr(cmd->args[curArg], "$$")) != NULL) { // look for any occurances of $$

            // Calculate new length including PID replacement
            // - 2 to account for the $$
            // + 1 for the NULL at the end of the string
            int newLen = strlen(cmd->args[curArg]) + len - 2 + 1;
            char *newArg = malloc(newLen);
            
            // Copy up to "$$" position
            strncpy(newArg, cmd->args[curArg], pos - cmd->args[curArg]);
            newArg[pos - cmd->args[curArg]] = '\0';
            
            // Concatenate PID string and the rest of the original argument
            strcat(newArg, pid);
            strcat(newArg, pos + 2);
            
            // Replace original argument with modified one
            free(cmd->args[curArg]);
            cmd->args[curArg] = newArg;
        }
        curArg++;
    }
    
    return 0;  // Return 0 to indicate success
}


// Handle I/O redirection
int redirect(const Command *cmd) {
    // if an input file was specified
    if (cmd->input_file) {
        int inputFD = open(cmd->input_file, O_RDONLY); // open the file
        if (inputFD == -1) {
            perror("Error opening input file");
            _exit(1);
        }
        
        if (dup2(inputFD, STDIN_FILENO) == -1) { // use dup2 to redirect stdin to the input file
            perror("Error redirecting input");
            _exit(1);
        }
        close(inputFD);
    }

    // if an output file was specified
    if (cmd->output_file) {
        int outputFD = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // open the file
        if (outputFD == -1) {
            perror("Error opening output file");
            _exit(1);
        }

        if (dup2(outputFD, STDOUT_FILENO) == -1) { // use dup2 to redirect the stdout to output file
            perror("Error redirecting output");
            _exit(1);
        }
        close(outputFD);
    }
    return 0;
}


// Check if background processes have completed
void reap() {
    int i;
    int wstatus;
    for (i = 0; i < 512; i++) { // loop through processes array
        if (processes[i] == -1) continue; // if the current index does not hold a pid then skip
        if (waitpid(processes[i], &wstatus, WNOHANG) > 0) { // if the process has completed
            // print the completion message
            if (WIFEXITED(wstatus)) {
                printf("background pid %d is done: exit value %d\n", processes[i], WEXITSTATUS(wstatus));
                
            } else if (WIFSIGNALED(wstatus)) {
                printf("background pid %d is done: terminated by signal %d\n", processes[i], WTERMSIG(wstatus));
                
            }
            processes[i] = -1; // reset the value at the current index to -1
        }
    }
}

// Add a pid to the processes array
int addbg(pid_t pid) {

    // find the next open index
    int i = 0;
    while (i < 512 && processes[i] != -1) {
        i++;
    }

    if (i >= 512) { // check if the array is full
        perror("Too many background processes");
        return 1;
    }

    // set the index to the pid
    processes[i] = pid;

    return 0;
}

// Set all values in processes to -1
void initprocesses() {
    int i;
    for (i = 0; i < MAX_BG_PROCESSES; i++) {
        processes[i] = -1;
    }
}

// Signal handler for SIGINT
void handleSIGINT(int sig) {
    pid_t pid = getpid();
    write(STDOUT_FILENO, "\nForeground process %d terminated by SIGINT\n", pid); // we need to use write instead of printf because write is reentrant and printf isn't
    exit(130);
}

// Signal handler for SIGTSTP
void handleSIGTSTP(int sig) {
    if (fgOnly == false) {
        // Change to foreground-only mode
        fgOnly = true;
        write(STDOUT_FILENO, "\nEntering foreground-only mode (& is now ignored)\n", 51); // we need to use write instead of printf because write is reentrant and printf isn't
    } else {
        // Change to normal mode
        fgOnly = false;
        write(STDOUT_FILENO, "\nExiting foreground-only mode (& is now allowed)\n", 49); // we need to use write instead of printf because write is reentrant and printf isn't
    }
    
}

// Register signal handlers
void initSignalHandlers() {

    // Setting up the signals
    sigemptyset(&sa_SIGTSTP.sa_mask);
    sigemptyset(&sa_SIGINT.sa_mask);

    sa_SIGINT.sa_handler = SIG_IGN; // ignore SIGINT
    sa_SIGINT.sa_flags = 0;
    sa_SIGTSTP.sa_handler = handleSIGTSTP;
    sa_SIGTSTP.sa_flags = SA_RESTART;

    // Handles CNTRL C and CNTRL Z
    if (sigaction(SIGINT, &sa_SIGINT, NULL) == -1) {
        perror("Error registering SIGINT handler");  
    }
    if (sigaction(SIGTSTP, &sa_SIGTSTP, NULL) == -1) {
        perror("Error registering SIGTSTP handler");  
    }
}
