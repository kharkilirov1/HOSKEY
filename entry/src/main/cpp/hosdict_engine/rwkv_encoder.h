#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hoskey::rwkv {

struct Status {
  bool ok = false;
  std::string message;
  static Status Ok() { return {true, {}}; }
  static Status Error(std::string msg) { return {false, msg}; }
};

class RwkvEncoder {
 public:
  RwkvEncoder() = default;
  // Non-copyable/movable: bound tensor pointers alias into owned_, so a copy or
  // move would leave them dangling against the source object's buffer.
  RwkvEncoder(const RwkvEncoder&) = delete;
  RwkvEncoder& operator=(const RwkvEncoder&) = delete;
  RwkvEncoder(RwkvEncoder&&) = delete;
  RwkvEncoder& operator=(RwkvEncoder&&) = delete;

  Status Load(const uint8_t* weights, size_t n);
  void Step(const float* word_emb, const float* state_in, float* hidden_out,
            float* state_out) const;

 private:
  struct Tensor {
    const float* data = nullptr;
    std::vector<int> shape;
    size_t count = 0;
  };
  struct Block {
    const float* ln1_g = nullptr;
    const float* ln1_b = nullptr;
    const float* ln2_g = nullptr;
    const float* ln2_b = nullptr;
    const float* tm_xk = nullptr;
    const float* tm_xv = nullptr;
    const float* tm_xr = nullptr;
    const float* tm_pk = nullptr;
    const float* tm_pv = nullptr;
    const float* tm_pr = nullptr;
    const float* tm_decay = nullptr;
    const float* tm_bonus = nullptr;
    const float* tm_wk = nullptr;
    const float* tm_wv = nullptr;
    const float* tm_wr = nullptr;
    const float* tm_wo = nullptr;
    const float* cm_xk = nullptr;
    const float* cm_xr = nullptr;
    const float* cm_pk = nullptr;
    const float* cm_pr = nullptr;
    const float* cm_wk = nullptr;
    const float* cm_wv = nullptr;
    const float* cm_wr = nullptr;
  };

  const float* input_w_ = nullptr;
  const float* input_b_ = nullptr;
  const float* out_ln_g_ = nullptr;
  const float* out_ln_b_ = nullptr;
  float eps_ = 1e-5f;
  Block blocks_[2];
  std::vector<uint8_t> owned_;

  Tensor Find(const std::string& name) const;
  Status Bind();
};

}  // namespace hoskey::rwkv
