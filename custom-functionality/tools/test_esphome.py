#!/usr/bin/env python3
"""Compile and run the actual lambdas extracted from sh03.yaml.

Only ESPHome UART/entities/time are stubbed. Compare the wire bytes against the
MCU protocol header. An actual ESP-IDF compile checks framework compatibility.
"""
from pathlib import Path
import hashlib, json, os, re, subprocess, tempfile
import yaml

ROOT=Path(__file__).resolve().parents[1]
CONFIG=ROOT/'esphome/sh03.yaml'

STUB=r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include "sh03_protocol.h"
inline uint32_t fake_time=100,read_advance=0;
uint32_t millis() { return fake_time; }
uint32_t random_uint32() { return 123; }
struct Preferences {
 uint32_t stored=0,pending=0;bool exists=false,dirty=false,fail_save=false,fail_sync=false;
 struct Preference {
  Preferences *owner;
  bool load(uint32_t *value) {if(!owner->exists)return false;*value=owner->stored;return true;}
  bool save(const uint32_t *value) {
   if(owner->fail_save)return false;owner->pending=*value;owner->dirty=true;return true;
  }
 };
 template<class T> Preference make_preference(uint32_t key,bool in_flash) {
  static_assert(sizeof(T)==4);assert(key==0x53335230 && in_flash);return {this};
 }
 bool sync() {
  if(fail_sync)return false;
  if(dirty){stored=pending;exists=true;dirty=false;}return true;
 }
};
inline Preferences preferences;
inline Preferences *global_preferences=&preferences;
struct UART {
 std::deque<uint8_t> rx; std::vector<uint8_t> tx;
 size_t available() const { return rx.size(); }
 bool read_byte(uint8_t *p) { if(rx.empty())return false; *p=rx.front();rx.pop_front();fake_time+=read_advance;return true; }
 void write_array(const uint8_t *p,size_t n) { tx.insert(tx.end(),p,p+n); }
};
namespace sensor { struct Sensor {
 float state=NAN; unsigned publishes=0;
 void publish_state(float v) {state=v;++publishes;}
}; }
namespace binary_sensor { struct BinarySensor {
 bool state=false,known=false; bool has_state() const {return known;}
 void publish_state(bool v) {state=v;known=true;}
}; }
namespace text_sensor { struct TextSensor {std::string state;void publish_state(const std::string &v) {state=v;}}; }
namespace switch_ { struct Switch {bool state=false,known=false;void publish_state(bool v) {state=v;known=true;}}; }
namespace number { struct Number {float state=NAN;void publish_state(float v) {state=v;}}; }
namespace select { struct Select {
 std::string state;std::vector<std::string> options;
 void publish_state(const std::string &v) {state=v;}
 std::optional<size_t> index_of(const std::string &v) const {
   for(size_t i=0;i<options.size();++i)if(options[i]==v)return i;return {};
 }
 std::optional<std::string> at(size_t i) const {if(i<options.size())return options[i];return {};}
}; }
struct Script {
 std::function<void(int,int,int)> callback;
 void execute(int ch,int operation,int value) {callback(ch,operation,value);}
};
struct BootScript {
 std::function<void(int)> callback;
 void execute(int operation) {callback(operation);}
};
'''
TEST=r'''
static std::vector<uint8_t> make_frame(uint8_t type,uint16_t sequence,const uint8_t *p,uint8_t n) {
 uint8_t frame[SH03_WIRE_FRAME];auto size=sh03_wire_encode(frame,type,sequence,p,n);
 return {frame,frame+size};
}
static void input(Bridge &b,const std::vector<uint8_t> &frame) {
 for(auto byte:frame)b.dryer_uart.rx.push_back(byte);
 b.poll();
}
static void state(Bridge &b,uint8_t *p) {input(b,make_frame(1,1,p,30));}
static void ack(Bridge &b,uint16_t seq,uint8_t op,uint8_t result) {
 uint8_t p[3]={0,op,result};input(b,make_frame(3,seq,p,3));
}
int main() {
 Bridge b;b.boot();
 assert(!b.uart_connected.state && b.dryer_uart.tx.empty());
 assert(std::isnan(b.ch1_target_temperature.state));
 b.ch1_running_on();assert(b.dryer_uart.tx.empty() && b.last_command.state.find("disconnected")!=std::string::npos);
 uint8_t p[30]={0};p[4]=50;p[5]=20;p[6]=10;
 sh03_put16(p+8,523);sh03_put16(p+10,32);sh03_put16(p+12,65);
 sh03_put32(p+18,28800);sh03_put32(p+26,7200);
 read_advance=1;state(b,p);p[0]=1;state(b,p);read_advance=0;
 assert(b.uart_connected.state && !b.ch1_running.state && fabs(b.ch1_temperature.state-52.3f)<0.01);
 assert(b.ch1_remaining_time.state==120 && b.ch1_target_temperature.state==50 && b.ch1_duration.state==8);
 assert(b.ch1_mode.state=="Timed" && b.ch1_material.state=="PLA");
 // Legacy telemetry must not invent a display state. Commands are confirmed
 // by state packets, never optimistically by sending or by ACK alone.
 assert(!b.displays_enabled.known);
 p[0]=0;p[1]=SH03_FLAG_DISPLAY_CONTROL|SH03_FLAG_DISPLAY_ENABLED;state(b,p);
 assert(b.displays_enabled.known && b.displays_enabled.state);
 b.dryer_uart.tx.clear();b.displays_enabled_off();
 uint16_t display_seq=sh03_get16(b.dryer_uart.tx.data()+4);
 uint8_t display_cmd[6]={0,SH03_OP_DISPLAY,0,0,0,0};
 assert(b.dryer_uart.tx==make_frame(2,display_seq,display_cmd,6) && b.displays_enabled.state);
 ack(b,display_seq,SH03_OP_DISPLAY,0);assert(b.displays_enabled.state);
 p[1]=SH03_FLAG_DISPLAY_CONTROL;state(b,p);assert(!b.displays_enabled.state);
 b.dryer_uart.tx.clear();b.displays_enabled_on();
 display_seq=sh03_get16(b.dryer_uart.tx.data()+4);display_cmd[2]=1;
 assert(b.dryer_uart.tx==make_frame(2,display_seq,display_cmd,6) && !b.displays_enabled.state);
 ack(b,display_seq,SH03_OP_DISPLAY,0);assert(!b.displays_enabled.state);
 p[0]=1;p[1]=SH03_FLAG_DISPLAY_CONTROL|SH03_FLAG_DISPLAY_ENABLED;state(b,p);
 assert(b.displays_enabled.state);b.dryer_uart.tx.clear();
 // Ignore legacy timeout reports; the display switch is the only control.
 uint8_t config[2]={60,0};
 input(b,make_frame(SH03_DISPLAY_CONFIG,2,config,2));
 assert(b.displays_enabled.state && b.dryer_uart.tx.empty());
 b.ch1_running_on();assert(!b.ch1_running.state && b.dryer_uart.tx.size()==15);
 uint16_t seq=sh03_get16(b.dryer_uart.tx.data()+4);
 uint8_t cmd[6]={0,1,1,0,0,0};assert(b.dryer_uart.tx==make_frame(2,seq,cmd,6));
 size_t sent=b.dryer_uart.tx.size();b.ch1_target_temperature_set(70);assert(b.dryer_uart.tx.size()==sent);
 ack(b,seq+1,1,0);assert(b.pending);ack(b,seq,2,0);assert(b.pending);
 // Poll queries must not replace the sequence of the outstanding command.
 b.query();auto expected=make_frame(4,b.wire_sequence,nullptr,0);
 assert(std::vector<uint8_t>(b.dryer_uart.tx.begin()+sent,b.dryer_uart.tx.end())==expected);
 ack(b,seq,1,0);assert(!b.pending && b.last_command.state=="Accepted" && !b.ch1_running.state);
 p[0]=0;p[1]=3;state(b,p);assert(b.ch1_running.state && b.ch1_drying.state);
 sent=b.dryer_uart.tx.size();b.ch1_target_temperature_set(70);assert(b.ch1_target_temperature.state==50);
 seq=sh03_get16(b.dryer_uart.tx.data()+sent+4);ack(b,seq,2,2);
 assert(b.last_command.state.find("Busy")!=std::string::npos && b.ch1_target_temperature.state==50);
 sent=b.dryer_uart.tx.size();b.ch1_duration_set(12);assert(sh03_get32(b.dryer_uart.tx.data()+sent+9)==43200);
 sent=b.dryer_uart.tx.size();fake_time+=2100;b.poll();assert(!b.pending && b.last_command.state.find("Timeout")!=std::string::npos);
 assert(b.dryer_uart.tx.size()==sent);fake_time+=4000;b.poll();
 assert(!b.uart_connected.state && std::isnan(b.ch1_temperature.state) && b.ch1_fault.state=="Disconnected");
 b.ch1_running_off();assert(b.dryer_uart.tx.size()==sent);
 b.displays_enabled_off();assert(b.dryer_uart.tx.size()==sent);
 p[0]=0;p[1]=0;state(b,p);p[0]=1;state(b,p);
 assert(b.uart_connected.state && !b.ch1_running.state && b.dryer_uart.tx.size()==sent);
 b.ch1_mode_set("Humidity");assert(b.dryer_uart.tx[sent+8]==4 && sh03_get32(b.dryer_uart.tx.data()+sent+9)==1);
 ack(b,b.pending_sequence,4,0);
 sent=b.dryer_uart.tx.size();b.ch2_material_set("PETG");
 assert(b.dryer_uart.tx[sent+7]==1 && b.dryer_uart.tx[sent+8]==6 && sh03_get32(b.dryer_uart.tx.data()+sent+9)==1);
 fake_time+=2100;b.poll();
 // Validate the YAML RX parser itself against all single-bit corruptions.
 p[0]=0;auto good=make_frame(1,50,p,30);
 for(size_t i=0;i<good.size();++i)for(unsigned bit=0;bit<8;++bit) {
  fake_time+=600;b.poll();unsigned before=b.ch1_temperature.publishes;
  auto bad=good;bad[i]^=1u<<bit;input(b,bad);assert(b.ch1_temperature.publishes==before);
 }
 for(unsigned n=0;n<=64;++n) {
  fake_time+=600;b.poll();uint8_t payload[64];
  for(unsigned i=0;i<n;++i)payload[i]=(uint8_t)(i*73+n);
  input(b,make_frame(99,65535,payload,n));unsigned before=b.ch1_temperature.publishes;
  input(b,good);assert(b.ch1_temperature.publishes==before+1);
 }
 fake_time+=600;b.poll();unsigned before=b.ch1_temperature.publishes;
 input(b,{good.begin(),good.begin()+8});assert(b.ch1_temperature.publishes==before);
 input(b,{good.begin()+8,good.end()});assert(b.ch1_temperature.publishes==before+1);
 fake_time+=600;b.poll();input(b,{good.begin(),good.begin()+8});fake_time+=600;b.poll();
 before=b.ch1_temperature.publishes;input(b,{good.begin()+8,good.end()});assert(b.ch1_temperature.publishes==before);
 input(b,good);assert(b.ch1_temperature.publishes==before+1);
 // Long noise stays bounded and a subsequent frame resynchronizes.
 uint32_t random=1234567;
 for(unsigned i=0;i<100000;++i) {random=random*1664525+1013904223;b.dryer_uart.rx.push_back((uint8_t)(random>>24));if(i%200==199)b.poll();}
 fake_time+=600;b.poll();input(b,good);assert(fabs(b.ch1_temperature.state-52.3f)<0.01);
 // Wraparound of the millisecond clock must not mark fresh telemetry stale.
 fake_time=0xfffffff0;state(b,p);p[0]=1;state(b,p);fake_time+=60;b.poll();assert(b.uart_connected.state);
 fake_time+=5001;b.poll();assert(!b.uart_connected.state);
 // Execute the actual API forwarding lambda, compare to the MCU encoder.
 b.pending=false;b.dryer_uart.tx.clear();
 b.ota_packet(0x12,456,{0,32,0,8,0,16,0,0,1,2,3,4,3,3,1,0});
 uint8_t begin[16]={0,32,0,8,0,16,0,0,1,2,3,4,3,3,1,0};
 assert(b.dryer_uart.tx==make_frame(0x12,456,begin,16) && b.ota_active);
 sent=b.dryer_uart.tx.size();b.query();b.ch1_running_on();b.displays_enabled_off();assert(b.dryer_uart.tx.size()==sent);
 assert(b.last_command.state.find("update in progress")!=std::string::npos);
 for(int op:{0,2,0x16,0x18,256}) b.ota_packet(op,123,{});
 b.ota_packet(0x11,-1,{});b.ota_packet(0x11,65536,{});b.ota_packet(0x12,1,{});
 b.ota_packet(0x13,1,{0,0,0,0,256,1});b.ota_packet(0x13,1,{0,0,0,0,1});
 b.ota_packet(0x13,1,std::vector<int>(66,0));b.ota_packet(0x11,1,{1});
 assert(b.dryer_uart.tx.size()==sent);
 b.pending=true;b.ota_packet(0x11,1,{});assert(b.dryer_uart.tx.size()==sent);b.pending=false;
 uint8_t reply[18]={0x13,0};sh03_put32(reply+2,60);sh03_put32(reply+6,61564);
 sh03_put32(reply+10,0x12345678);sh03_put32(reply+14,0x00010303);
 input(b,make_frame(0x18,456,reply,18));
 assert(b.sh03_ota_reply.state=="1,456,19,0,60,61564,305419896,66307");
 input(b,make_frame(0x18,456,reply,18));assert(b.sh03_ota_reply.state[0]=='2');
 b.ota_release();assert(!b.ota_active);
 b.ota_packet(0x17,1,{});assert(!b.ota_active);
 fake_time=0xfffffff0;b.ota_packet(0x11,2,{});fake_time+=59999;b.poll();assert(b.ota_active);
 fake_time+=2;b.poll();assert(!b.ota_active);
 // Persist an explicit one-shot request, then reboot BOTH boards. No Wi-Fi
 // is available in this harness; the real boot lambda must still send HELLO.
 preferences={};b.ota_recovery(1);assert(b.recovery_saved && preferences.stored==0x53335231);
 assert(!b.recovery_hold);fake_time=100;Bridge recovered;recovered.boot();
 assert(recovered.recovery_hold && recovered.ota_active && preferences.stored==0);
 assert(recovered.dryer_uart.tx==make_frame(0x11,recovered.wire_sequence,nullptr,0));
 recovered.dryer_uart.tx.clear();fake_time+=90000;recovered.poll();
 assert(recovered.ota_active);recovered.recovery_tick();
 assert(recovered.dryer_uart.tx==make_frame(0x11,recovered.wire_sequence,nullptr,0));
 // A connected uploader takes ownership before BEGIN/DATA can be disturbed.
 recovered.ota_packet(0x11,500,{});assert(!recovered.recovery_hold && recovered.ota_active);
 sent=recovered.dryer_uart.tx.size();recovered.recovery_tick();assert(recovered.dryer_uart.tx.size()==sent);
 Bridge ordinary;ordinary.boot();assert(!ordinary.recovery_hold && ordinary.dryer_uart.tx.empty());
 // Cancel before cycling power; the next ordinary boot must not hold SH03.
 b.ota_recovery(1);b.ota_recovery(2);assert(b.recovery_saved && preferences.stored==0);
 // Bounded offline recovery, including millis wrap. RUN needs no extra GPIO.
 b.ota_recovery(1);fake_time=0xfffffff0;Bridge expired;expired.boot();expired.dryer_uart.tx.clear();
 fake_time+=299999;expired.recovery_tick();assert(expired.recovery_hold);
 expired.dryer_uart.tx.clear();fake_time+=1;expired.recovery_tick();
 assert(!expired.recovery_hold && !expired.ota_active);
 assert(expired.dryer_uart.tx==make_frame(0x15,expired.wire_sequence,nullptr,0));
 b.ota_recovery(1);Bridge cancel;cancel.boot();cancel.dryer_uart.tx.clear();cancel.ota_recovery(2);
 assert(!cancel.recovery_hold && cancel.dryer_uart.tx==make_frame(0x15,cancel.wire_sequence,nullptr,0));
 // Never report that a marker survived power loss if NVS did not commit it.
 preferences.fail_sync=true;b.ota_recovery(1);assert(!b.recovery_saved);
 preferences.dirty=false;preferences.fail_sync=false;preferences.fail_save=true;
 b.ota_recovery(1);assert(!b.recovery_saved);preferences.fail_save=false;
 std::cout<<"PASS: YAML lambdas, canonical wire bytes, CRC/fragmentation/noise, confirmed controls, ACK matching, timeout/reconnect, clock advance/wrap\n";
}
'''

def main():
    class Loader(yaml.SafeLoader):pass
    Loader.add_constructor('!secret',lambda loader,node:loader.construct_scalar(node))
    Loader.add_constructor('!lambda',lambda loader,node:loader.construct_scalar(node))
    config=yaml.load(CONFIG.read_text(),Loader=Loader)
    assert 'external_components' not in config and 'includes' not in config['esphome']
    groups={'sensor':'sensor::Sensor','binary_sensor':'binary_sensor::BinarySensor',
            'text_sensor':'text_sensor::TextSensor','number':'number::Number',
            'switch':'switch_::Switch','select':'select::Select'}
    globals_={g['id'] for g in config['globals']}
    entities={e['id'] for group in groups for e in config[group]}|{'dryer_uart','sh03_command','sh03_boot_control'}
    def transform(code):
        def replace(match):
            name=match[1]
            assert name in globals_|entities,name
            return name if name in globals_ or code[match.end():].startswith('.') else f'(&{name})'
        return re.sub(r'id\((\w+)\)',replace,code)
    def body(actions):return transform(actions[0]['lambda'])
    cpp=[STUB,'struct Bridge {','UART dryer_uart; Script sh03_command; BootScript sh03_boot_control;']
    for g in config['globals']:
        assert not g['restore_value']
        cpp.append(f'{g["type"]} {g["id"]} = {g["initial_value"]};')
    for group,type_ in groups.items():
        for e in config[group]:cpp.append(f'{type_} {e["id"]};')
    cpp.append('Bridge() { sh03_command.callback=[this](int ch,int operation,int value) {send(ch,operation,value);};')
    cpp.append('sh03_boot_control.callback=[this](int operation) {boot_control(operation);};')
    materials=[m['name'] for m in json.loads((ROOT/'config/materials.json').read_text())]
    for e in config['select']:
        assert e['lambda'].strip()=='return {};'
        if e['id'].endswith('_material'):assert e['options']==materials
        cpp.append(e['id']+'.options={'+','.join(json.dumps(s) for s in e['options'])+'};')
    cpp.append('}')
    cpp.append('void boot() {'+body(config['esphome']['on_boot']['then'])+'}')
    cpp.append('void send(int ch,int operation,int value) {'+body(config['script'][0]['then'])+'}')
    cpp.append('void boot_control(int operation) {'+body(config['script'][1]['then'])+'}')
    cpp.append('void recovery_tick() {'+body(next(i['then'] for i in config['interval'] if i['interval']=='250ms'))+'}')
    cpp.append('void query() {'+body(next(i['then'] for i in config['interval'] if i['interval']=='5s'))+'}')
    cpp.append('void poll() {'+body(next(i['then'] for i in config['interval'] if i['interval']=='10ms'))+'}')
    actions={a['action']:a for a in config['api']['actions']}
    cpp.append('void ota_packet(int operation,int sequence,const std::vector<int> &payload) {'+body(actions['sh03_ota_packet']['then'])+'}')
    cpp.append('void ota_release() {'+body(actions['sh03_ota_release']['then'])+'}')
    cpp.append('void ota_recovery(int command) {'+body(actions['sh03_ota_recovery']['then'])+'}')
    for e in config['switch']:
        assert e['restore_mode']=='DISABLED' and not e['optimistic']
        for event in ('on','off'):cpp.append('void '+e['id']+'_'+event+'() {'+body(e['turn_'+event+'_action'])+'}')
    for group,param in [('number','float x'),('select','const std::string &x')]:
        for e in config[group]:
            assert e['lambda'].strip()=='return {};'
            cpp.append('void '+e['id']+'_set('+param+') {'+body(e['set_action'])+'}')
    cpp+=['};',TEST]
    with tempfile.TemporaryDirectory(prefix='sh03-lambda-test-') as temp:
        temp=Path(temp);source=temp/'test.cpp';source.write_text('\n'.join(cpp))
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-I'+str(ROOT/'include'),str(source),'-o',str(temp/'test')],check=True)
        subprocess.run([str(temp/'test')],check=True)
    report={'status':'PASS','yaml_sha256':hashlib.sha256(CONFIG.read_bytes()).hexdigest(),
      'scope':'Actual YAML lambdas with stub UART/entities/time, canonical MCU protocol frames and host address/UB sanitizers; no Wi-Fi or board simulation'}
    directory=ROOT/'analysis/standalone';directory.mkdir(parents=True,exist_ok=True)
    (directory/'esphome-tests.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
