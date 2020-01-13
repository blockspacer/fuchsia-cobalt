#include "src/algorithms/experimental/integer_encoder.h"

#include "src/algorithms/experimental/distributions.h"
#include "src/algorithms/experimental/random.h"
#include "src/algorithms/experimental/randomized_response.h"
#include "src/registry/buckets_config.h"

namespace cobalt {

using config::IntegerBucketConfig;

namespace {

IntegerBuckets MakeIntegerBuckets(int64_t min_int, uint32_t num_buckets, uint32_t step_size) {
  IntegerBuckets buckets;
  auto linear_buckets = buckets.mutable_linear();
  linear_buckets->set_floor(min_int);
  linear_buckets->set_num_buckets(num_buckets);
  linear_buckets->set_step_size(step_size);
  return buckets;
}

double GetMultiplierSum(const std::vector<double>& multipliers) {
  double sum = 0;
  for (const double m : multipliers) {
    sum += m;
  }
  return sum;
}

}  // namespace

IntegerEncoder::IntegerEncoder(BitGeneratorInterface<uint32_t>* gen, const IntegerBuckets& buckets,
                               double p, const RoundingStrategy& rounding_strategy)
    : gen_(gen), rounding_strategy_(rounding_strategy) {
  bucket_config_ = IntegerBucketConfig::CreateFromProto(buckets);
  randomizer_ = std::make_unique<ResponseRandomizer>(gen, bucket_config_->OverflowBucket(), p);
}

IntegerEncoder::IntegerEncoder(BitGeneratorInterface<uint32_t>* gen, int64_t min_int,
                               int64_t max_int, uint32_t partitions, double p,
                               const RoundingStrategy& rounding_strategy)
    : gen_(gen), rounding_strategy_(rounding_strategy) {
  int64_t step_size = (max_int - min_int) / partitions;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  bucket_config_ = IntegerBucketConfig::CreateFromProto(buckets);
  randomizer_ = std::make_unique<ResponseRandomizer>(gen, bucket_config_->OverflowBucket(), p);
}

uint32_t IntegerEncoder::Encode(int64_t val) {
  uint32_t bucket_index = bucket_config_->BucketIndex(val);
  if (rounding_strategy_ == IntegerEncoder::kRandom) {
    bucket_index = RandomRound(val, bucket_index);
  }
  return randomizer_->Encode(bucket_index);
}

uint32_t IntegerEncoder::RandomRound(int64_t val, uint32_t bucket_index) {
  if (bucket_index == bucket_config_->UnderflowBucket() ||
      bucket_index == bucket_config_->OverflowBucket()) {
    return bucket_index;
  }
  int64_t lower_bound = bucket_config_->BucketFloor(bucket_index);
  int64_t width = (bucket_config_->BucketFloor(bucket_index + 1) - lower_bound);
  double pos = static_cast<double>(val - lower_bound) / width;
  return bucket_index += BernoulliDistribution(gen_, pos).Sample();
}

IntegerSumEstimator::IntegerSumEstimator(const IntegerBuckets& buckets, double p,
                                         const OutOfBoundsStrategy& underflow_strategy,
                                         const OutOfBoundsStrategy& overflow_strategy)
    : p_(p) {
  bucket_config_ = IntegerBucketConfig::CreateFromProto(buckets);
  frequency_estimator_ = std::make_unique<FrequencyEstimator>(bucket_config_->OverflowBucket());
  GetBucketMultipliers(&multipliers_, underflow_strategy, overflow_strategy);
  multiplier_sum_ = GetMultiplierSum(multipliers_);
}

IntegerSumEstimator::IntegerSumEstimator(int64_t min_int, int64_t max_int, uint32_t partitions,
                                         double p, const OutOfBoundsStrategy& underflow_strategy,
                                         const OutOfBoundsStrategy& overflow_strategy)
    : p_(p) {
  int64_t step_size = (max_int - min_int) / partitions;
  auto buckets = MakeIntegerBuckets(min_int, partitions, step_size);
  bucket_config_ = IntegerBucketConfig::CreateFromProto(buckets);
  frequency_estimator_ = std::make_unique<FrequencyEstimator>(bucket_config_->OverflowBucket());
  GetBucketMultipliers(&multipliers_, underflow_strategy, overflow_strategy);
  multiplier_sum_ = GetMultiplierSum(multipliers_);
}

std::tuple<double, uint64_t, uint64_t> IntegerSumEstimator::ComputeSum(
    const std::vector<uint32_t>& encoded_vals) {
  std::vector<uint64_t> frequencies = frequency_estimator_->GetFrequencies(encoded_vals);
  double raw_sum = 0.0;
  for (uint32_t index = 0; index < frequencies.size(); index++) {
    raw_sum += frequencies[index] * multipliers_[index];
  }
  auto underflow_count = frequencies[bucket_config_->UnderflowBucket()];
  auto overflow_count = frequencies[bucket_config_->OverflowBucket()];
  return {DebiasSum(raw_sum, encoded_vals.size()), underflow_count, overflow_count};
}

void IntegerSumEstimator::GetBucketMultipliers(std::vector<double>* multipliers,
                                               const OutOfBoundsStrategy& underflow_strategy,
                                               const OutOfBoundsStrategy& overflow_strategy) {
  uint32_t underflow_index = bucket_config_->UnderflowBucket();
  uint32_t overflow_index = bucket_config_->OverflowBucket();
  multipliers->resize(overflow_index + 1);
  switch (underflow_strategy) {
    case kClamp: {
      (*multipliers)[underflow_index] = bucket_config_->BucketFloor(1);
      break;
    }
    case kDiscard:
    default: {
      (*multipliers)[underflow_index] = 0;
    }
  }
  switch (overflow_strategy) {
    case kClamp: {
      (*multipliers)[overflow_index] = bucket_config_->BucketFloor(overflow_index);
      break;
    }
    case kDiscard:
    default: {
      (*multipliers)[overflow_index] = 0;
    }
  }
  for (uint32_t index = 1; index < overflow_index; index++) {
    (*multipliers)[index] = bucket_config_->BucketFloor(index);
  }
}

double IntegerSumEstimator::DebiasSum(int64_t raw_sum, uint64_t input_size) {
  double coeff = (static_cast<double>(input_size) * p_) / (bucket_config_->OverflowBucket() + 1);
  return static_cast<double>(raw_sum) - (coeff * multiplier_sum_);
}

}  // namespace cobalt
