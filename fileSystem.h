#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string.h>
#include <time.h>
#include<stdlib.h>


#include "define.h"
#include "dataStruct.h"

using namespace std;

char buf[TOTAL_SIZE];
// 模拟磁盘
FILE* virtualDisk;
// 超级块
struct SuperBlock* superBlock;
// 当前目录
struct FileDirectory* currFileDir;

// 两种bitmap
unsigned int inodeBitmap[128] = {0};
unsigned int blockBitmap[512] = {0};
unsigned int fileDirBitmap[4] = {0};

// 退出
bool logout = false;


//------------------------------转换函数------------------------------

unsigned int sizeToBlockId(unsigned int size)
{
    return (unsigned int)(size/1024);
}

unsigned int getBlockIdFromAddress(struct Address addr)
{
    return (unsigned int)(addr.ch[0]*64 + addr.ch[1]);
}

struct Address getAddressFromBlockId(unsigned int blockId)
{
    Address addr;
    addr.ch[2] = 0;
    addr.ch[1] = blockId%64;
    addr.ch[0] = (int)(blockId/64);
    return addr;
}

// 提取unsigned int 中pos位置的bit
int getBitFromUint(unsigned int num,int leftPos)
{
    int rightPos = 31 - leftPos;
    for(int i=0;i<rightPos;i++)
    {
        num/=2;
    }
    return num%2;
}

// 设置unsigned int 中pos位置的bit 返回unsigned int
unsigned int setBitFromUint(unsigned int num, int leftPos, int bit)
{
    unsigned int temp = 1;
    unsigned int utemp = num;
    int rightPos = 31 - leftPos;
    for(int i=0;i<rightPos;i++)
    {
        num/=2;
        temp*=2;
    }
    if(num%2 != bit)
    {
        if(num%2 == 1)
        {
            utemp-=temp;
        }
        else
        {
            utemp += temp;
        }
    }
    return utemp;
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
        int readSize = fread(inodeBitmap, sizeof(inodeBitmap), 128, virtualDisk);
        if(readSize != 128)
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
    else if(kind == 2)   // fileDir
    {
        bitmapPos = superBlock->fileDirBitmapStart*1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int readSize = fread(blockBitmap, sizeof(blockBitmap), 4, virtualDisk);
        if (readSize != 4)
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
        int writeSize = fwrite(inodeBitmap, sizeof(inodeBitmap), 128, virtualDisk);
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
    else if(kind == 1)   // fileDir
    {
        bitmapPos = superBlock->fileDirBitmapStart*1024;
        fseek(virtualDisk, bitmapPos, SEEK_SET);
        int writeSize = fread(blockBitmap, sizeof(blockBitmap), 4, virtualDisk);
        if (writeSize != 4)
        {
            return ERROR_WRITE_BITMAP;
        }
    }
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
    struct DiskInode* diskInode = (struct DiskInode*)calloc(1, sizeof(struct DiskInode));
    int readSize = fread(diskInode, sizeof(struct DiskInode), 1, virtualDisk);
    if (readSize != 1)
    {
        return NULL;
    }
    if (diskInode->createTime == 0) // new file
    {
        diskInode->fileSize = 0;
        diskInode->inodeId = inodeId;
        time_t timer;
        time(&timer);
        diskInode->createTime = timer;
        updateInode(diskInode);
    }
    diskInode->inodeId = inodeId;
    return diskInode;
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
    return NO_ERROR;
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
                blockBitmap[i] = setBitFromUint(blockBitmap[i], j, 1);
                find == true;
                blockId = i*32 + j;
                return blockId;
            }
            if(find == true)
                break;
            
        }
        if(find == true)
            break;
        
    }
}

// 为inode对应文件分配block, 调用函数前需要设置inode的fileSize
int allocateBlock(unsigned int inodeId, int fileSize)
{
    bitmapRead(1);
    struct DiskInode* inode = inodeGet(inodeId);
    inode->fileSize=fileSize;
    if (sizeToBlockId(inode->fileSize) > superBlock->freeBlock - 1) // -1 是为了间址所使用的block
    {
        return ERROR_INSUFFICIENT_FREE_BLOCKS;
    }
    int blockNum = (fileSize+1023)/1024;
    for(int i=0;i<min(blockNum, 10);i++)
    {
        unsigned int t = allocateOneBlock();
        inode->addr[i] = getAddressFromBlockId(t);
        // cout<<"the blockid "<<t<<endl;
        // cout<<getBlockIdFromAddress(inode->addr[i])<<endl;
    }
    blockNum-=10;
    if (blockNum > 0)
    {
        unsigned int blockId = allocateOneBlock();
        // blockBitmap[blockId]=1;
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




//-----------------------------字符相关--------------------------------

//查找某个字符在字符串中的位置
int strPos(char* str, int start, const char needle)
{
	for (int i = start; i<strlen(str); i++)
	{
		if (str[i] == needle)
			return i;
	}
	return -1;
}
//复制src到dst
int strCpy(char *dst, char *src, int offset)
{
	int len = strlen(src);
	if (len <= offset)
		return 0;
	int i;
	for (i = 0; i<len - offset; i++)
	{
		dst[i] = src[i + offset];
		//cout<<"dst["<<i<<"]"<<dst[i]<<endl;
	}
	dst[i] = 0;
	return 1;
}
//从start开始复制字符串
int subStr(char* src, char*dst, int start, int end = -1)
{
	int pos = 0;
	end == -1 ? end = strlen(src) : 0;
	for (int i = start; i<end; i++)
		dst[pos++] = src[i];
	dst[pos] = 0;
	return 1;
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
    superBlock->freeBlock = BLOCK_NUM;
    fseek(virtualDisk, 0, SEEK_SET);
    
    // 根目录初始化
    FileDirectory* root = (struct FileDirectory*)calloc(1, sizeof(FileDirectory));
    root->fileDirNum = 0;
    char str[] = "/";
    strncpy(root->fileDirectoryName, str, sizeof(root->fileDirectoryName));
    root->fileDirectoryId = 0;
    blockWrite(root, superBlock->fileDirStart, sizeof(struct FileDirectory), 1); // 写入磁盘
    fileDirBitmap[0] = setBitFromUint(fileDirBitmap[0], 0, 1);
    bitmapWrite(2); // 更新bitmap

    // blockBitmap初始化
    for(int i=0; i<superBlock->blockStart; i++)
    {
        blockBitmap[i/32] = setBitFromUint(blockBitmap[i/32], i%32, 1);
    }
    bitmapWrite(1);

    
    // int ans = blockRead(superBlock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    // cout<<superBlock->size<<endl;  // 16777216
    // cout<<ans<<endl;  // 1
    
    // fclose(virtualDisk);
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
    // 读入文件目录表(根目录)
    currFileDir = (struct FileDirectory*)calloc(1, sizeof(struct FileDirectory));
    blockRead(currFileDir, superBlock->fileDirStart, 0, sizeof(struct FileDirectory));
    
    // 读入bitmap
    bitmapRead(0);
    bitmapRead(1);
    bitmapRead(2);

    return NO_ERROR;
}



//------------------------------输入的响应事件------------------------------

/*
DiskInode* cd(char* path,DiskInode* node){
        if(path[0] == '/'&&strlen(path)==1){
            currInode->inodeId=0;
            return NULL;
        }
        int start=path[0]=='/';
        char t[16]={0};
        int pos=strPos(path,1,'/');
        subStr(path,t,start,pos);
        int type=node->type;
        
        if(type/1000==1){ //inode
            int count=node->fileSize/sizeof(FileDirectoryEntry);
            int addnum = count / 28 + (count % 28 >= 1 ? 1 : 0);
            FileDirectory* dir=(FileDirectory*)calloc(1,sizeof(FileDirectory));
            if(addnum>8) addnum=8;
            for(int add=0;add<addnum;add++){
                blockRead(dir,getBlockIdFromAddress(node->addr[add]),0,sizeof(FileDirectory));
                for(int i=0;i<dir->fileDirNum;i++){
                    if(strcmp(dir->fileDirectoryEntry[i].fileName,t)==0){
                        count=-1;
                        currFileDirEntry=dir->fileDirectoryEntry[i];
                        //DiskInode* tmpnode=inodeGet(dir->fileDirectoryEntry[i].inodeId);
                        DiskInode* tmpnode=(DiskInode*)calloc(1,sizeof(DiskInode));
                        fseek(virtualDisk, INODE_START*1024 + dir->fileDirectoryEntry[i].inodeId*DISK_INODE_SIZE, SEEK_SET); // 指针移到对应inode的位置
                        int readSize = fread(tmpnode, sizeof(struct DiskInode), 1, virtualDisk);
                        cout<<tmpnode->inodeId<<endl;
                        cout<<tmpnode->type<<endl;
                        cout<<tmpnode->createTime<<endl;
                        if(tmpnode->type/1000!=1){
                            cout<<"this path is not a dir"<<endl;
                            continue;
                        }
                        node=tmpnode;
                        break;
                    }
                }if(count==-1)
                  break;

            }if(count!=-1)
              node=NULL;
        }
        else{
            node=NULL;
            return node;
        }
        if (pos != -1 && node != NULL)
	    {
		subStr(path, t, pos + 1);
		return cd(t, node);
	    }
	if (pos == -1)
		return node;

}

int mkdir(char* filename){
    if(currInode->type/1000!=1){
        cout<<"current node is not a dir"<<endl;
        return NOT_A_DIR;
    }
    
    int count=currInode->fileSize/sizeof(FileDirectoryEntry);
    // cout<<"the count ="<<count<<endl;
    if(count>252){
        cout<<"can not make more dir in current dir"<<endl;
        return CAN_NOT_MAKE_MORE_DIR;
    }
    
    int addnum = count / 28 + (count % 28 >= 1 ? 1 : 0);
    FileDirectory* dir=(FileDirectory*)calloc(1,sizeof(FileDirectory));
    if(addnum>8) addnum=8;
    for(int add=0;add<addnum;add++){
        cout<<getBlockIdFromAddress(currInode->addr[add])<<endl;
        blockRead(dir,getBlockIdFromAddress(currInode->addr[add]),0,sizeof(FileDirectory));
        for(int i=0;i<dir->fileDirNum;i++)
            if(strcmp(dir->fileDirectoryEntry[i].fileName,filename)==0){
                     cout.write(filename, strlen(filename));
				     cout << " is exist in current dir!" << endl;
				     return -1;
                    }     
        }
        currInode->fileSize+=sizeof(currFileDirEntry);
        updateInode(currInode);
        int addr = count / 28;
        // cout<<"the read address is "<<getBlockIdFromAddress(currInode->addr[addr])<<endl;
        blockRead(dir,getBlockIdFromAddress(currInode->addr[addr]),0,sizeof(FileDirectoryEntry));
        strcpy(dir->fileDirectoryEntry[dir->fileDirNum].fileName,filename);
        //找到一个空闲的Inode，并创建
        int id=1;
        for(int i=0;i<superBlock->diskInodeNum;i++)
        {
            if(inodeInUse(i) == false)
            {
                inodeBitmap[i/32] = setBitFromUint(inodeBitmap[i/32], i%32, 1);
                bitmapWrite(0);
                allocateInode(i);
                allocateBlock(i,50);
                id=i;
                break;
            }
        }
       
        struct DiskInode* tmpnode=inodeGet(id);
        tmpnode->inodeId = id;
        tmpnode->type=1774;
        time_t timer;
        time(&timer);
        count++;
        tmpnode->createTime = timer;
        int e=updateInode(tmpnode);   // 写入磁盘
        cout<<"the iresult is "<<e<<endl;
        // cout<<"the bitmap number"<<id<<endl;
        
        dir->fileDirectoryEntry[dir->fileDirNum].inodeId=tmpnode->inodeId;
        // cout<<dir->fileDirectoryEntry[dir->directoryNum].fileName<<endl;
        dir->fileDirNum+=1;
        for(int i=0;i<dir->fileDirNum;i++){
            cout<<dir->fileDirectoryEntry[i].fileName<<" "<<dir->fileDirectoryEntry[i].inodeId<<endl;
        }
        fseek(virtualDisk, 0, SEEK_SET);
        // cout<<"the address is "<<getBlockIdFromAddress(currInode->addr[addr])<<endl;
        int d=blockWrite(dir,getBlockIdFromAddress(tmpnode->addr[addr]),0,sizeof(FileDirectory));
        // int c=blockWrite(dir,getBlockIdFromAddress(currInode->addr[addr]),0,sizeof(FileDirectory));
        // cout<<"the result of the blockwrite "<<c<<endl;
        return 1;  
}
int cat(char * filename)
{
	int inodeid = 0;
	int count = currInode->fileSize / sizeof(FileDirectoryEntry);
	FileDirectory* dir = (struct FileDirectory*)calloc(1, sizeof(FileDirectory));
	int addrnum = count / 28 + (count % 28 >= 1 ? 1 : 0);
	addrnum>4 ? addrnum = 8 : 0;
	for (int addr = 0; addr<addrnum; addr++)
	{
		blockRead(dir,getBlockIdFromAddress(currInode->addr[addr]), 0, sizeof(FileDirectory));
		for (int i = 0; i<dir->fileDirNum; i++)
		{
			if (strcmp(dir->fileDirectoryEntry[i].fileName, filename) == 0)
			{
				inodeid = dir->fileDirectoryEntry[i].inodeId;
				count = -1;
				break;
			}
		}
		if (count == -1)
			break;
	}
	if (inodeid == 0)
	{
		cout << "can not found the file ";
		cout.write(filename, strlen(filename));
		cout << endl;
		return -1;
	}
	struct DiskInode* inode = inodeGet(inodeid);
	int addr = inode->fileSize / 1024;
	int lastCount = inode->fileSize % 1024;
	int i;
	for (i = 0; i<addr; i++)
	{
		char content[1024] = { 0 };
		blockRead(&content,getBlockIdFromAddress(inode->addr[i]), 0, sizeof(char), 1024);
		cout.write(content, 1024);
	}
	char content[1024] = { 0 };
	blockRead(&content,getBlockIdFromAddress(inode->addr[i]), 0, sizeof(char), lastCount);
	cout.write(content, strlen(content));
	cout << endl;
	return 1;
}

//展示该目录下的文件
int ls(){
    int type=currInode->type;
    if(currInode->type/1000!=1){
        cout<<"this is not a dir"<<endl;
        return NOT_A_DIR;
    }
    int count = currInode->fileSize / sizeof(FileDirectoryEntry);
	FileDirectory* dir = (FileDirectory*)calloc(1, sizeof(FileDirectory));
	int addrnum = count / 28+(count % 28 >= 1 ? 1 : 0);
	addrnum>4 ? addrnum = 4 : 0;
	for (int addr = 0; addr<addrnum; addr++)
	{   
        cout<<getBlockIdFromAddress(currInode->addr[addr])<<endl;
		int a=blockRead(dir,getBlockIdFromAddress(currInode->addr[addr]), 0, sizeof(FileDirectory));
		for (int i = 0; i<dir->fileDirNum; i++)
            if(strcmp(currFileDirEntry.fileName,dir->fileDirectoryEntry[i].fileName)){
            DiskInode* ff=inodeGet(dir->fileDirectoryEntry[i].inodeId);
			cout<<"filename = "<<dir->fileDirectoryEntry[i].fileName<<" filesize :"<<ff->fileSize<<" createtime : "<<ff->createTime<<endl;
	}
    }
	return 1;
}
int sum(){
    for(int i=0;i<256;i++){
        cout<<inodeBitmap[i]<<" ";
    }
}

// 创建文件
int createFile(char* filename)
{
    if(currInode->type/1000!=1){
        cout<<"current node is not a dir"<<endl;
        return NOT_A_DIR;
    }
    
    int count=currInode->fileSize/sizeof(FileDirectoryEntry);
    if(count>252){
        cout<<"can not make more dir in current dir"<<endl;
        return CAN_NOT_MAKE_MORE_DIR;
    }
    int addnum = count / 28 + (count % 28 >= 1 ? 1 : 0);
    FileDirectory* dir=(FileDirectory*)calloc(1,sizeof(FileDirectory));
    if(addnum>8) addnum=8;
    for(int add=0;add<addnum;add++){
        blockRead(dir,getBlockIdFromAddress(currInode->addr[add]),0,sizeof(FileDirectory));
        for(int i=0;i<dir->fileDirNum;i++)
            if(strcmp(dir->fileDirectoryEntry[i].fileName,filename)==0){
                     cout.write(filename, strlen(filename));
				     cout << " is exist in current dir!" << endl;
				     return -1;
                    }     
        }
        currInode->fileSize+=sizeof(currFileDirEntry);
        updateInode(currInode);
        int addr = count / 4096;
        blockRead(dir,getBlockIdFromAddress(currInode->addr[addr]),0,sizeof(FileDirectoryEntry));
        strcpy(dir->fileDirectoryEntry[dir->fileDirNum].fileName,filename);
        //找到一个空闲的Inode，并创建
        int id;
        for(int i=1;i<256;i++)
            if(inodeBitmap[i]==0) {
                id=i;  
                inodeBitmap[i]=1;
                allocateInode(id);
                allocateBlock(id,100);
                break;
            }
        DiskInode* tmpnode = (struct DiskInode*)calloc(1, sizeof(DiskInode));
        tmpnode->fileSize = 100;
        tmpnode->inodeId = id;
        tmpnode->type=2774;
        time_t timer;
        time(&timer);
        tmpnode->createTime = timer;
        updateInode(tmpnode);   // 写入磁盘
       
        dir->fileDirectoryEntry[dir->fileDirNum].inodeId=tmpnode->inodeId;
        // cout<<dir->fileDirectoryEntry[dir->directoryNum].fileName<<endl;
        dir->fileDirNum+=1;
        cout<<getBlockIdFromAddress(currInode->addr[addr])<<endl;
        fseek(virtualDisk, 0, SEEK_SET);
        int c=blockWrite(dir,getBlockIdFromAddress(currInode->addr[addr]),0,sizeof(FileDirectory), 1);
        // cout<<sizeof(FileDirectory)<<endl;
        // int writeSize = fwrite(dir, 1, sizeof(FileDirectory), virtualDisk);
        // cout<<"WSize "<<writeSize<<endl;
        // dir->directoryNum+=1;
        // struct FileDirectory* d=(struct FileDirectory*)calloc(1, sizeof(struct FileDirectory));
        // int b=blockRead(d,getBlockIdFromAddress(currInode->addr[0]), 0, sizeof(FileDirectory), 1);
        //cout<<"secondtime"<<dir->fileDirectoryEntry[dir->directoryNum-1].fileName<<endl;
        //cout<<d->directoryNum<<endl;
        //cout<<b<<endl;
        //cout<<c<<endl;
        return 1;     
}
int sumblock(){
    for(int i=0;i<512;i++){
        cout<<blockBitmap[i]<<" ";
    }
}
// int cp(char * src)
// {
// 	char srcfile[16] = { 0 },newfile[15] = { 0 };
// 	int pos = strPos(src, 0, ' ');
// 	subStr(src, srcfile, 0, pos);
// 	subStr(src, newfile, pos + 1);
// 	int inodeid = 0;
// 	int count = currInode->fileSize / sizeof(FileDirectoryEntry);
// 	FileDirectory * dir = (struct FileDirectory*)calloc(1, sizeof(FileDirectory));
// 	int addrnum = count / 28 + (count % 28 >= 1 ? 1 : 0);
// 	addrnum>4 ? addrnum = 8:0;
// 	for (int addr = 0; addr<addrnum; addr++)
// 	{
// 		blockRead(dir,getBlockIdFromAddress(currInode->addr[addr]), 0, sizeof(FileDirectory));
// 		for (int i = 0; i<dir->fileDirNum; i++)
// 		{
// 			if (strcmp(dir->fileDirectoryEntry[i].fileName, srcfile) == 0)
// 			{
// 				inodeid = dir->fileDirectoryEntry[i].inodeId;
// 				count = -1;
// 				break;
// 			}
// 		}
// 		if (count == -1)
// 			break;
// 	}
// 	if (inodeid == 0)
// 	{
// 		cout << "can not found the file ";
// 		cout.write(srcfile, strlen(srcfile));
// 		cout << endl;
// 		return -1;
// 	}
// 	DiskInode* srcinode = inodeGet(inodeid);
// 	count = currInode->fileSize / sizeof(FileDirectoryEntry);
// 	if (count>252)
// 	{
// 		cout << "can not make more dir in the current dir!" << endl;
// 		return -1;
// 	}
// 	addrnum = count /28 + (count % 28 >= 1 ? 1 : 0);
// 	addrnum>4 ? addrnum = 4 : NULL;
// 	for (int addr = 0; addr<addrnum; addr++)
// 	{
// 		blockRead(dir, getBlockIdFromAddress(currInode->addr[addr]), 0, sizeof(FileDirectory));
// 		for (int i = 0; i<dir->fileDirNum; i++)
// 			if (strcmp(dir->fileDirectoryEntry[i].fileName, newfile) == 0)
// 			{
// 				cout.write(newfile, strlen(newfile));
// 				cout << " is exist in current dir!" << endl;
// 				return -1;
// 			}
// 	}
// 	currInode->fileSize += sizeof(FileDirectoryEntry);
// 	updateInode(currInode);
// 	int addr = count / 63;
// 	blockRead(dir, getBlockIdFromAddress(currInode->addr[addr]), 0, sizeof(FileDirectory));
// 	strcpy(dir->fileDirectoryEntry[dir->fileDirNum].fileName, newfile);
// 	    int id;
//         for(int i=1;i<256;i++)
//             if(inodeBitmap[i]==0) {
//                 id=i;  
//                 inodeBitmap[i]=1;
//                 allocateInode(id);
//                 allocateBlock(id,100);
//                 break;
//             }
//         DiskInode* tmpnode = (struct DiskInode*)calloc(1, sizeof(DiskInode));
//         tmpnode->fileSize = 100;
//         tmpnode->inodeId = id;
//         tmpnode->type=2774;
//         time_t timer;
//         time(&timer);
//         tmpnode->createTime = timer;
// 	//addr distribut
// 	count = srcinode->fileSize / 1024;
// 	int srcaddr = srcinode->fileSize / 1024 + (srcinode->fileSize % 1024 == 0 ? 0 : 1);
// 	if (srcinode->fileSize == 0)
// 		srcaddr = 1;
// 	for (int i = 0; i<srcaddr; i++)
// 	{
// 		tmpnode->addr[i] = getAddressFromBlockId(allocateBlock());
// 		char content[1024] = { 0 };
// 		blockRead(&content, getBlockIdFromAddress(srcinode->addr[i]), 0, sizeof(char), 1024);
// 		blockWrite(&content, getBlockIdFromAddress(tmpnode->addr[i]), 0, sizeof(char), 1024);
// 	}
// 	tmpnode->fileSize = srcinode->fileSize;
// 	updateInode(tmpnode);
// 	dir->fileDirectoryEntry[dir->fileDirNum].inodeId = tmpnode->inodeId;
// 	dir->fileDirNum += 1;
// 	blockWrite(dir,getBlockIdFromAddress(currInode->addr[addr]), 0, sizeof(FileDirectory));
// 	return 1;
// }

 
int dispatcher()
{
    // 补充welcome message, group info(names and IDs), copyright
    char command[8192] = {0};
    char str[8192] = {0};
    // cout<<"[@localhost /"<<(currInode->inodeId == 0?"/":currFileDirEntry.fileName)<<"]#";
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
    
    if(strcmp(command, "touch") == 0)
    {
        // createFile fileName fileSize
        
        strCpy(command, str, strlen(command) + 1);
        createFile(command);
    }
    else if(strcmp(command,"cd")==0){
        DiskInode* inode = NULL, *ret = NULL;
		strCpy(command, str, strlen(command) + 1);
		if (command[0] == '/')
			inode = root;
		else
			inode = currInode;
		ret = cd(command, inode);
		if (ret != NULL){
			currInode = ret;
            updateInode(currInode);
            // cout<<currInode->inodeId<<" "<<currInode->type<<endl;
    }
    }
    else if(strcmp(command,"ls")==0){
        ls();    
    }
    else if (strcmp(command, "mkdir") == 0)
	{
		strCpy(command, str, strlen(command) + 1);
		mkdir(command);
	}
	else if (strcmp(command, "cat") == 0)	{
		strCpy(command, str, strlen(command) + 1);
		cat(command);
	}
    else if(strcmp(command,"sum")==0){
        sum();
    }
    else if(strcmp(command,"sumblock")==0){
        sumblock();
    }

    
    // deleteFile filename
    // createDir
    // deleteDir
    // changeDir
    // dir
    // cp
    // sum
    // cat
    

}
*/