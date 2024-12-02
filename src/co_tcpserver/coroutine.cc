#include "coroutine.h"
#include "co_eventloop.h"
#include "coroutine_context.h"
#include "pthread_keys.h"

namespace cweb {
namespace tcpserver {
namespace coroutine {

static const size_t kCoroutineContextSize = 4096 * 1024;

Coroutine::Coroutine(std::function<void()> func, std::shared_ptr<CoEventLoop> loop) : func_(std::move(func)), context_(new CoroutineContext(kCoroutineContextSize, coroutineFunc, this)), loop_(loop) {}

Coroutine::~Coroutine() {
    delete context_;
}

void Coroutine::SwapIn() {
    // 切换状态 EXEC
    state_ = EXEC;
    // 切换上下文
    CoroutineContext::ContextSwap(
        ((CoEventLoop*)pthread_getspecific(util::PthreadKeysSingleton::GetInstance()->TLSEventLoop))
            ->GetMainCoroutine()->context_, context_);
}

void Coroutine::SwapOut() {
    CoroutineContext::ContextSwap(context_, 
        ((CoEventLoop*)pthread_getspecific(util::PthreadKeysSingleton::GetInstance()->TLSEventLoop))
            ->GetMainCoroutine()->context_);
}

void Coroutine::SwapTo(Coroutine *co) {
    // co 设置状态 状态 执行
    co->SetState(EXEC);
    // 切换上下文
    CoroutineContext::ContextSwap(context_, co->context_);
}

void Coroutine::SetState(enum State state) {
    if(state_ != READY && state == READY) {
        loop_->NotifyCoroutineReady(this);
    }
    state_ = state;
}

void Coroutine::coroutineFunc(void *vp) {
    Coroutine* co = (Coroutine*)vp;
    co->run();
}

void Coroutine::run() {
    // 调用代理的函数
    if(func_) func_();
    // 函数执行完设置 状态 TERM 结束
    state_ = TERM;
    // 切换回前一个 协程
    SwapOut();
}

}
}
}
