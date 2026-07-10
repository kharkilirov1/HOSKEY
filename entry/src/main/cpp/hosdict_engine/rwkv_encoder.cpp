#include "rwkv_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace hoskey::rwkv {
namespace {

constexpr int D_IN = 525;
constexpr int D = 500;
constexpr int D_FF = 2000;
constexpr int STATE = 4000;

struct Rec {
  std::string name;
  size_t offset = 0;
  std::vector<int> shape;
  size_t count = 0;
};

static std::unordered_map<std::string, Rec> g_index;
static const uint8_t* g_payload = nullptr;
static size_t g_payload_n = 0;

uint64_t ReadLe64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

std::string JsonString(const std::string& s, size_t& p) {
  if (s[p++] != '"') throw std::runtime_error("json string expected");
  std::string out;
  while (p < s.size()) {
    char c = s[p++];
    if (c == '"') break;
    if (c == '\\' && p < s.size()) c = s[p++];
    out.push_back(c);
  }
  return out;
}

void SkipWs(const std::string& s, size_t& p) {
  while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) ++p;
}

uint64_t JsonUint(const std::string& s, size_t& p) {
  SkipWs(s, p);
  uint64_t v = 0;
  while (p < s.size() && s[p] >= '0' && s[p] <= '9') v = v * 10 + uint64_t(s[p++] - '0');
  return v;
}

std::vector<int> JsonIntArray(const std::string& s, size_t& p) {
  std::vector<int> a;
  if (s[p++] != '[') throw std::runtime_error("array expected");
  SkipWs(s, p);
  while (p < s.size() && s[p] != ']') {
    a.push_back(static_cast<int>(JsonUint(s, p)));
    SkipWs(s, p);
    if (s[p] == ',') ++p;
    SkipWs(s, p);
  }
  if (p < s.size() && s[p] == ']') ++p;
  return a;
}

void ParseIndex(const std::string& s) {
  g_index.clear();
  size_t p = 0;
  SkipWs(s, p);
  if (s[p++] != '[') throw std::runtime_error("top array expected");
  SkipWs(s, p);
  while (p < s.size() && s[p] != ']') {
    if (s[p++] != '{') throw std::runtime_error("object expected");
    Rec r;
    while (p < s.size() && s[p] != '}') {
      SkipWs(s, p);
      std::string key = JsonString(s, p);
      SkipWs(s, p);
      if (s[p++] != ':') throw std::runtime_error("colon expected");
      SkipWs(s, p);
      if (key == "name") r.name = JsonString(s, p);
      else if (key == "offset") r.offset = static_cast<size_t>(JsonUint(s, p));
      else if (key == "shape") r.shape = JsonIntArray(s, p);
      else if (key == "nbytes") {
        size_t nbytes = static_cast<size_t>(JsonUint(s, p));
        r.count = nbytes / sizeof(float);
      } else if (key == "dtype") {
        (void)JsonString(s, p);
      }
      SkipWs(s, p);
      if (s[p] == ',') ++p;
      SkipWs(s, p);
    }
    if (p < s.size() && s[p] == '}') ++p;
    if (r.count == 0) {
      r.count = 1;
      for (int x : r.shape) r.count *= static_cast<size_t>(x);
    }
    g_index[r.name] = r;
    SkipWs(s, p);
    if (s[p] == ',') ++p;
    SkipWs(s, p);
  }
}

void MatVec(const float* x, const float* w, int in, int out, float* y) {
  for (int j = 0; j < out; ++j) {
    float sum = 0.0f;
    for (int i = 0; i < in; ++i) sum += x[i] * w[i * out + j];
    y[j] = sum;
  }
}

void LayerNorm(const float* x, const float* g, const float* b, float eps, float* y) {
  float mean = 0.0f;
  for (int i = 0; i < D; ++i) mean += x[i];
  mean /= D;
  float var = 0.0f;
  for (int i = 0; i < D; ++i) {
    float d = x[i] - mean;
    var += d * d;
  }
  var /= D;
  float inv = 1.0f / std::sqrt(var + eps);
  for (int i = 0; i < D; ++i) y[i] = (x[i] - mean) * inv * g[i] + b[i];
}

float Sigmoid(float x) {
  if (x >= 0.0f) {
    float z = std::exp(-x);
    return 1.0f / (1.0f + z);
  }
  float z = std::exp(x);
  return z / (1.0f + z);
}

void Mix(const float* x, const float* prev, const float* cx, const float* cp, float* y) {
  for (int i = 0; i < D; ++i) y[i] = x[i] * cx[i] + prev[i] * cp[i];
}

}  // namespace

Status RwkvEncoder::Load(const uint8_t* weights, size_t n) {
  try {
    if (n < 16 || std::memcmp(weights, "HRWKVW1\0", 8) != 0) return Status::Error("bad weight blob");
    uint64_t table_n = ReadLe64(weights + 8);
    if (16 + table_n > n) return Status::Error("truncated weight blob");
    ParseIndex(std::string(reinterpret_cast<const char*>(weights + 16), static_cast<size_t>(table_n)));
    g_payload = weights + 16 + table_n;
    g_payload_n = n - 16 - static_cast<size_t>(table_n);
    owned_.assign(weights, weights + n);
    g_payload = owned_.data() + 16 + table_n;
    return Bind();
  } catch (const std::exception& e) {
    return Status::Error(e.what());
  }
}

RwkvEncoder::Tensor RwkvEncoder::Find(const std::string& name) const {
  auto it = g_index.find(name);
  if (it == g_index.end()) return {};
  const Rec& r = it->second;
  if (r.offset + r.count * sizeof(float) > g_payload_n) return {};
  return {reinterpret_cast<const float*>(g_payload + r.offset), r.shape, r.count};
}

Status RwkvEncoder::Bind() {
  auto need = [&](const std::string& n, size_t c) -> const float* {
    Tensor t = Find(n);
    if (!t.data || t.count != c) throw std::runtime_error("missing/bad tensor: " + n);
    return t.data;
  };
  try {
    eps_ = *need("enc/rwkv/block_0/layer_normalization_8/batchnorm/add/y", 1);
    input_w_ = need("const_fold_opt__102", D_IN * D);
    input_b_ = need("enc/dense_1/BiasAdd/ReadVariableOp/resource", D);
    out_ln_g_ = need("enc/rwkv/layer_normalization_12/batchnorm/mul/ReadVariableOp/resource", D);
    out_ln_b_ = need("enc/rwkv/layer_normalization_12/batchnorm/ReadVariableOp/resource", D);

    Block& b0 = blocks_[0];
    b0.ln1_g = need("enc/rwkv/block_0/layer_normalization_8/batchnorm/mul/ReadVariableOp/resource", D);
    b0.ln1_b = need("enc/rwkv/block_0/layer_normalization_8/batchnorm/ReadVariableOp/resource", D);
    b0.ln2_g = need("enc/rwkv/block_0/layer_normalization_9/batchnorm/mul/ReadVariableOp/resource", D);
    b0.ln2_b = need("enc/rwkv/block_0/layer_normalization_9/batchnorm/ReadVariableOp/resource", D);
    b0.tm_xk = need("enc/rwkv/block_0/TimeMixing/mul/ReadVariableOp/resource", D);
    b0.tm_xv = need("enc/rwkv/block_0/TimeMixing/mul_2/ReadVariableOp/resource", D);
    b0.tm_xr = need("enc/rwkv/block_0/TimeMixing/mul_4/ReadVariableOp/resource", D);
    b0.tm_pk = need("enc/rwkv/block_0/TimeMixing/sub", D);
    b0.tm_pv = need("enc/rwkv/block_0/TimeMixing/sub_1", D);
    b0.tm_pr = need("enc/rwkv/block_0/TimeMixing/sub_2", D);
    b0.tm_decay = need("enc/rwkv/block_0/TimeMixing/Exp_3", D);
    b0.tm_bonus = need("enc/rwkv/block_0/TimeMixing/Exp", D);
    b0.tm_wk = need("const_fold_opt__121", D * D);
    b0.tm_wv = need("const_fold_opt__103", D * D);
    b0.tm_wr = need("const_fold_opt__99", D * D);
    b0.tm_wo = need("const_fold_opt__115", D * D);
    b0.cm_xk = need("enc/rwkv/block_0/TimeMixing/mul_12/ReadVariableOp/resource", D);
    b0.cm_xr = need("enc/rwkv/block_0/TimeMixing/mul_14/ReadVariableOp/resource", D);
    b0.cm_pk = need("enc/rwkv/block_0/TimeMixing/sub_3", D);
    b0.cm_pr = need("enc/rwkv/block_0/TimeMixing/sub_4", D);
    b0.cm_wk = need("const_fold_opt__97", D * D_FF);
    b0.cm_wv = need("const_fold_opt__98", D_FF * D);
    b0.cm_wr = need("const_fold_opt__100", D * D);

    Block& b1 = blocks_[1];
    b1.ln1_g = need("enc/rwkv/block_1/layer_normalization_10/batchnorm/mul/ReadVariableOp/resource", D);
    b1.ln1_b = need("enc/rwkv/block_1/layer_normalization_10/batchnorm/ReadVariableOp/resource", D);
    b1.ln2_g = need("enc/rwkv/block_1/layer_normalization_11/batchnorm/mul/ReadVariableOp/resource", D);
    b1.ln2_b = need("enc/rwkv/block_1/layer_normalization_11/batchnorm/ReadVariableOp/resource", D);
    b1.tm_xk = need("enc/rwkv/block_1/TimeMixing/mul/ReadVariableOp/resource", D);
    b1.tm_xv = need("enc/rwkv/block_1/TimeMixing/mul_2/ReadVariableOp/resource", D);
    b1.tm_xr = need("enc/rwkv/block_1/TimeMixing/mul_4/ReadVariableOp/resource", D);
    b1.tm_pk = need("enc/rwkv/block_1/TimeMixing/sub", D);
    b1.tm_pv = need("enc/rwkv/block_1/TimeMixing/sub_1", D);
    b1.tm_pr = need("enc/rwkv/block_1/TimeMixing/sub_2", D);
    b1.tm_decay = need("enc/rwkv/block_1/TimeMixing/Exp_3", D);
    b1.tm_bonus = need("enc/rwkv/block_1/TimeMixing/Exp", D);
    b1.tm_wk = need("const_fold_opt__101", D * D);
    b1.tm_wv = need("const_fold_opt__120", D * D);
    b1.tm_wr = need("const_fold_opt__118", D * D);
    b1.tm_wo = need("const_fold_opt__117", D * D);
    b1.cm_xk = need("enc/rwkv/block_1/TimeMixing/mul_12/ReadVariableOp/resource", D);
    b1.cm_xr = need("enc/rwkv/block_1/TimeMixing/mul_14/ReadVariableOp/resource", D);
    b1.cm_pk = need("enc/rwkv/block_1/TimeMixing/sub_3", D);
    b1.cm_pr = need("enc/rwkv/block_1/TimeMixing/sub_4", D);
    b1.cm_wk = need("const_fold_opt__119", D * D_FF);
    b1.cm_wv = need("const_fold_opt__108", D_FF * D);
    b1.cm_wr = need("const_fold_opt__107", D * D);
  } catch (const std::exception& e) {
    return Status::Error(e.what());
  }
  return Status::Ok();
}

void RwkvEncoder::Step(const float* word_emb, const float* state_in, float* hidden_out,
                       float* state_out) const {
  float x[D], a[D], b[D], c[D], r[D], k[D], v[D], ff[D_FF];
  MatVec(word_emb, input_w_, D_IN, D, x);
  for (int i = 0; i < D; ++i) x[i] = std::max(0.0f, x[i] + input_b_[i]);

  for (int bi = 0; bi < 2; ++bi) {
    const Block& bl = blocks_[bi];
    const float* st = state_in + bi * 4 * D;
    float* sto = state_out + bi * 4 * D;

    LayerNorm(x, bl.ln1_g, bl.ln1_b, eps_, a);
    Mix(a, st + 0 * D, bl.tm_xr, bl.tm_pr, b);
    MatVec(b, bl.tm_wr, D, D, r);
    for (int i = 0; i < D; ++i) r[i] = Sigmoid(r[i]);
    Mix(a, st + 0 * D, bl.tm_xv, bl.tm_pv, b);
    MatVec(b, bl.tm_wv, D, D, v);
    Mix(a, st + 0 * D, bl.tm_xk, bl.tm_pk, b);
    MatVec(b, bl.tm_wk, D, D, k);
    for (int i = 0; i < D; ++i) {
      float ek = std::exp(k[i]);
      float num = st[D + i] * bl.tm_decay[i] + ek * v[i];
      float den = st[2 * D + i] * bl.tm_decay[i] + ek;
      float wkv = (st[D + i] + ek * bl.tm_bonus[i] * v[i]) /
                  (st[2 * D + i] + ek * bl.tm_bonus[i]);
      sto[i] = a[i];
      sto[D + i] = num;
      sto[2 * D + i] = den;
      c[i] = r[i] * wkv;
    }
    MatVec(c, bl.tm_wo, D, D, b);
    for (int i = 0; i < D; ++i) x[i] += b[i];

    LayerNorm(x, bl.ln2_g, bl.ln2_b, eps_, a);
    Mix(a, st + 3 * D, bl.cm_xr, bl.cm_pr, b);
    MatVec(b, bl.cm_wr, D, D, r);
    for (int i = 0; i < D; ++i) r[i] = Sigmoid(r[i]);
    Mix(a, st + 3 * D, bl.cm_xk, bl.cm_pk, b);
    MatVec(b, bl.cm_wk, D, D_FF, ff);
    for (int i = 0; i < D_FF; ++i) ff[i] = std::max(0.0f, ff[i]) * std::max(0.0f, ff[i]);
    MatVec(ff, bl.cm_wv, D_FF, D, v);
    for (int i = 0; i < D; ++i) {
      sto[3 * D + i] = a[i];
      x[i] += r[i] * v[i];
    }
  }
  LayerNorm(x, out_ln_g_, out_ln_b_, eps_, hidden_out);
}

}  // namespace hoskey::rwkv
