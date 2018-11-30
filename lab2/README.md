# Basic File Server
YFS的架构：
![在这里插入图片描述](https://img-blog.csdnimg.cn/20181129194609433.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3dlc3Ricm9va2xpdQ==,size_16,color_FFFFFF,t_70)

YFS模块实现核心文件系统逻辑，这个模块作为`yfs_client`的单个进程运行，该进程支持本地主机上的挂载点，该模块有两部分组成：
1. FUSE接口：在`fuse.cc`中，它将FUSE操作从FUSE内核模块转换为YES客户端调用。
2. YES客户端：`yfs_client.{cc,h}，YES实现客户端文件系统逻辑，使用锁和扩展服务器来帮助它。
例如，在创建新文件时，`yfs_client`会将目录条目添加到扩展服务器的目录块中。

扩展服务器存储文件系统所有的数据，类似于不同文件系统的硬盘。在将来的实验中，可以在多个主机上运行YES客户端，所有主机都与同一个扩展服务器通信；
效果就是所有主都能看到并共享同一个文件系统。
多个YES客户端可以共享数据的唯一方法就是通过扩展服务器。
扩展服务器由两部分构成：
1. `extent_client`，使用RPC与extent server进行通信的包装类
2. `extent_server`，扩展服务器管理一个简单的键值存储，以`extent_protocol::extentid_t`为键，以`string`以及对应的属性为值。

## Part1：CREATE/MKNOD, LOOKUP, and READDIR
## Part2：SETATTR,READ,WRITE

这两个部分完成的代码是类似的，只是实现的功能不同。

1. 首先完成`extent_server.{h,cc}`，这个服务器要实现一个简单的键值对存储，能实现`put`、`get`、`getattr`、`remove`的功能。这些方法会在之后实现`yfs_client`时用到，例如要创建一个文件，则需要`get`一个目录，然后`put`创建一个文件，最后`put`将创建的文件放入该目录。
2. 接着实现`yfs_client.{h,cc}`，要能实现上面标题所显示的六种方法，最重要的是能理解`inum`是什么及有什么用，每个文件或者目录都有唯一的inum，因此还要实现一个产生唯一的`inum`的函数。
3. 最后就是实现`fuse.cc`了，参考注释很快就能完成了，这里会调用到上一步骤实现的函数。


## 调试

### create错误
```shell
test-lab-2-a: cannot create ./yfs1/file-yxfsroxwdfzzniqgxqzgxjpyxtwdievwhenavyda-24012-0 : No such file or directory
```
第一次create就失败了，检查一下有关create的代码，没发现有什么问题。回去再看一遍实验指导，发现有这样一句话：
```
FUSE假定根目录的inum是0x00000001。因此，您需要确保在yfs_client启动时，它已准备好导出存储在该inum下的空目录。
```
因此要在`extent_server`的构造函数中加上两行：
```cpp
  int ret;
  put(1, "", ret);
```

### open错误
```shell
test-lab-2-a: cannot open ./yfs1/file-nmhxcnnviodnsyfqcasqiorffhpoxtetbrhanteo-30075-198 : No such file or directory
```

经检查，发现了一个错误，在创建文件时，只是创建了文件，但并没有与目录绑定。
在第二个调用`put`中要是传入参数的目录的`inum`。

```cpp
int
yfs_client::create(inum parent, const char* name, inum& inum)
{
  std::string data;
  std::string file_name;
  if(ec->get(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  file_name = "/" + std::string(name) + "/";
  // 文件已经存在
  if (data.find(file_name) != std::string::npos)
  {
    return EXIST;
  }

  inum = random_inum(true);
  if(ec->put(inum, std::string()) != extent_protocol::OK)
  {
    return IOERR;
  }

  data.append(file_name + filename(inum) + "/");
  if(ec->put(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  return OK;
}
```

## 测试
```shell
$ ./start.sh 
starting ./lock_server 29818 > lock_server.log 2>&1 &
starting ./extent_server 29812 > extent_server.log 2>&1 &
starting ./yfs_client /home/liu/Desktop/yfsCpp11/lab2/yfs1 29812 29818 > yfs_client1.log 2>&1 &
starting ./yfs_client /home/liu/Desktop/yfsCpp11/lab2/yfs2 29812 29818 > yfs_client2.log 2>&1 &
$ ./test-lab-2-a.pl ./yfs1 
create file-cmrivvpetwwtwdazsapiphcemhnfinxrgdzxgqvi-13540-0
create file-wybthquccdqwjexfmngiseabesctrnoyyxtvpbta-13540-1
create file-ncrjwuoozrtdgggmqlxgmwuzpctamfvhhuasvdkr-13540-2
...
...
Passed all tests!
$ ./test-lab-2-b.pl ./yfs1 ./yfs2
Write and read one file: OK
Write and read a second file: OK
Overwrite an existing file: OK
Append to an existing file: OK
Write into the middle of an existing file: OK
Write beyond the end of an existing file: OK
Check that one cannot open non-existant file: OK
Check directory listing: OK
Read files via second server: OK
Check directory listing on second server: OK
Passed all tests
```