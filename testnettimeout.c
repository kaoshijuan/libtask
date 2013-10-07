#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <task.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>			/* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "taskimpl.h"

int success,fail,connfail;
void fetchtask(void *v);
void servetask(void *fd);
void timetask(void *v){
	while(1){
		taskdelay(1000);
		fprintf(stderr,"connfail:%d,success:%d,fail:%d,rate:%f%%\n",connfail,success,fail,((float)success)/(success+fail+connfail)*100);
	}
}

void taskmain(int argc,char **argv)
{
	signal(SIGPIPE,SIG_IGN);
	int i,n;
	if(argc != 2){
		fprintf(stderr, "usage: %s n \n",argv[0]);
		taskexitall(1);
	}
	n = atoi(argv[1]);
	for (i = 0;i < n ;i++){
		taskcreate(fetchtask,0,32768);
	}
	timetask(nil);
/*	taskcreate(timetask,0,32768);
	int ld = netannounce(TCP,"127.0.0.1",843);
	while(1){
		char szServer[32] = {0};
		int port = 0;
		int fd = netaccept(ld,szServer,&port);
		if(fd>0){
			taskcreate(servetask,&fd,32768);
		}else{
			fprintf(stderr,"accept failed:%s\n",strerror(errno));
		}
		
	}*/
}

int netlookup(char *name,uint32_t *ip);

void fetchtask(void *v){

	for(;;){
		int fd = netdial(TCP,"127.0.0.1",843);
		struct timeval stTv;
		stTv.tv_sec = 1;
		stTv.tv_usec = 0;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&stTv, sizeof(stTv));

		
		char szMsg[32];
		if (fd > 0){
			//fprintf(stderr,"connect ok, fd:%d\n",fd);
			fdwrite(fd,"nothing",strlen("nothing"));
			int ret = fdread(fd,szMsg,sizeof(szMsg));
			if(ret<0){
				//fprintf(stderr,"received failed:%s\n",strerror(errno));
				fail++;
			}else{
				//fprintf(stderr,"received response:%s\n",szMsg);
				success++;
			}
			taskdelay(500);
			close(fd);
			
		}else{
			connfail++;
		}
	}
	/*
	uint32_t ip;
	netlookup("127.0.0.1",&ip);
	struct sockaddr_in sa;
	
	memset(&sa, 0, sizeof sa);
	memmove(&sa.sin_addr, &ip, 4);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(843);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(connect(fd, (struct sockaddr*)&sa, sizeof sa) < 0 && errno != EINPROGRESS){
		close(fd);
		return ;
	}

	struct timeval stTime;
	stTime.tv_sec = 5;
	stTime.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&stTime, sizeof(stTime));

	write(fd,"nothing",strlen("nothing"));
	taskyield();
	char szMsg[32];
	int m = read(fd,szMsg,sizeof(szMsg));
	if (m < 0){
		fprintf(stderr,"read failed of:%s\n",strerror(errno));
	}else{
		fprintf(stderr,"recv:%d\n",m);
	}
	close(fd);
*/
}

void servetask(void *v){
	int fd = 0;
	memcpy(&fd,v,sizeof(fd));
	//fprintf(stderr,"got a connection,fd:%d\n",fd);
	while(1){
		char buf[32] = {0};
		if(fdread(fd,buf,sizeof(32)) <= 0){
			//fprintf(stderr,"remote closed\n");
			close(fd);
			break;
		}else{
			fdwrite(fd,"hello",strlen("hello"));
		}
	}
}

