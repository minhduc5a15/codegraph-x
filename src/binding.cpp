#include <napi.h>
#include <vector>
#include <string>
#include <memory>
#include "InMemoryGraphEngine.hpp"
#include "parallel_parser.hpp"

Napi::Value ParseWorkspace(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected an array of strings").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array files_js = info[0].As<Napi::Array>();
    std::vector<std::string> files;
    for (uint32_t i = 0; i < files_js.Length(); ++i) {
        files.push_back(files_js.Get(i).As<Napi::String>().Utf8Value());
    }

    ParallelParsingEngine parser;
    parser.execute(files);

    auto engine = std::make_shared<InMemoryGraphEngine>();
    engine->build_from_raw(parser.take_nodes(), parser.take_edges());

    Napi::Object result = Napi::Object::New(env);
    
    auto create_buffer = [&](const char* key, void* data, size_t bytes) {
        if (bytes == 0) {
            result.Set(key, Napi::ArrayBuffer::New(env, 0));
            return;
        }

        // Cấp phát một con trỏ giữ shared_ptr để truyền vào Finalizer
        auto* hint = new std::shared_ptr<InMemoryGraphEngine>(engine);
        
        auto buffer = Napi::ArrayBuffer::New(env, data, bytes, 
            [](Napi::Env /*env*/, void* /*data*/, std::shared_ptr<InMemoryGraphEngine>* hint_ptr) {
                delete hint_ptr; // Giảm ref count. Bằng 0 sẽ tự hủy Engine.
            }, 
            hint
        );
        result.Set(key, buffer);
    };

    create_buffer("nodes", engine->get_nodes_data(), engine->get_nodes_bytes());
    create_buffer("offsets", engine->get_offsets_data(), engine->get_offsets_bytes());
    create_buffer("edges", engine->get_edges_data(), engine->get_edges_bytes());
    create_buffer("stringPool", engine->get_string_pool_data(), engine->get_string_pool_bytes());

    return result;
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "ParseWorkspace"), Napi::Function::New(env, ParseWorkspace));
    return exports;
}

NODE_API_MODULE(codegraph_addon, InitAll)
