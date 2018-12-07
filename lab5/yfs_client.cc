// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
// #include "lock_client.h"
#include "lock_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  // m_lc = new lock_client(lock_dst);
  m_lc = new lock_client_cache(lock_dst);
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

yfs_client::inum
yfs_client::random_inum(bool isfile)
{
  inum ret = (unsigned long long)((rand() & 0x7fffffff) | (isfile << 31));
  ret = 0xffffffff & ret;

  return ret;
}

int
yfs_client::create(inum parent, const char* name, inum& inum)
{
  LockGuard lg(m_lc, parent);
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

int
yfs_client::lookup(inum parent, const char* name, inum& inum, bool* found)
{
  size_t pos, end;
  std::string data, file_name, ino;

  if(ec->get(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  file_name = "/" + std::string(name) + "/";
  pos = data.find(file_name);
  if(pos != std::string::npos)
  {
    *found = true;
    pos += file_name.size();
    end = data.find_first_of("/", pos);
    if(end != std::string::npos)
    {
      ino = data.substr(pos, end - pos);
      inum = n2i(ino.c_str());
    }
    else
    {
      return IOERR;
    }
  }
  else
  {
    return IOERR;
  }

  return OK;
}

int
yfs_client::readdir(inum inum, std::list<dirent> & dirents)
{
  std::string data, inum_str;
  size_t pos, name_end, name_len, inum_end, inum_len;
  if(ec->get(inum, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  pos = 0;
  while(pos != data.size())
  {
    dirent d;
    pos = data.find("/", pos);
    if(pos == std::string::npos)
    {
      break;
    }

    name_end = data.find_first_of("/", pos+1);
    name_len = name_end - pos - 1;
    d.name = data.substr(pos+1, name_len);


    inum_end = data.find_first_of("/", name_end + 1);
    inum_len = inum_end - inum_end - 1;
    inum_str = data.substr(name_end+1, inum_len);

    d.inum = n2i(inum_str.c_str());
    dirents.push_back(d);
    pos = inum_end + 1;
  }

  return OK;
}


int
yfs_client::setattr(inum inum, struct stat* attr)
{
  LockGuard lg(m_lc, inum);
  size_t size = attr->st_size;
  std::string buf;
  if(ec->get(inum, buf) != extent_protocol::OK)
  {
    return IOERR;    
  }

  buf.resize(size, '\0');

  if(ec->put(inum, buf) != extent_protocol::OK)
  {
    return IOERR;
  }

  return OK;
}

int
yfs_client::read(inum inum, off_t off, size_t size, std::string &buf)
{
  std::string data;
  size_t read_size;
  if(ec->get(inum, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  if(off >= data.size())
  {
    buf = std::string();
  }

  read_size = size;
  if(off + size > data.size())
  {
    read_size = data.size() - off;
  }
  buf = data.substr(off, read_size);

  return OK;
}

int
yfs_client::write(inum inum, off_t off, size_t size, const char *buf)
{
  LockGuard lg(m_lc, inum);
  std::string data;
  if(ec->get(inum, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  if(size + off > data.size())
  {
    data.resize(size + off, '\0');
  }

  for(size_t i = 0; i < size; i++)
  {
    data[off+i] = buf[i];
  }

  if(ec->put(inum, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  return OK; 
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &inum)
{
  LockGuard lg(m_lc, parent);
  std::string data;
  std::string dir_name;
  if (ec->get(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  dir_name = "/" + std::string(name) + "/";
  // 目录已经存在
  if (data.find(dir_name) != std::string::npos)
  {
    return EXIST;
  }

  inum = random_inum(false);
  if (ec->put(inum, std::string()) != extent_protocol::OK)
  {
    return IOERR;
  }

  data.append(dir_name + filename(inum) + "/");
  if (ec->put(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  return OK;
}

int yfs_client::unlink(inum parent, const char* name)
{
  LockGuard lg(m_lc, parent);
  std::string data;
  std::string file_name = "/" + std::string(name) + "/";
  size_t pos, end, len; 
  inum inum;

  if(ec->get(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }

  // 没有这个文件
  if((pos = data.find(file_name)) == std::string::npos)
  {
    return NOENT;
  }

  end = data.find_first_of("/", pos+file_name.size());
  if(end == std::string::npos)
  {
    return NOENT;
  }
  len = end - file_name.size() - pos;
  inum = n2i(data.substr(pos+file_name.size(), len));
  if(!isfile(inum))
  {
    return IOERR;
  }

  // 从目录中移除文件
  data.erase(pos, end-pos+1);
  if(ec->put(parent, data) != extent_protocol::OK)
  {
    return IOERR;
  }
  
  // 删除文件
  if(ec->remove(inum) != extent_protocol::OK)
  {
    return IOERR;
  }

  return OK;
}