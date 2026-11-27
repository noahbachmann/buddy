#pragma once
// One ONNX Runtime session plus its input/output names. Single-threaded on purpose:
// the models are tiny and run every 32–80 ms, so thread start-up would cost more than it saves.

#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace buddy::ears {

class OnnxModel {
public:
    OnnxModel(Ort::Env& env, const std::string& path);

    // Runs with inputs in the model's declared order; returns all outputs.
    std::vector<Ort::Value> run(std::vector<Ort::Value>& inputs);

    static const Ort::MemoryInfo& cpu();

private:
    static Ort::SessionOptions options();

    Ort::Session session_;
    std::vector<std::string> input_names_, output_names_;
    std::vector<const char*> input_ptrs_, output_ptrs_;
};

} // namespace buddy::ears
