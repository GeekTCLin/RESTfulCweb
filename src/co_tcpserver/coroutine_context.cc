#include "coroutine_context.h"
#include <string.h>

#define R15 0       // callee
#define R14 1       // callee
#define R13 2       // callee
#define R12 3       // callee
#define RBP 4       // 栈帧指针，标识当前栈帧的起始位置
#define RET 5       // 
#define RBX 6       // 任意寄存器 属于callee
#define RSP 7       // 栈顶指针
#define RDI 8       // 首个函数参数


// 还有一个比较重要的 %rip 指令寄存器 存储下一条指令

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

// from rdi 寄存器
// to   rsi 寄存器
/**
 * 
 *  leaq (%rsp), %rax
    movq %rax, 56(%rdi)     // rax 写入 regs[7] 即 RSP
    movq %rbx, 48(%rdi)     // rbx 写入 regs[6] 即 RBX
    movq 0(%rax), %rax
    movq %rax, 40(%rdi)     // 记录 regs[RET]
    movq %rbp, 32(%rdi)     // 记录 regs[RBP]
    movq %r12, 24(%rdi)     // 记录 regs[R12]
    movq %r13, 16(%rdi)     // 记录 regs[R13]
    movq %r14, 8(%rdi)      // 记录 regs[R14]
    movq %r15, (%rdi)       // 记录 regs[R15]
    xorq %rax, %rax

    movq 32(%rsi), %rbp     // 取出 regs[RBP]
    movq 56(%rsi), %rsp     // 取出 regs[RSP]
    movq (%rsi), %r15
    movq 8(%rsi), %r14
    movq 16(%rsi), %r13
    movq 24(%rsi), %r12
    movq 48(%rsi), %rbx
    movq 64(%rsi), %rdi
    leaq 8(%rsp), %rsp
    pushq 40(%rsi)

    ret
 */
void CoroutineContext::ContextSwap(CoroutineContext *from, CoroutineContext *to) {
    context_swap(from, to);
}

}
}
}
