#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/puzzle/puzzle.h"

int64_t sys_puzzle(uint64_t op, uint64_t a, uint64_t b)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;

    switch (op) {
        case 0:
            return (int64_t)puzzle_eat_piece(t, (int)a);
        case 1:
            return t->puzzle ? (int64_t)t->puzzle->total_regens : -ENOENT;
        case 2:
            return t->puzzle ? (int64_t)t->puzzle->total_eaten : -ENOENT;
        case 3:
            return t->puzzle ? (int64_t)t->puzzle->alive_pieces : -ENOENT;
        case 4:
            if (t->puzzle) puzzle_dump(t->puzzle);
            return t->puzzle ? 0 : -ENOENT;
        case 5:
            return t->puzzle ? (int64_t)t->puzzle->total_guard_restores : -ENOENT;
        case 6:
            return (int64_t)puzzle_tamper(t, (uintptr_t)a, (size_t)b);
        default:
            return -EINVAL;
    }
}
