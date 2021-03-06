#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stack>

#include "define.h"
#include "dataStruct.h"

using namespace std;

char buf[TOTAL_SIZE];
// 模拟磁盘
FILE *virtualDisk;
// 超级块
struct SuperBlock *superBlock;

struct FileDirectory *root;
// 当前目录
struct FileDirectory *currFileDir;

stack <char*> currPath;

// 两种bitmap
unsigned int inodeBitmap[128] = {0};
unsigned int blockBitmap[512] = {0};
unsigned int fileDirBitmap[4] = {0};

// 退出
bool logout = false;

//------------------------------中间函数------------------------------

// 将指定盘块号的内容读取到对应数据结构中
int blockRead(void *buffer, unsigned int blockId, int offset, int size, int count = 1)
{
    unsigned int blockPos = blockId * BLOCK_SIZE + offset;
    fseek(virtualDisk, blockPos, SEEK_SET);
    int readSize = fread(buffer, size, count, virtualDisk);
    if (readSize != count)
    {
        return ERROR_BLOCK_READ;
    }
    return NO_ERROR;
}

// 将数据写入盘块, 开始块号为blockId
int blockWrite(void *buffer, unsigned int blockId, int offset, int size, int count = 1)
{
    unsigned int blockPos = blockId * BLOCK_SIZE + offset;
    fseek(virtualDisk, blockPos, SEEK_SET);
    int writeSize = fwrite(buffer, size, count, virtualDisk);
    fflush(virtualDisk);
    if (writeSize != count)
    {
        return ERROR_BLOCK_WRITE;
    }
    return NO_ERROR;
}

unsigned int sizeToBlockId(unsigned int size)
{
    return (unsigned int)(size / 1024);
}

unsigned int getBlockIdFromAddress(struct Address addr)
{
    return (unsigned int)(addr.ch[0] * 64 + addr.ch[1]);
}

struct Address getAddressFromBlockId(unsigned int blockId)
{
    Address addr;
    addr.ch[2] = 0;
    addr.ch[1] = blockId % 64;
    addr.ch[0] = (int)(blockId / 64);
    return addr;
}

// 提取unsigned int 中pos位置的bit
int getBitFromUint(unsigned int num, int leftPos)
{
    int rightPos = 31 - leftPos;
    for (int i = 0; i < rightPos; i++)
    {
        num /= 2;
    }
    return num % 2;
}

// 设置unsigned int 中pos位置的bit 返回unsigned int
unsigned int setBitFromUint(unsigned int num, int leftPos, int bit)
{
    unsigned int temp = 1;
    unsigned int utemp = num;
    int rightPos = 31 - leftPos;
    for (int i = 0; i < rightPos; i++)
    {
        num /= 2;
        temp *= 2;
    }
    if (num % 2 != bit)
    {
        if (num % 2 == 1)
        {
            utemp -= temp;
        }
        else
        {
            utemp += temp;
        }
    }
    return utemp;
}

int charToInt(char* size)
{
    int len  = strlen(size);
    int sum = 0;
    int temp = 0;
    for(int i=0;i<len;i++)
    {
        sum *= 10;
        temp = (int)size[i] - 48;
        sum += temp;
    }
    return sum;
}

// 文件是否存在
int fileExist(char* fileName)
{
    int num = currFileDir->fileDirNum;
    int exist = 0;
    for(int i=0;i<num;i++)
    {
        if(strcmp(currFileDir->fileDirectoryEntry[i].fileName, fileName) == 0)
        {
            exist = 1;
        }
    }
    return exist;
}

// 从文件名获取文件Id
int getIdFromFileName(char* fileName)
{
    int num = currFileDir->fileDirNum;
    for(int i=0;i<num;i++)
    {
        if(strcmp(currFileDir->fileDirectoryEntry[i].fileName, fileName) == 0)
        {
            return currFileDir->fileDirectoryEntry[i].id;
        }
    }
    return -1;
}

int getCurrPath()
{
    if(currFileDir->fileDirectoryId == 0)
    {
        cout<<"/";
        return NO_ERROR;
    }

    int backId = currFileDir->fileDirectoryId;
    int num = 0;
    Path buffer[32];
    memset(buffer, 0, sizeof(buffer));
    while(currFileDir->parentDirId != 0)
    {
        unsigned int parentDirId = currFileDir->parentDirId;
        strcpy(buffer[num].dirName, currFileDir->fileDirectoryName);
        num++;
        blockRead(currFileDir, superBlock->fileDirStart + parentDirId, 0, sizeof(struct FileDirectory),1);
    }
    strcpy(buffer[num].dirName, currFileDir->fileDirectoryName);
    num++;

    char buff[1024];
    char buf[32];
    memset(buff, 0, sizeof(buff));
    memset(buf,0, sizeof(buf));

    for(int i=num-1;i>=0;i--)
    {
        strcpy(buf, buffer[i].dirName);
        strcat(buff, buf);
        memset(buf, 0, sizeof(buf));
    }
    cout<<buff;
    blockRead(currFileDir, superBlock->fileDirStart + backId, 0, sizeof(struct FileDirectory), 1);
    return NO_ERROR;
}

//------------------------------bitmap操作------------------------------

// kind=0 为inode,kind=1 为block
int bitmapRead(int kind)
{
    unsigned int bitmapPos;
    if (kind == 0) // inode
    {
        bitmapPos = superBlock->inodeBitmapStart * 1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int readSize = fread(inodeBitmap, sizeof(inodeBitmap), 1, virtualDisk);
        if (readSize != 1)
        {
            return ERROR_READ_BITMAP;
        }
    }
    else if (kind == 1) // block
    {
        bitmapPos = superBlock->blockBitmapStart * 1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int readSize = fread(blockBitmap, sizeof(blockBitmap), 1, virtualDisk);
        if (readSize != 1)
        {
            return ERROR_READ_BITMAP;
        }
    }
    else if (kind == 2) // fileDir
    {
        bitmapPos = superBlock->fileDirBitmapStart * 1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int readSize = fread(fileDirBitmap, sizeof(fileDirBitmap), 1, virtualDisk);
        if (readSize != 1)
        {
            return ERROR_READ_BITMAP;
        }
    }
    return NO_ERROR;
}

int bitmapWrite(int kind)
{
    unsigned int bitmapPos;
    if (kind == 0) // inode
    {
        bitmapPos = superBlock->inodeBitmapStart * 1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int writeSize = fwrite(inodeBitmap, sizeof(inodeBitmap), 1, virtualDisk);
        if (writeSize != 1)
        {
            return ERROR_WRITE_BITMAP;
        }
    }
    else if (kind == 1) // block
    {
        bitmapPos = superBlock->blockBitmapStart * 1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int writeSize = fwrite(blockBitmap, sizeof(blockBitmap), 1, virtualDisk);
        if (writeSize != 1)
        {
            return ERROR_WRITE_BITMAP;
        }
    }
    else if (kind == 2) // fileDir
    {
        bitmapPos = superBlock->fileDirBitmapStart * 1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int writeSize = fwrite(fileDirBitmap, sizeof(fileDirBitmap), 1, virtualDisk);
        if (writeSize != 1)
        {
            return ERROR_WRITE_BITMAP;
        }
    }
    return NO_ERROR;
}

// 释放block
int blockFree(unsigned int blockId)
{
    bitmapRead(1);
    blockBitmap[blockId / 32] = setBitFromUint(blockBitmap[blockId / 32], blockId % 32, 0);
    bitmapWrite(1);
    superBlock->freeBlock += 1;
    blockWrite(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    return NO_ERROR;
}

//------------------------------inode相关------------------------------



// 更新inode, 写入磁盘
int updateInode(struct DiskInode *inode)
{
    blockWrite(inode, superBlock->inodeStart + inode->inodeId/16, (inode->inodeId%16)*64, sizeof(struct DiskInode), 1);
    return NO_ERROR;
}

// 通过inodeId获取DiskInode指针
struct DiskInode *inodeGet(unsigned int inodeId)
{
    if (virtualDisk == NULL)
    {
        return NULL;
    }
    struct DiskInode *diskInode = (struct DiskInode *)calloc(1, sizeof(struct DiskInode));
    blockRead(diskInode, superBlock->inodeStart + inodeId/16, (inodeId%16)*64, sizeof(struct DiskInode), 1);
    diskInode->inodeId = inodeId;
    if (diskInode->fileSize == 0) // new file
    {
        diskInode->fileSize = 50;
        diskInode->inodeId = inodeId;
        diskInode->createTime = time(0);
        updateInode(diskInode);
        superBlock->freeInode = superBlock->freeInode-1;
        blockWrite(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    }
    return diskInode;
}

// 释放inode占用的block
int inodeFree(unsigned int inodeId)
{
    DiskInode* inode = inodeGet(inodeId);
    int fileSize = inode->fileSize;
    int blockNum = (fileSize + 1023)/1024;
    for(int i=0;i<min(10, blockNum);i++)
    {
        blockFree(getBlockIdFromAddress(inode->addr[i]));
    }
    blockNum -= 10;
    if(blockNum>0)
    {
        unsigned int addressBlockId = getBlockIdFromAddress(inode->addr[10]);
        Address address;
        for(int i=0;i<blockNum;i++)
        {
            blockRead(&address, addressBlockId, i*sizeof(struct Address), sizeof(struct Address), 1);
            blockFree(getBlockIdFromAddress(address));
        }
    }
}

// 判断inode是否被使用, 使用则返回True(1)
int inodeInUse(int inodeId)
{
    bitmapRead(0);
    int pos1 = (int)inodeId / 32; // 0~255
    int pos2 = inodeId % 32;      // 0~31
    if (getBitFromUint(inodeBitmap[pos1], pos2) == 1)
    {
        return 1;
    }
    else
        return 0;
}

int dirInUse(int id)
{
    bitmapRead(2);
    int pos1 = (int)id / 32; // 0~255
    int pos2 = id % 32;      // 0~31
    if (getBitFromUint(fileDirBitmap[pos1], pos2) == 1)
    {
        return 1;
    }
    else
        return 0;
}

// 只分配了inode, 没有分配磁盘空间
int allocateInode(int inodeId)
{
    if (superBlock->freeInode == 0)
    {
        return ERROR_INSUFFICIENT_FREE_INODE;
    }
    if (inodeInUse(inodeId) == 1)
    {
        return ERROR_INODEID_ALREADY_IN_USE;
    }
    inodeBitmap[inodeId / 32] = setBitFromUint(inodeBitmap[inodeId / 32], inodeId % 32, 1); // 更新bitmap
    struct DiskInode *inode = inodeGet(inodeId); // inodeGet 会分配id
    updateInode(inode);                          // 写入磁盘
    superBlock->freeInode = superBlock->freeInode - 1;
    blockWrite(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    bitmapWrite(0);
    return NO_ERROR;
}

//------------------------------block相关------------------------------



// 分配一块数据块 返回blockId  (未检验是否有空闲数据块)
unsigned int allocateOneBlock()
{
    bitmapRead(1);
    bool find = false;
    unsigned int blockId;
    for (int i = 0; i < 512; i++)
    {
        for (int j = 0; j < 31; j++)
        {
            if (getBitFromUint(blockBitmap[i], j) == 0)
            {
                blockBitmap[i] = setBitFromUint(blockBitmap[i], j, 1);
                find == true;
                blockId = i * 32 + j;
                return blockId;
            }
            if (find == true)
                break;
        }
        if (find == true)
            break;
    }
}

// 为inode对应文件分配block, 调用函数前需要设置inode的fileSize
int allocateBlock(unsigned int inodeId)
{
    bitmapRead(1);
    struct DiskInode *inode = inodeGet(inodeId);
    
    if (sizeToBlockId(inode->fileSize) > superBlock->freeBlock - 1) // -1 是为了间址所使用的block
    {
        return ERROR_INSUFFICIENT_FREE_BLOCKS;
    }
    int blockNum = (inode->fileSize + 1023) / 1024;
    superBlock->freeBlock = superBlock->freeBlock - blockNum;
    for (int i = 0; i < min(blockNum, 10); i++)
    {
        unsigned int t = allocateOneBlock();
        inode->addr[i] = getAddressFromBlockId(t);
    }
    blockNum -= 10;
    if (blockNum > 0)
    {
        superBlock->freeBlock-=1;
        unsigned int blockId = allocateOneBlock();
        // blockBitmap[blockId]=1;
        inode->addr[10] = getAddressFromBlockId(blockId);
        struct Address *address = (struct Address *)calloc(1, sizeof(struct Address));
        for (int i = 0; i < blockNum; i++)
        {
            // 中间变量暂存, 因为临时变量不能取地址
            Address temp = getAddressFromBlockId(allocateOneBlock());
            address = &temp;
            blockWrite(address, blockId, i * sizeof(struct Address), sizeof(struct Address), 1);
        }
    }
    updateInode(inode);
    bitmapWrite(1);
    blockWrite(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    return NO_ERROR;
}

//-----------------------------字符相关--------------------------------

// 查找某个字符在字符串中的位置
int strPos(char *str, int start, const char needle)
{
    for (int i = start; i < strlen(str); i++)
    {
        if (str[i] == needle)
            return i;
    }
    return -1;
}

// 复制src到dst
int strCpy(char *dst, char *src, int offset)
{
    int len = strlen(src);
    if (len <= offset)
        return 0;
    int i;
    for (i = 0; i < len - offset; i++)
    {
        dst[i] = src[i + offset];
        //cout<<"dst["<<i<<"]"<<dst[i]<<endl;
    }
    dst[i] = 0;
    return 1;
}

// 从start开始复制字符串
int subStr(char *src, char *dst, int start, int end = -1)
{
    int pos = 0;
    end == -1 ? end = strlen(src) : 0;
    for (int i = start; i < end; i++)
        dst[pos++] = src[i];
    dst[pos] = 0;
    return 1;
}

//--------------------------------初始化--------------------------------

// 初始化+格式化磁盘, 第一次运行程序的时候需执行, 之后执行加载磁盘
int initialize(const char *path)
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
    // superBlock初始化
    superBlock = (struct SuperBlock *)calloc(1, sizeof(struct SuperBlock));
    superBlock->size = 16 * 1024 * 1024;
    superBlock->start = START;
    superBlock->fileDirStart = FILE_DIR_START;
    superBlock->fileDirBitmapStart = FILE_DIR_BITMAP_START;
    superBlock->inodeBitmapStart = INODE_BITMAP_START;
    superBlock->blockBitmapStart = BLOCK_BITMAP_START;
    superBlock->inodeStart = INODE_START;
    superBlock->blockStart = BLOCK_START;
    superBlock->blockNum = BLOCK_NUM;
    superBlock->blockSize = BLOCK_SIZE;
    superBlock->diskInodeNum = DISK_INODE_NUM;
    superBlock->entryNum = ENTRY_NUM;
    superBlock->diskInodeSize = DISK_INODE_SIZE;
    superBlock->freeInode = DISK_INODE_NUM - 1;
    superBlock->freeBlock = BLOCK_NUM - 391;

    blockWrite(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);

    // 根目录初始化
    FileDirectory *root = (struct FileDirectory *)calloc(1, sizeof(FileDirectory));
    root->fileDirNum = 0;
    root->fileDirectoryId = 0;
    root->parentDirId = 0;
    char str[27] = {'/'};
    strcpy(root->fileDirectoryName, str);
    root->fileDirectoryId = 0;
    blockWrite(root, superBlock->fileDirStart, sizeof(struct FileDirectory), 1); // 写入磁盘
    fileDirBitmap[0] = setBitFromUint(fileDirBitmap[0], 0, 1);
    bitmapWrite(2); // 更新bitmap

    // blockBitmap初始化
    for (int i = 0; i < superBlock->blockStart; i++)
    {
        blockBitmap[i / 32] = setBitFromUint(blockBitmap[i / 32], i % 32, 1);
    }
    bitmapWrite(1);

    fclose(virtualDisk);
    return NO_ERROR;
}

// 加载磁盘
int loadVirtualDisk(const char *path)
{
    virtualDisk = fopen(path, "r+");
    if (virtualDisk == NULL)
    {
        return ERROR_VM_NOEXIST;
    }
    // 读入superBlock
    superBlock = (struct SuperBlock *)calloc(1, sizeof(struct SuperBlock));
    int check = blockRead(superBlock, START, 0, sizeof(struct SuperBlock), 1);
    if (check != NO_ERROR)
    {
        return ERROR_LOAD_SUPER_FAIL;
    }

    // 读入文件目录表(根目录)
    currFileDir = (struct FileDirectory *)calloc(1, sizeof(struct FileDirectory));
    blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory));

    // 读入bitmap
    bitmapRead(0);
    bitmapRead(1);
    bitmapRead(2);
    
    cout<<"Welcome!"<<endl;
    cout<<"Your IDs: 0"<<endl;
    cout<<"Your Names: root"<<endl;

    return NO_ERROR;
}

int fileRead(DiskInode *inode, char *buffer, int pos, int count)
{
    //超出文件大小
    if (pos + count > inode->fileSize)
    {
        return FILE_READ_OVERHEAD;
    }

    int total = 0;
    char *cache = new char[5 * 1024];
    //起点在二级地址中
    if (pos > 10 * BLOCK_SIZE)
    {
        //获取第11个地址指向的块，并获取块中写的内容
        Address secondartAddress = inode->addr[10];
        unsigned int addressBlockId = getBlockIdFromAddress(secondartAddress);

        int startBlockPos = pos / 1024 - 10;
        Address address;
        blockRead(&address, addressBlockId, startBlockPos * sizeof(Address), sizeof(Address));
        unsigned int blockId = getBlockIdFromAddress(address);

        //长度不足一个block
        if (count <= 1024 - (pos % 1024))
        {
            blockRead(buffer, blockId, pos % 1024, count);
            total += count;
        }
        else //超过一个block时，先把第一个block的读完，然后对齐
        {

            blockRead(cache, blockId, pos % 1024, 1024 - (pos % 1024));
            total += 1024 - (pos % 1024);
            count -= 1024 - (pos % 1024);
            int blockOffset = 1;
            memcpy(buffer, cache, 1024 - (pos % 1024));

            //把剩余的能完整读完的block读完
            while (count >= 1024)
            {
                blockRead(&address, addressBlockId, (startBlockPos + blockOffset) * sizeof(Address), sizeof(Address));
                blockId = getBlockIdFromAddress(address);
                blockRead(cache, blockId, 0, 1024);
                memcpy(buffer + total, cache, 1024);

                blockOffset++;
                total += 1024;
                count -= 1024;
            }

            //剩下最后一个不完整的block了
            blockRead(&address, addressBlockId, (startBlockPos + blockOffset) * sizeof(Address), sizeof(Address));
            blockId = getBlockIdFromAddress(address);
            blockRead(buffer, blockId, 0, count);
            memcpy(buffer + total, cache, count);

            total += count;
        }
    }
    else //起点在一级地址里
    {
        int startBlockPos = pos / 1024;
        int endBlockIndex = (pos + count) / 1024;
        unsigned int blockId = getBlockIdFromAddress(inode->addr[startBlockPos]);

        //长度不足一个block
        if (count <= 1024 - (pos % 1024))
        {
            blockRead(buffer, blockId, pos % 1024, count);
            total += count;
        }
        else //长度多于一个block
        {
            //先把第一个block的读完，然后对齐
            blockRead(cache, blockId, pos % 1024, 1024 - (pos % 1024));
            total += 1024 - (pos % 1024);
            count -= 1024 - (pos % 1024);
            memcpy(buffer, cache, 1024 - (pos % 1024));

            if (pos + count > 10 * BLOCK_SIZE) //会从直接地址延伸至间接地址
            {
                for (int i = startBlockPos + 1; i < 10; i++) //先读完一级地址的部分
                {
                    blockId = getBlockIdFromAddress(inode->addr[i]);
                    blockRead(cache, blockId, 0, 1024);
                    memcpy(buffer + total, cache, 1024);
                    total += 1024;
                    count -= 1024;
                }

                //获取第11个地址指向的块，并获取块中写的内容
                Address secondartAddress = inode->addr[10];
                unsigned int addressBlockId = getBlockIdFromAddress(secondartAddress);
                int restBlocks = (count + 1023) / 1024; //还剩下几个block
                Address address;

                for (int i = 0; i < restBlocks - 1; ++i) //把剩余的能完整读完的block读完
                {
                    blockRead(&address, addressBlockId, i * sizeof(Address), sizeof(Address));
                    blockId = getBlockIdFromAddress(address);

                    blockRead(cache, blockId, 0, 1024);
                    memcpy(buffer + total, cache, 1024);
                    total += 1024;
                    count -= 1024;
                }

                //处理最后一个块
                blockRead(&address, addressBlockId, (restBlocks - 1) * sizeof(Address), sizeof(Address));
                blockId = getBlockIdFromAddress(address);
                blockRead(cache, blockId, 0, count);
                memcpy(buffer + total, cache, count);
                total += count;
            }
            else //不会延伸至二级地址
            {

                for (int i = startBlockPos + 1; i < endBlockIndex; i++) //先读完能完整读完的block
                {
                    blockId = getBlockIdFromAddress(inode->addr[i]);
                    blockRead(cache, blockId, 0, 1024);
                    memcpy(buffer + total, cache, 1024);
                    total += 1024;
                    count -= 1024;
                }

                //最后一个block
                blockId = getBlockIdFromAddress(inode->addr[endBlockIndex]);
                blockRead(cache, blockId, 0, count);
                memcpy(buffer + total, cache, count);
                total += count;
            }
        }
    }

    delete[] cache;
    return NO_ERROR;
}

//文件写入暂时只设计能一次性写完的，从某个pos开始的太难实现
int fileWrite(DiskInode *inode, char *buffer, int size)
{
    if (size > MAX_FILE_SIZE)
    {
        return FILE_TOO_LARGE;
    }

    unsigned int blockId;
    /*
    //先释放原来的文件内容，再写入新的内容
    for (int i = 0; i < 10; ++i)
    {
        blockId = getBlockIdFromAddress(inode->addr[i]);
        blockFree(blockId);
    }

    //如果原来的文件有使用二级地址，也要把二级地址free掉
    if (inode->fileSize > 10 * BLOCK_SIZE)
    {
        Address secondartAddress = inode->addr[10];
        unsigned int addressBlockId = getBlockIdFromAddress(secondartAddress);

        int secondaryAddressUsed = (inode->fileSize + 1023) / 1024 - 10;
        Address address;
        for (int i = 0; i < secondaryAddressUsed; ++i)
        {
            blockRead(&address, addressBlockId, i * sizeof(Address), sizeof(Address));
            blockId = getBlockIdFromAddress(address);
            blockFree(blockId);
        }
    }

    //最后再把第11个地址free掉
    blockId = getBlockIdFromAddress(inode->addr[10]);
    blockFree(blockId);
    */
    int blocksNeeded = (size + 1023) / 1024;

    //开始写入，判断是否需要用到二级地址
    char *cache = new char[5 * BLOCK_SIZE];
    if (size > 10 * BLOCK_SIZE)
    {
        //先写满10个一级地址
        for (int i = 0; i < 10; ++i)
        {
            blockId = getBlockIdFromAddress(inode->addr[i]);
            memcpy(cache, buffer + i * 1024, 1024);
            blockWrite(cache, blockId, 0, 1024);
            size -= 1024;
        }

        Address secondartAddress = inode->addr[10];
        unsigned int addressBlockId = getBlockIdFromAddress(secondartAddress);
        Address address;
        for (int i = 0; i < blocksNeeded - 11; ++i)
        {
            blockRead(&address, addressBlockId, i * sizeof(Address), sizeof(Address));
            blockId = getBlockIdFromAddress(address);
            memcpy(cache, buffer + (i + 10) * 1024, 1024);

            blockWrite(cache, blockId, 0, 1024);
            size -= 1024;
        }

        //最后一个block
        blockRead(&address, addressBlockId, blocksNeeded - 11 * sizeof(Address), sizeof(Address));
        blockId = getBlockIdFromAddress(address);
        memcpy(cache, buffer + (blocksNeeded - 1) * 1024, size);
        blockWrite(cache, blockId, 0, size);
    }
    else
    {
        for (int i = 0; i < blocksNeeded - 1; ++i)
        {
            blockId = getBlockIdFromAddress(inode->addr[i]);
            memcpy(cache, buffer + i * 1024, 1024);
            blockWrite(cache, blockId, 0, 1024);
            size -= 1024;
        }

        //最后一个block
        blockId = getBlockIdFromAddress(inode->addr[blocksNeeded - 1]);
        memcpy(cache, buffer + (blocksNeeded - 1) * 1024, size);
        blockWrite(cache, blockId, 0, size);
    }

    //保存新的inode
    blockWrite(inode, superBlock->inodeStart + (inode->inodeId) / 16, (inode->inodeId % 16) * 64, sizeof(struct DiskInode));

    delete[] cache;
    return NO_ERROR;
}

//------------------------------输入的响应事件------------------------------

FileDirectory *cd(char *path, FileDirectory *node)
{
    if (path[0] == '/' && strlen(path) == 1)
    {
        currFileDir->fileDirectoryId = 0;
        return NULL;
    }
    char ss[10] = {'/'};
    int flag = 1;
    int start = path[0] == '/';
    char t[16] = {0};
    int pos = strPos(path, 1, '/');
    subStr(path, t, start, pos);
    FileDirectory *dir = (FileDirectory *)calloc(1, sizeof(FileDirectory));
    strcat(ss, t);
    cout << ss << endl;
    blockRead(dir, superBlock->fileDirStart + node->fileDirectoryId, 0, sizeof(FileDirectory));
    for (int i = 0; i < dir->fileDirNum; i++)
    {
        if (dir->fileDirectoryEntry[i].fileName[0] == '/')
        { //目录
            if (strcmp(ss, dir->fileDirectoryEntry[i].fileName) == 0)
            {
                FileDirectory *tmpdir = (FileDirectory *)calloc(1, sizeof(FileDirectory));
                fseek(virtualDisk, superBlock->fileDirStart * 1024 + dir->fileDirectoryEntry[i].id * 1024, SEEK_SET); // 指针移到对应inode的位置
                int readSize = fread(tmpdir, sizeof(FileDirectory), 1, virtualDisk);
                cout << dir->fileDirectoryEntry[i].id << endl;
                cout << tmpdir->fileDirectoryName << endl;
                node = tmpdir;
                flag = -1;
                break;
            }
        }
    }
    if (flag != -1)
        node = NULL;
    // else{
    //     node=NULL;
    //     return node;
    // }
    cout << pos << endl;
    if (pos != -1 && node != NULL)
    {
        subStr(path, t, pos + 1);
        return cd(t, node);
    }
    if (pos == -1)

        return node;
}

// 路径中的最后一项,修改当前路径
char* changePath(char* path)
{
    if(strcmp(path, "/") == 0)
    {
        blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory), 1);
        return NULL;
    }

    int backId = currFileDir->fileDirectoryId;
    blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory));
    char* token = strtok(path, "/");
    char* token_;
    while((token_ = strtok(NULL, "/")))
    {
        int newId = -1;
        char temp[35] = {'/'};
        strcat(temp, token);
        for(int i=0;i<currFileDir->fileDirNum;i++)
        {
            if(strcmp(temp, currFileDir->fileDirectoryEntry[i].fileName) == 0)
            {
                newId = currFileDir->fileDirectoryEntry[i].id;
                break;
            }
        }
        if(newId = -1)
        {
            cout<<"Input path not exist"<<endl;
            blockRead(currFileDir, superBlock->fileDirStart + backId, 0, sizeof(struct FileDirectory), 1);
            return NULL;
        }
        else
        {
            blockRead(currFileDir, superBlock->fileDirStart + newId, 0, sizeof(struct FileDirectory), 1);
        }
        token = token_;
    }
    return token;
}

int changeDir(char *path)
{
    if(strcmp(path, "/") == 0)
    {
        blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory), 1);
        return NO_ERROR;
    }
    int backId = currFileDir->fileDirectoryId;
    blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory), 1);
    //cout<<currFileDir->fileDirectoryId<<endl;
    char* token = strtok(path, "/");
    int newId;
    newId = -1;
    char temp[35] = {'/'};
    strcat(temp, token);
    for(int i=0; i<currFileDir->fileDirNum; i++)
    {
        if(strcmp(temp,currFileDir->fileDirectoryEntry[i].fileName) == 0)
        {
            newId = currFileDir->fileDirectoryEntry[i].id;
            break;
        }
    }
    if(newId == -1)
    {
        cout<<"Input path not exist"<<endl;
        blockRead(currFileDir, superBlock->fileDirStart + backId, 0, sizeof(struct FileDirectory), 1);
        return ERROR_PATH_NOEXIST;
    }
    else
    {
        //cout<<newId<<endl;
        blockRead(currFileDir, superBlock->fileDirStart + newId, 0, sizeof(struct FileDirectory), 1);
    }

    while((token = strtok(NULL, "/")))
    {
        newId = -1;
        char temp[35] = {'/'};
        strcat(temp, token);
        //cout<<temp<<endl;
        for(int i=0; i<currFileDir->fileDirNum; i++)
        {
            if(strcmp(temp,currFileDir->fileDirectoryEntry[i].fileName) == 0)
            {
                newId = currFileDir->fileDirectoryEntry[i].id;
                break;
            }
        }
        if(newId == -1)
        {
            cout<<"Input path not exist"<<endl;
            blockRead(currFileDir, superBlock->fileDirStart + backId, 0, sizeof(struct FileDirectory), 1);
            return ERROR_PATH_NOEXIST;
        }
        else
        {
            //cout<<newId<<endl;
            blockRead(currFileDir, superBlock->fileDirStart + newId, 0, sizeof(struct FileDirectory), 1);
        }
    }
    return NO_ERROR;
}

int createDir(char *path)
{
    if(path[0] != '/')
    {
        char ss[35] = {"/"};
        strcat(ss, path); // 加/
        FileDirectory *dir = (FileDirectory *)calloc(1, sizeof(FileDirectory));
        blockRead(dir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
        for (int i = 0; i < dir->fileDirNum; i++)
        {
            if (strcmp(dir->fileDirectoryEntry[i].fileName, ss) == 0)
            {
                cout << "it is exit in current directory" << endl;
                return -1;
            }
        }
        int id;
        strcpy(dir->fileDirectoryEntry[dir->fileDirNum].fileName, ss);
        //寻找到空闲的目录ID
        for (int i = 0; i < 4; i++)
        {
            if (dirInUse(i) == false)
            {
                fileDirBitmap[i / 32] = setBitFromUint(fileDirBitmap[i / 32], i % 32, 1);
                bitmapWrite(2);
                id = i;
                break;
            }
        }
        cout<<"newDirId: "<<id<<endl;
        cout<<"parentDirId: "<<dir->fileDirectoryId<<endl;
        FileDirectory *newDir = (FileDirectory *)calloc(1, sizeof(FileDirectory));
        newDir->fileDirectoryId = id;
        newDir->parentDirId = dir->fileDirectoryId;
        strcpy(newDir->fileDirectoryName, ss);

        dir->fileDirectoryEntry[dir->fileDirNum].id = newDir->fileDirectoryId;
        dir->fileDirectoryEntry[dir->fileDirNum].parentId = currFileDir->fileDirectoryId;
        dir->fileDirNum += 1;
        //cout << newDir->fileDirectoryId << endl;
        int c = blockWrite(dir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
        int d = blockWrite(newDir, superBlock->fileDirStart + newDir->fileDirectoryId, 0, sizeof(FileDirectory));
    }
    else
    {
        changeDir(path);
        char* fileName;
        char* temp;
        temp = strtok(path, "/");
        fileName = temp;
        while((temp = strtok(NULL, "/")))
        {
            fileName = temp;
        }
        createDir(fileName);
    }
    return NO_ERROR;
}

int cat(char *fileName)
{
    if(fileExist(fileName) == 1)
    {
        int inodeId = getIdFromFileName(fileName);
        DiskInode* diskInode = inodeGet(inodeId);
        memset(buf, 0, sizeof(buf));
        fileRead(diskInode, buf, 0, diskInode->fileSize);
        cout<<buf<<endl;
    }
    else
    {
        cout<<"File not exist."<<endl;
    }
}

int sum()
{
    bitmapRead(1);
    cout<<"freeblock: "<<superBlock->freeBlock<<endl;
    for (int i = 0; i < 512; i++)
    {
        cout << blockBitmap[i] << " ";
    }
}

// 创建文件
int createFile(char *filename, int filesize = 50)
{
    bitmapRead(0);
    int count = currFileDir->fileDirNum;
    FileDirectory *dir = (FileDirectory *)calloc(1, sizeof(FileDirectory));
    blockRead(dir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
    for (int i = 0; i < dir->fileDirNum; i++)
    {
        if (strcmp(dir->fileDirectoryEntry[i].fileName, filename) == 0)
        {
            cout.write(filename, strlen(filename));
            cout << "it is exit in current directory" << endl;
            return -1;
        }
    }
    int id;
    strcpy(dir->fileDirectoryEntry[dir->fileDirNum].fileName, filename);
    //寻找到空闲的目录ID
    bitmapRead(0);
    for (int i = 0; i < superBlock->diskInodeNum; i++)
    {
        if (inodeInUse(i) == false)
        {
            bitmapWrite(0);
            id = i;
            allocateInode(id);
            break;
        }
    }

    struct DiskInode *tmpnode = inodeGet(id);
    tmpnode->fileSize = filesize;
    updateInode(tmpnode);
    allocateBlock(id);
    
    dir->fileDirectoryEntry[dir->fileDirNum].id = id;
    dir->fileDirNum += 1;
    int d = blockWrite(dir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
    
    cout<<"Create file success, inodeId is: "<<tmpnode->inodeId<<endl;

    return 1;
}

int sumblock()
{
    for (int i = 0; i < 512; i++)
    {
        cout << blockBitmap[i] << " ";
    }
}

int deleteFile(char *fileName)
{
    int number = currFileDir->fileDirNum;
    for (int i = 0; i < number; ++i)
    {
        FileDirectoryEntry entry = currFileDir->fileDirectoryEntry[i];
        if (strcmp(entry.fileName, fileName) == 0)
        {
            inodeFree(entry.id);
            DiskInode *inode = inodeGet(entry.id);
            inode->fileSize = 0;

            //删掉这个inode
            blockWrite(inode, superBlock->inodeStart + (inode->inodeId) / 16, (inode->inodeId % 16) * 64, sizeof(struct DiskInode));

            bitmapRead(0);
            inodeBitmap[inode->inodeId / 32] = setBitFromUint(inodeBitmap[inode->inodeId / 32], inode->inodeId % 32, 0);
            bitmapWrite(0);

            //将删除位置后面的元素挨个往前移
            for (int j = i; j < number - 1; ++j)
            {
                currFileDir->fileDirectoryEntry[j] = currFileDir->fileDirectoryEntry[j + 1];
            }

            currFileDir->fileDirNum--;
            blockWrite(currFileDir, superBlock->fileDirStart + (currFileDir->fileDirectoryId), 0, sizeof(struct FileDirectory));

            return NO_ERROR;
        }
    }

    return FILE_NOT_EXIST;
}

//怎么判断是文件还是目录？
bool isDir(char *path)
{
    return path[0] == '/';
}

FileDirectory *parentDir(int id)
{
    FileDirectory *dir = (struct FileDirectory *)calloc(1, sizeof(struct FileDirectory));
    blockRead(dir, superBlock->fileDirStart + id, 0, sizeof(struct FileDirectory));

    return dir;
}

int deleteDir(char *path)
{
    if (!isDir(path))
    {
        deleteFile(path);
        return NO_ERROR;
    }

    for (int i = 0; i < currFileDir->fileDirNum; ++i)
    {
        //找到要删的那个
        if (strcmp(currFileDir->fileDirectoryEntry[i].fileName, path) == 0)
        {
            int backup = currFileDir->fileDirectoryId;
            //进入该目录
            blockRead(currFileDir, superBlock->fileDirStart + currFileDir->fileDirectoryEntry[i].id, 0, sizeof(struct FileDirectory));
            for (int j = 0; j < currFileDir->fileDirNum; ++j)
                deleteDir(currFileDir->fileDirectoryEntry[j].fileName);

            //清掉当前这个空目录
            bitmapRead(2);
            fileDirBitmap[currFileDir->fileDirectoryId / 32] = setBitFromUint(fileDirBitmap[currFileDir->fileDirectoryId / 32], currFileDir->fileDirectoryId % 32, 0);
            bitmapWrite(2);

            //回到上一级
            currFileDir = parentDir(backup);

            //将当前目录里的这个空文件夹删掉，将删除位置后面的元素挨个往前移
            for (int j = i; j < currFileDir->fileDirNum - 1; ++j)
            {
                currFileDir->fileDirectoryEntry[j] = currFileDir->fileDirectoryEntry[j + 1];
            }
            currFileDir->fileDirNum -= 1;
            blockWrite(currFileDir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
            return NO_ERROR;
        }
    }

    return FILE_NOT_EXIST;
}

//展示该目录下的文件
int ls()
{
    FileDirectory *dir = (FileDirectory *)calloc(1, sizeof(FileDirectory));
    blockRead(dir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
    for (int i = 0; i < dir->fileDirNum; i++)
        if (dir->fileDirectoryEntry[i].fileName[0] == '/')
        {
            cout << dir->fileDirectoryEntry[i].fileName << " "<<endl;
        }
        else
        {
            DiskInode *ff = inodeGet(dir->fileDirectoryEntry[i].id);
            cout<< "filename = " << dir->fileDirectoryEntry[i].fileName << " filesize :" << ff->fileSize << " createtime : " << ff->createTime << endl;
        }
}

// copy file1 to file2
int cp(char* file1, char* file2)
{
    int file1Id = getIdFromFileName(file1);
    int file2Id = getIdFromFileName(file2);
    DiskInode* file1Inode = inodeGet(file1Id);
    DiskInode* file2Inode = inodeGet(file2Id);
    if(file2Inode->fileSize < file1Inode->fileSize)
    {
        cout<<"The fileSize of file2 is smaller than file1";
    }
    else
    {
        memset(buf, 0, sizeof(buf));
        fileRead(file1Inode, buf, 0, file1Inode->fileSize);
        fileWrite(file2Inode, buf, file1Inode->fileSize);
    }
}


// 解析输入
int dispatcher()
{
    // 补充welcome message, group info(names and IDs), copyright
    char* command;
    char ss[20] = {'/'};
    char str[8192] = {0};
    blockRead(currFileDir, superBlock->fileDirStart + currFileDir->fileDirectoryId, 0, sizeof(FileDirectory));
    
    cout<<endl<<"[@localhost ";
    getCurrPath();
    cout<<" ]#";

    char ch = getchar();
    int num = 0;
    if (ch == 10) // 换行符的ACSII是10 '\n'
        return 0;
    while (ch != 10)
    {
        str[num] = ch;
        ch = getchar();
        num++;
    }
    command = strtok(str, " "); // 用空格分割, 将空格换成'\0', '\0'是字符串结束的标志
    //printf("%s\n", command);

    if (strcmp(command, "createFile") == 0)
    {
        // createFile fileName fileSize
        //printf("%s\n", &command);
        char* fileName = strtok(NULL, " ");
        char* fileSize = strtok(NULL, " ");
        if(fileSize == NULL)
        {
            createFile(fileName);
        }
        createFile(fileName, charToInt(fileSize));
    }
    else if (strcmp(command, "cd") == 0)
    {
        FileDirectory *inode = NULL, *ret = NULL;
        command = strtok(NULL, " ");
        if (command[0] == '/')
            inode = root;
        else
        {
            inode = currFileDir;
        }
        ret = cd(command, inode);
        if (ret != NULL)
        {
            currFileDir = ret;
            cout << currFileDir->fileDirectoryName << " " << currFileDir->fileDirectoryId << endl;
        }
    }
    else if (strcmp(command, "dir") == 0)
    {
        ls();
    }
    else if(strcmp(command, "changeDir") == 0)
    {
        command = strtok(NULL, " ");
        changeDir(command);
    }
    else if (strcmp(command, "createDir") == 0)
    {
        command = strtok(NULL, " ");
        createDir(command);
    }
    else if (strcmp(command, "cat") == 0)
    {
        command = strtok(NULL, " ");
        cat(command);
    }
    else if (strcmp(command, "sum") == 0)
    {
        sum();
    }
    else if (strcmp(command, "sumblock") == 0)
    {
        sumblock();
    }
    else if (strcmp(command, "deleteFile") == 0)
    {
        command = strtok(NULL, " ");
        deleteFile(command);
        cout<<"call deleteFile"<<endl;
    }
    else if (strcmp(command, "deleteDir") == 0)
    {
        command = strtok(NULL, " ");
        deleteDir(command);
        cout<<"call deleteDir"<<endl;
    }
    else if (strcmp(command, "cp") == 0)
    {
        char* file1 = strtok(NULL, " ");
        char* file2 = strtok(NULL, " ");

        if(fileExist(file1) && fileExist(file2))
        {
            if(strcmp(file1, file2) != 0)
            {
                cp(file1, file2);
            }
        }
        else
        {
            cout<<"File not exist"<<endl;
        }
    }
    else if(strcmp(command, "allo") == 0)
    {
        char buff[100]={"Hello World!"};
        DiskInode* temp = inodeGet(0);
        fileWrite(temp, buff, sizeof(buff));
    }
    else if(strcmp(command, "exit") == 0)
    {
        logout == true;
    }
}