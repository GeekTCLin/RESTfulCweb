#include "co_eventloop.h"
#include "coroutine.h"
#include "co_event.h"
#include "timer.h"
#include "poller.h"
#include "pthread_keys.h"

namespace cweb {
namespace tcpserver {
namespace coroutine {

Coroutine* CoEventLoop::GetMainCoroutine() {
    return main_coroutine_;
}

void CoEventLoop::Run() {
    running_ = true;
    createWakeupfd();
    pthread_setspecific(util::PthreadKeysSingleton::GetInstance()->TLSEventLoop, this);
    pthread_setspecific(util::PthreadKeysSingleton::GetInstance()->TLSMemoryPool, memorypool_.get());
    // 主线程的 执行体为 loop 循环
    main_coroutine_ = new Coroutine(std::bind(&CoEventLoop::loop, this));
    loop();
}

void CoEventLoop::AddTaskWithState(Functor cb, bool stateful) {
    Coroutine* co = new Coroutine(std::move(cb));
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(stateful) {
            stateful_ready_coroutines_.Push(co);
        }else {
            stateless_ready_coroutines_.Push(co);
        }
    }
    if(!isInLoopThread()) {
        wakeup();
    }
}

// stateful 默认为true，一般情况下 加入 stateful_ready_coroutines_ 链表
// 不知道 stateless_ready_coroutines_ 链表的作用
void CoEventLoop::AddCoroutineWithState(Coroutine* co, bool stateful) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(stateful) {
            stateful_ready_coroutines_.Push(co);
        }else {
            stateless_ready_coroutines_.Push(co);
        }
    }
    if(!isInLoopThread()) {
        wakeup();
    }
}

void CoEventLoop::AddTask(Functor cb) {
    Coroutine* co = new Coroutine(std::move(cb));
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stateful_ready_coroutines_.Push(co);
    }
    if(!isInLoopThread()) {
        wakeup();
    }
}

void CoEventLoop::AddTasks(std::vector<Functor>& cbs) {
    std::unique_lock<std::mutex> lock(mutex_);
    for(Functor cb : cbs) {
        Coroutine* co = new Coroutine(std::move(cb));
        stateful_ready_coroutines_.Push(co);
    }
    if(!isInLoopThread()) {
        wakeup();
    }
}

void CoEventLoop::UpdateEvent(Event *event) {
    events_[((CoEvent*)event)->fd_] = (CoEvent*)event;
    poller_->UpdateEvent(event);
}

void CoEventLoop::RemoveEvent(Event *event) {
    events_.erase(event->Fd());
    poller_->RemoveEvent(event);
}

CoEvent* CoEventLoop::GetEvent(int fd) {
    if(fd < 0) return nullptr;
    auto iter = events_.find(fd);
    if(iter != events_.end()) {
        return (CoEvent*)events_[fd];
    }
    return nullptr;
}

Coroutine* CoEventLoop::GetCurrentCoroutine() {
    return running_coroutine_;
}

void CoEventLoop::NotifyCoroutineReady(Coroutine *co) {
    assert(isInLoopThread());
    hold_coroutines_.Erase(co);
    running_coroutines_.Push(co);
}

//主协程
void CoEventLoop::loop() {
    while(running_) {
        active_events_.clear();
        int timeout = timermanager_->NextTimeoutInterval();
        Time now = poller_->Poll(timeout, active_events_);
  
        handleActiveEvents(now);
        handleTimeoutTimers();
        
        // 取出首个协程
        running_coroutine_ = running_coroutines_.Front();

        if(!running_coroutine_) {
            // running_coroutines_ 长度为0，将 ready 状态的协程 移动至 runing
            moveReadyCoroutines();
            running_coroutine_ = running_coroutines_.Front();
        }
        
        while(running_coroutine_ && running_) {
            running_coroutine_->SetLoop(std::dynamic_pointer_cast<CoEventLoop>(shared_from_this()));
            // 主协程 切换 至 子协程
            // running_coroutine_ 会设置 状态为 EXEC
            // 保存 主协程 loop 函数栈， 替换为 子协程 fn 方法栈
            main_coroutine_->SwapTo(running_coroutine_);
            
            // 这里应该是 子协程 替换回 主协程 main_coroutine_ 才继续执行
            switch (running_coroutine_->State()) {
                case Coroutine::State::EXEC: {
                    next_coroutine_ = running_coroutines_.Next(running_coroutine_);
                    if(!next_coroutine_) {
                        moveReadyCoroutines();
                        next_coroutine_ = running_coroutines_.Next(running_coroutine_);
                    }
                    
                    running_coroutine_ = next_coroutine_;
                }
                    break;
                case Coroutine::State::HOLD: {
                    // 将当前协程挂起，加入 hold_coroutines_
                    running_coroutines_.Erase(running_coroutine_);
                    hold_coroutines_.Push(running_coroutine_);
                    next_coroutine_ = running_coroutines_.Next(running_coroutine_);
                    if(!next_coroutine_) {
                        moveReadyCoroutines();
                        next_coroutine_ = running_coroutines_.Next(running_coroutine_);
                    }
                    running_coroutine_ = next_coroutine_;
                }
                    break;
                    
                case Coroutine::State::TERM:
                default: {
                    next_coroutine_ = running_coroutines_.Next(running_coroutine_);
                    if(!next_coroutine_) {
                        moveReadyCoroutines();
                        next_coroutine_ = running_coroutines_.Next(running_coroutine_);
                    }
                    
                    running_coroutines_.Erase(running_coroutine_);
                    delete running_coroutine_;
                    running_coroutine_ = next_coroutine_;
                    
                }
                    break;
            }
        }
    }
}

void CoEventLoop::handleActiveEvents(Time time) {
    for(Event* event : active_events_) {
        event->HandleEvent(time);
    }
}

void CoEventLoop::handleTimeoutTimers() {
    //timermanager_->ExecuteAllTimeoutTimer();
    std::vector<Timer*> timeouts;
    if(timermanager_->PopAllTimeoutTimer(timeouts)) {
        for(Timer* timeout : timeouts) {
            AddTask([timeout, this](){
                timeout->Execute();;
                RemoveTimer(timeout);
            });
        }
    }
}

// 将 stateful_ready_coroutines_ 协程 加入 running_coroutines_
void CoEventLoop::moveReadyCoroutines() {
    std::unique_lock<std::mutex> lock(mutex_);
    size_t size = stateful_ready_coroutines_.Size();
    if(size > 0) {
        running_coroutines_.Push(stateful_ready_coroutines_);
    }
    
    if(size < 5 && stateless_ready_coroutines_.Size() > 0) {
        running_coroutines_.Push(stateless_ready_coroutines_);
    }
}

}
}
}
