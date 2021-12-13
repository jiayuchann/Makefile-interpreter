#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h> 
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>

#define STACK_LENGTH 50
#define EMPTY (-1)
#define STACK_EMPTY INT_MIN

char* mystack[STACK_LENGTH];
int top = EMPTY;
struct recipe **recipes;
int recipecount;

struct recipe {
    char *target;
    char **deps;
    char **commands;
    int depcount;
    int commandcount;
};

bool push(char* str){
    if (top >= STACK_LENGTH - 1) {
        return false;
    }

    top++;
    mystack[top] = str;
    return true;
}

char* pop() {
    if (top == EMPTY) {
        return NULL;
    }

    char* result = mystack[top];
    top--;
    return result;
}

int runCommand(char *command){
    printf("%s\n", command);
    fflush(stdout);

    // basically an array of strings, separation done by spaces
    char *commandArray[10000];
    int commandArrayIndex = 0;

    char *tempCommand = strdup(command);
    char *tempCommandCopy = tempCommand;
    char *token = "";
    while ((token = strsep(&tempCommandCopy, " ")) != NULL){
        if (token[0] == '"' && token[strlen(token)-1] == '"'){
            token[strlen(token)-1] = '\0';
            memmove(token, token+1, strlen(token));
        }else if (token[0] == '\"'){
            memmove(token, token+1, strlen(token));
        }else if (token[strlen(token)-1] == '\"'){
            token[strlen(token)-1] = '\0';
        }
        if (strstr(token, "\\\\")){
            for (int b = 0; b < strlen(token); b++){
                if (token[b] == '\\'){
                    memmove(&token[b], &token[b+1], strlen(token)- b);
                }
            }
        }
        
        commandArray[commandArrayIndex] = token;
        commandArrayIndex++;
    }

    commandArray[commandArrayIndex] = 0;
    
    /* reference: https://stackoverflow.com/questions/46715490/c-shell-redirection-and-piping-working-but-not-a-combination-of-input-and-outp */
    int commandArgLocation[3000] = {0};
    int pipeCount = 0;
    char *inputPath;
    char *outputPath;
    bool inputRedirectionExists = false;
    bool outputRedirectionExists = false;
    
    /* store pipeCount and location of command/arguments, 
    which will be needed for the pipe loop and exec() call later*/
    for (int i = 0; i < commandArrayIndex; i++){
        if (strcmp(commandArray[i], "|") == 0){
            // clears the index spot, so when execvp() is called later, the arguments stop at the null terminator
            commandArray[i] = 0;
            /* a little bit confusing, but draw out the whole structure for i, commandArray and commandArgLocation.
            this is the index to be used by execvp() to take in the correct command and argument/s */ 
            commandArgLocation[pipeCount + 1] = i + 1;
            pipeCount++;
        }else if (strcmp(commandArray[i], "<") == 0){
            // takes in the input path 
            inputPath = commandArray[i + 1];
            // clears the index spot, so when execvp() is called later, the arguments stop at the null terminator
            commandArray[i] = 0;
            inputRedirectionExists = true;
        }else if (strcmp(commandArray[i], ">") == 0){
            // takes in the output path
            outputPath = commandArray[i + 1];
            // clears the index spot, so when execvp() is called later, the arguments stop at the null terminator
            commandArray[i] = 0;
            outputRedirectionExists = true;
        }else {
            commandArgLocation[i] = i;
        }
    }

    int leftPipe[2] = {0,0};
    int rightPipe[2];
    pid_t pid;
    pid_t pidArray[2000];
    int status = 0;

    for (int i = 0; i <= pipeCount; i++){
        // create just enough amount of pipes
        if (pipeCount > 0 && i != pipeCount){
            /* rightPipe is for the inter-process communication between parent and child processes
            leftPipe is just a reference to the previous rightPipe, so when fork() happens,
            we can still read from the previous pipe and also generate a new rightPipe if needed */
            pipe(rightPipe);
        }

        if ((pid = fork()) == -1){
            perror("fork");
            exit(1);

        }else if (pid == 0){
            // child process
            // input redirection
            if ((i == 0) && inputRedirectionExists){
                int inputFD = open(inputPath, O_RDONLY, 0400);
                if (inputFD == -1){
                    perror("Failed to open input file\n");
                    return(EXIT_FAILURE);
                }
                close(0);
                dup(inputFD); //stdin replaced with inputFD, to read the file
                close(inputFD);
            }

            //output redirection
            if ((i == pipeCount) && outputRedirectionExists){
                int outputFD = creat(outputPath, 0700);
                if (outputFD == -1){
                    perror("Failed to open output file\n");
                    return(EXIT_FAILURE);
                }
                close(1);
                dup(outputFD); //stdout replaced with outputFD, to write to the file
                close(outputFD);
            }
            
            // here we check the position of the child process, if there are pipes in the command.
            if (pipeCount > 0){
                if (i == 0){
                    // first section of the command (xxxxxx | ...... | ......)
                    close(1);
                    dup(rightPipe[1]); // stdout replaced with write end of rightPipe
                    close(rightPipe[1]);
                    close(rightPipe[0]);
                    /* since it is the first section of the command, there will be no leftPipe */
                }else if (i < pipeCount){
                    // middle section of the command (...... | xxxxxxx | .......)
                    close(0);
                    dup(leftPipe[0]); //stdin replaced with read end of leftPipe
                    close(leftPipe[0]);
                    close(leftPipe[1]);
                    close(1);
                    dup(rightPipe[1]); //stdout replaced with write end of rightPipe
                    close(rightPipe[0]);
                    close(rightPipe[1]);
                }else{
                    // final section of the command (...... | ........ | xxxxxxx)
                    close(0);
                    dup(leftPipe[0]); //stdin replaced with read end of leftPipe
                    close(leftPipe[0]);
                    close(leftPipe[1]);
                    /* no need to close rightPipe since it is not created */
                }
            }

            // execute command
            execvp(commandArray[commandArgLocation[i]], &commandArray[commandArgLocation[i]]);

            // only reached if execvp returned back to this process
            perror("command execution failed\n");

        }else{
            //parent process
            if (i > 0){
                close(leftPipe[0]);
                close(leftPipe[1]);
            }

            // leftPipe references rightPipe, in the last loop, leftPipe = itself
            leftPipe[0] = rightPipe[0];
            leftPipe[1] = rightPipe[1];
            
            // collect the pid
            pidArray[i] = pid;


            // wait on pids after all processes are created
            if (i == pipeCount){
                sleep(1);
                for (int k = 0; k <= pipeCount; k++){
                    waitpid(pidArray[k], &status, 0);
                }
            }
                    
        }
    }

    // freeing the strdup variable created at the start
    free(tempCommand);

    return status;
}

time_t get_mtime(const char *path){
    struct stat statbuf;
    if (lstat(path, &statbuf) == -1){
        perror(path);
        exit(1);
    }
    return statbuf.st_mtime;
}

// my method: only store targets/deps which are targets on the stack
void* evaluate() {
    char* stackItem = pop();
    
    if (access(stackItem, F_OK) == -1){
        // stack item does not exist in directory

        // check if stack item is a target in file
        bool stackItemIsTarget = false;
        int targetIndex = 0;
        for (int i = 0; i < recipecount; i++){
            if (strcmp(stackItem, recipes[i]->target) == 0){
                stackItemIsTarget = true;
                targetIndex = i;
            }
        }

        if (stackItemIsTarget){
            // check if dependencies are also targets in the file
            for (int y = 0; y < recipes[targetIndex]->depcount; y++){
                for (int z = 0; z < recipecount; z++){
                    if (strcmp(recipes[targetIndex]->deps[y], recipes[z]->target) == 0){
                        push(recipes[targetIndex]->deps[y]);
                        if (evaluate() != 0){
                            return 0;
                        }
                    }
                }
            }

            int depsExist = 0;
            for (int y = 0; y < recipes[targetIndex]->depcount; y++){
                if (access(recipes[targetIndex]->deps[y], F_OK) == 0){
                    depsExist++;
                }
            }

            // if deps exists in directory
            if (depsExist == recipes[targetIndex]->depcount){
                for (int a = 0; a < recipes[targetIndex]->commandcount; a++){
                    int status = runCommand(recipes[targetIndex]->commands[a]);
                    if (status != 0){
                        break;
                    }
                }
                return 0;
            }else{
                //dependency/dependencies missing in current directory;
                return (int*)1;
            }

        } else{
            //Target is not in fakefile and does not exist in directory
            return 0;
        }

    } else{
        // stack item exists in directory

        // check if stack item is a target in file
        bool stackItemIsTarget = false;
        int targetIndex = 0;
        for (int i = 0; i < recipecount; i++){
            if (strcmp(stackItem, recipes[i]->target) == 0){
                stackItemIsTarget = true;
                targetIndex = i;
            }
        }

        if (stackItemIsTarget){
            // check if dependencies are also targets in the file
            for (int y = 0; y < recipes[targetIndex]->depcount; y++){
                for (int z = 0; z < recipecount; z++){
                    if (strcmp(recipes[targetIndex]->deps[y], recipes[z]->target) == 0){
                        push(recipes[targetIndex]->deps[y]);
                        if (evaluate() != 0){
                            return 0;
                        }
                    }
                }
            }

            int depsExist = 0;
            for (int y = 0; y < recipes[targetIndex]->depcount; y++){
                if (access(recipes[targetIndex]->deps[y], F_OK) == 0){
                    depsExist++;
                }
            }

            // if deps exist in directory
            if (depsExist == recipes[targetIndex]->depcount){
                // check modification time
                bool needUpdate = false;
                time_t stackItemTimeMod = get_mtime(stackItem);
                for (int y = 0; y < recipes[targetIndex]->depcount; y++){
                    if (stackItemTimeMod < get_mtime(recipes[targetIndex]->deps[y])){
                        needUpdate = true;
                    }
                }
                if (needUpdate){
                    for (int a = 0; a < recipes[targetIndex]->commandcount; a++){
                        int status = runCommand(recipes[targetIndex]->commands[a]);
                        if (status != 0){
                            break;
                        }
                    }
                    return 0;
                }else{
                    //Target is already up to date, no need to make it again
                }
                
            }else{
                //dependency/dependencies missing in current directory
                return (int*)1;
            }
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {

    FILE *fakefile = fopen("fakefile", "r");
    if ( fakefile == NULL )
    {
        fprintf(stderr, "Could not open fakefile\n");
        exit(1);
    } 

    int c = getc(fakefile);
    if (c == EOF){
        fprintf(stderr, "./fake: empty file!\n");
        exit(1);
    }

    char str[10000];
    recipes = calloc(1000, sizeof(char*));
    recipecount = 0;

    while (fgets(str, 10000, fakefile)) {
        if (str[0] != '#' && str[0] != '\n'){
            if (str[0] != '\t'){
                // rule target and dependencies
                char *line = str;
                recipes[recipecount] = calloc(10000, sizeof(char));

                // target
                recipes[recipecount]->target = calloc(200, sizeof(char));

		        char *token = strsep(&line, ":");
                strcpy(recipes[recipecount]->target, token);

                // dependencies
                recipes[recipecount]->deps = calloc(10, sizeof(char*)); 
                int tempi = 0;
                token = strsep(&line, " ");
                token = strsep(&line, " ");

                while (token != NULL){
                    // getting rid of '\n'
                    if (token[strlen(token)-1] == '\n'){
                        token[strlen(token)-1] = '\0';
                    } 

                    recipes[recipecount]->deps[tempi] = calloc(50, sizeof(char));
                    strcpy(recipes[recipecount]->deps[tempi], token);

                    recipes[recipecount]->depcount++;
                    token = strsep(&line, " ");
                    tempi++;
                }
                recipes[recipecount]->commandcount = 0;
                recipecount++;
            }

            if (str[0] == '\t') {
                // commands
                int commandcount = recipes[recipecount-1]->commandcount;
                recipes[recipecount-1]->commands = realloc(recipes[recipecount-1]->commands, ((commandcount + 1) * sizeof(char*)));
                recipes[recipecount-1]->commands[commandcount] = calloc(1000, sizeof(char));

                // getting rid of '\n' and '\t'
                memmove(str, str+1, strlen(str));
                if (str[strlen(str)-1] == '\n'){
                    str[strlen(str)-1] = '\0';
                }     

                strcpy(recipes[recipecount-1]->commands[commandcount], str);
                recipes[recipecount-1]->commandcount++;
            }          

        }
    }
    fclose(fakefile);

    // printing the fakefile
    // for (int i = 0; i < recipecount; i++){
    //     printf("%s: ", recipes[i]->target);
    //     for (int y = 0; y < recipes[i]->depcount; y++){
    //         printf("%s ", recipes[i]->deps[y]);
    //     }
    //     printf("\n");
    //     for (int z = 0; z < recipes[i]->commandcount; z++){
    //         printf("%s\n", recipes[i]->commands[z]);
    //     }
    //     printf("\n");
    // }

    if (recipes[0] == NULL){
        fprintf(stderr, "./fake: empty file!\n");
        exit(1);
    }

    if (argv[1] != NULL){
        push(argv[1]);
    }else {
        push(recipes[0]->target);
    }
    
    evaluate();
    

    // free memory
    for (int i = 0; i < recipecount; i++){
        for (int y = 0; y < recipes[i]->commandcount; y++){
            free(recipes[i]->commands[y]);
        }
        free(recipes[i]->commands);

        for (int y = 0; y < recipes[i]->depcount; y++){
            free(recipes[i]->deps[y]);
        }
        free(recipes[i]->deps);
        free(recipes[i]->target);
        free(recipes[i]);
    }
    free(recipes);

    return 0;
}
