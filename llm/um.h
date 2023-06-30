
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

struct UM {
  explicit UM(const std::vector<uint8_t> &contents);

  void Run(const std::function<int()> &GetChar,
           const std::function<void(uint8_t)> &PutChar);

 private:
  uint32_t reg[8] = {0,0,0,0,0,0,0,0};
  uint32_t ip = 0;
  std::vector<uint32_t *> mem;
  std::vector<uint32_t> freelist;

  // Size is stored in the first slot.
  uint32_t usize(uint32_t id) const {
    return *mem[id];
  }

  // Skip first slot (size).
  uint32_t *arr(uint32_t id) {
    return mem[id] + 1;
  }

  void resize(uint32_t id, uint32_t size);
  void ufree(uint32_t id);
  uint32_t ulloc(uint32_t size);
};
