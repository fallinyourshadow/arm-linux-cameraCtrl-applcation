#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <malloc.h>
#include <sys/types.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include "functions.h"
#include "cameraFunc.c"

void * shmptr = NULL;

int main(int argc, char **argv)
{
    int i;
    int ret;
    int fd;
    int press_cnt[3];
    pid_t porc;
    int pid;
    char * command;
    volatile int isStart = 0;
	int shmid;
	key_t key;
	int signal = 0;
	int isPuase = 0;
    command = (char *)calloc(64,1);
    
    system("insmod s3c24xx_buttons_driver.ko");
    system("mknod /dev/fb0 c 29 0");
    system("mknod /dev/video0 c 81 0");
    system("mknod /dev/buttons c 232 0"); 
    key = ftok("signal",1);
    fd = open("/dev/buttons",O_RDWR| O_NONBLOCK, 0);
    
    if (fd < 0) {
        printf("Can't open /dev/buttons\n");
        return -1;
    }

    while (1) {
        ret = read(fd, press_cnt, sizeof(press_cnt));
        if (ret < 0) {
            printf("read err!\n");
            continue;
        }
	//按键扫描
        for (i = 0; i < sizeof(press_cnt)/sizeof(press_cnt[0]); ++i) 
	{
		if(press_cnt[i])//按键按下
		{
			if(press_cnt[2] == 1 && isStart == 0)//按键1_start
			{
			    porc = vfork();
			    if(0 == porc)
			    {
	 			pid = getpid();
				//创建共享内存模拟信号
				shmid = shmget(key,4,IPC_CREAT | IPC_EXCL | 0600);
				shmptr = shmat(shmid,0,0);
				isStart = 1;
				execl("camera",NULL,NULL);
				exit(1);
			    }
		        	
			}
			else if(press_cnt[1] == 1 && isStart == 1)//按键2_stop
			{
				signal = 1;
				memcpy(shmptr,&signal,4);//写信号
				while(signal == 1)
				{
					memcpy(&signal,shmptr,4);//读信号
				}
				sprintf(command,"%s%d","kill -9 ",pid);
		    		system(command);
				memset(command,0,strlen(command));
				isStart = 0;
				shmctl(shmid,IPC_RMID,NULL);//清除共享内存
				printf("stop_capturing!\n");
			}

			else if(press_cnt[0] == 1 && isStart == 1)//按键3_caught
			{
				if(isPuase == 1)
				{
					signal = 0;
					memcpy(shmptr,&signal,4);
					isPuase = 0;
					printf("Sreen_Caught\n");
				}
				else
				{
					signal = 2;
					memcpy(shmptr,&signal,4);
					isPuase = 1;
					printf("Sreen_Regain\n");	
				}
			}
		}
	}
    }
    return 0;
}


