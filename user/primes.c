#include "../kernel/types.h"
#include "user.h"

void get_primes(int* nums, int len){
    if(len <= 0)
        return;
    int p[2];
    pipe(p);
    if(fork() == 0){
        close(p[1]);
        int temp[len];
        read(p[0],temp,4*len);
        close(p[0]);
        int cur_prime = temp[0];
        printf("prime %d\n",cur_prime);
        for(int j = 0 ; j < len ; j++){
            if(temp[j] % cur_prime == 0){
                for(int k = j + 1 ; k < len ; k++){
                    temp[k-1] = temp[k];
                }
                len--;
                j--;
            }    
        }
        get_primes(temp,len);
        exit(0);
    }
    else{
        close(p[0]);
        write(p[1],nums,4*len);
        close(p[1]);
        wait(0);
    }
    exit(0);
}

int main(int argc, char* argv[]){
    int nums[34];
    for(int i = 0 ; i < 34 ; i++){
        nums[i] = i + 2;
    }
    get_primes(nums,34);
    exit(0);
}