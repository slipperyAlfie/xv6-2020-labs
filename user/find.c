#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"
#include "../kernel/fs.h"

// 返回路径末端的文件名，如/a/b/c，则返回c
char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), 0, DIRSIZ-strlen(p));
  return buf;
}

void find(char* path, char* target){
    struct stat st;
    struct dirent de;
    char buf[512], *p;
    int fd;
    
    //printf("%s  %s\n",path,fmtname(path));
    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_FILE:
        p = fmtname(path);
        if(strcmp(p,target) == 0){
            printf("%s\n",path);
        }
        close(fd);
        break;
    
    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf+strlen(buf);
        *p++ = '/';
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            if(strcmp(de.name,".") == 0 || strcmp(de.name,"..") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            find(buf,target);
        }
        break;
    
    default:
        break;
    }
    return;

}

int main(int argc, char* argv[]){

    if(argc < 3){
        printf("argc < 2!\n");
        exit(0);
    }
    find(argv[1],argv[2]);
    exit(0);
}