#include "systemcalls.h"
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    const int ret = system(cmd);
    if (ret == -1 || ret != 0)  return false;

    return true;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    int status;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    // command[count] = command[count];

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    pid_t pid = fork();
    if (pid == -1) {
        if (errno == EAGAIN) {
            perror("Error: fork() failed due to resource allocation failure - rlimit reached");
        }
        else if (errno == ENOMEM) {
            perror("Error: fork() failed - insufficient kernel memory ");
        }
        else {
            perror("Error: fork() failed");
        }
        va_end(args);
        return false;
    }

    /*
     *child process handling
     */
    if (pid == 0) {
        // char *comm_for_exec = command[0];
        // char **comm_args = command;
        int ret = execv("/bin/sh", command);
        if (ret == -1) {
            perror("Error: execv() failed");
            exit(EXIT_FAILURE);
        }
        return true;
    }
    if (waitpid(pid, &status, 0) == -1) {
        perror("Error: waitpid() failed");
        va_end(args);
        return false;
    }
    if (WIFEXITED(status)) {
        printf ("Normal termination with exit status=%d\n",
                        WEXITSTATUS (status));
        va_end(args);
        return true;
    }
    va_end(args);
    return false;


}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    // command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    int child_pid;
    const int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) { perror("open"); abort(); }
    switch (child_pid = fork()) {
        case -1: perror("fork"); abort();
        case 0:
            if (dup2(fd, 1) < 0) { perror("dup2"); abort(); }
        close(fd);
        do_exec(command); perror("do_exec"); abort();
        default:
            close(fd);
        /* do whatever the parent wants to do. */
    }

    va_end(args);

    return true;
}
