// Protocol regression test: escaped frames, checksum rejection, manual resume.
#include "chario_copland.h"
#include <cassert>
#include <iostream>
static uint16_t word(const std::vector<uint8_t>& b, size_t i) { return (b.at(i)<<8)|b.at(i+1); }
static std::vector<uint8_t> frame(std::vector<uint8_t> b) {
    if(b.size()&1)b.push_back(0);
    uint16_t s=0;for(size_t i=0;i<b.size();i+=2)s+=word(b,i);
    uint16_t h=s+b.size();
    std::vector<uint8_t> p={uint8_t(h>>8),uint8_t(h),uint8_t(s>>8),uint8_t(s),uint8_t(b.size()>>8),uint8_t(b.size())};
    p.insert(p.end(),b.begin(),b.end());
    std::vector<uint8_t> out={0xbd,0xbd};
    for(auto c:p){out.push_back(c);if(c==0xbd)out.push_back(0x9d);}
    return out;
}
static void send(CharIoCopland& p,std::vector<uint8_t> b) {for(auto c:frame(b))p.xmit_char(c);}
static std::vector<uint8_t> read(CharIoCopland& p) {
    std::vector<uint8_t> raw;
    while(p.rcv_char_available()){uint8_t c;p.rcv_char(&c);raw.push_back(c);}
    assert(raw.size()>=12 && raw[0]==0xbd && raw[1]==0xbd);
    std::vector<uint8_t> b;
    for(size_t i=2;i<raw.size();i++){b.push_back(raw[i]);if(raw[i]==0xbd){assert(raw.at(++i)==0x9d);}}
    return {b.begin()+6,b.end()};
}
int main() {
    CharIoCopland p;
    send(p,{0,3,0,5});assert(!p.rcv_char_available());
    send(p,{0,3,3,0xe8,0xbd,0xbd});assert((read(p)==std::vector<uint8_t>{0,3,0,2,0,0}));
    auto bad=frame({0,7,4,0x51});bad[2]^=1;for(auto c:bad)p.xmit_char(c);
    assert(!p.rcv_char_available());CharIoCopland::continue_guest();assert(!p.rcv_char_available());
    send(p,{0,9,4,0x51});read(p);
    CharIoCopland::continue_guest();auto req=read(p);assert(word(req,2)==0x12c);
    std::vector<uint8_t> reply={req[0],req[1],0,2,0,0};reply.insert(reply.end(),16,0xbd);send(p,reply);
    req=read(p);assert(word(req,2)==0x64 && req.size()==24);
    reply={req[0],req[1],0,2,0,0};reply.insert(reply.end(),12,0);reply[10]=1;send(p,reply);
    req=read(p);assert(word(req,2)==0x69 && req.size()==26 && req[4]==1 && req[8]==0xbd);
    send(p,{req[0],req[1],0,2,0,0});assert(!p.rcv_char_available());
    CharIoCopland::continue_guest();assert(!p.rcv_char_available());
    // Oversized and malformed packets are discarded; the next valid frame works.
    for(auto c:std::vector<uint8_t>{0xbd,0xbd,0,0,0,0,0xff,0xfe})p.xmit_char(c);
    send(p,{0,11,0,2});assert(word(read(p),0)==11);
    std::cout << "Copland serial protocol tests passed\n";
}
