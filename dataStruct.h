#include "define.h"

// UpperCamelCase


// 超级块 4*6+1000 = 1024B
struct SuperBlock
{
    unsigned int size;                     // 磁盘大小
    unsigned int fileDirStart;             // 文件目录表起始块号
    unsigned int inodeBitmapStart;         // inode位图起始块号
    unsigned int blockBitmapStart;         // 空闲块位图起始块号
    unsigned int inodeStart;               // inode块起始块号
    unsigned int BlockStart;               // 数据块起始块号
    char fill[1000];                       // 填充
};

// 磁盘索引节点 64B
struct DiskInode
{
    int type;              // 文件类型
    unsigned int inodeId;  // 索引节点id
    int fileSize;          // 文件大小 按字节
    int addr[11];          // 物理地址 addr[0]~addr[9]是直接地址, addr[10]是间接地址
    int createTime;        // 创建时间
    int modifyTime;        // 修改时间
};

// 文件目录项  32+1 = 33B
struct FileDirectoryEntry
{
    char fileName[MAX_FILE_NAME];          // 文件名
    unsigned int inodeId;                  // 索引节点编号
};

// 文件目录  max = 4*1024*33 + 1 B = 123 Blocks
struct FileDirectory
{
    int directoryNum;                                         // 目录数目
    FileDirectoryEntry fileDirectoryEntry[DISK_INODE_NUM];    // 目录项数组
};

// 文件
struct File
{
    DiskInode *diskInode;                  // 磁盘索引节点
    int f_curpos;                          // ???
};