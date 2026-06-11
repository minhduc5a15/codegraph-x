#include <napi.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "in_memory_graph_engine.hpp"
#include "parallel_parser.hpp"
#include "watchdog.hpp"

std::shared_ptr<ParallelParsingEngine> global_parser = nullptr;
std::shared_ptr<InMemoryGraphEngine> global_engine = nullptr;

class UpdateWorkspaceWorker : public Napi::AsyncWorker {
public:
    UpdateWorkspaceWorker(Napi::Env& env, std::vector<std::string> files)
        : Napi::AsyncWorker(env), files(std::move(files)), promise(Napi::Promise::Deferred::New(env)) {}

    ~UpdateWorkspaceWorker() override = default;

    Napi::Promise GetPromise() const { return promise.Promise(); }

protected:
    void Execute() override {
        if (!global_parser) {
            global_parser = std::make_shared<ParallelParsingEngine>();
        }
        global_parser->execute(files);
        global_parser->build_flat_graph();

        engine = std::make_shared<InMemoryGraphEngine>();

        auto [pool, lookup] = global_parser->take_string_pool();
        engine->take_string_pool(std::move(pool), std::move(lookup));

        const auto temp_nodes = global_parser->take_nodes();
        std::vector<NodeRecord> raw_nodes;
        raw_nodes.reserve(temp_nodes.size());

        for (const auto& tn : temp_nodes) {
            NodeRecord nr{};
            nr.node_id = tn.node_id;
            nr.name_pool_offset = tn.name_offset;
            nr.path_pool_offset = tn.path_offset;
            nr.start_line = tn.start_line;
            nr.end_line = tn.end_line;
            nr.type = tn.type;
            nr.start_column = tn.start_column;
            nr.flags = tn.flags;
            raw_nodes.push_back(nr);
        }

        engine->build_from_raw(std::move(raw_nodes), global_parser->take_edges());
    }

    void OnOK() override {
        Napi::Env env = Env();
        Napi::Object result = Napi::Object::New(env);

        auto create_buffer = [&](const char* key, void* data, size_t bytes) {
            if (bytes == 0) {
                result.Set(key, Napi::ArrayBuffer::New(env, 0));
                return;
            }

            auto* hint = new std::shared_ptr<InMemoryGraphEngine>(engine);

            auto buffer = Napi::ArrayBuffer::New(
                env,
                data,
                bytes,
                [](Napi::Env /*env*/, void* /*data*/, std::shared_ptr<InMemoryGraphEngine>* hint_ptr) {
                    delete hint_ptr;
                },
                hint
            );
            result.Set(key, buffer);
        };

        create_buffer("nodes", engine->get_nodes_data(), engine->get_nodes_bytes());
        create_buffer("offsets", engine->get_offsets_data(), engine->get_offsets_bytes());
        create_buffer("edges", engine->get_edges_data(), engine->get_edges_bytes());
        create_buffer("stringPool", engine->get_string_pool_data(), engine->get_string_pool_bytes());

        create_buffer("nameIndex", engine->get_name_index_data(), engine->get_name_index_bytes());
        create_buffer("shortNameIndex", engine->get_short_name_index_data(), engine->get_short_name_index_bytes());
        create_buffer("pathIndex", engine->get_path_index_data(), engine->get_path_index_bytes());
        create_buffer("incomingOffsets", engine->get_incoming_offsets_data(), engine->get_incoming_offsets_bytes());
        create_buffer("incomingEdges", engine->get_incoming_edges_data(), engine->get_incoming_edges_bytes());

        global_engine = engine;

        promise.Resolve(result);
    }

    void OnError(const Napi::Error& e) override { promise.Reject(e.Value()); }

private:
    std::vector<std::string> files;
    std::shared_ptr<InMemoryGraphEngine> engine;
    Napi::Promise::Deferred promise;
};

Napi::Value UpdateWorkspace(const Napi::CallbackInfo& info) {
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

    auto* worker = new UpdateWorkspaceWorker(env, std::move(files));
    worker->Queue();
    return worker->GetPromise();
}

Napi::Value SetupWatchdog(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    int parent_pid = 0;
    if (info.Length() > 0 && info[0].IsNumber()) {
        parent_pid = info[0].As<Napi::Number>().Int32Value();
    }
    initialize_parent_death_watchdog(parent_pid);
    return env.Undefined();
}

Napi::Value SearchSubstring(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!global_engine) {
        return Napi::Array::New(env, 0);
    }
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected a string query").ThrowAsJavaScriptException();
        return env.Null();
    }
    const std::string query = info[0].As<Napi::String>().Utf8Value();
    const auto results = global_engine->search_substring(query);
    Napi::Array js_results = Napi::Array::New(env, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        js_results.Set(i, Napi::Number::New(env, results[i]));
    }
    return js_results;
}

Napi::Value SearchPathSubstring(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!global_engine) {
        return Napi::Array::New(env, 0);
    }
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected a string query").ThrowAsJavaScriptException();
        return env.Null();
    }
    const std::string query = info[0].As<Napi::String>().Utf8Value();
    const auto results = global_engine->search_path_substring(query);
    Napi::Array js_results = Napi::Array::New(env, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        js_results.Set(i, Napi::Number::New(env, results[i]));
    }
    return js_results;
}

Napi::Value SearchFuzzy(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!global_engine) {
        return Napi::Array::New(env, 0);
    }
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected a string query").ThrowAsJavaScriptException();
        return env.Null();
    }
    const std::string query = info[0].As<Napi::String>().Utf8Value();
    const auto results = global_engine->search_fuzzy(query);
    Napi::Array js_results = Napi::Array::New(env, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        js_results.Set(i, Napi::Number::New(env, results[i]));
    }
    return js_results;
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "UpdateWorkspace"), Napi::Function::New(env, UpdateWorkspace));
    exports.Set(Napi::String::New(env, "SetupWatchdog"), Napi::Function::New(env, SetupWatchdog));
    exports.Set(Napi::String::New(env, "SearchSubstring"), Napi::Function::New(env, SearchSubstring));
    exports.Set(Napi::String::New(env, "SearchPathSubstring"), Napi::Function::New(env, SearchPathSubstring));
    exports.Set(Napi::String::New(env, "SearchFuzzy"), Napi::Function::New(env, SearchFuzzy));
    return exports;
}

NODE_API_MODULE(codegraph_addon, InitAll)
