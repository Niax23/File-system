#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <regex.h>
#include "tcp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#define BLOCKSIZE 256
#define MAX_INODE_MAP_INDEX 32  
#define MAX_BLOCK_MAP_INDEX 128
#define MAX_INODE_COUNT 1024            //32*32 = 1024
#define MAX_BLOCK_COUNT 4096            //128*32 = 4096
#define NO_DATA_BLOCKS 132   // 3 for superblock,128 for inode table,1 for user table
#define SUPER_SIZE 768 // superblock needs 3 blocks
#define NO_PARENT 1025 // an inode has no parent,then the id of its parent will be this
#define INODE_PER_BLOCK 8   // 256/32 = 8
#define INODE_TABLE_START_BLOCK 3 //
#define DIRECT_BLOCKS 8
#define DIR_ENTR 16
#define MAX_LINK_BLOCKS 136
#define MAX_FILE_SIZE MAX_LINK_BLOCKS * BLOCKSIZE
#define USERS 16








//对磁盘进行读写操作(也是与step1的server通信的部分)
tcp_client diskClient;
int CYLINDERS;
int SECTORS;

int read_block_from_disk(int block, char *buf){
  char command[BLOCKSIZE+1];  
  int c = block / SECTORS;
  int s = block % SECTORS; 
  sprintf(command, "R %d %d\n", c, s);
  client_send(diskClient, command, strlen(command) + 1);
  int n = client_recv(diskClient, buf, BLOCKSIZE);
  

  return n;
}

// //写一个block
// int write_block_to_disk(int startpoint, char *buf){
//   static char command[4096];  
//   int c = startpoint / SECTORS;
//   int s = startpoint % SECTORS; 
//   sprintf(command, "W %d %d %d", c, s,256);
//   static char b[256];
//   memcpy(b,buf,256);
//   int len = strlen(command);
//   command[len] = ' ';
//   memcpy(command + len + 1,buf,256);
//   command[len + 256 + 1] = '\n';
//   command[len + 256 + 2] = 0;
//   client_send(diskClient, command, strlen(command) + 1);
//   //printf("%s",command);
//   int n = client_recv(diskClient, command, sizeof(command));
//   printf("%s",command);
//   return n;
// }

//写多个连续的block
// int write_blocks_to_disk(int startpoint, char *buf, int blocknum){
  
//   for(int i = 0 ;i< blocknum;i++){
//     if(write_block_to_disk(startpoint + i, buf + i * BLOCKSIZE)<0)return -1;
//   }
//     return 0;
// }

int write_blocks_to_disk(int startpoint,char* buf,int blocknum){
    char command[2*BLOCKSIZE];
    char data[BLOCKSIZE];
    for(int i = 0;i < blocknum;i++){
        int c =(startpoint + i) / SECTORS;
        int s = (startpoint + i) % SECTORS;
        memcpy(data,buf+i*BLOCKSIZE,BLOCKSIZE);

        char temp[64];
        sprintf(temp,"W %d %d %d ",c,s,BLOCKSIZE);
        int len = strlen(temp);
        strcpy(command,temp);
        memcpy(command+len,buf + i * BLOCKSIZE,BLOCKSIZE);
        command[len+BLOCKSIZE] = 0;



        client_send(diskClient,command,len+BLOCKSIZE+1);

        int n = client_recv(diskClient,command,sizeof(command));
        command[n] = 0;
    }

return 0;

}








//superblock定义
typedef struct SuperBlock{
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_free_blocks_count;
    uint32_t s_root;
    uint32_t block_bitmap[128];  //按位记录block是否空闲，每个数含32位
    uint32_t inode_bitmap[32];  //按位记录inode是否空闲，每个数含32位
} SuperBlock; //660 bytes, 3 blocks

//定义inode
typedef struct inode{
    uint8_t i_mode;                     //0代表文件，1代表目录
    uint8_t i_link_count;               //已有的连接数量
    uint16_t i_size;                    //记录文件大小
    uint32_t i_timestamp;               //记录文件修改时间
    uint16_t i_parent;                  //父inode编号（65535表示无）
    uint16_t i_direct[8];               //直接块（记录block编号，block编号统一使用uine16_t）
    uint16_t i_single_indirect;         //一层间接块
    uint16_t i_double_indirect;
    uint16_t i_index;
} inode; //32 bytes

//UserNode定义
typedef struct UserNode{
    uint16_t dir_id; //用户主目录的inode id
    uint16_t valid;   //用户是否可用
    char username[12];

} UserNode; //16 bytes

//本文件系统的inode table
inode inode_table[MAX_INODE_COUNT]; // 1024 * 32 bytes,128 blocks

//本文件系统的superblock
SuperBlock mysuper;

UserNode user_table[USERS];
typedef struct dir_entry {   
    uint16_t inode_id;  //目录项对应的文件inode id
    uint8_t type;       //目录项是文件夹还是文件
    uint8_t used;       //该项是否正在使用     
    char name[12];      // 目录项名字,最长12字节
} dir_entry;  // 16bytes
//256bytes一个block，故每个目录共可以装16项







typedef struct {
    int client_id;               // 客户端ID
    int cur_dir_id;
    int cur_user_id;    
    char cur_path[30];
} client_context;

client_context client_table[__FD_SETSIZE];

sem_t mutex;




//superblock的相关函数

void superblock_renew(){
    char buf[SUPER_SIZE];
    memset(buf, 0, sizeof(buf));        //将superblock写入内存块1-3
    memcpy(buf, &mysuper, sizeof(mysuper));
    write_blocks_to_disk(0, buf, 3);
}


void superblock_init(){
     mysuper.s_inodes_count = 0; // no inodes used now
     mysuper.s_blocks_count = NO_DATA_BLOCKS; // 132 used to initialize
     mysuper.s_free_blocks_count = MAX_BLOCK_COUNT - NO_DATA_BLOCKS;
     mysuper.s_free_inodes_count = MAX_INODE_COUNT;
     memset(mysuper.block_bitmap,0,sizeof(mysuper.block_bitmap));//初始化两个bitmap，全部清零表示没有被用
     memset(mysuper.inode_bitmap,0,sizeof(mysuper.inode_bitmap));
     
     //把superblock占用的block在bitmap中表示出来
      for(int i = 0; i <= 3; i++)
        mysuper.block_bitmap[i] = ~0;          //全1
    mysuper.block_bitmap[4] = (0xf0000000);


    //把superblock写入disk
     superblock_renew();
}

//分配disk中空闲块，返回分配的空闲块的id,-1表示失败
int new_block(){

    if (!mysuper.s_free_blocks_count) return -1;
    for (int i = 4; i < MAX_BLOCK_MAP_INDEX; i++) {//132块分配给spb和inodetable，从4开始
        uint32_t bits = mysuper.block_bitmap[i];
        for (int j = 0; j < 32; j++){
            if ((bits >> (31-j)) & 1) continue; //此块不空闲
            else {
                mysuper.s_free_blocks_count--;
                mysuper.s_blocks_count++;
                mysuper.block_bitmap[i] |= 1 << (31-j);//bitmap置1，表示占用

                superblock_renew();
                return i*32+j;
            }
        }
    }
    return -1;
}


int block_free(int block_id){
    if(block_id< NO_DATA_BLOCKS)return -1; //不允许清空superblock和inodetable
    int i = block_id / 32;
    int j = block_id % 32;
    mysuper.block_bitmap[i] ^= 1 << (31-j);
    mysuper.s_blocks_count--;
    mysuper.s_free_blocks_count++;

    //是否要真正清空内存块？
    superblock_renew();

}


//inode相关函数




int inode_init(inode* node, uint16_t index, uint8_t mode, uint8_t link, uint16_t size, uint16_t parent){
    node->i_index = index;
    node->i_mode = mode;
    node->i_link_count = link;
    node->i_size = size;
    node->i_parent = parent;
    node->i_timestamp = time(NULL);
    memset(node->i_direct, 0, sizeof(node->i_direct));
    node->i_single_indirect = 0;
    node->i_double_indirect = 0;
    write_inode_to_disk(node, index);
    return 0;
}








int inode_table_init(){

  //initialize root inode
   inode root;
   root.i_mode = 1;
   root.i_link_count = 0;
   root.i_size = 0;
   root.i_timestamp = time(NULL);
   root.i_parent = NO_PARENT;
   memset(root.i_direct,0,sizeof(root.i_direct));
   root.i_single_indirect = 0;
   root.i_double_indirect = 0;
   
   root.i_index = 0;
   inode_table[0] = root;
   mysuper.s_free_inodes_count--;
   mysuper.s_inodes_count++;
   mysuper.inode_bitmap[0] = (1<<31);//最高位为1 其余全为0,目前只有root被使用
   printf("ok here\n");

   //initialize the rest inodes 作为可以用来分配的待用inode
   for(int i = 1;i < MAX_INODE_COUNT;i++){
     inode_init(&inode_table[i],i,0,0,0,NO_PARENT);
   }
 
   write_inode_to_disk(&inode_table[0],0);

   return 0;

}

int write_inode_to_disk(inode* node,int index){
 // printf("index %d");
  int block = INODE_TABLE_START_BLOCK + index / INODE_PER_BLOCK;
    int inode_start = sizeof(inode) * (index % INODE_PER_BLOCK);
    char buf[BLOCKSIZE];
    if (read_block_from_disk(block, buf) < 0) return -1;
    memcpy(buf + inode_start, node, sizeof(inode));
    write_blocks_to_disk(block, buf, 1);
    return 0;
}
int read_inode_from_disk(inode* node, uint16_t index){
    int block_id = INODE_TABLE_START_BLOCK + index / INODE_PER_BLOCK;
    int inode_start = sizeof(inode) * (index % INODE_PER_BLOCK);
    char buf[BLOCKSIZE];
    read_block_from_disk(block_id, buf);
    memcpy(node, &buf[inode_start], sizeof(inode));
    return 1;
}

//分配一个空闲的inode,返回id，失败返回-1
int new_inode(){
   if (!mysuper.s_free_inodes_count) return -1;
    for (int i = 0; i < MAX_INODE_MAP_INDEX; i++) {
        uint32_t bits = mysuper.inode_bitmap[i];
        for (int j = 0; j < 32; j++){
            if ((bits >> (31-j)) & 1) continue; //此inode不空闲
            else {
                mysuper.s_free_inodes_count--;
                mysuper.s_inodes_count++;
                mysuper.inode_bitmap[i] |= 1 << (31-j);//bitmap置1，表示占用

                superblock_renew();
                return i*32+j;
            }
        }
    }
    return -1;


}
//释放inode,释放Inode内部的内存块，文件夹则默认目录中所有内容都已释放，直接释放内存块这应该在更高层的函数中完成释放
int inode_free(int index){ //需要修改的有，inode信息，inode存储位置的block，inode bitmap
inode* node = &inode_table[index];
 if (node->i_index <= 0) return -1; 
    int i = node->i_index / 32;
    int j = node->i_index % 32;
    mysuper.inode_bitmap[i] ^= 1 << (31-j);    //修改inode table

    for(int i = 0;i < DIRECT_BLOCKS;i++){//清空direct blocks
        if(node->i_direct[i]!=0)block_free(node->i_direct[i]);
    }
    if(node->i_single_indirect!=0){ //清空single_indirect
        u_int16_t indirect_blocks[128];
        read_block_from_disk(node->i_single_indirect,indirect_blocks);
        for(int i = 0;i < 128;i++){
            if(indirect_blocks[i]!=0)block_free(indirect_blocks[i]);
        }
    }
    inode_init(&inode_table[index],index,0,0,0,NO_PARENT);
    mysuper.s_inodes_count--;
    mysuper.s_free_inodes_count++;

    superblock_renew();
    return 0;

}

//寻找目标名字的文件是否在文件夹中，成功返回inode id，失败-1
int check_in_dir(inode* dir, char* name, int type){
   if (dir->i_mode == 0) return -1;//dir不是文件夹
     printf("name to find:%s\n",name);
    char buf[BLOCKSIZE];
    dir_entry dir_entries[DIR_ENTR];
    for (int i = 0; i < DIRECT_BLOCKS; i++){
        if (dir->i_direct[i]==0) continue; //空块跳过
        read_block_from_disk(dir->i_direct[i], buf);
        memcpy(&dir_entries, buf, BLOCKSIZE);
        printf("direct %d  block: %d\n",i,dir->i_direct[i]);
        printf("direct %d not empty block\n",i);
         printf("get dir entries here and check:\n");
      for(int i = 0;i < 16;i++){
        printf("%d type: %d, used: %d,name %s,index: %d \n",i,dir_entries[i].type,dir_entries[i].used,dir_entries[i].name,dir_entries[i].inode_id);
      }
        for(int j = 0; j < DIR_ENTR; j++){
            
            if (dir_entries[j].used == 0) continue;
    //        printf("%d used\n",j);
            if (strcmp(dir_entries[j].name, name) == 0 && dir_entries[j].type == type){ 
                return dir_entries[j].inode_id;
            }
        }
    }
    return -1;
}
//在文件夹下创建文件/目录，成功返回新文件对应的inode id，失败-1
int mk_in_dir(inode* dir, char* name, uint8_t type){
    if (dir->i_mode == 0) return -1;//dir不是文件夹
    char buf[BLOCKSIZE];
    dir_entry dir_entries[DIR_ENTR];
    for (int i = 0; i < DIRECT_BLOCKS; i++){//先看分配的块有没有空位
        if (dir->i_direct[i]==0) continue; //优先看已分配的块是否有空entry，空块跳先过
        if (read_block_from_disk(dir->i_direct[i], buf)<0) continue;
        memcpy(&dir_entries, buf, BLOCKSIZE);
        for(int j = 0; j < DIR_ENTR; j++){
            if (dir_entries[j].used == 0){
              int newinode_id = new_inode();
              inode_init(&inode_table[newinode_id],newinode_id,type,0,0,dir->i_index);
              dir_entries[j].used = 1;
              dir_entries[j].type = type;
              dir_entries[j].inode_id = newinode_id;
              strcpy(dir_entries[j].name,name); 

              memcpy(buf,&dir_entries,BLOCKSIZE);
              write_blocks_to_disk(dir->i_direct[i],buf,1);
              write_inode_to_disk(dir,dir->i_index);
              return newinode_id;
            }
        }
    }
    printf("need new blocks\n");
    if(dir->i_link_count < DIRECT_BLOCKS){//还有空块，分配一块出来
    int empty_id;
    for(int i = 0;i < DIRECT_BLOCKS;i++){
      if(dir->i_direct[i] == 0){
        empty_id = i;
        break;
      }
    }//获得空块的位置
     printf("empty_id: %d\n",empty_id);
    int block_id = new_block();
     printf("block_id: %d\n",block_id);
    char buf[BLOCKSIZE];
    if(block_id >0){
      dir->i_direct[empty_id] = block_id;
      dir->i_link_count++;

      for(int i = 1; i< DIR_ENTR; i++){   
            dir_entries[i].used = 0;
            dir_entries[i].inode_id = 0;
              dir_entries[i].type = 0;
       }


      dir_entries[0].type = type;
      dir_entries[0].used = 1;
      strcpy(dir_entries[0].name, name);
      int newinode_id = new_inode();
       printf("newinode_id: %d\n",newinode_id);
      inode_init(&inode_table[newinode_id],newinode_id,type,0,0,dir->i_index);
      dir_entries[0].inode_id = newinode_id;
      printf("make dir entries here and check:\n");
      for(int i = 0;i < 16;i++){
      //  printf("%d type: %d, used: %d,name %s,index: %d \n",i,dir_entries[i].type,dir_entries[i].used,dir_entries[i].name,dir_entries[i].inode_id);
      }

      memcpy(buf,&dir_entries,BLOCKSIZE);
      write_blocks_to_disk(block_id,buf,1);
      write_inode_to_disk(dir,dir->i_index);
      return newinode_id;
    }
    
    }
    return -1;
  }

int dele_file_from_dir(inode* dir,char *args){
    if (dir->i_mode == 0) return -1;//dir不是文件夹
    char buf[BLOCKSIZE];
    dir_entry dir_entries[DIR_ENTR];
    for (int i = 0; i < DIRECT_BLOCKS; i++){
        if(!dir->i_direct[i]) continue;//空块
        int block_id = dir->i_direct[i];
        read_block_from_disk(block_id,buf);
        memcpy(&dir_entries,buf,BLOCKSIZE);
        printf("rm and get dir entries here and check:\n");
      for(int i = 0;i < 16;i++){
        printf("%d type: %d, used: %d,name %s,index: %d \n",i,dir_entries[i].type,dir_entries[i].used,dir_entries[i].name,dir_entries[i].inode_id);
      }


        for(int i = 0;i<DIR_ENTR;i++){
            if(strcmp(dir_entries[i].name,args)==0 && dir_entries[i].type == 0){
                
                printf("gonna free inode\n");

              inode_free(dir_entries[i].inode_id);
              printf("ok here\n");
              dir_entries[i].used = 0;
              dir->i_timestamp = time(NULL);

              memcpy(buf,&dir_entries,BLOCKSIZE);
              write_blocks_to_disk(block_id,buf,1);
              write_inode_to_disk(dir,dir->i_index);
              return 0;
            }
        }
    }
    return -1; //没找着文件

}
//检查文件夹是否为空，为空则将空的目录项所占的内存块释放，返回0，不为空则返回-1
int deal_empty_dir(int id){
    inode* node =&inode_table[id];
    char buf[BLOCKSIZE];
    dir_entry dir_entries[DIR_ENTR];
    for(int i = 0;i < DIRECT_BLOCKS;i++){
        if(node->i_direct[i] == 0)continue;
        read_block_from_disk(node->i_direct[i],buf);
        memcpy(&dir_entries,buf,BLOCKSIZE);
        for(int j = 0;j < DIR_ENTR;j++){
            if(dir_entries[j].used)return -1;
        }
        //本块16个目录项全为空，释放该块
        block_free(node->i_direct[i]);
        node->i_direct[i] = 0;
    }
    return 0;
}
int dele_dir_from_dir(inode* dir,char *args){
    if (dir->i_mode == 0) return -1;//dir不是文件夹
    char buf[BLOCKSIZE];
    dir_entry dir_entries[DIR_ENTR];
    for (int i = 0; i < DIRECT_BLOCKS; i++){
        if(!dir->i_direct[i]) continue;//空块
        int block_id = dir->i_direct[i];
        read_block_from_disk(block_id,buf);
        memcpy(&dir_entries,buf,BLOCKSIZE);
        printf("rmdir and get dir entries here and check:\n");
      for(int i = 0;i < 16;i++){
        printf("%d type: %d, used: %d,name %s,index: %d \n",i,dir_entries[i].type,dir_entries[i].used,dir_entries[i].name,dir_entries[i].inode_id);
      }


        for(int i = 0;i<DIR_ENTR;i++){
            if(strcmp(dir_entries[i].name,args)==0 && dir_entries[i].type == 1){
                if(deal_empty_dir(dir_entries[i].inode_id) < 0)return -2;
                
                printf("gonna free inode\n");

              inode_free(dir_entries[i].inode_id);
              dir_entries[i].used = 0;
              dir->i_timestamp = time(NULL);

              memcpy(buf,&dir_entries,BLOCKSIZE);
              write_blocks_to_disk(block_id,buf,1);
              write_inode_to_disk(dir,dir->i_index);
              return 0;
            }
        }
    }
    return -1; //没找着文件

}
int cmp_dirs_name(const void *a, const void *b) {
    const dir_entry *entry_a = *(const dir_entry **)a;
    const dir_entry *entry_b = *(const dir_entry **)b;
    return strcmp(entry_a->name, entry_b->name);
}

int list_inodes_in_dir(inode* dir,char* output){
    // for(int i = 0;i <10;i++){
    //     printf("%s\n",inode_table[i].)
    // }
    dir_entry dir_entries[DIR_ENTR];
    char buf[BLOCKSIZE];
    dir_entry* diretories[DIR_ENTR * DIRECT_BLOCKS];
    dir_entry* files[DIR_ENTR*DIRECT_BLOCKS];
    int num_dir = 0;
    int num_file = 0;
    for(int i = 0;i < DIRECT_BLOCKS;i++){
        if(dir->i_direct[i]!= 0){
            printf("direct %d not zero\n",i);
            read_block_from_disk(dir->i_direct[i],buf);
            memcpy(&dir_entries,buf,BLOCKSIZE);
            for(int j = 0;j <DIR_ENTR;j++){
                if(dir_entries[j].used==0)continue;
                printf("name %s\n",dir_entries[j].name);
                if(dir_entries[j].type == 0) files[num_file++] = &dir_entries[j];
                else diretories[num_dir++] = &dir_entries[j];

            }

        }
    }
        for(int i = 0;i < num_dir;i++){
        printf("%s",diretories[i]->name);
    }
    qsort(diretories,num_dir,sizeof(diretories[0]),cmp_dirs_name);
    qsort(files,num_file,sizeof(diretories[0]),cmp_dirs_name);
  
    output[0] = '\0';
    for(int i = 0;i < num_dir;i++){
        strcat(output,"dire ");
        inode* node = &inode_table[diretories[i]->inode_id];
        char size_str[20];  // 缓冲区，用于存储大小字符串
        sprintf(size_str, "%u", node->i_size);
        strcat(output,size_str);
        strcat(output," ");
        struct tm *timeinfo;
        char buffer[20];  // 用于存储时间字符串的缓冲区
        time_t rawtime = (time_t)node->i_timestamp;
        timeinfo = localtime(&rawtime);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        strcat(output,buffer);
        strcat(output," ");
        strcat(output,diretories[i]->name);
        strcat(output,"\n");
    
    }
    for(int i = 0;i < num_file;i++){
         strcat(output,"file ");
        inode* node = &inode_table[files[i]->inode_id];
        char size_str[20];  // 缓冲区，用于存储大小字符串
        sprintf(size_str, "%u", node->i_size);
        strcat(output,size_str);
        strcat(output," ");
        struct tm *timeinfo;
        char buffer[20];  // 用于存储时间字符串的缓冲区
        time_t rawtime = (time_t)node->i_timestamp;
        timeinfo = localtime(&rawtime);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        strcat(output,buffer);
        strcat(output," ");
        strcat(output,files[i]->name);
        strcat(output,"\n");
    }

    return 0;

}
int read_from_file_node(int id,char* cont){
    inode* node = &inode_table[id];
    int size = node->i_size;
    char buf[BLOCKSIZE];
    int blocks = node->i_link_count;
    printf("when read size %d links %d\n",size,blocks);
    for(int i = 0;i < ((blocks < DIRECT_BLOCKS)?blocks:DIRECT_BLOCKS);i++){
        read_block_from_disk(node->i_direct[i],buf);
        memcpy(cont + i * BLOCKSIZE,buf,BLOCKSIZE);
    }
    if(blocks > DIRECT_BLOCKS){
        uint16_t indirect_blocks[128];
        read_block_from_disk(node->i_single_indirect,buf);
        memcpy(indirect_blocks,buf,BLOCKSIZE);
        for(int i = 0;i < blocks - DIRECT_BLOCKS;i++){
            read_block_from_disk(indirect_blocks[i],buf);
            memcpy(cont + (i + DIRECT_BLOCKS) * BLOCKSIZE,buf,BLOCKSIZE);
        }
    }
    cont[size] = 0;
    return 0;
}

int write_to_file_inode(int id,char* input,int len){
    uint16_t indirect_blocks[128];// block id 是u16，即2byte，256byte的block可以装下128个id
    printf("id in write %d\n",id);
    inode* node = &inode_table[id];
    char buf[BLOCKSIZE];
    if (node->i_mode) return -1;    //不是文件则退出
    int cur_blocks = node->i_link_count;
    int blocks_needed = (len + BLOCKSIZE - 1)/BLOCKSIZE;//向上取整
    if(blocks_needed > mysuper.s_free_blocks_count)return -1; //文件内存不够

    if(cur_blocks > blocks_needed){//需要释放一些block
        if(cur_blocks<=DIRECT_BLOCKS){
           for(int i = blocks_needed;i < cur_blocks;i++){
            block_free(node->i_direct[i]);
        }
      }
        else if(cur_blocks > DIRECT_BLOCKS && blocks_needed <= DIRECT_BLOCKS){
             for(int i = blocks_needed;i < DIRECT_BLOCKS;i++){
            block_free(node->i_direct[i]);
        }
        //释放indirect
        read_block_from_disk(node->i_single_indirect,buf);
        memcpy(indirect_blocks,buf,BLOCKSIZE);
        int rest = cur_blocks - DIRECT_BLOCKS;
        for(int i = 0;i < rest;i++){
            block_free(indirect_blocks[i]);
            indirect_blocks[i] = 0;
        }
        block_free(node->i_single_indirect);
        node->i_single_indirect = 0;

    }
    else if(blocks_needed > DIRECT_BLOCKS){
                read_block_from_disk(node->i_single_indirect,buf);
        memcpy(indirect_blocks,buf,BLOCKSIZE);
        int rest = cur_blocks - DIRECT_BLOCKS;
        for(int i = blocks_needed - DIRECT_BLOCKS;i < rest;i++){
            block_free(indirect_blocks[i]);
            indirect_blocks[i] = 0;
        }
        write_blocks_to_disk(node->i_single_indirect,indirect_blocks,1);

    }

    }
    else if(cur_blocks < blocks_needed){//需要再申请一些block
    if(blocks_needed <= 8){
        for(int i = cur_blocks;i < blocks_needed;i++){
            int new_id = new_block();
            if(new_id < 0) return -1;
            node->i_direct[i] = new_id;
        }
    } 
    else if(blocks_needed > 8 && cur_blocks <= 8){
              for(int i = cur_blocks;i < DIRECT_BLOCKS;i++){
            int new_id = new_block();
            if(new_id < 0) return -1;
            node->i_direct[i] = new_id;
        }
        node->i_single_indirect = new_block();
         memcpy(indirect_blocks,buf,BLOCKSIZE);
         int rest = blocks_needed - DIRECT_BLOCKS;
         for(int i = 0;i < rest;i++){
            int new_id = new_block();
            if(new_id < 0)return -1;
            indirect_blocks[i] = new_id;
         }
    }
    else if(cur_blocks > DIRECT_BLOCKS){
        read_block_from_disk(node->i_single_indirect,buf);
        memcpy(indirect_blocks,buf,BLOCKSIZE);
        for(int i = cur_blocks - DIRECT_BLOCKS;i < blocks_needed - DIRECT_BLOCKS;i++){
            indirect_blocks[i] = new_block();
        }

    }
    if(blocks_needed > DIRECT_BLOCKS)write_blocks_to_disk(node->i_single_indirect,indirect_blocks,1);

    }
    //write,此时需要的block全部分配好
    for(int i = 0;i < ((blocks_needed < DIRECT_BLOCKS)?blocks_needed:DIRECT_BLOCKS);i++){
        memcpy(buf,input + i * BLOCKSIZE,BLOCKSIZE);
        write_blocks_to_disk(node->i_direct[i],buf,1);
    }
    if(blocks_needed > DIRECT_BLOCKS){
        read_block_from_disk(node->i_single_indirect,buf);
        memcpy(indirect_blocks,buf,BLOCKSIZE);
        for(int i = 0;i < blocks_needed - DIRECT_BLOCKS;i++){
            memcpy(buf,input + (DIRECT_BLOCKS + i) * BLOCKSIZE,BLOCKSIZE);
            write_blocks_to_disk(indirect_blocks[i],buf,1);
        }
    }
    node->i_link_count = blocks_needed;
    node->i_timestamp = time(NULL);
    node->i_size = strlen(input);
    printf("d0: %d\n d1: %d\n links: %d\n size: %d\n",node->i_direct[0],node->i_direct[1],node->i_link_count,node->i_size);
    write_inode_to_disk(node,id);
  
    return 0;
}



bool formatted = 0; //是否初始化
int cur_user_id;
uint16_t cur_dir = 0;//当前目录对应inode的index  
char cur_path[25562] = "/";
bool logged = 0;



int load_state(){
    //load superblock
    char spbuf[SUPER_SIZE];
    read_block_from_disk(0,spbuf);
    read_block_from_disk(1,spbuf + BLOCKSIZE);
    read_block_from_disk(2,spbuf + 2 * BLOCKSIZE);
    memcpy(&mysuper,spbuf,660);
    //load inode table
       for (int i = 0; i < MAX_INODE_COUNT; i++){  //写回inode table到diskfile
                if (read_inode_from_disk(&inode_table[i], i)<0) {
                        printf("Read Inode failed.\n");
                        break;
                    }
                }
    char buf[BLOCKSIZE];
    read_block_from_disk(NO_DATA_BLOCKS - 1,buf);
    memcpy(&user_table,buf,BLOCKSIZE);
    formatted = user_table[0].valid;
    return 0;

}


// f命令，文件系统初始化

int handle_f(tcp_buffer *write_buf, char *args, int len){
    superblock_init();                             //初始化super block
   // printf("spb init done");
    cur_dir = 0;                        //初始化当前目录为root
    strcpy(cur_path, "/");
    if(inode_table_init() < 0){
      //error
      send_to_buffer(write_buf, "init failed", 12);
      return 0;
    }
    UserNode* node;
    for(int i = 0;i < USERS;i++){
        node = &user_table[i];
        if(!i){
            node->dir_id = 0;
            char rname[10] = "root";
            strcpy(node->username,rname);
            node->valid = 1;
        }
        else{
            node->dir_id = NO_PARENT;
            node->valid = 0;
        }
    }
    write_blocks_to_disk(NO_DATA_BLOCKS - 1,&user_table,1);

    formatted = 1;
    cur_user_id = 0;
      char inf[1000] = "Format done";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    printf("%d\n",mysuper.s_inodes_count);
    return 0;
}


int handle_mk(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    printf("cur_dir: %d",cur_dir);
   if(args[0]==0 || args[0] == '\n'){
        static char errormsg[] = "the file has no name!";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    
    if (check_in_dir(&inode_table[cur_dir], args, 0) >=0 ) {   //重名
       char inf[1000] = "the file already exists";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0; 
    }

    if (mk_in_dir(&inode_table[cur_dir], args, 0) < 0){
         static char errormsg[] = "fail to make file";
         send_to_buffer(write_buf, errormsg, sizeof(errormsg));
    }
    else{
          char inf[1000] = "file made";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    }
       return 0;
}

int handle_mkdir(tcp_buffer *write_buf, char *args, int len){
   if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
       if(args[0]==0 || args[0] == '\n'){
          char inf[1000] = "the directory has no name";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
       if (check_in_dir(&inode_table[cur_dir], args, 1) >=0 ) {   //重名

       char inf[1000] = "the directory already exists";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        return 0; 
    }
     if (mk_in_dir(&inode_table[cur_dir], args, 1) < 0){
       static char errormsg[] = "fail to make directory";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
     }
     else{
          char inf[1000] = "directory made!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

     }
       return 0;
    
}

int handle_rm(tcp_buffer *write_buf, char *args, int len){
  if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
      if(args[0]==0 || args[0] == '\n'){
        static char errormsg[] = "the file has no name!";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    if (check_in_dir(&inode_table[cur_dir], args, 0) <0 ) {   //没找着
         char inf[1000] = "file not found";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

        return 0; 
    }
    if(dele_file_from_dir(&inode_table[cur_dir],args)<0){
             static char inf[] = "delete failed";
    send_to_buffer(write_buf, inf, sizeof(inf));
    }
      char inf[1000] = "file removed";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

    return 0;
}
int handle_rmdir(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    if(args[0]==0 || args[0] == '\n'){
        char inf[1000] = "the directory has no name!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    int res = check_in_dir(&inode_table[cur_dir], args, 1);
    if (res < 0) {   //没找着
    char inf[1000] = "directory not found";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        return 0; 
    }
    for(int i = 1;i < USERS;i++){
        if(user_table[i].dir_id == res){
              char inf[1000] = "cannot delete user home directory";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        return 0; 
        }

    }
    if(dele_dir_from_dir(&inode_table[cur_dir],args)<0){
      char inf[1000] = "delete failed,the directory is not empty";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    return 0;
    }
     
     char inf[1000] = "directory removed";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));


    return 0;
}
int handle_ls(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    char out[10000];
    memset(out,0,9999);
    printf("in ls cur id:%d\n",cur_dir);
    list_inodes_in_dir(&inode_table[cur_dir],out);
    int pos = strlen(out);
    out[pos] = '\n ';
    if(!cur_user_id){
    memcpy(out + pos,"(root)",6);
    memcpy(out+pos+6,cur_path,strlen(cur_path));
    }
    else memcpy(out+pos,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,out,strlen(out)+1);
    return 0;
}
int handle_cd(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    if(args[0]==0 || args[0] == '\n'){
      char inf[1000] = "the directory has no name!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    uint16_t ori_dir = cur_dir;     //先保存当前dir_id和路径字符串
    char ori_path[1000]; 
    strcpy(ori_path, cur_path);
        char* path = strtok(args, "/"); 
       printf("curpath: %s\n",cur_path);
    while (path != NULL && path[0] != ' ') {
        if (strcmp(path, "..") == 0) {  //回到上一级
        if(cur_user_id && user_table[cur_user_id].dir_id == cur_dir){
             char inf[1000] = "permission denied";
             int pos = strlen(inf);
             inf[pos] = '\n';
             if(!cur_user_id){
             memcpy(inf + pos + 1,"(root)",6);
             memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
            else memcpy(inf+pos+1,cur_path,strlen(cur_path));
            send_to_buffer(write_buf,inf,sizeof(inf));
                return 0; 
        }
            uint16_t parent = inode_table[cur_dir].i_parent;
            if (parent != NO_PARENT) {    
                cur_dir = parent; 

                if (strcmp(cur_path, "/") != 0) {
                    char* last_slash = strrchr(cur_path, '/');
                    *last_slash = '\0';
                   
                }
                if (parent == 0) strcpy(cur_path, "/");
            }
        } else if (strcmp(path, ".") != 0) {    //.代表当前目录，无视之
        printf("path: %s\n",path);
            int result = check_in_dir(&inode_table[cur_dir], path, 1);
            if (result < 0) {
             
                cur_dir = ori_dir;  // 找不到对应文件夹，回退
                strcpy(cur_path, ori_path);
                char inf[1000] = "directory not found";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
                return 0; 
            }
            int l = strlen(cur_path);
               printf("curpath: %s\n",cur_path);
           // cur_path[l - 1] = '\0';
            if (cur_dir!=0) strcat(cur_path, "/");
            cur_dir = result;
            strcat(cur_path, path);
        }
        path = strtok(NULL, "/"); 
    //    for(int i = 0;i < strlen(path);i++)printf("i %d char %c ",i,path[i]);
    }
    send_to_buffer(write_buf,cur_path,strlen(cur_path));
    return 0;
}

int handle_cat(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    if(args[0]==0 || args[0] == '\n'){
      char inf[1000] = "the directory has no name!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    int target = check_in_dir(&inode_table[cur_dir],args,0);
    if(target < 0){
       char inf[1000] = "file not exist!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0;
    }
    char cont[MAX_FILE_SIZE];
    read_from_file_node(target,cont);
     int pos = strlen(cont);
    cont[pos] = '\n';
    if(!cur_user_id){
    memcpy(cont + pos + 1,"(root)",6);
    memcpy(cont+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(cont+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,cont,strlen(cont));
    return 0;
}
int handle_w(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    char data[4096];
    memset(data,0,sizeof(data));
    int l;
    char name[12];
    sscanf(args, "%s %d %[^\n]", name, &l, data);
    printf("%s %d %s\n",name,l,data);
    if(l != strlen(data)){
              char inf[1000] = "wrong arguments";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0;
    }
    int id = check_in_dir(&inode_table[cur_dir],name,0);
    if(id < 0){
        char inf[1000] = "file not exist!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0;
    }
    int filesize = inode_table[id].i_size;
    if(filesize + l > MAX_FILE_SIZE){
      char inf[1000] = "exceed max size!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0;
    }
    if(write_to_file_inode(id,data,l) < 0){
         static char errormsg[] = "write failed";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
      return 0;
    }


      char inf[1000] = "write finished!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    return 0;
}
//通过cat和w实现
int handle_i(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    char filename[16];
    int l = 0, pos = 0;
    char data[4096];
    sscanf(args, "%s %d %d %[^\n]", filename, &pos, &l, data);
    if(l != strlen(data)){
      static char errormsg[] = "data and length dosent match";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
      return 0;
    }
    int id;//插入文件节点的id
     if ((id = check_in_dir(&inode_table[cur_dir], filename, 0)) < 0){   //文件不存在
         char inf[1000] = "file not exist!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        return 0;
    }
    
    inode* node = &inode_table[id];
    int filesize = node->i_size;
    if(filesize < pos){ //pos重定位成文件末尾
      pos = filesize; 
    }
    char readBuf[4096];
    if(read_from_file_node(id,readBuf)<0){
      static char errormsg[] = "read failed";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
      return 0;
    }
    int new_size = filesize + l;
    if (new_size > 4095){
       char inf[1000] = "exceed max size!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        return 0;
    }  
    
    // 在指定位置插入数据
    memmove(readBuf + pos + l, readBuf + pos, filesize - pos); // 向后移动数据
    memcpy(readBuf + pos, data, l); // 插入数据
    readBuf[new_size] = '\0';
    printf("id in insert: %d\n",id);

    if (write_to_file_inode(id, readBuf,new_size)<0) 
        {
      static char errormsg[] = "write failed";
      send_to_buffer(write_buf, errormsg, sizeof(errormsg));
        }
    else {
         char inf[1000] = "insert finished";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    }

    return 0;
}
int handle_d(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    char filename[16];
    char data[4096];
    int l = 0, pos = 0;
    sscanf(args, "%s %d %d", filename, &pos, &l);
    int id;
    if ((id = check_in_dir(&inode_table[cur_dir], filename, 0)) < 0){   //文件不存在
    char inf[1000] = "file doesnt exist";
    int pos = strlen(inf);
    inf[pos] = '\n';
    memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        return 0;
    }
    printf("id: %d\n",id);
    inode* node = &inode_table[id];
    int filesize = node->i_size;
    printf("filesize: %d\n",filesize);
    printf("pos: %d\n",pos);
    if (pos > filesize) {//删除位置越界
      char inf[1000] = "delete out of bound!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0;
     }
    if (pos + l > filesize) l = filesize - pos; //长度超过则定位到文件末尾
    char readBuf[4096];
    if(read_from_file_node(id,readBuf) < 0){
        static char inf[1000] = "read failed";
      int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
      return 0;

    }
    memcpy(data, readBuf, pos);
    memcpy(data + pos, readBuf + pos + l, filesize - pos - l);
    data[filesize-l] = '\0';
    if (write_to_file_inode(id, data,sizeof(data))<0) {
         static char inf[1000] = "write failed";
         int pos = strlen(inf);
         inf[pos] = '\n';
         if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
         send_to_buffer(write_buf,inf,sizeof(inf));

    }
    else {
      char inf[1000] = "delete finished";
      int pos = strlen(inf);
      inf[pos] = '\n';
      if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
      send_to_buffer(write_buf,inf,sizeof(inf));
    }

    return 0;
}
int handle_su(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
     bool root = 0;
     if(args[0]==0 || args[0] == '\n'){
       root = 1;
    }
    int dir_to_change = 0;
    int user_id;
    char path[1000] = "/";
    bool found = 0;
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    printf("args: %s\n",args);
    if(!root){for(int i = 1;i < USERS;i++){
        UserNode* node = &user_table[i];
        if(node->valid && strcmp(node->username,args) == 0){
            found = 1;
            dir_to_change = node->dir_id;
            user_id = i;
            strcat(path,args);
        }

      }
    }
    else {
        dir_to_change = cur_dir;
        user_id = 0;
        strcpy(path,cur_path);
    }
    if(!found && !root){
        char inf[1000] = "user not found";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

    return 0;

    }
    strcpy(cur_path,path);
    cur_user_id = user_id;
    cur_dir = dir_to_change;
    printf("now the cur dir id %d",cur_dir);

    char inf[1000] = "user changed";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

    return 0;

}
int handle_adduser(tcp_buffer *write_buf, char *args, int len){
    if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
    if(cur_user_id){
    char inf[1000] = "Permission denied,you need to log in the root user";
    int pos = strlen(inf);
    inf[pos] = '\n';
    memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));


      return 0; 
    }
    if(args[0]==0 || args[0] == '\n'){
      char inf[1000] = "need user name!";
    int pos = strlen(inf);
    inf[pos] = '\n';
   if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    bool made = 0;
    int empty_index = -1;
     for(int i = 0;i < USERS;i++){
        UserNode* node = &user_table[i];
        if(empty_index < 0 && !node->valid)empty_index = i;
        if(node->valid && strcmp(args,node->username) == 0){
             char inf[1000] = "user exists";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    return 0;

        }
     }
   
        if(empty_index >= 0){
            UserNode* node = &user_table[empty_index];
            int id = mk_in_dir(&inode_table[0],args,1);
            node->valid = 1;
            node->dir_id = id;
            strcpy(node->username,args);
            made = 1;
            char inf[1000] = "user created!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
        }
    
   else{
    char inf[1000] = "exceed max users";
    int pos = strlen(inf);
    inf[pos] = '\n';
    memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
   }
   write_blocks_to_disk(NO_DATA_BLOCKS - 1,&user_table,1);
    return 0;
}
int handle_deleteuser(tcp_buffer *write_buf, char *args, int len){
     if (!formatted) {send_to_buffer(write_buf, "not format", 11); return 0;}
       if(cur_user_id){
    char inf[1000] = "Permission denied,you need to log in the root user";
    int pos = strlen(inf);
    inf[pos] = '\n';
    memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

      return 0; 
    }
     if(args[0]==0 || args[0] == '\n'){
      char inf[1000] = "need user name!";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));

      return 0; 
    }
    for(int i = 0;i < strlen(args);i++) if(args[i] == '\n')args[i] = 0;
    UserNode* node;
    bool found = 0;
    for(int i = 1;i < USERS;i++){//cannot delete root
    node = &user_table[i];
    if(node->valid && strcmp(node->username,args) == 0){
        found = 1;
       if(dele_dir_from_dir(&inode_table[0],args) < 0){
         char inf[1000] = "need to empty the user folder";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    return 0;

       }
       node->valid = 0;
    }
    if(!found){
         char inf[1000] = "user not found";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    return 0;
    }
    write_blocks_to_disk(NO_DATA_BLOCKS - 1,&user_table,1);
     char inf[1000] = "user deleted";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    return 0;

    }



    return 0;
}
int handle_e(tcp_buffer *write_buf, char *args, int len){
    static char inf[] = "Bye!";
    send_to_buffer(write_buf, inf, sizeof(inf));
    return -1;
}
static struct {
    const char *name;
    int (*handler)(tcp_buffer *write_buf, char *, int);
} cmd_table[] = {
    {"f", handle_f},
    {"mk", handle_mk},
    {"mkdir", handle_mkdir},
    {"rm", handle_rm},
    {"rmdir",handle_rmdir},
    {"ls",handle_ls},
    {"cd",handle_cd},
    {"cat",handle_cat},
    {"w",handle_w},
    {"i",handle_i},
    {"d",handle_d},
    {"e",handle_e},
    {"su",handle_su},
    {"adduser",handle_adduser},
    {"deleteuser",handle_deleteuser}
};
#define NCMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

int checkBit(uint32_t num, int j) {
    // 假设 u32int 是定义为 unsigned int
    unsigned int mask = 1 << (31 - j);
    return (num & mask) != 0;
}
void add_client(int id) {
    // some code that are executed when a new client is connected
    // you don't need this in step1
    client_table[id].cur_dir_id = 0;
    client_table[id].cur_user_id = 0;
    strcpy(client_table[id].cur_path,"/");
}

int handle_client(int id, tcp_buffer *write_buf, char *msg, int len) {
    sem_wait(&mutex);
    cur_dir = client_table[id].cur_dir_id;
    cur_user_id = client_table[id].cur_user_id;
    strcpy(cur_path,client_table[id].cur_path);

    
    int i = cur_dir / 32;
    int j = cur_dir % 32;
    int res1 = checkBit(mysuper.inode_bitmap[i],j);
    int res2 = user_table[cur_user_id].valid;
    if(!res1 || !res2) {
        cur_dir = 0;
        cur_user_id = 0;
        strcpy(cur_path,"/");
            static char inf[1000] = "the directory you are in is deleted,you will be back to the root dir";
    int pos = strlen(inf);
    inf[pos] = '\n';
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    client_table[id].cur_dir_id =cur_dir  ;
    client_table[id].cur_user_id =cur_user_id;
    strcpy(client_table[id].cur_path,cur_path);
    sem_post(&mutex);
    return 0;
    }



    char *p = strtok(msg, " \r\n");
    if(p == NULL){
         static char inf[1000] = "";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    sem_post(&mutex);
    return 0;
    }
    int ret = 1;
    for (int i = 0; i < NCMD; i++)
        if (strcmp(p, cmd_table[i].name) == 0) {
            ret = cmd_table[i].handler(write_buf, p + strlen(p) + 1, len - strlen(p) - 1);
            break;
        }
    if (ret == 1) {
        static char inf[1000] = "Unknown command";
    int pos = strlen(inf);
    inf[pos] = '\n';
    if(!cur_user_id){
    memcpy(inf + pos + 1,"(root)",6);
    memcpy(inf+pos+7,cur_path,strlen(cur_path));
    }
    else memcpy(inf+pos+1,cur_path,strlen(cur_path));
    send_to_buffer(write_buf,inf,sizeof(inf));
    }
    if (ret < 0) {
        client_table[id].cur_dir_id =cur_dir  ;
    client_table[id].cur_user_id =cur_user_id;
    strcpy(client_table[id].cur_path,cur_path);
        sem_post(&mutex);
        return -1;
    }
    client_table[id].cur_dir_id =cur_dir  ;
    client_table[id].cur_user_id =cur_user_id;
    strcpy(client_table[id].cur_path,cur_path);
     sem_post(&mutex);
}

void clear_client(int id) {
    // some code that are executed when a client is disconnected
    // you don't need this in step2
}

int main(int argc, char *argv[]) {

    int diskport = atoi(argv[2]);
    diskClient = client_init("localhost", diskport);
    int fsport = atoi(argv[3]);
    static char buf[4096];
    for(int i = 0;i < __FD_SETSIZE;i++){
        client_table[i].cur_dir_id = 0;
         client_table[i].cur_user_id = 0;
          strcpy(client_table[i].cur_path,"/");
    }
    strcpy(buf, "I\n");
    client_send(diskClient, buf, strlen(buf) + 1);
    int n = client_recv(diskClient, buf, sizeof(buf));
    sscanf(buf, "%d\ %d\n", &CYLINDERS, &SECTORS);

    sem_init(&mutex,0,1);
    

    load_state();

    tcp_server server = server_init(fsport, 1, add_client, handle_client, clear_client);

    server_loop(server);

}

//

