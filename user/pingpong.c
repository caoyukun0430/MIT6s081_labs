// pingpong.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
  int pp2c[2];
  int pc2p[2];
  char buf[1];

  pipe(pp2c);
  pipe(pc2p);

  if (fork() == 0) {
    // child process
    // disable write of pp2c and read of pc2p in child process
    close(pp2c[1]);
    close(pc2p[0]);

    // child read
    read(pp2c[0], buf, 1);
    printf("child receive: %c\n", buf[0]);
    printf("%d: received ping\n", getpid());

    // child write
    write(pc2p[1], "c", 1);

    // close 
    close(pp2c[0]);
    close(pc2p[1]);
    exit(0);
  } else {
    // parent process
    // disable read of pp2c and write of pc2p in parent process
    close(pp2c[0]);
    close(pc2p[1]);

    // parent write
    write(pp2c[1], "p", 1);

    // parent read
    read(pc2p[0], buf, 1);
    printf("parent receive: %c\n", buf[0]);
    printf("%d: received pong\n", getpid());

    // close 
    close(pp2c[1]);
    close(pc2p[0]);
    wait(0);
  }

  exit(0);
}