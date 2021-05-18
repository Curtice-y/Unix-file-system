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
// 两种bitmap
unsigned int inodeBitmap[256] = {0};
unsigned int blockBitmap[512] = {0};

// 退出
bool logout = false;


//------------------------------转换函数------------------------------

unsigned int sizeToBlockNum(unsigned int size)
{
    return (unsigned int)(size/1024);
}

unsigned int getBlockIdFromAddress(struct Address* addr)
{
    return (unsigned int)(addr->ch[0]*64 + addr->ch[1]/4);
}

struct Address getAddressFromBlockId(unsigned int blockId)
{
    Address addr;
    addr.ch[2] = 0;
    addr.ch[1] = (blockId*4)%256;
    addr.ch[0] = (int)(blockId/64);
    return addr;
}



//------------------------------bitmap操作------------------------------

// kind=0 为inode,kind=1 为block
int bitmapRead(int kind)
{
    unsigned int bitmapPos;
    if (kind == 0)   // inode
    {
        bitmapPos = superBlock->inodeBitmapStart*1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int readSize = fread(inodeBitmap, sizeof(inodeBitmap), 256, virtualDisk);
        if(readSize != 256)
        {
            return ERROR_READ_BITMAP;
        }
    }
    else if(kind == 1)   // block
    {
        bitmapPos = superBlock->blockBitmapStart*1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int readSize = fread(blockBitmap, sizeof(blockBitmap), 512, virtualDisk);
        if (readSize != 512)
        {
            return ERROR_READ_BITMAP;
        }
    }
    return NO_ERROR;
}
int bitmapWrite(int kind)
{
    unsigned int bitmapPos;
    if (kind == 0)   // inode
    {
        bitmapPos = superBlock->inodeBitmapStart*1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int writeSize = fwrite(inodeBitmap, sizeof(inodeBitmap), 256, virtualDisk);
        if(writeSize != 256)
        {
            return ERROR_WRITE_BITMAP;
        }
    }
    else if(kind == 1)   // block
    {
        bitmapPos = superBlock->blockBitmapStart*1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int writeSize = fread(blockBitmap, sizeof(blockBitmap), 512, virtualDisk);
        if (writeSize != 512)
        {
            return ERROR_WRITE_BITMAP;
        }
    }
    return NO_ERROR;
}

// 提取unsigned int 中pos位置的bit
int getBitFromUint(unsigned int num,int leftPos)
{
    int rightPos = 31 - leftPos;
    for(int i=0;i<=rightPos;i++)
    {
        num/=2;
    }
    return num%2;
}

// 设置unsigned int 中pos位置的bit 返回unsigned int
unsigned int setBitFromUint(unsigned int num, int leftPos, int bit)
{
    unsigned int temp = 1;
    int rightPos = 31 - leftPos;
    for(int i=0;i<=rightPos;i++)
    {
        num/=2;
        temp*=2;
    }
    if(num%2 != bit)
    {
        if(num%2 == 1)
        {
            num-=temp;
        }
        else
        {
            num += temp;
        }
    }
    return num;
}

//------------------------------block相关------------------------------

// 将指定盘块号的内容读取到对应数据结构中
int blockRead(void* buffer, unsigned int blockId, int offset, int size, int count = 1)
{
    unsigned int blockPos = blockId*superBlock->blockSize + offset;
    fseek(virtualDisk, blockPos, SEEK_SET);
    int readSize = fread(buffer, size, count, virtualDisk);
    if (readSize != count)
    {
        return ERROR_BLOCK_READ;
    }
    return NO_ERROR;
}

// 将数据写入盘块, 开始块号为blockId
int blockWrite(void* buffer, unsigned int blockId, int offset, int size, int count = 1)
{
    unsigned int blockPos = blockId*superBlock->blockSize + offset;
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
int blockFree(unsigned int blockId)
{
    bitmapRead(1);
    blockBitmap[blockId/32] = setBitFromUint(blockBitmap[blockId/32], blockId%32, 0);
    bitmapWrite(1);
}

// 分配一块数据块 返回blockId  (未检验是否有空闲数据块)
unsigned int allocateOneBlock()
{
    bool find = false;
    unsigned int blockId;
    for(int i=0;i<512;i++)
    {
        for(int j=0;j<31;j++)
        {
            if(getBitFromUint(blockBitmap[i], j) == 0)
            {
                find == true;
                blockId = i*32 + j;
            }
            if(find == true)
                break;
        }
        if(find == true)
            break;
    }
}

// 为inode对应文件分配block, 调用函数前需要设置inode的fileSize
int allocateBlock(unsigned int inodeId)
{
    bitmapRead(1);
    struct DiskInode* inode = inodeGet(inodeId);
    if (sizeToBlockNum(inode->fileSize) > superBlock->freeBlock - 1) // -1 是为了间址所使用的block
    {
        return ERROR_INSUFFICIENT_FREE_BLOCKS;
    }
    int fileSize = inode->fileSize;
    for(int i=0;i<min(fileSize, 10);i++)
    {
        inode->addr[i] = getAddressFromBlockId(allocateOneBlock());
    }
    fileSize-=10;
    if (fileSize > 0)
    {
        unsigned int blockId = allocateOneBlock();
        inode->addr[10] = getAddressFromBlockId(blockId);
        struct Address* address = (struct Address*)calloc(1, sizeof(struct Address));
        for(int i=0;i<fileSize;i++)
        {
            // 中间变量暂存, 因为临时变量不能取地址
            Address temp = getAddressFromBlockId(allocateOneBlock());
            address = &temp;
            blockWrite(address, blockId, i*sizeof(struct Address), sizeof(struct Address), 1);
        }
    }
    updateInode(inode);
    bitmapWrite(1);
    return NO_ERROR;
}



//------------------------------inode相关------------------------------

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
struct DiskInode* inodeGet(unsigned int inodeId)
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

// 判断inode是否被使用, 使用则返回True(1)
int inodeInUse(int inodeId)
{
    bitmapRead(0);
    int pos1 = (int)inodeId/32;   // 0~255
    int pos2 = inodeId%32;        // 0~31
    if (getBitFromUint(inodeBitmap[pos1], pos2) == 1)
    {
        return 1;
    }
    else return 0;
}

// 只分配了inode, 没有分配磁盘空间
int allocateInode(int inodeId)
{
    if(superBlock->freeInode == 0)
    {
        return ERROR_INSUFFICIENT_FREE_INODE;
    }
    if(inodeInUse(inodeId) == 1)
    {
        return ERROR_INODEID_ALREADY_IN_USE;
    }
    inodeBitmap[inodeId/32] = setBitFromUint(inodeBitmap[inodeId/32], inodeId%32, 1);  // 更新bitmap
    struct DiskInode* inode = inodeGet(inodeId);  // inodeGet 会分配id
    updateInode(inode);  // 写入磁盘
    superBlock->freeInode = superBlock->freeInode - 1;
    bitmapWrite(0);
    return NO_ERROR;
}



//--------------------------------初始化--------------------------------

// 初始化+格式化磁盘, 第一次运行程序的时候需执行, 之后执行加载磁盘
int initialize(const char* path)
{
    // 所有打开方式都是 "r+""
    // 没有写入的
    virtualDisk = fopen(path, "r+");
    memset(buf, 0, sizeof(buf));
    fwrite(buf, 1, TOTAL_SIZE, virtualDisk);
    if(virtualDisk == NULL)
    {
        return ERROR_VM_NOEXIST;
    }
    // 格式化
    // superBlock初始化
    superBlock = (struct SuperBlock*)calloc(1, sizeof(struct SuperBlock));
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
    superBlock->freeInode = DISK_INODE_NUM - 1;
    superBlock->freeBlock = BLOCK_NUM;
    fseek(virtualDisk, 0, SEEK_SET);
    blockWrite(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);

    // root初始化
    root = (struct DiskInode*)calloc(1, sizeof(DiskInode));
    root->fileSize = 0;
    root->inodeId = 0;
    time_t timer;
    time(&timer);
    root->createTime = timer;
    updateInode(root);   // 写入磁盘
    
    // 文件目录初始化
    currFileDir = (struct FileDirectory*)calloc(1, sizeof(FileDirectory));
    currFileDir->directoryNum = 1; // 只有根目录
    char str[] = "/";
    strncpy(currFileDirEntry.fileName, str, sizeof(currFileDirEntry.fileName));
    currFileDirEntry.inodeId = 0; 
    currFileDir->fileDirectoryEntry[0] = currFileDirEntry;

    // Bitmap初始化
    for(int i=0; i<superBlock->blockStart; i++)
    {
        blockBitmap[i/32] = setBitFromUint(blockBitmap[i/32], i%32, 1);
    }
    bitmapWrite(1);

    /* 测试
    int ans = blockRead(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    cout<<superBlock->size<<endl;  // 16777216
    cout<<ans<<endl;  // 1
    */
    fclose(virtualDisk);
    return NO_ERROR;
}

// 加载磁盘
int loadVirtualDisk(const char* path)
{
    virtualDisk = fopen(path, "r+");
    if(virtualDisk == NULL)
    {
        return ERROR_VM_NOEXIST;
    }
    // 读入superBlock
    superBlock = (struct SuperBlock*)calloc(1, sizeof(struct SuperBlock));
    fseek(virtualDisk, START, SEEK_SET);
    int readSize = fread(superBlock, sizeof(struct SuperBlock), 1, virtualDisk);
    if(readSize != 1)
    {
        return ERROR_LOAD_SUPER_FAIL;
    }
    // 读入文件目录表
    currFileDir = (struct FileDirectory*)calloc(1, sizeof(struct FileDirectory));
    blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory));
    
    // 读入bitmap
    bitmapRead(0);
    bitmapRead(1);

    return NO_ERROR;
}



//------------------------------输入的响应事件------------------------------

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