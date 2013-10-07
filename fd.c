#include "taskimpl.h"
#include <fcntl.h>

#include <sys/types.h>			/* See NOTES */
#include <sys/socket.h>
#include <stdio.h>

static Tasklist sleeping;
static int sleepingcounted;

static Tasklist blocking;
static int blockingcounted;

static uvlong nsec(void);
static int fdcheckblock(int);
static int startedfdtask;


// Scalable Linux-specific implementation
#include <sys/epoll.h>

static int epfd;

void
fdtask(void *v)
{
	int i, ms;
	Task *t;
	uvlong now;

	tasksystem();
	taskname("fdtask");
    struct epoll_event events[20000];
	for(;;){
		/* let everyone else run */
		while(taskyield() > 0)
			;
		/* we're the only one runnable - epoll for i/o */
		errno = 0;
		taskstate("epoll");
		if((t=sleeping.head) == nil)
			ms = -1;
		else{
			/* sleep at most 100ms */
			now = nsec();
			if(now >= t->alarmtime)
				ms = 0;
			else if(now+500*1000*1000LL >= t->alarmtime)
				ms = (t->alarmtime - now)/1000000;
			else
				ms = 500;
		}
        int nevents;
		if((nevents = epoll_wait(epfd, events, 20000, ms)) < 0){
			if(errno == EINTR)
				continue;
			fprint(2, "epoll: %s\n", strerror(errno));
			taskexitall(0);
		}

		/* wake up the guys who deserve it */
		for(i=0; i<nevents; i++){
			//deleting it from blocking queue
			for (t = blocking.head; t!= nil && t!= events[i].data.ptr; t=t->next)
				;
			if(t==events[i].data.ptr)
				deltask(&blocking,t);
            taskready((Task *)events[i].data.ptr);
		}

		now = nsec();
		while((t=sleeping.head) && now >= t->alarmtime){
			deltask(&sleeping, t);
			if(!t->system && --sleepingcounted == 0)
				taskcount--;
			taskready(t);
		}

		/*wake up the guys who are blocked */

		while((t=blocking.head) && now >= t->alarmtime){
			deltask(&blocking, t);
			if(!t->system && --blockingcounted == 0)
				taskcount--;
			taskready(t);
		}
	}
}


void
fdwait(int fd, int rw)
{
	if(!startedfdtask){
		startedfdtask = 1;
        epfd = epoll_create(1);
        assert(epfd >= 0);
		taskcreate(fdtask, 0, 32768 * 10);
	}

	taskstate("fdwait for %s", rw=='r' ? "read" : rw=='w' ? "write" : "error");
    struct epoll_event ev = {0};
    ev.data.ptr = taskrunning;
	switch(rw){
	case 'r':
		ev.events |= EPOLLIN | EPOLLPRI;
		break;
	case 'w':
		ev.events |= EPOLLOUT;
		break;
	}

    int r = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    int duped = 0;
    if (r < 0 || errno == EEXIST) {
        duped = 1;
        fd = dup(fd);
        int r = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
        assert(r == 0);
    }
	taskswitch();
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
    if (duped)
        close(fd);
}


int fdcheckblock(int fd)
{
	uvlong now,when;
	Task* t;
	now = nsec();


	struct timeval stTime;
	socklen_t len = sizeof(stTime);
	if(getsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&stTime,&len) == 0){
		when = now + stTime.tv_sec * 1000 * 1000000 + stTime.tv_usec;
		if (when == now){
			return 0;
		}
	}else{
		fprintf(stderr,"getsockopt failed:%s\n",strerror(errno));
		return 0;
	}
	
	for(t=blocking.head; t!=nil && t->alarmtime < when; t=t->next)
		;

	if(t){
		taskrunning->prev = t->prev;
		taskrunning->next = t;
	}else{
		taskrunning->prev = blocking.tail;
		taskrunning->next = nil;
	}
	
	t = taskrunning;
	t->alarmtime = when;
	if(t->prev)
		t->prev->next = t;
	else
		blocking.head = t;
	if(t->next)
		t->next->prev = t;
	else
		blocking.tail = t;

	if(!t->system && blockingcounted++ == 0)
		taskcount++;

	return 1;
}



uint
taskdelay(uint ms)
{
	uvlong when, now;
	Task *t;
	
	if(!startedfdtask){
		startedfdtask = 1;
        epfd = epoll_create(1);
        assert(epfd >= 0);		
		taskcreate(fdtask, 0, 32768 * 10);
	}

	now = nsec();
	when = now+(uvlong)ms*1000000;
	for(t=sleeping.head; t!=nil && t->alarmtime < when; t=t->next)
		;

	if(t){
		taskrunning->prev = t->prev;
		taskrunning->next = t;
	}else{
		taskrunning->prev = sleeping.tail;
		taskrunning->next = nil;
	}
	
	t = taskrunning;
	t->alarmtime = when;
	if(t->prev)
		t->prev->next = t;
	else
		sleeping.head = t;
	if(t->next)
		t->next->prev = t;
	else
		sleeping.tail = t;

	if(!t->system && sleepingcounted++ == 0)
		taskcount++;
	taskswitch();

	return (nsec() - now)/1000000;
}


/* Like fdread but always calls fdwait before reading. */
int
fdread1(int fd, void *buf, int n)
{
	int m;
	do
		fdwait(fd, 'r');
	while((m = read(fd, buf, n)) < 0 && errno == EAGAIN);
	return m;
}

int
fdread(int fd, void *buf, int n)
{
	int m;

	int b = fdcheckblock(fd);
	if(b){
		while(1){
			fdwait(fd,'r');
			m = read(fd,buf,n);
			break;
		}
	}else{
		while((m=read(fd, buf, n)) < 0 && errno == EAGAIN){
			fdwait(fd, 'r');
		}
	}
	return m;
}

int
fdwrite(int fd, void *buf, int n)
{
	int m, tot;
	
	for(tot=0; tot<n; tot+=m){
		while((m=write(fd, (char*)buf+tot, n-tot)) < 0 && errno == EAGAIN)
			fdwait(fd, 'w');
		if(m < 0)
			return m;
		if(m == 0)
			break;
	}
	return tot;
}

int
fdnoblock(int fd)
{
	return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL)|O_NONBLOCK);
}

static uvlong
nsec(void)
{
	struct timeval tv;

	if(gettimeofday(&tv, 0) < 0)
		return -1;
	return (uvlong)tv.tv_sec*1000*1000*1000 + tv.tv_usec*1000;
}

