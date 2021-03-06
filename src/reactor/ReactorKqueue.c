#include "swoole.h"
#include <string.h>

#ifdef HAVE_KQUEUE
#include <sys/event.h>

typedef struct swReactorKqueue_s swReactorKqueue;
typedef struct _swFd
{
	uint32_t fd;
	uint32_t fdtype;
} swFd;

static int swReactorKqueue_add(swReactor *reactor, int fd, int fdtype);
static int swReactorKqueue_set(swReactor *reactor, int fd, int fdtype);
static int swReactorKqueue_del(swReactor *reactor, int fd);
static int swReactorKqueue_wait(swReactor *reactor, struct timeval *timeo);
static void swReactorKqueue_free(swReactor *reactor);

struct swReactorKqueue_s
{
	int epfd;
	int event_max;
	struct kevent *events;
};

int swReactorKqueue_create(swReactor *reactor, int max_event_num)
{
	//create reactor object
	swReactorKqueue *reactor_object = sw_malloc(sizeof(swReactorKqueue));
	if (reactor_object == NULL)
	{
		swTrace("[swReactorKqueueCreate] malloc[0] fail\n");
		return SW_ERR;
	}
	reactor->object = reactor_object;
	reactor_object->events = sw_calloc(max_event_num, sizeof(struct kevent));

	if (reactor_object->events == NULL)
	{
		swTrace("[swReactorKqueueCreate] malloc[1] fail\n");
		return SW_ERR;
	}
	//kqueue create
	reactor_object->event_max = max_event_num;
	reactor_object->epfd = kqueue();
	if (reactor_object->epfd < 0)
	{
		swTrace("[swReactorKqueueCreate] kqueue_create[0] fail\n");
		return SW_ERR;
	}

	//binding method
	reactor->add = swReactorKqueue_add;
	reactor->set = swReactorKqueue_set;
	reactor->del = swReactorKqueue_del;
	reactor->wait = swReactorKqueue_wait;
	reactor->free = swReactorKqueue_free;
	reactor->setHandle = swReactor_setHandle;
	return SW_OK;
}

static void swReactorKqueue_free(swReactor *reactor)
{
	swReactorKqueue *this = reactor->object;
	close(this->epfd);
	sw_free(this->events);
	sw_free(this);
}

static int swReactorKqueue_add(swReactor *reactor, int fd, int fdtype)
{
	swReactorKqueue *this = reactor->object;
	struct kevent e;
	swFd fd_;
	int ret;
	bzero(&e, sizeof(e));

	int fflags = 0;
	fd_.fd = fd;
	fd_.fdtype = swReactor_fdtype(fdtype);

	if(swReactor_event_read(fdtype))
	{
#ifdef NOTE_EOF
		fflags = NOTE_EOF;
#endif
		EV_SET(&e, fd, EVFILT_READ, EV_ADD, fflags, 0, NULL);
		memcpy(&e.udata, &fd_, sizeof(swFd));
		ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
		if (ret < 0)
		{
			swWarn("kevent fail. Error: %s[%d]", strerror(errno), errno);
			return SW_ERR;
		}
	}
	if(swReactor_event_write(fdtype))
	{
		EV_SET(&e, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
		memcpy(&e.udata, &fd_, sizeof(swFd));
		ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
		if (ret < 0)
		{
			swWarn("kevent fail. Error: %s[%d]", strerror(errno), errno);
			return SW_ERR;
		}
	}

	memcpy(&e.udata, &fd_, sizeof(swFd));
	swTrace("[THREAD #%ld]EP=%d|FD=%d\n", pthread_self(), this->epfd, fd);

	return SW_OK;
}

static int swReactorKqueue_set(swReactor *reactor, int fd, int fdtype)
{
	swReactorKqueue *this = reactor->object;
	struct kevent e;
	swFd fd_;
	int ret;
	bzero(&e, sizeof(e));

	int fflags = 0;
	fd_.fd = fd;
	fd_.fdtype = swReactor_fdtype(fdtype);
	if(swReactor_event_read(fdtype))
	{
#ifdef NOTE_EOF
		fflags = NOTE_EOF;
#endif
		EV_SET(&e, fd, EVFILT_READ, EV_ADD, fflags, 0, NULL);
		memcpy(&e.udata, &fd_, sizeof(swFd));
		ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
		if (ret < 0)
		{
			swWarn("kevent fail. Error: %s[%d]", strerror(errno), errno);
			return SW_ERR;
		}
	}
	else
	{
		EV_SET(&e, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		memcpy(&e.udata, &fd_, sizeof(swFd));
		ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
		if (ret < 0)
		{
			swWarn("kevent fail. Error: %s[%d]", strerror(errno), errno);
			return SW_ERR;
		}
	}
	if(swReactor_event_write(fdtype))
	{
		EV_SET(&e, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
		memcpy(&e.udata, &fd_, sizeof(swFd));
		ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
		if (ret < 0)
		{
			swWarn("kevent fail. Error: %s[%d]", strerror(errno), errno);
			return SW_ERR;
		}
	}
	else
	{
		EV_SET(&e, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
		memcpy(&e.udata, &fd_, sizeof(swFd));
		ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
		if (ret < 0)
		{
			swWarn("kevent fail. Error: %s[%d]", strerror(errno), errno);
			return SW_ERR;
		}
	}

	swTrace("[THREAD #%ld]EP=%d|FD=%d\n", pthread_self(), this->epfd, fd);

	return SW_OK;
}

static int swReactorKqueue_del(swReactor *reactor, int fd)
{
	swReactorKqueue *this = reactor->object;
	struct kevent e;
	int ret;

    EV_SET(&e, fd, EVFILT_READ, EV_DELETE | EV_CLEAR, 0, 0, NULL);

	ret = kevent(this->epfd, &e, 1, NULL, 0, NULL);
	if (ret < 0)
	{
		return -1;
	}
	close(fd);
	return SW_OK;
}

static int swReactorKqueue_wait(swReactor *reactor, struct timeval *timeo)
{
	swEvent event;
	swFd fd_;
	swReactorKqueue *this = reactor->object;
	swReactor_handle handle;

	int i, n, ret;
    struct timespec t;

    t.tv_sec = timeo->tv_sec;
    t.tv_nsec = timeo->tv_usec;

	while (swoole_running > 0)
	{
		n = kevent(this->epfd, NULL, 0, this->events, this->event_max, &t);

		if (n < 0)
		{
			//swTrace("kqueue error.EP=%d | Errno=%d\n", this->epfd, errno);
			if(swReactor_error(reactor) < 0)
			{
				swWarn("Kqueue[#%d] Error: %s[%d]", reactor->id, strerror(errno), errno);
				return SW_ERR;
			}
			else
			{
				continue;
			}
		}
		else if (n == 0)
		{
			if(reactor->onTimeout != NULL)
			{
				reactor->onTimeout(reactor);
			}
			continue;
		}
		for (i = 0; i < n; i++)
		{
			if (this->events[i].udata)
			{
				memcpy(&fd_, &(this->events[i].udata), sizeof(fd_));
				event.fd = fd_.fd;
				event.from_id = reactor->id;
				event.type = fd_.fdtype;
				//read
				if (this->events[i].filter == EVFILT_READ)
				{
					handle = swReactor_getHandle(reactor, SW_EVENT_READ, event.type);
					ret = handle(reactor, &event);
					if (ret < 0)
					{
						swWarn("kqueue event handler fail. fd=%d|errno=%d.Error: %s[%d]", event.fd, errno, strerror(errno), errno);
					}
				}
				//write
				else if (this->events[i].filter == EVFILT_WRITE)
				{
					handle = swReactor_getHandle(reactor, SW_EVENT_WRITE, event.type);
					ret = handle(reactor, &event);
					if (ret < 0)
					{
						swWarn("kqueue event handler fail. fd=%d|errno=%d.Error: %s[%d]", event.fd, errno, strerror(errno), errno);
					}
				}
				else
				{
					swWarn("kqueue event unknow filter=%d", this->events[i].filter);
				}
			}

		}
		if(reactor->onFinish != NULL)
		{
			reactor->onFinish(reactor);
		}
	}
	return 0;
}
#endif
