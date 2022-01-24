/*
 * Copyright (c) NVIDIA
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <algorithm>

#include <schedulers/detail/graph/consumer.hpp>
#include <schedulers/detail/graph/graph_instance.hpp>
#include <schedulers/detail/storage.hpp>

enum class device_type
{
  host,
  device
};

#ifdef _NVHPC_CUDA
#include <nv/target>

__host__ __device__ inline device_type get_device_type()
{
  if target (nv::target::is_host)
  {
    return device_type::host;
  }
  else
  {
    return device_type::device;
  }
}
#elif defined(__clang__) && defined(__CUDA__)
__host__ inline device_type get_device_type() { return device_type::host; }
__device__ inline device_type get_device_type() { return device_type::device; }
#endif

inline __host__ __device__ bool is_on_gpu()
{
  return get_device_type() == device_type::device;
}

template <int N = 1>
class flags_storage_t
{
  static_assert(N > 0);

  int *flags_{};

public:
  class flags_t
  {
    int *flags_{};

    flags_t(int *flags)
        : flags_(flags)
    {}

  public:
    __device__ void set(int i = 0) const { atomicAdd(flags_ + i, 1); }

    friend flags_storage_t;
  };

  flags_storage_t(const flags_storage_t &) = delete;
  flags_storage_t(flags_storage_t &&) = delete;

  void operator()(const flags_storage_t &) = delete;
  void operator()(flags_storage_t &&) = delete;

  flags_t get() { return {flags_}; }

  flags_storage_t()
  {
    cudaMalloc(&flags_, sizeof(int) * N);
    cudaMemset(flags_, 0, sizeof(int) * N);
  }

  ~flags_storage_t()
  {
    cudaFree(flags_);
    flags_ = nullptr;
  }

  bool all_set_once()
  {
    int flags[N];
    cudaMemcpy(flags, flags_, sizeof(int) * N, cudaMemcpyDeviceToHost);

    return std::count(std::begin(flags), std::end(flags), 1) == N;
  }

  bool all_unset() { return !all_set_once(); }
};

class receiver_tracer_t
{
  struct state_t
  {
    cudaGraph_t graph_{};

    std::size_t set_value_was_called_{};
    std::size_t set_stopped_was_called_{};
    std::size_t set_error_was_called_{};

    std::size_t num_nodes_{};
    std::size_t num_edges_{};

    state_t() { cudaGraphCreate(&graph_, 0); }

    ~state_t() { cudaGraphDestroy(graph_); }
  };

  state_t state_{};

public:
  struct receiver_t
  {
    state_t &state_;

    receiver_t(state_t &state)
        : state_(state)
    {}

    friend void tag_invoke(std::execution::set_value_t,
                           receiver_t &&self,
                           std::span<cudaGraphNode_t>) noexcept
    {
      cudaGraph_t graph = self.state_.graph_;
      cudaGraphGetNodes(graph, nullptr, &self.state_.num_nodes_);
      cudaGraphGetEdges(graph, nullptr, nullptr, &self.state_.num_edges_);

      self.state_.set_value_was_called_++;
    }

    friend void tag_invoke(std::execution::set_stopped_t,
                           receiver_t &&self) noexcept
    {
      self.state_.set_stopped_was_called_++;
    }

    friend void tag_invoke(std::execution::set_error_t,
                           receiver_t &&self,
                           std::exception_ptr) noexcept
    {
      self.state_.set_error_was_called_++;
    }

    [[nodiscard]] example::cuda::graph::detail::graph_info_t
    graph() const noexcept
    {
      return {state_.graph_};
    }

    [[nodiscard]] example::cuda::graph::detail::sink_consumer_t
    get_consumer() const noexcept
    {
      return {};
    }

    friend std::byte *tag_invoke(example::cuda::get_storage_t,
                                 const receiver_t &self) noexcept
    {
      return nullptr;
    }

    static constexpr bool is_cuda_graph_api = true;
  };

  receiver_t get() { return {state_}; }

  [[nodiscard]] bool set_value_was_called() const
  {
    return state_.set_value_was_called_ > 0;
  }
  [[nodiscard]] bool set_stopped_was_called() const
  {
    return state_.set_stopped_was_called_ > 0;
  }
  [[nodiscard]] bool set_error_was_called() const
  {
    return state_.set_error_was_called_ > 0;
  }
  [[nodiscard]] bool set_value_was_called_once() const
  {
    return state_.set_value_was_called_ == 1;
  }
  [[nodiscard]] bool set_stopped_was_called_once() const
  {
    return state_.set_stopped_was_called_ == 1;
  }
  [[nodiscard]] bool set_error_was_called_once() const
  {
    return state_.set_error_was_called_ == 1;
  }

  [[nodiscard]] std::size_t num_nodes() const { return state_.num_nodes_; }
  [[nodiscard]] std::size_t num_edges() const { return state_.num_edges_; }
};
