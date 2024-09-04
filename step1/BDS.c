#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <regex.h>
#include "tcp_utils.h"
int pre_c = -1;
// Block size in bytes
#define BLOCKSIZE 256
int ncyl, nsec, ttd;
char* diskfile;
// return a negative value to exit
int cmd_i(tcp_buffer *write_buf, char *args, int len) {
    static char buf[64];
    sprintf(buf, "%d %d", ncyl, nsec);

    // send to buffer, including the null terminator
    send_to_buffer(write_buf, buf, strlen(buf) + 1);
    return 0;
}
void raise_error(tcp_buffer* write_buf) {

    send_to_buffer(write_buf, "Parameter error!", 17);
}
int cmd_r(tcp_buffer *write_buf, char *args, int len) {
    int c, s;
    char* p = strtok(args, " \n\r");
    c = atoi(p);
    p = p + strlen(p) + 1;
    s = atoi(p);
    if (c >= ncyl || s >= nsec) {
        send_to_buffer(write_buf, "No", 3);
        return 0;
    }
        int dc;
    if(pre_c < 0) dc = c;
    else {
        dc = (c - pre_c > 0)?(c - pre_c):(c - pre_c);
        pre_c = c;
    }
    usleep(dc * ttd);
    int start = BLOCKSIZE * (c * nsec + s);
    static char buf[256];
   // sprintf(buf, "Yes ");
    //memcpy(&buf[4],  & diskfile[start], BLOCKSIZE);
   // sprintf(buf, "%d", c);
   memcpy(buf,& diskfile[start], BLOCKSIZE);
    send_to_buffer(write_buf, buf, sizeof(buf));
    return 0;
    
}

int Myparse(char* line,char* argv[],int lim){
    char* p;
    int argc = 0;
    p = strtok(line," ");
    while(p){
        argv[argc] = p;
        argc++;
        if(argc >= lim - 1)break;
        p = strtok(NULL," ");
    }
    if(argc >= lim - 1){
        argv[argc] = p + strlen(p)+1;
        argc++;
    }
    else{
        argv[argc] = NULL;
    }
    return argc;
}
int cmd_w(tcp_buffer *write_buf,char* args,int len){
    char* command[1000];
    int num = Myparse(args,command,4);
    int c = atoi(command[0]);
    int s = atoi(command[1]);
    int l = atoi(command[2]);
    printf("c s l %d %d %d\n",c,s,l);
    int dc;
    if(pre_c < 0) dc = c;
    else {
        dc = (c - pre_c > 0)?(c - pre_c):(c - pre_c);
        pre_c = c;
    }
    usleep(dc * ttd);
    static char yes[] = "Yes ";
    send_to_buffer(write_buf,yes,sizeof(yes));
    int offset = (c*nsec+s)*BLOCKSIZE;
    memcpy(&diskfile[offset],command[3],l);
    return 0;
}

int cmd_e(tcp_buffer *write_buf, char *args, int len) {
    send_to_buffer(write_buf, "Bye!", 5);
    return -1;
}

static struct {
    const char *name;
    int (*handler)(tcp_buffer *write_buf, char *, int);
} cmd_table[] = {
    {"I", cmd_i},
    {"R", cmd_r},
    {"W", cmd_w},
    {"E", cmd_e},
};

#define NCMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

void add_client(int id) {
    // some code that are executed when a new client is connected
    // you don't need this in step1
}

int handle_client(int id, tcp_buffer *write_buf, char *msg, int len) {
    char *p = strtok(msg, " \r\n");
    int ret = 1;
    for (int i = 0; i < NCMD; i++)
        if (strcmp(p, cmd_table[i].name) == 0) {
            ret = cmd_table[i].handler(write_buf, p + strlen(p) + 1, len - strlen(p) - 1);
            break;
        }
    if (ret == 1) {
        static char unk[] = "Unknown command";
        send_to_buffer(write_buf, unk, sizeof(unk));
    }
    if (ret < 0) {
        return -1;
    }
}

void clear_client(int id) {
    // some code that are executed when a client is disconnected
    // you don't need this in step2
}


int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <disk file name> <cylinders> <sector per cylinder> "
                "<track-to-track delay> <port>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }
    // args
    char *diskfname = argv[1];
    ncyl = atoi(argv[2]);
    nsec = atoi(argv[3]);
    ttd = atoi(argv[4]);  // ms
    int port = atoi(argv[5]);

    // open file
    int fd = open(diskfname, O_RDWR | O_CREAT, 0);
    if (fd < 0) {
        printf("Error: Could not open file '%s'.\n", diskfname);
        exit(-1);
    }
    // stretch the file
    long FILESIZE = BLOCKSIZE * ncyl * nsec;
    int result = lseek(fd, FILESIZE - 1, SEEK_SET);
    if (result == -1) {
        perror("Error calling lseek() to 'stretch' the file");
        close(fd);
        exit(-1);
    }
    result = write(fd, "", 1);
    if (result != 1) {
        perror("Error writing last byte of the file");
        close(fd);
        exit(-1);
    }
    // mmap
   
    diskfile = (char*)mmap(NULL, FILESIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, 0);
    if (diskfile == MAP_FAILED) {
        close(fd);
        printf("Error: Could not map file.\n");
        exit(-1);
    }
    // command
    tcp_server server = server_init(port, 1, add_client, handle_client, clear_client);
    server_loop(server);
}
