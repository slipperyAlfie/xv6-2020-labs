#include "../kernel/types.h"
#include "user.h"

int main(int argc,char* argv[]){
    int p[2];
    int p1[2];
    pipe(p);
    pipe(p1);
    char buf[5];
    int pid = 0;
    int fork_flag = fork();
    if(fork_flag > 0){
        close(p1[1]);
        close(p[0]);
        write(p[1],"ping",5);
        close(p[1]);
        read(p1[0],buf,5);
        close(p1[0]);
        pid = getpid();
        printf("%d: received %s\n",pid,buf);
        wait(0);
    }
    else if(fork_flag == 0){
        close(p1[0]);
        close(p[1]);
        read(p[0],buf,5);
        close(p[0]);
        pid = getpid();
        printf("%d: received %s\n",pid,buf);
        write(p1[1],"pong",5);
        close(p1[1]);
        exit(0);
    }
    else{
        printf("fork error!\n");
        exit(-1);
    }
    exit(0); //确保进程退出
}