#ifndef EXSC_H
#define EXSC_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __linux__
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#elif _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <stdint.h>
#endif

#define EXSC_CONNAMELEN 64

// struct that uses at the top level (external connection)
struct exsc_excon
{
    int ix;                     // index in connections array
    int id;                     // unique identificator
    char addr[INET_ADDRSTRLEN]; // connection address
    char name[EXSC_CONNAMELEN]; // connection name
};

// initialize exsc core
// maxsrvcnt: servers count that will be use
void exsc_init(int maxsrvcnt);

// start the server
// port: server port
// timeout: the time after which the server disconnects client in case of inactivity
// timeframe: the time in wich the server must complete the request (10: faster but higher cpu usage, 100: slower but lower cpu usage)
// recvbufsize: The size of the buffer that the server thread will read at a time
// concnt: maximum connections count
// newcon: callback that is called when incoming new connection
// closecon: callback that is called when close connection
// recv: callback that is called when receive some data
// ext: callback that is called every iteration of the exsc loop
// return value: server descriptor
int exsc_start(uint16_t port, int timeout, int timeframe, int recvbufsize, int concnt,
               void newcon(struct exsc_excon excon),
               void closecon(struct exsc_excon excon),
               void recv(struct exsc_excon excon, char *buf, int bufsize),
               void ext());

// send data via connection
// des: server descitptor
// excon: connection that receives data
// buf: buffer with data
// bufsize: buffer size
void exsc_send(int des, struct exsc_excon *excon, char *buf, int bufsize);

// send data via name
// des: server descitptor
// conname: connection name that receives data
// buf: buffer with data
// bufsize: buffer size
void exsc_sendbyname(int des, char *conname, char *buf, int bufsize);

// set connection name
// des: server descitptor
// excon: connection
// name: new connectin name
void exsc_setconname(int des, struct exsc_excon *excon, char *name);

// connect to some server via address and port (after connect we will be able to send and receive data)
// des: server descitptor
// addr: connection address
// port: connection port
// excon: connection
void exsc_connect(int des, const char *addr, uint16_t port, struct exsc_excon *excon);

#ifdef __cplusplus
}
#endif
#endif // EXSC_H
