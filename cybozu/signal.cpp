#include "signal.hpp"

#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <unistd.h>

namespace {

const char ABORT_MESSAGE[] = "got SIGABRT.\n";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
void handle_abort [[noreturn]] (int) {
    ::write(STDERR_FILENO, ABORT_MESSAGE, sizeof(ABORT_MESSAGE) - 1);
    cybozu::dump_stack();
    std::abort();
}
#pragma GCC diagnostic pop

} // anonymous namespace

namespace cybozu {

std::unique_ptr<signal_reader> signal_setup(std::initializer_list<int> sigs) {
    sigset_t mask[1];
    sigemptyset(mask);
    for( int i: sigs )
        sigaddset(mask, i);
    int e = pthread_sigmask(SIG_BLOCK, mask, NULL);
    if( e != 0 )
        throw_unix_error(e, "pthread_sigmask");

    // signal disposition is a per-process attribute.
    struct sigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    if( sigaction(SIGPIPE, &act, NULL) == -1 )
        throw_unix_error(errno, "sigaction");

    std::memset(&act, 0, sizeof(act));
    act.sa_handler = handle_abort;
    act.sa_flags = SA_RESETHAND;
    if( sigaction(SIGABRT, &act, NULL) == -1 )
        throw_unix_error(errno, "sigaction");

    return std::unique_ptr<signal_reader>( new signal_reader(mask) );
}

} // namespace cybozu
