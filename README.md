# smallsh
This is a portfolio project for Operating Systems 1.

Smallsh implements a subset of features of well-known shells, such as bash. This program:

  - Provides a prompt for running commands
  - Handles blank lines and comments, which are lines beginning with the # character
  - Provides expansion for the variable $$
  - Executes the 3 commands exit, cd, and status via code built into the shell
  - Executes other commands by creating new processes using a function from the exec family of functions
  - Supports input and output redirection
  - Supports running commands in foreground and background processes
  - Implements custom handlers for 2 signals, SIGINT and SIGTSTP
    - SIGINT is sent by CTRL-C and will kill the foreground child process
    - SIGTSTP is sent by CTRL-Z and will toggle foreground only mode
   
## Running smallsh
In the root directory of smallsh, run the default make command to compile the code into an executable.
```bash
make
```

To run the executable, use the following command.
```bash
./smallsh
```

To exit the smallsh program, run the exit command.
```bash
exit
```


