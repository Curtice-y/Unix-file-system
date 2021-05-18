#include <iostream>
#include <stdio.h>


#include "fileSystem.h"

using namespace std;

int main()
{
    const char* path = "vm.dat";
    ///* 第一次运行时需要初始化
    int check = initialize(path);
    cout<<"check: "<<check<<endl;
	while (!logout)
	{
		dispatcher();
	}
    //loadVirtualDisk(path);
    //root = inodeGet(0); // 获取根目录
    // login();
    system("pause");
}