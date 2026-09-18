#include <gtest/gtest.h>

#include "AIOxx/signal.hpp"

#include <initializer_list>
#include <unistd.h>

using namespace AIO;

namespace {

struct BlockedSignals {
  sigset_t mask{}, previous{};

  explicit BlockedSignals(std::initializer_list<int> signals) {
    EXPECT_EQ(sigemptyset(&mask), 0);
    for (auto signal : signals)
      EXPECT_EQ(sigaddset(&mask, signal), 0);
    EXPECT_EQ(pthread_sigmask(SIG_BLOCK, &mask, &previous), 0);
  }

  ~BlockedSignals() {
    EXPECT_EQ(pthread_sigmask(SIG_SETMASK, &previous, nullptr), 0);
  }
};

} // namespace

TEST(Signal, Pending) {
  BlockedSignals blocked{SIGUSR1};
  run_in_new([&](BasicScheduler *sched) {
    auto signals = SignalFD::open(sched->io(), blocked.mask);
    EXPECT_FALSE(signals.try_read());

    EXPECT_EQ(raise(SIGUSR1), 0);
    auto info = signals.try_read();
    EXPECT_TRUE(info.has_value());
    if (info) {
      EXPECT_EQ(info->ssi_signo, static_cast<unsigned>(SIGUSR1));
      EXPECT_EQ(info->ssi_pid, static_cast<unsigned>(getpid()));
    }
    EXPECT_FALSE(signals.try_read());
    std::move(signals).close();
  });
}

TEST(Signal, ReadWaits) {
  BlockedSignals blocked{SIGUSR1, SIGUSR2};
  run_in_new([&](BasicScheduler *sched) {
    auto signals = SignalFD::open(sched->io(), blocked.mask);
    auto sender = sched->fiber([&] {
      EXPECT_EQ(raise(SIGUSR1), 0);
      sched->yield();
      EXPECT_EQ(raise(SIGUSR2), 0);
    });

    EXPECT_EQ(signals.read().ssi_signo, static_cast<unsigned>(SIGUSR1));
    EXPECT_EQ(signals.read().ssi_signo, static_cast<unsigned>(SIGUSR2));
    sched->await(std::move(sender));
    std::move(signals).close();
  });
}

TEST(Signal, CancelReadiness) {
  BlockedSignals blocked{SIGUSR1};
  run_in_new([&](BasicScheduler *sched) {
    auto signals = SignalFD::open(sched->io(), blocked.mask);
    bool called = false;
    auto cancelled = signals.ready(FD::IN).map_result([&] { called = true; });
    sched->yield();
    std::move(cancelled).cancel();

    auto next = signals.ready(FD::IN);
    EXPECT_EQ(raise(SIGUSR1), 0);
    sched->await(std::move(next));
    EXPECT_FALSE(called);
    EXPECT_EQ(signals.read().ssi_signo, static_cast<unsigned>(SIGUSR1));
    std::move(signals).close();

    sigset_t mask{};
    EXPECT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &mask), 0);
    EXPECT_EQ(sigismember(&mask, SIGUSR1), 1);
  });
}

TEST(Signal, Unreceivable) {
  run_in_new([](BasicScheduler *sched) {
    for (auto signal : {SIGKILL, SIGSTOP}) {
      sigset_t mask{};
      EXPECT_EQ(sigemptyset(&mask), 0);
      EXPECT_EQ(sigaddset(&mask, signal), 0);
      EXPECT_THROW((void) SignalFD::open(sched->io(), mask), FD::Error);
    }
  });
}
