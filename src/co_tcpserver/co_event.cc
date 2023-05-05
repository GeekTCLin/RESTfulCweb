#include "co_event.h"
#include "timer.h"
#include "co_eventloop.h"
#include "coroutine.h"
#include "co_eventloop.h"

namespace cweb {
namespace tcpserver {
namespace coroutine {

CoEvent::CoEvent(CoEventLoop* loop, int fd)
: Event(loop, fd) {}

CoEvent::~CoEvent() {}

void CoEvent::HandleEvent(Time receiveTime) {
    //处理读写过程中对段关闭的情况
    if(revents_ & (HUP_EVENT | ERR_EVENT)) {
        revents_ |= (READ_EVENT | WRITE_EVENT) & events_;
    }
    
    if(revents_ & READ_EVENT) {
        this->DisableReading();
        if(read_coroutine_) {
            read_coroutine_->SetState(Coroutine::READY);
        }else {
            read_coroutine_ = new Coroutine([this, receiveTime](){
                read_callback_(receiveTime);
            });
            ((CoEventLoop*)loop_)->AddCoroutineWithState(read_coroutine_);
        }
    }
    
    if(revents_ & WRITE_EVENT) {
        this->DisableWriting();
        if(write_coroutine_) {
            write_coroutine_->SetState(Coroutine::READY);
        }else {
            write_coroutine_ = new Coroutine(write_callback_);
            ((CoEventLoop*)loop_)->AddCoroutineWithState(write_coroutine_);
        }
    }
}

void CoEvent::SetReadCoroutine(Coroutine *co) {
    if(read_coroutine_ == nullptr) read_coroutine_ = co;
}

void CoEvent::SetWriteCoroutine(Coroutine *co) {
    if(write_coroutine_ == nullptr) write_coroutine_ = co;
}

}
}
}
