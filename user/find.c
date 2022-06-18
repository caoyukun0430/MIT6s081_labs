// find.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *filename)
// refer to https://fanxiao.tech/posts/MIT-6S081-notes/#16-lab-1-unix-utilities
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  //   // de are all items inside fd, which is the buf path
  //   // de.name .. de.size 16
  //   // ls: buf a/.
  //   // ls: buf a/..
  //   // ..             has 1 1 1024
  //   // de.name b de.size 16
  //   // ls: buf a/..
  //   // ls: buf a/b
  //   // b              has 2 24 0
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    // char* strcpy(char* destination, const char* source);
		strcpy(buf, path);
		p = buf+strlen(buf);
		*p++ = '/';
		// don't recursive over . and ..
    if(de.inum == 0 || (strcmp(de.name, ".") == 0) || (strcmp(de.name, "..") == 0))
			continue;
        // get the full path name of the current file/directory selected
		memmove(p, de.name, DIRSIZ);
		p[DIRSIZ] = 0;
   
		if (stat(buf, &st) < 0) {
			fprintf(2, "ERROR: cannot stat %s\n", buf);
		}
   
		switch (st.type) {
    // 当前路径为文件, 直接判断
		case T_FILE:
			if (strcmp(filename, de.name) == 0) {
				printf("%s\n", buf);
			}
			break;
		case T_DIR:
				find(buf, filename);
		}	
	}

  // switch(st.type){
  // case T_FILE:
  //   printf("%s %d %d %l\n", fmtname(path), st.type, st.ino, st.size);
  //   break;

  // case T_DIR:
  //   if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
  //     printf("ls: path too long\n");
  //     break;
  //   }
  //   strcpy(buf, path);
  //   p = buf+strlen(buf);
  //   *p++ = '/';
  //   // de are all items inside fd, which is the buf path
  //   // de.name .. de.size 16
  //   // ls: buf a/.
  //   // ls: buf a/..
  //   // ..             has 1 1 1024
  //   // de.name b de.size 16
  //   // ls: buf a/..
  //   // ls: buf a/b
  //   // b              has 2 24 0
  //   while(read(fd, &de, sizeof(de)) == sizeof(de)){
  //     printf("de.name %s de.size %d\n", de.name, sizeof(de));
  //     // don't recursive over . and ..
  //     if(de.inum == 0 || (strcmp(de.name, ".") == 0) || (strcmp(de.name, "..") == 0))
  //       continue;
  //     // add/replace de.name the item to end of path 
  //     memmove(p, de.name, DIRSIZ);
  //     p[DIRSIZ] = 0;
  //     if(stat(buf, &st) < 0){
  //       printf("ls: cannot stat %s\n", buf);
  //       continue;
  //     }
  //     printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
  //   }
  //   break;
  // }
  close(fd);
  return;
}

int
main(int argc, char *argv[])
{
   if (argc != 3){
        fprintf(2, "Please enter a dir and a filename!\n");
        exit(1);
    }else{
        char *path = argv[1];
        char *filename = argv[2];
        find(path, filename);
        exit(0);
    }
}
