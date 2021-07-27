#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>

#define TEST_COUNTS 10
#define PORT 10000

// ./udp_latency 0
// ./udp_latency 1 server_host
int main(int argc, char** argv) {
    char buffer[100];
    int mode = atoi(argv[1]);
    // server
    if (mode == 0) {
        int sfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sfd == -1)
            printf("socket() failed\n");

        struct sockaddr_in servaddr;
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        servaddr.sin_port = htons(PORT);
        int bindret = bind(sfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
        if (bindret == -1)
            printf("bind() failed\n");

        int listenret = listen(sfd, 10);
        if (listenret == -1)
            printf("listen() failed\n");

        for (int i = 0; i < TEST_COUNTS; ++i) {
            ssize_t recvsize = recvfrom(sfd, buffer, 1, 0);
            ssize_t arksize = send(connfd, buffer, 1, 0);
            if (arksize == -1)
                printf("send() for ark failed\n");

            int closeret = close(connfd);
            if (closeret == -1)
                printf("close() for accept failed\n");
        }
        int closeret = close(sfd);
        if (closeret == -1)
            printf("close() for bind failed\n");
    }
    // client
    else {
        double total_sec = 0.0;
        for (int i = 0; i < TEST_COUNTS; ++i) {
            int cfd = socket(AF_INET, SOCK_STREAM, 0);
            if (cfd == -1)
                printf("socket() failed\n");

            struct sockaddr_in servaddr;
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(PORT);
            int inetret = inet_aton(argv[2], &servaddr.sin_addr);
            if (inetret == 0)
                printf("inet_aton() failed\n");

            struct timeval time1, time2;
            gettimeofday(&time1, NULL);
            ssize_t sendsize = sendto(cfd, buffer, 1, 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
            if (sendsize == -1)
                printf("sendto() failed\n");
            socklen_t addrlen;
            ssize_t recvsize = recvfrom(cfd, buffer, 1, 0, (struct sockaddr*)&servaddr, &addrlen);
            if (recvsize == -1)
                printf("recvfrom() for ark failed\n");
            gettimeofday(&time2, NULL);
            int sec = time2.tv_sec - time1.tv_sec;
            int usec = time2.tv_usec - time1.tv_usec;
            total_sec += (sec + usec * 1.0e-6);
            int closeret = close(cfd);
            if (closeret == -1)
                printf("close() failed\n");
        }
        total_sec /= TEST_COUNTS;
        printf("Average ping-pong time: %f ms\n", total_sec * 1.0e3);
    }
}