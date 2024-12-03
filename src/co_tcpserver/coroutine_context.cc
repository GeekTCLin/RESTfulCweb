#include "coroutine_context.h"
#include <string.h>

#define R15 0       // callee
#define R14 1       // callee
#define R13 2       // callee
#define R12 3       // callee
#define RBP 4       // 栈帧指针，标识当前栈帧的起始位置
#define RET 5       // 保存函数地址，主要用于间接修改RIP
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
 * leaq指令操作的是地址（或地址表达式），而mov指令操作的是数据值
 * leaq指令将计算出的有效地址存储到目标寄存器中，而mov指令将源操作数的值复制到目标操作数中
 * 
 * movq指令用于在寄存器之间或寄存器与内存之间传输64位（8字节）的数据，movq指令不支持直接将一个寄存器的值用作另一个内存操作的源地址，40(%rdi) 为 内存操作地址
 * 
 * xorq 异或操作
 * 
 * pushq 将操作数压入栈
 * 
 * ret  对应 popq %rip 把栈顶保存的返回地址传给rip寄存器
 * 
 * 另外rip寄存器不能直接修改所以结合call和ret的特性去保存和构造跳转地址做到协程下一步执行指令的记录与恢复
 * 
 * 进入 context_swap 时发生 call 命令，而一条 call 相当于
 *  pushq %rip
    jmpq fun （fun 对应 context_swap）
 * 此时已将 rip 存入了栈，而rsp 此时也指向这个 rip 值（对应该协程执行函数的返回地址）
 * 
 *  leaq (%rsp), %rax       // 将栈指针 的值（此时时rip的地址） 加载到 寄存器 %rax 中
    movq %rax, 56(%rdi)     // rax 写入 regs[7] 即 RSP
    movq %rbx, 48(%rdi)     // rbx 写入 regs[6] 即 RBX
    movq 0(%rax), %rax      // 取出栈顶指针指向的内存值，写入 %rax，此时取出 rip
    movq %rax, 40(%rdi)     // 写入 regs[RET] 
    movq %rbp, 32(%rdi)     // 写入 regs[RBP]
    movq %r12, 24(%rdi)     // 写入 regs[R12]
    movq %r13, 16(%rdi)     // 写入 regs[R13]
    movq %r14, 8(%rdi)      // 写入 regs[R14]
    movq %r15, (%rdi)       // 写入 regs[R15]
    xorq %rax, %rax         // 与自身进行异或操作，清零

    movq 32(%rsi), %rbp     // 取出 regs[RBP]
    movq 56(%rsi), %rsp     // 取出 regs[RSP]
    movq (%rsi), %r15
    movq 8(%rsi), %r14
    movq 16(%rsi), %r13
    movq 24(%rsi), %r12
    movq 48(%rsi), %rbx
    movq 64(%rsi), %rdi
    leaq 8(%rsp), %rsp
    pushq 40(%rsi)          // 

    ret                     // 将 40(%rsi) 赋值给 rip，如果是首次执行，对应就是进入 fn ，进入 coroutineFunc
 */
void CoroutineContext::ContextSwap(CoroutineContext *from, CoroutineContext *to) {
    context_swap(from, to);
}

}
}
}
