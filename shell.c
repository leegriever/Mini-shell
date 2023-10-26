#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// a struct to contain the command and it's index in arglist
typedef struct commands {
    int id;
    int symbol_index;
} command;

// enum to specify commands (used for command.id and validity checks)
typedef enum command_ids {
    BASIC,
    BACKGROUND,
    PIPE, 
    REDIRECT,
    FORK,
    WAIT,
    DUP,
} id;

// auxiliary functions declaration 
int error_handler(int x);
void execvp_handler(int index, char ** arglist);
void sigint_to_dfl_handler();
void validity(id com, int x);
command determine_command(int count, char ** arglist);
int execute_command(char ** arglist, id com);
int execute_pipe(char ** arglist, int symbol_index);
int execute_redirect(char ** arglist, int symbol_index);

// general error handler to print error messages and return/exit 
int error_handler(int x){
    fprintf(stderr, "%s\n", strerror(errno));
    if (x == 0){
        return 0;
    }
    else if (x == -1){
        return -1;
    }
    exit(1);
}

// auxilary function to execute execvp and print errors
void execvp_handler(int index, char ** arglist){
    execvp(arglist[index], arglist);
    fprintf(stderr, "%s\n", strerror(errno));
	exit(1);
}

// auxilary function that sets SIGINT to deafult handle, and checks for success
void sigint_to_dfl_handler(){
    if (signal(SIGINT, SIG_DFL) == SIG_ERR){
        error_handler(1);
    }
}

// general validity check of possible errors
void validity(id com, int x){
    if (x == -1){
        // file open failed
        if (com == REDIRECT){
            error_handler(0);
        }
        // dup2 fail
        if (com == DUP){
            error_handler(1);
        }
    }
    // check pid validity
    if ((com == FORK) && (x < 0)){                   
        error_handler(0);
    }
    // pipe creation failed
    if (com == PIPE){
        error_handler(0);
    }
    // wait for command to be completed and check 
    if (com == WAIT){                   
        if ((waitpid(x, 0, 0) == -1) && ((errno != ECHILD && errno != EINTR))) { 
            error_handler(0);
            
        }
    }   
}

// determine and return the curr command to be executed and it's index in arglist
command determine_command(int count, char ** arglist){
    command com;
    com.id = BASIC;
    com.symbol_index = 0;
    for (int i = 1; i < count; i++){
        if (strcmp(arglist[i], "&") == 0){
            com.id = BACKGROUND;
            com.symbol_index = i;
            arglist[i] = NULL;
            break;
        }
        if (strcmp(arglist[i], "|") == 0){  
            com.id = PIPE;
            com.symbol_index = i;
            arglist[i] = NULL;
            break;
        }
        if (strcmp(arglist[i], "<") == 0){
            com.id = REDIRECT;
            com.symbol_index = i;
            arglist[i] = NULL;
            break;
        }
    }
    return com;
} 

int prepare(void){
    // ignore sigint for parent (curr) process
    if (signal(SIGINT, SIG_IGN) == SIG_ERR){
        error_handler(-1);
        
    }
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR){
        error_handler(-1);
    }
    // ZOMBIE handeling
    // struct sigaction sa;
    // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = SA_RESTART | SA_NOCLDWAIT;
    // sa.sa_handler = SIG_IGN;
    // if (sigaction(SIGCHLD, &sa, NULL) == -1){
    //     error_handler("Error - sigacation failed", 1);
    // }
    return 0;
}

int process_arglist(int count, char **arglist){
    command com = determine_command(count, arglist);
    if ((com.id == BASIC) || (com.id == BACKGROUND)){
        return execute_command(arglist, com.id);
    }
    if (com.id == PIPE){
        return execute_pipe(arglist, com.symbol_index);
    }
    if (com.id == REDIRECT){
        return execute_redirect(arglist, com.symbol_index);
    }
    return 1;
}


int finalize(void){
	return 0;
}

// execute basic command (no symbol) and background (&) coomand
int execute_command(char ** arglist, id com){
    int pid;
    pid = fork();
	validity(FORK, pid);
    if (pid == 0){
        // child process
        if (com == BASIC){
            sigint_to_dfl_handler();
        }
        execvp_handler(0, arglist);
    }
    else {
        // parent process
        if (com == BASIC){
            // not in backgroung mode, wait. 
            validity(WAIT, pid);
        }
    }
    return 1;
}

// execute pipe (|) command
int execute_pipe(char ** arglist, int symbol_index){
    int read = 0;
    int write = 1;
    int pipefd[2];
    int pid1 = 0;
    int pid2 = 0;
    if (pipe(pipefd) == -1){ 
        validity(PIPE, 0);
    }
    pid1 = fork();
    validity(FORK, pid1);
    if (pid1 == 0){
        // 1'st process
        sigint_to_dfl_handler();
        close(pipefd[read]);
        // redirect stdout to pipefd[read] and check success
        validity(DUP, dup2(pipefd[write], 1));   
        close(pipefd[write]);
        execvp_handler(0, arglist);
    }
    else {
        pid2 = fork();
        validity(FORK, pid2);
        if (pid2 == 0){
            // 2'nd process
            sigint_to_dfl_handler();
            close(pipefd[write]);
            // redirect stdin to pipefd[read] and check success
            validity(DUP, dup2(pipefd[read], 0));
            close(pipefd[read]);
            // execvp needs to get arglist after |
            execvp(arglist[symbol_index + 1], &arglist[symbol_index + 1]);
            error_handler(1);   
        }
        else{
            close(pipefd[read]);
            close(pipefd[write]);
            validity(WAIT, pid1);
            validity(WAIT, pid2);
        }
}

    return 1;
}

// execute redirect (<) command 
int execute_redirect(char ** arglist, int symbol_index){
    int pid;
    int fd = open(arglist[symbol_index + 1], O_RDONLY);
    validity(REDIRECT, fd);
    pid = fork();
    validity(FORK, pid);
    if (pid == 0){
        // child process
        sigint_to_dfl_handler();
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR){
            error_handler(1);
        }
        // redirect stdin to the given file
        validity(DUP, dup2(fd, 0));
        close(fd);
        execvp_handler(0, arglist);
    }
    else{
        // parent process, wait for child
        close(fd);
        validity(WAIT, pid);
    }
    return 1;
}

