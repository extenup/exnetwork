#ifndef EXSC_H
#define EXSC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifdef __linux__
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#elif _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#endif

#define EXSC_CONNAMELEN 64

struct exsc_excon
{
    int ix;
    int id;
    char addr[INET_ADDRSTRLEN];
    char name[EXSC_CONNAMELEN];
};

struct exsc_incon
{
    struct exsc_excon excon;
    int sock;

    char *recvbuf;

    volatile int sendready;
    int sendbufsize;
    char *sendbuf;
    int sent;

    time_t lastact;
};

void sleepms(int time);
int gettimems();

void exsc_init(int maxconfigcnt);
int exsc_start(uint16_t port, int timeout, int timeframe, int recvbufsize, int concnt,
                void newcon(struct exsc_excon excon),
                void closecon(struct exsc_excon excon),
                void recv(struct exsc_excon excon, char *buf, int bufsize));
void exsc_send(int des, struct exsc_excon *excon, char *buf, int bufsize);
void exsc_sendbyname(int des, char *conname, char *buf, int bufsize);
void exsc_setconname(int des, struct exsc_excon *excon, char *name);

#ifdef __cplusplus
}
#endif
#endif // EXSC_H

