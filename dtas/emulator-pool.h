
#ifndef _DTAS_EMULATOR_POOL_H
#define _DTAS_EMULATOR_POOL_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../fceulib/emulator.h"
#include "base/logging.h"

struct EmulatorPool {
 private:
  struct Wrapper;
 public:

  explicit EmulatorPool(const std::string &romfile) :
    romfile(romfile) {
    // We create a single emulator and save the state so that
    // it can be unprotected by the mutex.
    auto emu = Acquire();
    start_state = emu->SaveUncompressed();
  }

  ~EmulatorPool() {
    std::unique_lock<std::mutex> ml(mutex);
    CHECK(ready.size() == all.size()) << "Not all emulators were returned.";
    ready.clear();
    all.clear();
  }

  // Move-only wrapper, like std::unique_ptr. Returns the emulator to
  // the pool when destroyed.
  struct Lease {
    Emulator *get() { return wrapper->emu.get(); }
    const Emulator *get() const { return wrapper->emu.get(); }
    ~Lease() {
      if (wrapper != nullptr) {
        std::unique_lock<std::mutex> ml(parent->mutex);
        wrapper->in_use = false;
        parent->ready.push_back(wrapper);
      }
    }

    Emulator &operator *() {
      return *wrapper->emu;
    }

    Emulator *operator ->() {
      return wrapper->emu.get();
    }

    // Move-only.
    Lease(Lease &&other) noexcept :
      parent(other.parent),
      wrapper(other.wrapper) {
      other.wrapper = nullptr;
      other.parent = nullptr;
    }

    Lease& operator=(Lease &&other) noexcept {
      if (this != &other) {
        // If overwriting, return the lease.
        if (wrapper != nullptr) {
          std::unique_lock<std::mutex> ml(parent->mutex);
          wrapper->in_use = false;
          parent->ready.push_back(wrapper);
        }

        wrapper = other.wrapper;
        parent = other.parent;
        other.wrapper = nullptr;
        other.parent = nullptr;
      }
      return *this;
    }

   private:
    friend struct EmulatorPool;
    Lease(EmulatorPool *parent, Wrapper *wrapper) :
      parent(parent), wrapper(wrapper) {
      CHECK(wrapper != nullptr);
    }
    // Not copyable.
    Lease(Lease const&) = delete;
    Lease& operator=(Lease const&) = delete;

    friend struct EmulatorPool;
    EmulatorPool *parent = nullptr;
    Wrapper *wrapper = nullptr;
  };

  Lease Acquire() {
    std::unique_lock<std::mutex> ml(mutex);
    if (ready.empty()) {
      Emulator *emu = Emulator::Create(romfile);
      CHECK(emu != nullptr);
      all.emplace_back(
          new Wrapper{
            .emu = std::unique_ptr<Emulator>(emu),
            .in_use = false
          });
      ready.push_back(all.back().get());
    }
    CHECK(!ready.empty());
    Wrapper *w = ready.back();
    ready.pop_back();
    CHECK(!w->in_use);
    return Lease(this, w);
  }

  Lease AcquireClean() {
    auto lease = Acquire();
    CHECK(!start_state.empty());
    lease->LoadUncompressed(start_state);
    return lease;
  }

  const std::vector<uint8_t> &StartState() const { return start_state; }

 private:
  struct Wrapper {
    std::unique_ptr<Emulator> emu;
    bool in_use = false;
  };

  const std::string romfile;
  // The (uncompressed) state right after loading the game.
  // Morally const, and can be accessed without the mutex.
  std::vector<uint8_t> start_state;

  std::mutex mutex;
  std::vector<std::unique_ptr<Wrapper>> all;
  std::vector<Wrapper *> ready;
};

#endif
