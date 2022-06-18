// xargs.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"
#define MAXLEN 100

// Read the input from stdout and split the parameter by space and newline character.
// scope not cover flag 
int main(int argc, char *argv[]) 
{
    if(argc <= 1) {
        fprintf(2, "usage: xargs command (arg...)\n");
        exit(1);
    }
    char *command = argv[1];
    char buf;
    char new_argv[MAXARG][MAXLEN]; // assuming the maximun single parameter length is 512
    char *p_new_argv[MAXARG];

    while(1) {
        memset(new_argv, 0, MAXARG * MAXLEN); // reset the parameter

        // xargs is argv[0], put everything after xargs into first part of new_argv[]
        for(int i = 1; i < argc; ++i) {
            strcpy(new_argv[i-1], argv[i]);
            // printf("new_argv[i-1] %s\n", new_argv[i-1]);
        }
        // printf("argc %d\n", argc);
        int cur_argc = argc - 1;
        int offset = 0;
        int is_read = 0;

        // append args before | xargs to new_argv[]
        while((is_read = read(0, &buf, 1)) > 0) {
            // read an arg is done
            if(buf == ' ') {
                cur_argc++;
                offset = 0;
                continue;
            }
            // read one line is done
            if(buf == '\n') {
                break;
            }
            if(offset==MAXLEN) {
                fprintf(2, "xargs: parameter too long\n");
                exit(1);
            }
            if(cur_argc == MAXARG) {
                fprintf(2, "xargs: too many arguments\n");
                exit(1);
            }
            new_argv[cur_argc][offset++] = buf;
            // printf("offset %d buf %c new_argv[cur_argc] %s\n", offset, buf,  new_argv[cur_argc]);
        }
        // printf("cur_argc %d new_argv[cur_argc] %s\n", cur_argc, new_argv[cur_argc]);

        // encounters EOF of input or \nread is not valid anymore, break while(1) 
        if(is_read <= 0) {
            break;
        } 
        for(int i = 0; i <= cur_argc; ++i) {
            p_new_argv[i] = new_argv[i];
        }
        if(fork() == 0) {
            exec(command, p_new_argv);
            exit(1);
        } else {
          wait((int*) 0);
        }
        
    }
    exit(0);
}
