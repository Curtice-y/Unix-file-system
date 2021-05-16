#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string.h>
#include <time.h>


#include "define.h"
#include "dataStruct.h"

using namespace std;

char buf[TOTAL_SIZE];
// 模拟磁盘
// #0: 不用  #1: 超级块  #2~124: 文件目录表
// #125: inode位图 #126~127: 数据块位图
// #128~283: inode数据 #284~16383: 数据块
FILE* virtualDisk;
// 超级块
struct SuperBlock* superBlock;
// 当前文件目录项
struct FileDirectoryEntry currFileDirEntry;
// 当前文件目录
struct FileDirectory* currFileDir;

// ROOT节点
struct DiskInode* root;
// 节点数组
struct DiskInode inodeArr[DISK_INODE_NUM];
// 当前节点
struct DiskInode* currInode;



// 退出
bool logout = false;



// 初始化+格式化磁盘, 只执行一次
int initialize(const char* path)
{
    // 所有打开方式都是 "r+""
    // 没有写入的
    virtualDisk = fopen(path, "r+");
    memset(buf, 0, sizeof(buf));
    fwrite(buf, 1, TOTAL_SIZE, virtualDisk);
    if (virtualDisk == NULL)
    {
        return ERROR_VM_NOEXIST;
    }
    // 格式化
    // 写入superBlock
    superBlock = (struct SuperBlock*)calloc(1, sizeof(struct SuperBlock));
    fseek(virtualDisk, START, SEEK_SET);    // 找到盘块#1
    superBlock->size = 16*1024*1024;
    superBlock->start = START;
    superBlock->fileDirStart = FILE_DIR_START;
    superBlock->inodeBitmapStart = INODE_BITMAP_START;
    superBlock->blockBitmapStart = BLOCK_BITMAP_START;
    superBlock->inodeStart = INODE_START;
    superBlock->blockStart = BLOCK_START;
    superBlock->blockNum = BLOCK_NUM;
    superBlock->blockSize = BLOCK_SIZE;
    superBlock->diskInodeNum = DISK_INODE_NUM;
    superBlock->diskInodeSize = DISK_INODE_SIZE;
    fwrite(superBlock, sizeof(struct SuperBlock), 1, virtualDisk);
    fflush(virtualDisk);

    fclose(virtualDisk);

    /* 测试输入
    virtualDisk = fopen(path, "r+");
    superBlock = (struct SuperBlock*)calloc(1, sizeof(struct SuperBlock));
    fseek(virtualDisk, START, SEEK_SET);    // 找到盘块#1
    int readSize = fread(superBlock, sizeof(struct SuperBlock), 1, virtualDisk);
    cout<<"readSize: "<<readSize<<endl;
    cout<<superBlock->size<<endl;
    */
    return NO_ERROR;
}

// 将指定盘块号的内容读取到对应数据结构中
int blockRead(void* buffer, unsigned short int blockId, int offset, int size, int count = 1)
{
    long int blockPos = blockId*superBlock->blockSize + offset;
    fseek(virtualDisk, blockPos, SEEK_SET);
    int readSize = fread(buffer, size, count, virtualDisk);
    if (readSize != count)
    {
        return ERROR_BLOCK_READ;
    }
    return NO_ERROR;
}
// 将数据写入盘块, 开始块号为blockId
int blockWrite(void* buffer, unsigned short int blockId, int offset, int size, int count = 1)
{
    long int blockPos = blockId*superBlock->blockSize + offset;
    fseek(virtualDisk, blockPos, SEEK_SET);
    int writeSize = fwrite(buffer, size, count, virtualDisk);
    fflush(virtualDisk);
    if (writeSize != count)
    {
        return ERROR_BLOCK_WRITE;
    }
    return NO_ERROR;
}
// 释放
int blockFree()
{
    cout<<"?"<<endl;
}

// 加载磁盘
int loadVirtualDisk(const char* path)
{
    virtualDisk = fopen(path, "r+");
    if(virtualDisk == NULL)
    {
        return ERROR_VM_NOEXIST;
    }
    superBlock = (struct SuperBlock*)calloc(1, sizeof(struct SuperBlock));
    fseek(virtualDisk, START, SEEK_SET);
    int readSize = fread(superBlock, sizeof(struct SuperBlock), 1, virtualDisk);
    if (readSize != 1)
    {
        return ERROR_LOAD_SUPER_FAIL;
    }
    
    return NO_ERROR;
}

// 更新inode, 写入磁盘
int updateInode(struct DiskInode* inode)
{
    int inodePos = superBlock->inodeStart*1024 + inode->inodeId*superBlock->diskInodeSize;
    fseek(virtualDisk, inodePos, SEEK_SET);  // 定位到对应inode
    int writeSize = fwrite(inode, sizeof(struct DiskInode), 1, virtualDisk);
    fflush(virtualDisk);
    if(writeSize != 1)
    {
        return ERROR_UPDATE_INODE_FAIL;
    }
    return NO_ERROR;
}

// 通过inodeId获取DiskInode指针
struct DiskInode* inodeGet(int inodeId)
{
    if (virtualDisk == NULL)
    {
        return NULL;
    }
    int inodePos = INODE_START*1024 + inodeId*DISK_INODE_SIZE;
    fseek(virtualDisk, inodePos, SEEK_SET); // 指针移到对应inode的位置
    int readSize = fread(&inodeArr[inodeId], sizeof(struct DiskInode), 1, virtualDisk);
    if (readSize != 1)
    {
        return NULL;
    }
    if (inodeArr[inodeId].createTime == 0) // new file
    {
        inodeArr[inodeId].fileSize = 0;
        inodeArr[inodeId].inodeId = inodeId;
        time_t timer;
        time(&timer);
        inodeArr[inodeId].createTime = timer;
        updateInode(&inodeArr[inodeId]);
    }
    inodeArr[inodeId].inodeId = inodeId;
    return &inodeArr[inodeId];
}


// 输入响应
// 创建文件
int createFile()
{
    int a;
}

int dispatcher()
{
    // 补充welcome message, group info(names and IDs), copyright
    char command[8192] = {0};
    char str[8192] = {0};
    cout<<"[@localhost /"<<(currInode->inodeId == 0?"/":currFileDirEntry.fileName)<<"]#";
    char ch = getchar();
    int num = 0;
    if(ch == 10) // 换行符的ACSII是10 '\n'
        return 0;
    while(ch != 10)
    {
        str[num] = ch;
        ch = getchar();
        num++;
    }
    strlwr(str);            // 大写字母转为小写字母
    strcpy(command, str);   // 复制
    strtok(command, " ");   // 用空格分割, 将空格换成'\0', '\0'是字符串结束的标志
    
    if(strcmp(command, "createFile") == 0)
    {
        // createFile fileName fileSize
        createFile();
    }
    /*
    deleteFile filename
    createDir
    deleteDir
    changeDir
    dir
    cp
    sum
    cat
    */

}