#include <iostream>
#include <stdio.h>

#include "fileSystem.h"

using namespace std;

int main()
{   
    const char *path = "vm.dat";
    ///* 第一次运行时需要初始化
    //initialize(path);
    loadVirtualDisk(path);
    while (!logout)
    {
    	dispatcher();
    }
    system("pause");

}