#ifndef SMALLSH_HEADER
#define SMALLSH_HEADER

#include <stdbool.h>
#include <sys/types.h>

#define MAX_ARGS 512
#define MAX_BG_PROCESSES 512

typedef struct {
    char *name; // name of the command e.g. cd                 
    char *args[MAX_ARGS + 1]; // array that holds the arguments     
    char *input_file; // input file name             
    char *output_file; // output file name            
    bool background; // whether or not to run this command in the background
} Command;

void shell();
void populateCommand(Command *cmd, char commandLine[2048]);
void exitShell();
void cd(char *args[MAX_ARGS + 1]);
int execCMD(Command *cmd);
int expandPID(Command *cmd);
int redirect(const Command *cmd);
void reap();
int addbg(pid_t pid);
void initprocesses();
void handleSIGINT(int sig);
void handleSIGTSTP(int sig);
void initSignalHandlers();

#endif
