#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "tcp_utils.h"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s localhost <Port> N", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[2]);
    int N = atoi(argv[3]);
    tcp_client client = client_init("localhost", port);
    static char buf[4096];
    int Cylinders, SECTOR_PER_CYLINDER;
    // get disk information through I command
    strcpy(buf, "I\n");
    client_send(client, buf, strlen(buf) + 1);
    int n = client_recv(client, buf, sizeof(buf));
    sscanf(buf, "%d\ %d\n", &Cylinders, &SECTOR_PER_CYLINDER);
    printf("C %d S %d\n", Cylinders, SECTOR_PER_CYLINDER);
    for (int _ = 0; _ < N;_++) {
        char Command = rand() % 2 == 0 ? 'R' : 'W';
        if(Command == 'R'){
            int c = rand() % Cylinders;
            int s = rand() % SECTOR_PER_CYLINDER;
            sprintf(buf, "R %d %d\n", c, s);
        }
        else{
            int c = rand() % Cylinders;
            int s = rand() % SECTOR_PER_CYLINDER;
            int l = 1+ rand() % 256;
            char to_write[257];
            for (int i = 0; i < 256; i++) {
                if (i < l) to_write[i] = 'A' + rand() % 26;
                else to_write[i] = '\0';
            }
            sprintf(buf, "W %d %d %d", c, s,l);
            int len = strlen(buf);
            buf[len] = ' ';
            for (int i = 1; i <= l; i++)buf[len + i] = to_write[i - 1];
            buf[len + l + 1] = '\n';
            buf[len + l + 2] = 0;
            //printf(buf);
        }
        client_send(client, buf, strlen(buf) + 1);
        printf("%s", buf);
        printf("Command executed successfully!\n\n");
        int n = client_recv(client, buf, sizeof(buf));
        buf[n] = 0;
       
        if (strcmp(buf, "Bye!") == 0) break;
    }
    client_destroy(client);
}
