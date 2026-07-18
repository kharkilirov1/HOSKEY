#pragma once

#include "status.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace hoskey::engine {

struct RnntModelBuffer {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

struct RnntSwipeModelBuffers {
  RnntModelBuffer sampling;
  std::array<RnntModelBuffer, 4> pre;
  std::array<RnntModelBuffer, 4> post;
  RnntModelBuffer joint;
  // m0 from the ynnlm bundle.  Static reverse of TJoint::Run plus the model
  // signatures identifies dec_char/output_hidden[750] as the RNNT predictor
  // vector consumed by joint/in/pred[750].
  RnntModelBuffer predictor;
};

// Exact learned component runner for the recovered Yandex swipe RNNT.  Trie
// predictor-state decoding is deliberately a separate contract: the component
// runner can be validated and shipped without fabricating the 750-wide state.
class RnntSwipeModel {
 public:
  using FeatureWindow = std::array<float, 5 * 52>;
  using EncoderVector = std::array<float, 100>;
  using QkvVector = std::array<float, 300>;
  using QkvLeftCache = std::array<float, 10 * 300>;
  using QkvRightCache = std::array<float, 5 * 300>;
  using ProjectionCache = std::array<float, 9 * 100>;
  using AttentionCoefficients = std::array<float, 16 * 4>;
  using PredictorState = std::array<float, 1500>;
  using PredictorVector = std::array<float, 750>;
  using JointLogits = std::array<float, 38>;

  RnntSwipeModel() = default;
  ~RnntSwipeModel();
  RnntSwipeModel(const RnntSwipeModel&) = delete;
  RnntSwipeModel& operator=(const RnntSwipeModel&) = delete;

  Status Load(const RnntSwipeModelBuffers& buffers);
  // Cheap serialized-buffer gate shared by production loading and the
  // device-negative audit. It identifies the exact missing/corrupt component
  // before any MindSpore model is built.
  static Status ValidateBuffers(const RnntSwipeModelBuffers& buffers,
                                std::string* diagnostics = nullptr);
  void Reset();

  [[nodiscard]] bool components_loaded() const { return components_loaded_; }
  [[nodiscard]] const std::string& diagnostics() const { return diagnostics_; }

  // The original 3.5 KB position graph is exactly the standard 100-wide
  // sinusoidal transform.  Computing it directly avoids a MindSpore Lite 2.6
  // SIN/COS runtime hang and removes no learned parameters.
  static EncoderVector PositionalEmbedding(uint32_t position);

  bool RunSampling(const FeatureWindow& input, EncoderVector* output) const;
  bool RunPre(size_t layer, const EncoderVector& embedding,
              const EncoderVector& position, EncoderVector* ffm1,
              QkvVector* qkv) const;
  bool RunPost(size_t layer, const EncoderVector& ffm1, const QkvVector& qkv,
               const QkvLeftCache& qkv_left,
               const QkvRightCache& qkv_right,
               const ProjectionCache& projection,
               AttentionCoefficients* coefficients, EncoderVector* ffm2,
               EncoderVector* next_projection) const;
  bool RunJoint(const EncoderVector& encoder,
                const PredictorVector& predictor, JointLogits* logits) const;
  bool RunPredictor(float token, const PredictorState& state,
                    JointLogits* probabilities, PredictorVector* hidden,
                    PredictorState* next_state) const;

  // Executes the recovered four-layer streaming Conformer contract over one
  // 5x52 sampling window per frame.  Each post graph receives 10 left QKV
  // rows, the current QKV row, five right rows, and nine prior projections;
  // unavailable boundary rows are zero padded, matching TConformer::Flush.
  bool Encode(const std::vector<FeatureWindow>& windows,
              std::vector<EncoderVector>* output) const;

  // Forced RNNT Viterbi score for a trie/geometry candidate. `tokens` are the
  // recovered 38-way char-decoder ids (blank=0, start=1, end=2, '-'=5,
  // Cyrillic а..я=6..37).  Predictor score is the independent sum of char LM
  // log probabilities used by the four primary swipe ranker features.
  bool ScoreTokens(const std::vector<EncoderVector>& encoder,
                   const std::vector<int>& tokens, float* rnnt_score,
                   float* predictor_score) const;

 private:
  bool components_loaded_ = false;
  std::string diagnostics_ = "not_loaded";
  mutable std::mutex inference_mutex_;
  void* sampling_ctx_ = nullptr;
  std::array<void*, 4> pre_ctx_{};
  std::array<void*, 4> post_ctx_{};
  void* joint_ctx_ = nullptr;
  void* predictor_ctx_ = nullptr;
  void* sampling_model_ = nullptr;
  std::array<void*, 4> pre_model_{};
  std::array<void*, 4> post_model_{};
  void* joint_model_ = nullptr;
  void* predictor_model_ = nullptr;
};

}  // namespace hoskey::engine
