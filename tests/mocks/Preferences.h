#pragma once
#include "Arduino.h"
#include <map>
struct Preferences{ std::map<std::string,std::string> m;
 size_t getString(const char*k,char*b,size_t n){auto it=m.find(k); if(it==m.end()){b[0]=0;return 0;} strlcpy(b,it->second.c_str(),n);return it->second.size();}
 void putString(const char*k,const char*v){m[k]=v;} };
