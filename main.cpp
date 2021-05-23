#include <iostream>
#include <stdio.h>

#include "fileSystem.h"

using namespace std;

int main()
{
    const char *path = "vm.dat";
    //cout<< setBitFromUint(5, 30, 1) <<endl;
    ///* 第一次运行时需要初始化
    int check = initialize(path);
    fseek(virtualDisk, 1024, SEEK_SET);
    SuperBlock* superblock = (struct SuperBlock *)calloc(1, sizeof(struct SuperBlock));
    int ans = blockRead(superblock, superBlock->start, 0, sizeof(struct SuperBlock), 1);
    cout<<superblock->size<<endl;  // 16777216
    cout<<ans<<endl;  // 1
    //fread(buf, 1024, 1, virtualDisk);
    printf("%s", buf);

    //cout<<"check: "<<check<<endl;
    // while (!logout)
    // {
    // 	dispatcher();
    // }
    //loadVirtualDisk(path);
    //root = inodeGet(0); // 获取根目录

    // login();
}