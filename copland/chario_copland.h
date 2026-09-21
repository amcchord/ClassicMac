// ClassicMac Copland debugger serial peer. GPL-3.0-or-later.
// Protocol derived from Michael Steil's mist64/dingusppc web/worker.js.
#pragma once
#include <devices/serial/chario.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

class CharIoCopland : public CharIoBackEnd {
    inline static CharIoCopland* active = nullptr;
    std::deque<uint8_t> receive;
    std::vector<uint8_t> packet, process_id;
    bool in_packet = false, escape = false, halted = false;
    int sync = 0;
    uint16_t sequence = 200, pending_sequence = 0, pending_request = 0;
    static uint16_t word(const std::vector<uint8_t>& b, size_t i) {
        return (uint16_t(b[i]) << 8) | b[i+1];
    }
    static uint16_t sum(const std::vector<uint8_t>& b, size_t start, size_t end) {
        uint16_t s = 0;
        for (size_t i=start; i+1<end; i+=2) s += word(b,i);
        return s;
    }
    void status(bool stopped) {
        halted = stopped;
        if (const char* path = std::getenv("CLASSICMAC_STATUS_PATH")) {
            std::string temporary = std::string(path) + ".tmp";
            if (FILE* f = fopen(temporary.c_str(), "w")) {
                fputs(stopped ? "halted\n" : "running\n", f);
                if (fclose(f) == 0) rename(temporary.c_str(), path);
            }
        }
    }
    void frame(std::vector<uint8_t> body) {
        if (body.size() & 1) body.push_back(0);
        // Bound output even if a broken guest floods requests without reading.
        if (receive.size() + 2 * (body.size()+6) + 2 > 32768) return;
        auto checksum = sum(body,0,body.size());
        std::vector<uint8_t> bytes = {0,0,uint8_t(checksum>>8),uint8_t(checksum),uint8_t(body.size()>>8),uint8_t(body.size())};
        checksum=sum(bytes,2,6); bytes[0]=checksum>>8; bytes[1]=checksum;
        bytes.insert(bytes.end(),body.begin(),body.end());
        receive.push_back(0xbd); receive.push_back(0xbd);
        for (auto c:bytes) {receive.push_back(c); if(c==0xbd) receive.push_back(0x9d);}
    }
    void request(uint16_t type, std::vector<uint8_t> payload = {}) {
        pending_sequence = sequence; sequence += 2; pending_request = type;
        payload.insert(payload.begin(), {uint8_t(pending_sequence>>8), uint8_t(pending_sequence), uint8_t(type>>8), uint8_t(type)});
        frame(payload);
    }
    void finish() {
        in_packet=false; escape=false; sync=0;
        if(packet.size()<10 || word(packet,0)!=sum(packet,2,6) ||
           word(packet,2)!=sum(packet,6,packet.size())) return;
        auto seq=word(packet,6), type=word(packet,8);
        if (!(seq&1) && pending_request && seq==pending_sequence) {
            auto request_type=pending_request; pending_request=0;
            if (packet.size()<12 || word(packet,10)!=0) return;
            if (request_type==0x12c && packet.size()>=28) {
                process_id.assign(packet.begin()+12,packet.begin()+28);
                auto payload=process_id; payload.insert(payload.end(),4,0);
                request(0x64,payload);
            } else if (request_type==0x64 && packet.size()>=24) {
                std::vector<uint8_t> payload(packet.begin()+16,packet.begin()+20);
                payload.insert(payload.end(),process_id.begin(),process_id.end());
                payload.insert(payload.end(),2,0); request(0x69,payload);
            } else if (request_type==0x69) status(false);
            return;
        }
        if ((seq&1) && type!=5) frame({uint8_t(seq>>8),uint8_t(seq),0,2,0,0});
        if (type==1105) {
            pending_request=0; status(true);
            fputs("Copland assertion: ",stderr);
            for(size_t i=10;i<packet.size();i++) {
                auto c=packet[i]; fputc(c>=32 && c<127 ? c:'.',stderr);
            }
            fputc('\n',stderr);
        }
    }
    void append(uint8_t c) {
        packet.push_back(c);
        if(packet.size()>=6) {
            auto length=word(packet,4);
            if(length<4 || length>8192 || (length&1)) {in_packet=false;sync=0;}
            else if(packet.size()==6+length) finish();
        }
    }
public:
    CharIoCopland() { active=this; status(false); }
    ~CharIoCopland() { if(active==this) active=nullptr; }
    static void continue_guest() {
        // Manual only; never silently skip an Apple assertion. Retrying replaces
        // an unanswered request, so a lost reply cannot permanently disable Continue.
        if(active && active->halted) active->request(0x12c);
    }
    bool rcv_char_available() override { return !receive.empty(); }
    bool rcv_char_available_now() override { return !receive.empty(); }
    int rcv_char(uint8_t* c) override { *c=receive.empty()?0xff:receive.front(); if(!receive.empty())receive.pop_front(); return 0; }
    int xmit_char(uint8_t c) override {
        if(!in_packet) {
            if(c==0xbd && sync) {packet.clear();in_packet=true;escape=false;sync=0;}
            else sync=(c==0xbd);
            return 0;
        }
        if(escape) {
            escape=false;
            if(c==0x9d) append(0xbd);
            else if(c==0xbd) packet.clear(); // Resynchronize on a new frame.
            else {in_packet=false;sync=0;}
        } else if(c==0xbd) escape=true;
        else append(c);
        return 0;
    }
};
