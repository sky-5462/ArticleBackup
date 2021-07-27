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
#define BUFFER_SIZE (1<<30)
#define MESSAGE_SIZE (1<<10)

// ./tcp_bandwidth 0
// ./tcp_bandwidth 1 server_host
int main(int argc, char** argv) {
    char* buffer = (char*)malloc(BUFFER_SIZE);
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
            struct sockaddr_in clientaddr;
            socklen_t addrlen = sizeof(clientaddr);
            int connfd = accept(sfd, (struct sockaddr*)&clientaddr, &addrlen);
            if (connfd == -1)
                printf("accept() failed\n");

            int index = 0;
            while (1) {
                ssize_t recvsize = recv(connfd, buffer + index, MESSAGE_SIZE, 0);
                index += recvsize;
                if (recvsize == 0)
                    break;
            }
            ssize_t acksize = send(connfd, buffer, 1, 0);
            if (acksize == -1)
                printf("send() for ack failed\n");

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
            int connret = connect(cfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
            if (connret == -1)
                printf("connect() failed\n");

            struct timeval time1, time2;
            gettimeofday(&time1, NULL);
            for (int j = 0; j < BUFFER_SIZE; j += MESSAGE_SIZE) {
                ssize_t sendsize = send(cfd, buffer + j, MESSAGE_SIZE, 0);
                if (sendsize == -1)
                    printf("send() failed\n");
            }
            int shutdownret = shutdown(cfd, SHUT_WR);
            if (shutdownret == -1)
                printf("shutdown() failed\n");
            ssize_t recvsize = recv(cfd, buffer, 1, 0);
            if (recvsize == -1)
                printf("recv() for ack failed\n");
            gettimeofday(&time2, NULL);
            int sec = time2.tv_sec - time1.tv_sec;
            int usec = time2.tv_usec - time1.tv_usec;
            total_sec += (sec + usec * 1.0e-6);
            int closeret = close(cfd);
            if (closeret == -1)
                printf("close() failed\n");
        }
        total_sec /= TEST_COUNTS;
        printf("Average transfer time: %f ms\n", total_sec * 1.0e3);
        printf("Average bandwidth: %f GB/s\n", BUFFER_SIZE * 1.0e-9/ total_sec);
    }
}