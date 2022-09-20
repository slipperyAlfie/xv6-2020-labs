#include "../kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]){
    char buf[512];
    char* p = buf;
    char* arg[argc + 1];
    int idx = 0, i = 0;
    for(i = 0 ; i < argc - 1 ; i++){
        arg[i] = argv[i + 1];
    }
    arg[argc] = 0;
    memset(buf,0,sizeof(buf));
    
        while(read(0,buf + idx,1) > 0){
            //printf("%s\n",buf);
            if(buf[idx] == '\n'){
                buf[idx] = '\0';
                arg[i] = p;
                if(fork() == 0){
                    exec(argv[1],arg);
                    exit(0);
                }
                else{
                    wait(0);
                }
                //printf("exec complete!\n");
                p = buf + idx + 1;
            }
            idx++;
        }
    
    exit(0);
}