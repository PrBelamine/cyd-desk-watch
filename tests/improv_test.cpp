#include "Improv.h"
#include <WiFi.h>
#include <cassert>
static void send(uint8_t type,std::vector<uint8_t> data){ std::vector<uint8_t> p={'I','M','P','R','O','V',1,type,(uint8_t)data.size()}; p.insert(p.end(),data.begin(),data.end()); uint8_t s=0; for(auto b:p)s+=b; p.push_back(s); for(auto b:p)Serial.in.push_back(b);}
static std::vector<std::vector<uint8_t>> packets(){ std::vector<std::vector<uint8_t>> r; auto&o=Serial.out; size_t i=0; while(i+10<=o.size()){ assert(!memcmp(&o[i],"IMPROV",6)); size_t len=o[i+8]; std::vector<uint8_t> p(o.begin()+i,o.begin()+i+9+len+1); uint8_t s=0; for(size_t k=0;k+1<p.size();++k)s+=p[k]; assert(s==p.back()); r.push_back(p); i+=9+len+1; assert(o[i]=='\n'); ++i;} Serial.out.clear(); return r;}
int main(){ Preferences prefs; improvBegin(prefs);
  char s[33],pw[65]; assert(!wifiLoadSaved(prefs,s,sizeof s,pw,sizeof pw));
  // garbage then request current state
  for (char c : std::string("hello IIMPRO")) Serial.in.push_back(uint8_t(c));   // noise before a packet is ignored
  send(3,{2,0}); improvPoll(); auto r=packets(); assert(r.size()==3 && r[0][7]==2 && r[0][9]==0 && r[1][7]==1 && r[1][9]==0x02 && r[2][7]==4);   // ready + empty result
  // wrong password: times out
  { std::vector<uint8_t> d={1,0}; std::string ss="Home", pp="bad"; d.push_back(ss.size()); d.insert(d.end(),ss.begin(),ss.end()); d.push_back(pp.size()); d.insert(d.end(),pp.begin(),pp.end()); d[1]=d.size()-2; send(3,d); }
  improvPoll(); r=packets(); assert(r.size()==2 && r[1][7]==1 && r[1][9]==0x03); // error cleared + provisioning
  fakeMillis()=25000; improvPoll(); r=packets(); bool sawErr=false; for(auto&p:r) if(p[7]==2&&p[9]==3) sawErr=true; assert(sawErr);
  assert(!wifiLoadSaved(prefs,s,sizeof s,pw,sizeof pw));
  // good password
  { std::vector<uint8_t> d={1,0}; std::string ss="Home", pp="goodpass"; d.push_back(ss.size()); d.insert(d.end(),ss.begin(),ss.end()); d.push_back(pp.size()); d.insert(d.end(),pp.begin(),pp.end()); d[1]=d.size()-2; send(3,d); }
  improvPoll(); improvPoll(); r=packets(); bool prov=false,res=false; for(auto&p:r){ if(p[7]==1&&p[9]==0x04)prov=true; if(p[7]==4&&p[9]==1)res=true; } assert(prov&&res);
  assert(wifiLoadSaved(prefs,s,sizeof s,pw,sizeof pw) && !strcmp(s,"Home") && !strcmp(pw,"goodpass"));
  // device info + bad checksum
  send(3,{3,0}); improvPoll(); r=packets(); assert(r.size()>=2 || r.size()==1);
  Serial.in.push_back('I');Serial.in.push_back('M');Serial.in.push_back('P');Serial.in.push_back('R');Serial.in.push_back('O');Serial.in.push_back('V');Serial.in.push_back(1);Serial.in.push_back(3);Serial.in.push_back(2);Serial.in.push_back(2);Serial.in.push_back(0);Serial.in.push_back(0x55); improvPoll(); r=packets(); assert(r.size()==1 && r[0][7]==2 && r[0][9]==1);
  puts("improv test ok"); }
