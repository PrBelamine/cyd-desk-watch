#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
using std::size_t;
struct String : std::string { String(){} String(const char*s):std::string(s){} String(const std::string&s):std::string(s){}
  const char* c_str() const {return std::string::c_str();} unsigned length() const {return size();}
  String operator+(const char*o) const {return String(std::string(*this)+o);} };
inline String operator+(const char* a,const String&b){return String(std::string(a)+b);}
inline size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s); if(n){size_t c=l>=n?n-1:l; memcpy(d,s,c); d[c]=0;} return l;}
inline uint32_t& fakeMillis(){static uint32_t m=0;return m;}
inline uint32_t millis(){return fakeMillis();}
struct SerialMock{ std::deque<uint8_t> in; std::vector<uint8_t> out;
  int available(){return in.size();} int read(){int c=in.front();in.pop_front();return c;}
  void write(const uint8_t*d,size_t n){out.insert(out.end(),d,d+n);} void write(uint8_t c){out.push_back(c);} };
inline SerialMock Serial;
