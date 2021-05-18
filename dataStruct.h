#include "define.h"

// UpperCamelCase


// 超级块 4*14+968 = 1024B
struct SuperBlock
{
    unsigned int size;                     // 磁盘大小
    unsigned int start;                    // 总起始块号
    unsigned int fileDirStart;             // 文件目录表起始块号
    unsigned int inodeBitmapStart;         // inode位图起始块号
    unsigned int blockBitmapStart;         // 空闲块位图起始块号
    unsigned int inodeStart;               // inode块起始块号
    unsigned int blockStart;               // 数据块起始块号
    unsigned int blockNum;                 // 数据块总数
    unsigned int blockSize;                // 数据块大小
    unsigned int diskInodeNum;             // inode总数
    unsigned int fileInodeNum;             // 一个文件夹中inode最多的个数
    unsigned int diskInodeSize;            // inode大小
    unsigned int freeInode;                // 剩余可分配inode数
    unsigned int freeBlock;                // 剩余可分配block数
    char fill[968];                        // 填充
};

// 地址 3B
struct Address
{
    char ch[3];
};

// 磁盘索引节点 64B = 4B*5 + 3B*11 + 11B
struct DiskInode
{
    int type;              // 文件类型
    unsigned int inodeId;  // 索引节点id
    int fileSize;          // 文件大小 按字节
    Address addr[11];      // 物理地址 addr[0]~addr[9]是直接地址, addr[10]是间接地址
    int createTime;        // 创建时间
    int modifyTime;        // 修改时间
    char fill[11];         // 填充
};

// 文件目录项  32+4 = 36B
struct FileDirectoryEntry
{
    char fileName[MAX_FILE_NAME];          // 文件名
    unsigned int inodeId;                  // 索引节点编号
};

// 文件目录  max = 8 + 28*36(1004) + 12 = 1 Block
struct FileDirectory
{
    int fileDirNum;                                           // 包含的文件/目录数量
    int fileDirectoryId;                                      // 文件目录Id
    FileDirectoryEntry fileDirectoryEntry[FILE_INODE_NUM];    // 目录项数组
    char fill[12];                                            // 填充
};

