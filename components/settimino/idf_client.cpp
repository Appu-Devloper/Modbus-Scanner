#include "Platform.h"
#include "lwip/sockets.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
char settimino_interface[8]={0};
static bool wait_fd(int fd,bool writing,int64_t deadline) {
    for(;;) {
        int64_t left=deadline-esp_timer_get_time();
        if(left<=0) {errno=ETIMEDOUT;return false;}
        fd_set fds;FD_ZERO(&fds);FD_SET(fd,&fds);
        timeval timeout={static_cast<long>(left/1000000),static_cast<long>(left%1000000)};
        int result=select(fd+1,writing?nullptr:&fds,writing?&fds:nullptr,nullptr,&timeout);
        if(result>0) return true;
        if(result==0) {errno=ETIMEDOUT;return false;}
        if(errno!=EINTR) return false;
    }
}
void Client::stop() { if(fd>=0) close(fd);fd=-1; }
int Client::connect(IPAddress address,uint16_t port) {
    stop();fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd<0) return 0;
    ifreq iface={};strncpy(iface.ifr_name,settimino_interface,sizeof(iface.ifr_name)-1);
    if(setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&iface,sizeof(iface))<0 ||
       fcntl(fd,F_SETFL,O_NONBLOCK)<0) {stop();return 0;}
    sockaddr_in peer={};peer.sin_family=AF_INET;peer.sin_port=htons(port);peer.sin_addr.s_addr=address.addr;
    int result=::connect(fd,reinterpret_cast<sockaddr *>(&peer),sizeof(peer));
    int error=0;
    if(result<0) {
        error=errno;
        if(error==EINPROGRESS && wait_fd(fd,true,esp_timer_get_time()+3000000)) {
            socklen_t length=sizeof(error);
            if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)<0) error=errno;
        } else error=errno;
    }
    if(error) {printf("Settimino TCP error=%d (%s)\n",error,strerror(error));stop();return 0;}
    return 1;
}
int Client::available() {int count=0;return fd>=0 && ioctl(fd,FIONREAD,&count)==0?count:0;}
bool Client::connected() {
    if(fd<0) return false;
    uint8_t byte;
    int result=recv(fd,&byte,1,MSG_PEEK|MSG_DONTWAIT);
    return result>0 || (result<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
}
void Client::flush() {uint8_t data[128];while(fd>=0 && recv(fd,data,sizeof(data),MSG_DONTWAIT)>0) {}}
size_t Client::read(uint8_t *buffer,size_t size) {
    size_t done=0;int64_t deadline=esp_timer_get_time()+3000000;
    while(done<size && fd>=0 && wait_fd(fd,false,deadline)) {
        int count=recv(fd,buffer+done,size-done,0);
        if(count<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if(count<=0) break;
        done+=count;
    }
    // Log exact S7 response header, independently of Settimino's error mapping.
    if(done>=12 && buffer[0]==0x32 && (buffer[1]==2 || buffer[1]==3) &&
       (buffer[10] || buffer[11]))
        printf("PLC S7 response error: 0x%02X%02X\n",buffer[10],buffer[11]);
    if(done!=size) {stop();return 0;}
    return done;
}
size_t Client::write(const uint8_t *buffer,size_t size) {
    size_t done=0;int64_t deadline=esp_timer_get_time()+3000000;
    while(done<size && fd>=0 && wait_fd(fd,true,deadline)) {
        int count=send(fd,buffer+done,size-done,0);
        if(count<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if(count<=0) break;
        done+=count;
    }
    return done;
}
