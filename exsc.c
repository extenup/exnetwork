#include "exsc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

#ifdef __linux__
#include <unistd.h>
#include <pthread.h>
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
    volatile int sendready; // ready to send
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

    void (*callback_newcon)(struct exsc_excon con);
    void (*callback_closecon)(struct exsc_excon con);
    void (*callback_recv)(struct exsc_excon con, char *buf, int bufsize);

#ifdef __linux__
    pthread_t thr;
#elif _WIN32
    DWORD thr;
#endif
};

struct exsc_thrarg
{
    int des; // server descriptor
};

int g_maxsrvcnt = 0;     // max servers count
int g_srvcnt = 0;        // servers count
struct exsc_srv *g_srvs; // servers

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
pthread_t crtthr()
{
    return pthread_self();
}

void closesock(int sock)
{
    close(sock);
}
#elif _WIN32
DWORD crtthr()
{
    return GetThreadId(GetCurrentThread());
}

void closesock(int sock)
{
    closesocket(sock);
}
#endif

#ifdef __linux__
void *exsc_thr(void *arg)
#elif _WIN32
DWORD WINAPI exsc_thr(LPVOID arg)
#endif
{
    struct exsc_thrarg *listenthr_arg;
    struct exsc_srv *serv;
    int listen_sock;
    int opt;
    struct sockaddr_in addr;
    int addrsize;
    int new_sock;
    int i;
    int readsize;
    time_t t;
    int sent;
    int begtime;
    int endtime;
    int waitms;

    listenthr_arg = arg;
    serv = &g_srvs[listenthr_arg->des];

    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    setsocknonblock(listen_sock);

    opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
    {
        perror("ecsc setsockopt");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(serv->port);
    addrsize = sizeof(addr);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, serv->inconcnt) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        begtime = gettimems();

        t = time(NULL);

        while ((new_sock = accept(listen_sock, (struct sockaddr *)&addr, (socklen_t *)&addrsize)) > 0)
        {
            setsocknonblock(new_sock);

            for (i = 0; i < serv->inconcnt; i++)
            {
                if (serv->incons[i].sock == 0)
                {
                    if (serv->inconmax < i)
                    {
                        serv->inconmax = i;
                        printf("%d connections count: %d\n", listenthr_arg->des, serv->inconmax);
                    }

                    serv->incons[i].excon.ix = i;
                    serv->incons[i].excon.id = getconid();
                    inet_ntop(AF_INET, &addr.sin_addr, serv->incons[i].excon.addr, INET_ADDRSTRLEN);
                    serv->incons[i].sock = new_sock;
                    serv->incons[i].recvbuf = malloc(serv->recvbufsize * sizeof(char));
                    serv->incons[i].lastact = t;

                    break;
                }
            }

            if (i < serv->inconcnt)
            {
                serv->callback_newcon(serv->incons[i].excon);
            }
            else
            {
                closesock(new_sock);
                printf("exsc_thr WARNING connections are over\n");
            }
        }

        for (i = 0; i < serv->inconmax + 1; i++)
        {
            if (serv->incons[i].sock != 0)
            {
                if (t - serv->incons[i].lastact < serv->timeout)
                {
                    if ((readsize = recv(serv->incons[i].sock, serv->incons[i].recvbuf, serv->recvbufsize, 0)) > 0)
                    {
                        serv->incons[i].lastact = t;
                        serv->callback_recv(serv->incons[i].excon, serv->incons[i].recvbuf, readsize);
                    }

                    if (serv->incons[i].sendready == 1)
                    {
                        serv->incons[i].sendready = 0;
                        sent = send(serv->incons[i].sock,
                                    serv->incons[i].sendbuf + serv->incons[i].sent,
                                    serv->incons[i].sendbufsize - serv->incons[i].sent,
                                    MSG_NOSIGNAL);

                        if (sent > 0)
                        {
                            serv->incons[i].sent += sent;
                            if (serv->incons[i].sent == serv->incons[i].sendbufsize)
                            {
                                free(serv->incons[i].sendbuf);
                                serv->incons[i].sendbuf = NULL;
                                serv->incons[i].sendready = 0;
                                serv->incons[i].sendbufsize = 0;
                                serv->incons[i].sent = 0;
                            }
                        }
                        serv->incons[i].sendready = 1;
                    }
                }
                else
                {
                    serv->callback_closecon(serv->incons[i].excon);

                    closesock(serv->incons[i].sock);
                    free(serv->incons[i].recvbuf);
                    if (serv->incons[i].sendbuf != NULL)
                    {
                        free(serv->incons[i].sendbuf);
                    }
                    memset(&serv->incons[i], 0, sizeof(struct exsc_incon));
                }
            }
        }

        endtime = gettimems();

        waitms = serv->timeframe - (endtime - begtime);
        if (waitms < 0)
        {
            waitms = 0;
        }
        sleepms(waitms);
    }

    free(listenthr_arg);
#ifdef __linux__
    return NULL;
#elif _WIN32
    return 0;
#endif
}

void exsc_init(int maxsrvcnt)
{
    g_maxsrvcnt = maxsrvcnt;
    g_srvs = malloc(g_maxsrvcnt * sizeof(struct exsc_srv));
    g_srvcnt = 0;
}

int exsc_start(uint16_t port, int timeout, int timeframe, int recvbufsize, int concnt,
                void newcon(struct exsc_excon con),
                void closecon(struct exsc_excon con),
                void recv(struct exsc_excon con, char *, int))
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
        srv->incons = malloc(srv->inconcnt * sizeof(struct exsc_incon));
        memset(srv->incons, 0, srv->inconcnt * sizeof(struct exsc_incon));

        srv->callback_newcon = newcon;
        srv->callback_closecon = closecon;
        srv->callback_recv = recv;

        arg = malloc(sizeof(struct exsc_thrarg));
        arg->des = des;

        // strart the server thread
#ifdef __linux__
        pthread_create(&srv->thr, NULL, exsc_thr, arg);
#elif _WIN32
        srv->thr = GetThreadId(CreateThread(NULL, 0, exsc_thr, arg, 0, NULL));
#endif
    }
    else
    {
        printf("exsc_start ERROR need to increase max servers count in function exsc_init(maxsrvcnt)\n");
    }

    return des;
}

void exsc_send(int des, struct exsc_excon *excon, char *buf, int bufsize)
{
    struct exsc_srv *srv;
    int newbufsize;
    char *newbuf;

    srv = &g_srvs[des];

    if (srv->incons[excon->ix].excon.id == excon->id)
    {

        int tries = 0; //!!!
        if (srv->thr != crtthr()) //!!!
        { //!!!
            char str[10000] = { 0 }; //!!!
            if (bufsize < 10000) //!!!
            { //!!!
                memcpy(str, buf, bufsize); //!!!
                printf("DIFFERENT THREDS %s\n", str); //!!!
            } //!!!
        } //!!!
        while (srv->thr != crtthr() && srv->incons[excon->ix].sendready == 0)
        {
            tries++; //!!!
            if (tries > 10) //!!!
            { //!!!
                printf("WARNING exsc_send %d %d\n", (int)srv->thr, (int)crtthr()); //!!!
            } //!!!
        }

        srv->incons[excon->ix].sendready = 0;
        if (srv->incons[excon->ix].sendbuf == NULL)
        {
            srv->incons[excon->ix].sendbufsize = bufsize;
            srv->incons[excon->ix].sendbuf = malloc(srv->incons[excon->ix].sendbufsize * sizeof(char));
            memcpy(srv->incons[excon->ix].sendbuf, buf, srv->incons[excon->ix].sendbufsize);
            srv->incons[excon->ix].sent = 0;
        }
        else
        {
            newbufsize = srv->incons[excon->ix].sendbufsize + bufsize;
            newbuf = malloc(newbufsize * sizeof(char));

            memcpy(newbuf, srv->incons[excon->ix].sendbuf, srv->incons[excon->ix].sendbufsize);
            memcpy(newbuf + srv->incons[excon->ix].sendbufsize, buf, bufsize);

            free(srv->incons[excon->ix].sendbuf);

            srv->incons[excon->ix].sendbufsize = newbufsize;
            srv->incons[excon->ix].sendbuf = newbuf;
        }
        srv->incons[excon->ix].sendready = 1;
    }
}

void exsc_sendbyname(int des, char *conname, char *buf, int bufsize)
{
    struct exsc_srv *srv;
    int ix;
    struct exsc_excon excon;

    srv = &g_srvs[des];

    for (ix = 0; ix < srv->inconmax + 1; ix++)
    {
        if (strcmp(srv->incons[ix].excon.name, conname) == 0)
        {
            excon = srv->incons[ix].excon;
            exsc_send(des, &excon, buf, bufsize);
        }
    }
}

void exsc_setconname(int des, struct exsc_excon *excon, char *name)
{
    struct exsc_srv *srv;

    srv = &g_srvs[des];

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
}
