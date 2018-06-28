#include <afina/coroutine/Engine.h>

#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace Afina {
namespace Coroutine {


void Engine::Store(context &ctx) {
    char cur_stack_head;
    char * head = &cur_stack_head;
    ctx.Low = StackBottom;
    ctx.Hight = head;

    char *last_stack = std::get<0>(ctx.Stack);
    free(last_stack);
    size_t stack_size = ctx.Low - head + 1;
    auto *cur_stack = (char *)malloc(stack_size);
    std::memcpy(cur_stack, head, stack_size);
    ctx.Stack = std::make_tuple(cur_stack, stack_size);
}

void Engine::Restore(context &ctx) {

    char cur_stack_head;
    if (&cur_stack_head > ctx.Hight) {
        this->Restore(ctx);
    } else {
        std::memcpy(ctx.Hight, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
        cur_routine = &ctx;
        longjmp(ctx.Environment, 1);
    }

}

void Engine::yield() {
    if (alive == nullptr) {
        return;
    }
    if (alive == cur_routine && alive->next != nullptr) {
        sched(alive->next);
    }
    sched(alive);

}

void Engine::sched(void *routine_) {
    context &ctx = *(static_cast<context *>(routine_));
    if (cur_routine != idle_ctx) {
        Store(*cur_routine);
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
    }
    Restore(ctx);
}

} // namespace Coroutine
} // namespace Afina
