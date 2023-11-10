#include <algorithm>
#include <cmath>
#include <memory>
#include <opy.hpp>
#include <random>

namespace lib {

namespace utils {
/**
 *  NOTE: Not fast
 */
template <typename T>
class UniformSelector {
 public:
  std::vector<T> items;
  UniformSelector() {}
  void add_item(T item) { items.emplace_back(item); }

  template <typename Rng>
  const T &select(Rng &rng) const {
    std::uniform_int_distribution<> dist(0, items.size() - 1);
    return items[dist(rng)];
  }
};
}  // namespace utils

template <typename Order>
class BolzmanMarkovChain {
  using Real = typename Order::V;
  enum Result {
    Accepted = 0,
    Rejected,
    MinusConnection,
    UnderLimit,
    NotConverged,
  };

 public:
  const int ndim;
  const Real threshold;
  const Real beta;
  const Real scale;

  BolzmanMarkovChain(int ndim, const Real *w, const Real *K, Real threshold,
                     Real beta, Real scale, int seed)
      : ndim(ndim),
        threshold(threshold),
        beta(beta),
        scale(scale),
        evaluator(3000, 1e-4, 1, 10000, ndim),  // NOTE: Default parameter
        w(new(std::align_val_t{64}) Real[ndim]),
        K(new(std::align_val_t{64}) Real[ndim * ndim]),
        _rng(seed) {
    for (int i = 0; i < ndim; i++) {
      for (int j = i + 1; j < ndim; j++) {
        _target_indexs.add_item(i * ndim + j);
      }
    }

    std::copy_n(w, ndim, &this->w[0]);
    std::copy_n(K, ndim * ndim, &this->K[0]);

    K_sum = 0;
    for (int i = 0; i < ndim * ndim; i++) K_sum += K[i];
  }

  Real *connection() { return &K[0]; }
  Real energy() { return K_sum / ndim; }

  void step(int n) {
    for (int i = 0; i < n; i++) step();
  }

  void step() {
    const auto idx = _target_indexs.select(_rng);
    const auto xdi = (idx % ndim) * ndim + (idx / ndim);
    const auto prev_K_idx = K[idx];
    const auto prev_K_sum = K_sum;
    const auto d = std::normal_distribution<>(0, scale)(_rng);

    K[idx] += d;
    K[xdi] += d;
    K_sum += 2 * d;

    const auto result = try_step(idx, d);

    if (result != Result::Accepted) {
      K[idx] = K[xdi] = prev_K_idx;
      K_sum = prev_K_sum;
    }
  }

 private:
  Result try_step(int updated_idx, Real updated_delta) {
    if (K[updated_idx] < 0) {
      return Result::MinusConnection;
    }

    const auto status = evaluator.eval(&K[0], &w[0]);
    if (status != EvalStatus::Ok) {
      return Result::NotConverged;
    }
    const auto R = evaluator.result();
    if (R < threshold) {
      return Result::UnderLimit;
    }

    // dE = E(t+1) - E(t)
    // E = K_sum / ndim
    const auto dE = (2 * updated_delta) / ndim;
    if (dE <= 0 ||
        std::uniform_real_distribution<>(0, 1)(_rng) <= std::exp(-beta * dE)) {
      return Result::Accepted;
    } else {
      return Result::Rejected;
    }
  }

  OrderEvaluatorRK45<Real, Order> evaluator;

  std::unique_ptr<Real[]> w;
  std::unique_ptr<Real[]> K;
  Real K_sum;

  utils::UniformSelector<int> _target_indexs;
  std::mt19937 _rng;
};
}  // namespace lib
