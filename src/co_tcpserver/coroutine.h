#ifndef CWEB_COROUTINE_COROUTINE_H_
#define CWEB_COROUTINE_COROUTINE_H_

#include <vector>
#include <functional>
#include "linked_list.h"
#include "co_event.h"

namespace cweb {
namespace tcpserver {
namespace coroutine {

class CoroutineContext;
class CoEventLoop;
class CoEvent;
class Coroutine : public util::LinkedListNode {
    
public:
    enum State {
        READY,          // 初始状态
        HOLD,           // 挂起，加入hold_coroutines_
        EXEC,           // 执行
        TERM            // 执行结束
    };
    
    Coroutine(std::function<void()> func, std::shared_ptr<CoEventLoop> loop = nullptr);
    ~Coroutine();
    void SwapIn();
    void SwapOut();
    void SwapTo(Coroutine* co);
    
    void SetState(State state);
    State State() const {return state_;}
    void SetLoop(std::shared_ptr<CoEventLoop> loop) {loop_ = loop;}
    
private:
    enum State state_ = READY;
    CoroutineContext* context_;             // 协程上下文
    std::function<void()> func_;            // 执行的方法体
    std::shared_ptr<CoEventLoop> loop_;     // 绑定的循环对象
    CoEvent* event_ = nullptr;
    void run();
    static void coroutineFunc(void* vp);
};

}
}
}

#endif
