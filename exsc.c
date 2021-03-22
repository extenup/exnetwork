// version 1.0.0

#include "exsc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include "crossthread.h"

#ifdef __linux__
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#elif _WIN32
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// struct that uses in core only (internal connection)
struct exsc_incon
{
    struct exsc_excon excon;
    int sock;               // tcp socket
    char *recvbuf;          // receive buffer
    int sendbufsize;        // send buffer size
    char *sendbuf;          // send buffer
    int sent;               // size in bytes that was sent
    time_t lastact;         // last activity
};

struct exsc_srv
{
    uint16_t port;
    int timeout;   // seconds
    int timeframe; // milliseconds
    int recvbufsize;

    int inconcnt;
    int inconmax;
    struct exsc_incon *incons;

    char (*banlst)[INET6_ADDRSTRLEN]; // ban list

    void (*callback_newcon)(struct exsc_excon con);
    void (*callback_closecon)(struct exsc_excon con);
    void (*callback_recv)(struct exsc_excon con, char *buf, int bufsize);
    void (*callback_ext)();

    pthread_mutex_t mtx;
    pthread_t thr;
};

struct exsc_thrarg
{
    int des; // server descriptor
};

int g_maxsrvcnt = 0;     // max servers count
int g_srvcnt = 0;        // servers count
struct exsc_srv *g_srvs; // servers

void *exmalloc(size_t size, const char *desc)
{
    void *ptr = malloc(size);
    if (ptr == NULL)
    {
        printf("ERROR malloc returns NULL %s", desc);
        exit(1);
    }
    return ptr;
}

// sleep in milliseconds
void sleepms(int time)
{
#ifdef __linux__
    usleep(time * 1000);
#elif _WIN32
    Sleep(time);
#endif
}

// get number of milliseconds that have elapsed since the system was started
int gettimems()
{
#ifdef __linux__
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#elif _WIN32
    return GetTickCount();
#endif
}

void setsocknonblock(int sock)
{
#ifdef __linux__
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
#elif _WIN32
    u_long opt;
    opt = 1;
    ioctlsocket(sock, FIONBIO, &opt);
#endif
}

int getconid()
{
    static int conid = 0;
    conid++;
    return conid;
}

#ifdef __linux__
void closesock(int sock)
{
    close(sock);
}
#elif _WIN32
void closesock(int sock)
{
    closesocket(sock);
}
#endif

void *exsc_thr(void *arg)
{
    struct exsc_thrarg *thr_arg;
    struct exsc_srv *srv;
    int listen_sock;
    int opt;
    struct sockaddr_in srvaddr;
    int srvaddrsize;
    int newsock;
    char newsockaddr[INET_ADDRSTRLEN];
    int isbanaddr;
    int i;
    int readsize;
    time_t t;
    int sent;
    int begtime;
    int endtime;
    int waitms;

    thr_arg = arg;
    srv = &g_srvs[thr_arg->des];

    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    setsocknonblock(listen_sock);

    opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) != 0)
    {
        perror("ecsc setsockopt");
        exit(EXIT_FAILURE);
    }

    srvaddrsize = sizeof(srvaddr);

    memset(&srvaddr, 0, srvaddrsize);
    srvaddr.sin_family = AF_INET;
    srvaddr.sin_addr.s_addr = INADDR_ANY;
    srvaddr.sin_port = htons(srv->port);

    if (bind(listen_sock, (struct sockaddr *)&srvaddr, srvaddrsize) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, srv->inconcnt) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        begtime = gettimems();

        t = time(NULL);

        pthread_mutex_lock(&srv->mtx);

        while ((newsock = accept(listen_sock, (struct sockaddr *)&srvaddr, (socklen_t *)&srvaddrsize)) > 0)
        {
            inet_ntop(AF_INET, &srvaddr.sin_addr, newsockaddr, INET_ADDRSTRLEN);

            isbanaddr = 0;
            for (i = 0; i < srv->inconmax + 1; i++)
            {
                if (strcmp(srv->banlst[i], "") != 0)
                {
                    if (strcmp(srv->banlst[i], newsockaddr) == 0)
                    {
                        isbanaddr = 1;
                        break;
                    }
                }
                else
                {
                    break;
                }
            }

            if (isbanaddr == 0)
            {
                setsocknonblock(newsock);

                for (i = 0; i < srv->inconcnt; i++)
                {
                    if (srv->incons[i].sock == 0)
                    {
                        if (srv->inconmax < i)
                        {
                            srv->inconmax = i;
                            printf("%d connections count: %d\n", thr_arg->des, srv->inconmax);
                        }

                        srv->incons[i].excon.ix = i;
                        srv->incons[i].excon.id = getconid();
                        memcpy(srv->incons[i].excon.addr, newsockaddr, INET_ADDRSTRLEN);
                        srv->incons[i].sock = newsock;
                        srv->incons[i].recvbuf = exmalloc(srv->recvbufsize * sizeof(char), "exsc_thr recvbuf");
                        srv->incons[i].lastact = t;

                        break;
                    }
                }

                if (i < srv->inconcnt)
                {
                    srv->callback_newcon(srv->incons[i].excon);
                }
                else
                {
                    closesock(newsock);
                    printf("exsc_thr WARNING connections are over\n");
                }
            }
            else
            {
                closesock(newsock);
                printf("exsc_thr addr %s is banned\n", newsockaddr);
            }
        }

        for (i = 0; i < srv->inconmax + 1; i++)
        {
            if (srv->incons[i].sock != 0)
            {
                if (t - srv->incons[i].lastact < srv->timeout)
                {
                    if ((readsize = recv(srv->incons[i].sock, srv->incons[i].recvbuf, srv->recvbufsize, 0)) > 0)
                    {
                        srv->incons[i].lastact = t;
                        srv->callback_recv(srv->incons[i].excon, srv->incons[i].recvbuf, readsize);
                    }

                    if (srv->incons[i].sendbufsize > 0)
                    {
                        sent = send(srv->incons[i].sock,
                                    srv->incons[i].sendbuf + srv->incons[i].sent,
                                    srv->incons[i].sendbufsize - srv->incons[i].sent,
                                    MSG_NOSIGNAL);

                        if (sent > 0)
                        {
                            srv->incons[i].sent += sent;
                            if (srv->incons[i].sent == srv->incons[i].sendbufsize)
                            {
                                free(srv->incons[i].sendbuf);
                                srv->incons[i].sendbuf = NULL;
                                srv->incons[i].sendbufsize = 0;
                                srv->incons[i].sent = 0;
                            }
                        }
                    }
                }
                else
                {
                    srv->callback_closecon(srv->incons[i].excon);

                    closesock(srv->incons[i].sock);
                    free(srv->incons[i].recvbuf);
                    if (srv->incons[i].sendbuf != NULL)
                    {
                        free(srv->incons[i].sendbuf);
                    }
                    memset(&srv->incons[i], 0, sizeof(struct exsc_incon));
                }
            }
        }

        srv->callback_ext();

        pthread_mutex_unlock(&srv->mtx);

        endtime = gettimems();

        waitms = srv->timeframe - (endtime - begtime);
        if (waitms < 0)
        {
            waitms = 0;
        }
        sleepms(waitms);
    }

    free(thr_arg);
    return NULL;
}

void exsc_init(int maxsrvcnt)
{
    char buf[256];
    int hostname;
    struct hostent *host;

#ifdef _WIN32
    WSADATA wsaData = { 0 };
    int iResult = 0;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (iResult != 0)
    {
        wprintf(L"WSAStartup failed: %d\n", iResult);
        perror("WSAStartup");
        exit(EXIT_FAILURE);
    }
#endif

    g_maxsrvcnt = maxsrvcnt;
    g_srvs = exmalloc(g_maxsrvcnt * sizeof(struct exsc_srv), "exsc_init g_srvs");
    g_srvcnt = 0;
}

int exsc_start(uint16_t port, int timeout, int timeframe, int recvbufsize, int concnt,
                void newcon(struct exsc_excon con),
                void closecon(struct exsc_excon con),
                void recv(struct exsc_excon con, char *, int),
                void ext())
{
    int des = -1;
    struct exsc_srv *srv;
    struct exsc_thrarg *arg;

    if (g_srvcnt < g_maxsrvcnt)
    {
        des = g_srvcnt;
        srv = &g_srvs[des];

        g_srvcnt++;

        srv->port = port;
        srv->timeout = timeout;
        srv->timeframe = timeframe;
        srv->recvbufsize = recvbufsize;

        srv->inconcnt = concnt;
        srv->inconmax = 0;
        srv->incons = exmalloc(srv->inconcnt * sizeof(struct exsc_incon), "exsc_start incons");
        memset(srv->incons, 0, srv->inconcnt * sizeof(struct exsc_incon));

        srv->banlst = exmalloc(srv->inconcnt * INET_ADDRSTRLEN, "exsc_start banlst");
        memset(srv->banlst, 0, srv->inconcnt * INET6_ADDRSTRLEN);

        srv->callback_newcon = newcon;
        srv->callback_closecon = closecon;
        srv->callback_recv = recv;
        srv->callback_ext = ext;

        arg = exmalloc(sizeof(struct exsc_thrarg), "exsc_start arg");
        arg->des = des;

        // initialize mutex
        pthread_mutex_init(&srv->mtx, NULL);

        // strart the server thread
        pthread_create(&srv->thr, NULL, exsc_thr, arg);
    }
    else
    {
        printf("exsc_start ERROR need to increase max servers count in function exsc_init(maxsrvcnt)\n");
    }

    return des;
}

void exsend(struct exsc_srv *srv, struct exsc_excon *excon, char *buf, int bufsize)
{
    int newbufsize;
    char *newbuf;

    if (srv->incons[excon->ix].excon.id == excon->id)
    {
        if (srv->incons[excon->ix].sendbuf == NULL)
        {
            srv->incons[excon->ix].sendbufsize = bufsize;
            srv->incons[excon->ix].sendbuf = exmalloc(srv->incons[excon->ix].sendbufsize * sizeof(char), "exsend sendbuf");
            memcpy(srv->incons[excon->ix].sendbuf, buf, srv->incons[excon->ix].sendbufsize);
            srv->incons[excon->ix].sent = 0;
        }
        else
        {
            newbufsize = srv->incons[excon->ix].sendbufsize + bufsize;
            newbuf = exmalloc(newbufsize * sizeof(char), "exsend newbuf");

            memcpy(newbuf, srv->incons[excon->ix].sendbuf, srv->incons[excon->ix].sendbufsize);
            memcpy(newbuf + srv->incons[excon->ix].sendbufsize, buf, bufsize);

            free(srv->incons[excon->ix].sendbuf);

            srv->incons[excon->ix].sendbufsize = newbufsize;
            srv->incons[excon->ix].sendbuf = newbuf;
        }
    }
}

void exlock(struct exsc_srv *srv)
{
    if (!pthread_equal(pthread_self(), srv->thr))
    {
        pthread_mutex_lock(&srv->mtx);
    }
}

void exunlock(struct exsc_srv *srv)
{
    if (!pthread_equal(pthread_self(), srv->thr))
    {
        pthread_mutex_unlock(&srv->mtx);
    }
}

void exsc_send(int des, struct exsc_excon *excon, char *buf, int bufsize)
{
    struct exsc_srv *srv;

    srv = &g_srvs[des];

    exlock(srv);
    exsend(srv, excon, buf, bufsize);
    exunlock(srv);
}

void exsc_sendbyname(int des, char *conname, char *buf, int bufsize)
{
    struct exsc_srv *srv;
    int ix;
    struct exsc_excon excon;

    srv = &g_srvs[des];

    exlock(srv);
    for (ix = 0; ix < srv->inconmax + 1; ix++)
    {
        if (strcmp(srv->incons[ix].excon.name, conname) == 0)
        {
            excon = srv->incons[ix].excon;
            exsend(srv, &excon, buf, bufsize);
        }
    }
    exunlock(srv);
}

void exsc_setconname(int des, struct exsc_excon *excon, char *name)
{
    struct exsc_srv *srv;

    srv = &g_srvs[des];

    exlock(srv);
    if (srv->incons[excon->ix].excon.id == excon->id)
    {
        if (strlen(name) + 1 < EXSC_CONNAMELEN)
        {
            memcpy(srv->incons[excon->ix].excon.name, name, sizeof(srv->incons[excon->ix].excon.name) - 1);
        }
        else
        {
            printf("exsc_setconname WARNING name is too long");
        }
    }
    exunlock(srv);
}

void exsc_connect(int des, const char *addr, uint16_t port, struct exsc_excon *excon)
{
    struct exsc_srv *srv;
    struct sockaddr_in srvaddr;
    int srvaddrsize;
    int new_sock;
    int i;
    time_t t;
    struct hostent *host;

    t = time(NULL);

    if ((new_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    setsocknonblock(new_sock);

    if ((host = gethostbyname(addr)) == NULL)
    {
        perror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    srvaddrsize = sizeof(srvaddr);
    memset(&srvaddr, 0, srvaddrsize);
    srvaddr.sin_family = AF_INET;
    memcpy(&srvaddr.sin_addr.s_addr, host->h_addr, host->h_length);
    srvaddr.sin_port = htons(port);

    connect(new_sock, (struct sockaddr *)&srvaddr, srvaddrsize);

    srv = &g_srvs[des];

    exlock(srv);
    for (i = 0; i < srv->inconcnt; i++)
    {
        if (srv->incons[i].sock == 0)
        {
            if (srv->inconmax < i)
            {
                srv->inconmax = i;
                printf("%d connections count: %d\n", des, srv->inconmax);
            }

            srv->incons[i].excon.ix = i;
            srv->incons[i].excon.id = getconid();
            inet_ntop(AF_INET, &srvaddr.sin_addr, srv->incons[i].excon.addr, INET_ADDRSTRLEN);
            srv->incons[i].sock = new_sock;
            srv->incons[i].recvbuf = exmalloc(srv->recvbufsize * sizeof(char), "exsc_thr recvbuf");
            srv->incons[i].lastact = t;

            *excon = srv->incons[i].excon;

            break;
        }
    }

    if (i < srv->inconcnt)
    {
        srv->callback_newcon(srv->incons[i].excon);
    }
    else
    {
        closesock(new_sock);
        printf("exsc_thr WARNING connections are over\n");
    }
    exunlock(srv);
}

void exsc_banaddr(int des, const char *addr)
{
    struct exsc_srv *srv;
    int ix;

    srv = &g_srvs[des];

    exlock(srv);
    for (ix = 0; ix < srv->inconmax + 1; ix++)
    {
        if (strcmp(srv->banlst[ix], "") == 0)
        {
            memcpy(srv->banlst[ix], addr, strlen(addr));
            break;
        }
    }
    exunlock(srv);
}
