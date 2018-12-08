// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include <mutex>
#include "extent_protocol.h"

struct extent {
  // 数据
  std::string data;
  // 数据属性
  extent_protocol::attr attr;
};

class extent_server {

 public:
  extent_server();

  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);

private:
  std::mutex m_mutex;
  std::map<extent_protocol::extentid_t, extent> m_dataMap;
};

#endif 







