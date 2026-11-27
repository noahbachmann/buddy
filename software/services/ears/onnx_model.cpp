#include "onnx_model.hpp"

#include <stdexcept>

namespace buddy::ears {

Ort::SessionOptions OnnxModel::options() {
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    return opts;
}

OnnxModel::OnnxModel(Ort::Env& env, const std::string& path) : session_(nullptr) {
    try {
        session_ = Ort::Session(env, path.c_str(), options());
    } catch (const Ort::Exception& e) {
        throw std::runtime_error("cannot load model " + path + ": " + e.what());
    }
    Ort::AllocatorWithDefaultOptions alloc;
    for (std::size_t i = 0; i < session_.GetInputCount(); ++i)
        input_names_.emplace_back(session_.GetInputNameAllocated(i, alloc).get());
    for (std::size_t i = 0; i < session_.GetOutputCount(); ++i)
        output_names_.emplace_back(session_.GetOutputNameAllocated(i, alloc).get());
    for (const auto& n : input_names_) input_ptrs_.push_back(n.c_str());
    for (const auto& n : output_names_) output_ptrs_.push_back(n.c_str());
}

std::vector<Ort::Value> OnnxModel::run(std::vector<Ort::Value>& inputs) {
    if (inputs.size() != input_ptrs_.size())
        throw std::logic_error("model expects " + std::to_string(input_ptrs_.size()) + " inputs");
    return session_.Run(Ort::RunOptions{nullptr}, input_ptrs_.data(), inputs.data(), inputs.size(),
                        output_ptrs_.data(), output_ptrs_.size());
}

const Ort::MemoryInfo& OnnxModel::cpu() {
    static const Ort::MemoryInfo info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    return info;
}

} // namespace buddy::ears
