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

struct exsc_config
{
    uint16_t g_port;
    int g_timeout; // seconds
    int g_timeframe; // milliseconds
    int g_recvbufsize;

    int g_inconcnt;
    int g_inconmax;
    struct exsc_incon *g_incons;

    void (*callback_newcon)(struct exsc_excon con);
    void (*callback_closecon)(struct exsc_excon con);
    void (*callback_recv)(struct exsc_excon con, char *buf, int bufsize);

#ifdef __linux__
    pthread_t g_listenthr;
#elif _WIN32
    HANDLE g_listenthr;
#endif
};

struct exsc_listenthr_arg
{
    int des;
};

int g_maxconfigcnt;
int g_configcnt = 0;
struct exsc_config *g_configs;

void sleepms(int time)
{
#ifdef __linux__
    usleep(time * 1000);
#elif _WIN32
    Sleep(time);
#endif
}

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
    ioctlsocket(listen_sock, FIONBIO, &opt);
#endif
}

int getconid()
{
    static int conid = 0;
    conid++;
    return conid;
}

#ifdef __linux__
void *exsc_listenthr(void *arg)
#elif _WIN32
DWORD WINAPI exsc_listenthr(LPVOID arg)
#endif
{
    struct exsc_listenthr_arg *listenthr_arg;
    struct exsc_config *config;
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
    config = &g_configs[listenthr_arg->des];

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
    addr.sin_port = htons(config->g_port);
    addrsize = sizeof(addr);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, config->g_inconcnt) < 0)
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

            for (i = 0; i < config->g_inconcnt; i++)
            {
                if (config->g_incons[i].sock == 0)
                {
                    if (config->g_inconmax < i)
                    {
                        config->g_inconmax = i;
                    }

                    config->g_incons[i].excon.ix = i;
                    config->g_incons[i].excon.id = getconid();
                    inet_ntop(AF_INET, &addr.sin_addr, config->g_incons[i].excon.addr, INET_ADDRSTRLEN);
                    config->g_incons[i].sock = new_sock;
                    config->g_incons[i].recvbuf = malloc(config->g_recvbufsize * sizeof(char));
                    config->g_incons[i].lastact = t;

                    break;
                }
            }

            if (i < config->g_inconcnt)
            {
                config->callback_newcon(config->g_incons[i].excon);
            }
            else
            {
                close(new_sock);
                puts("exsc WARNING connections are over");
            }
        }

        for (i = 0; i < config->g_inconmax + 1; i++)
        {
            if (config->g_incons[i].sock != 0)
            {
                if (t - config->g_incons[i].lastact < config->g_timeout)
                {
                    if ((readsize = recv(config->g_incons[i].sock, config->g_incons[i].recvbuf, config->g_recvbufsize, 0)) > 0)
                    {
                        config->g_incons[i].lastact = t;
                        config->callback_recv(config->g_incons[i].excon, config->g_incons[i].recvbuf, readsize);
                    }

                    if (config->g_incons[i].sendready == 1)
                    {
                        sent = send(config->g_incons[i].sock,
                                    config->g_incons[i].sendbuf + config->g_incons[i].sent,
                                    config->g_incons[i].sendbufsize - config->g_incons[i].sent,
                                    MSG_NOSIGNAL);

                        if (sent > 0)
                        {
                            config->g_incons[i].sent += sent;
                            if (config->g_incons[i].sent == config->g_incons[i].sendbufsize)
                            {
                                free(config->g_incons[i].sendbuf);
                                config->g_incons[i].sendbuf = NULL;
                                config->g_incons[i].sendready = 0;
                                config->g_incons[i].sendbufsize = 0;
                                config->g_incons[i].sent = 0;
                            }
                        }
                    }
                }
                else
                {
                    config->callback_closecon(config->g_incons[i].excon);

                    close(config->g_incons[i].sock);
                    free(config->g_incons[i].recvbuf);
                    if (config->g_incons[i].sendbuf != NULL)
                    {
                        free(config->g_incons[i].sendbuf);
                    }
                    memset(&config->g_incons[i], 0, sizeof(struct exsc_incon));
                }
            }
        }

        endtime = gettimems();

        waitms = config->g_timeframe - (endtime - begtime);
        if (waitms < 0)
        {
            waitms = 0;
        }
        sleepms(waitms);
    }

    free(listenthr_arg);
    return NULL;
}

void exsc_init(int maxconfigcnt)
{
    g_maxconfigcnt = maxconfigcnt;
    g_configs = malloc(g_maxconfigcnt * sizeof(struct exsc_config));
    g_configcnt = 0;
}

int exsc_start(uint16_t port, int timeout, int timeframe, int recvbufsize, int concnt,
                void newcon(struct exsc_excon con),
                void closecon(struct exsc_excon con),
                void recv(struct exsc_excon con, char *, int))
{
    int des;
    struct exsc_config *config;
    struct exsc_listenthr_arg *arg;

    des = g_configcnt;
    config = &g_configs[des];

    g_configcnt++;

    config->g_port = port;
    config->g_timeout = timeout;
    config->g_timeframe = timeframe;
    config->g_recvbufsize = recvbufsize;

    config->g_inconcnt = concnt;
    config->g_inconmax = 0;
    config->g_incons = malloc(config->g_inconcnt * sizeof(struct exsc_incon));
    memset(config->g_incons, 0, config->g_inconcnt * sizeof(struct exsc_incon));

    config->callback_newcon = newcon;
    config->callback_closecon = closecon;
    config->callback_recv = recv;

    arg = malloc(sizeof(struct exsc_listenthr_arg));
    arg->des = des;

#ifdef __linux__
    pthread_create(&config->g_listenthr, NULL, exsc_listenthr, arg);
#elif _WIN32
    g_listenthr = CreateThread(NULL, 0, config->exsc_listenthr, arg, 0, NULL);
#endif

    return des;
}

void exsc_send(int des, struct exsc_excon *excon, char *buf, int bufsize)
{
    struct exsc_config *config;
    int newbufsize;
    char *newbuf;

    config = &g_configs[des];

    if (config->g_incons[excon->ix].excon.id == excon->id)
    {
        config->g_incons[excon->ix].sendready = 0;
        if (config->g_incons[excon->ix].sendbuf == NULL)
        {
            config->g_incons[excon->ix].sendbufsize = bufsize;
            config->g_incons[excon->ix].sendbuf = malloc(config->g_incons[excon->ix].sendbufsize * sizeof(char));
            memcpy(config->g_incons[excon->ix].sendbuf, buf, config->g_incons[excon->ix].sendbufsize);
            config->g_incons[excon->ix].sent = 0;
        }
        else
        {
            newbufsize = config->g_incons[excon->ix].sendbufsize + bufsize;
            newbuf = malloc(newbufsize * sizeof(char));

            memcpy(newbuf, config->g_incons[excon->ix].sendbuf, config->g_incons[excon->ix].sendbufsize);
            memcpy(newbuf + config->g_incons[excon->ix].sendbufsize, buf, bufsize);

            free(config->g_incons[excon->ix].sendbuf);

            config->g_incons[excon->ix].sendbufsize = newbufsize;
            config->g_incons[excon->ix].sendbuf = newbuf;
        }
        config->g_incons[excon->ix].sendready = 1;
    }
}

void exsc_sendbyname(int des, char *conname, char *buf, int bufsize)
{
    struct exsc_config *config;
    int ix;
    struct exsc_excon excon;

    config = &g_configs[des];

    for (ix = 0; ix < config->g_inconmax + 1; ix++)
    {
        if (strcmp(config->g_incons[ix].excon.name, conname) == 0)
        {
            excon = config->g_incons[ix].excon;
            exsc_send(des, &excon, buf, bufsize);
        }
    }
}

void exsc_setconname(int des, struct exsc_excon *excon, char *name)
{
    struct exsc_config *config;

    config = &g_configs[des];

    if (config->g_incons[excon->ix].excon.id == excon->id)
    {
        strcpy(config->g_incons[excon->ix].excon.name, name);
    }
}
