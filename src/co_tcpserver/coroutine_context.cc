#include "coroutine_context.h"
#include <string.h>

#define R15 0
#define R14 1
#define R13 2
#define R12 3
#define RBP 4       // 栈帧指针，标识当前栈帧的起始位置
#define RET 5
#define RBX 6
#define RSP 7       // 栈顶指针
#define RDI 8       // 首个函数参数

namespace cweb {
namespace tcpserver {
namespace coroutine {

void context_swap(CoroutineContext* from, CoroutineContext* to);

CoroutineContext::CoroutineContext() {}

// fn 为 Coroutine 的 coroutineFunc 方法
CoroutineContext::CoroutineContext(size_t size, void (*fn)(void*), const void* vp) : ss_size(size) {
    Init(size, fn, vp);
}

CoroutineContext::~CoroutineContext() {
    free(ss_sp);
}

void CoroutineContext::Init(size_t size, void (*fn)(void*), const void* vp) {
    ss_sp = (char*)malloc(size); //堆 低->高
    //移动到高地址 栈 高->低
    char* sp = ss_sp + ss_size - sizeof(void*);
    
    // 对齐 移动（16 - ss_size % 16）
    // -16LL在二进制表示中，除了最低4位是0000（因为16是2的4次方，所以-16在补码表示中，最低4位之外全是1，最低4位是0
    // 与 -16LL & 相当于 去掉低4位，剩余数为 16的模数
    // 故sp地址 16字节对齐
    sp = (char*)((unsigned long)sp & -16LL);
    
    memset(regs, 0, sizeof(regs));
    
    *(void**)sp = (void*)fn;
    
    //初始时栈顶位置
    regs[RBP] = sp + sizeof(void*);
    regs[RSP] = sp; //栈顶指针存储函数地址 函数运行过程栈指针向高地址增长
    regs[RDI] = (void*)vp;      // RDI 首个参数，对应coroutine 对象指针
    
    regs[RET] = fn; //函数返回地址
}

void CoroutineContext::ContextSwap(CoroutineContext *from, CoroutineContext *to) {
    context_swap(from, to);
}

}
}
}
