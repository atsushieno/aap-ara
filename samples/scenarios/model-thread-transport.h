#pragma once
#include <aap/ara/internal/wire.h>
#include <future>
#include <queue>
#include <mutex>
#include <thread>

// Binder reverse calls arrive on Binder threads. While an outbound call waits,
// pump those calls on TestHost's creation thread, as the ARA contract requires.
class ModelThreadTransport {
    std::thread::id owner = std::this_thread::get_id();
    std::mutex mutex;
    std::condition_variable ready;
    std::queue<std::function<void()>> callbacks;
    std::queue<aap_ara_content_update_t> contentUpdates;
public:
    aap_ara_extension_t wire{};
    aap_ara_extension_t* remote;
    aap_ara_host_extension_t reverse{};
    aap_ara_host_extension_t* services{};
    template<class F> auto outbound(F action) -> decltype(action()) {
        auto result = std::async(std::launch::async, [&, action] {
            struct Signal { ModelThreadTransport& t; ~Signal() { t.ready.notify_one(); } } signal{*this};
            return action();
        });
        std::unique_lock<std::mutex> lock(mutex);
        while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            if (callbacks.empty()) { ready.wait_for(lock, std::chrono::milliseconds(1)); continue; }
            auto callback = std::move(callbacks.front()); callbacks.pop();
            lock.unlock(); callback(); lock.lock();
        }
        lock.unlock();
        // Content notifications may synchronously query the plug-in (the upstream
        // logger does). Deliver them after the outbound command has returned so
        // the plug-in model thread is free, but before notifyModelUpdates exits.
        auto deliver = [&] {
            for (;;) {
                std::unique_lock<std::mutex> guard(mutex);
                if (contentUpdates.empty()) break;
                auto update = contentUpdates.front(); contentUpdates.pop();
                guard.unlock();
                services->notify_content_changed(services, nullptr, &update);
            }
        };
        if constexpr (std::is_void_v<decltype(action())>) {
            result.get(); deliver();
        } else {
            auto value = result.get(); deliver(); return value;
        }
    }
    template<class F> auto incoming(F action) -> decltype(action()) {
        if (std::this_thread::get_id() == owner) return action();
        auto task = std::make_shared<std::packaged_task<decltype(action())()>>(action);
        auto result = task->get_future();
        { std::lock_guard<std::mutex> lock(mutex); callbacks.push([task] { (*task)(); }); }
        ready.notify_one();
        return result.get();
    }
    explicit ModelThreadTransport(aap_ara_extension_t* remote);
    void attach(aap_ara_host_extension_t* host) {
        services = host;
        reverse = {this,
            [](aap_ara_host_extension_t* e, AndroidAudioPluginHost* h, aap_ara_host_capability_t* c) {
                auto& s = *static_cast<ModelThreadTransport*>(e->aapxs_context);
                s.incoming([&] { s.services->get_host_capability(s.services, h, c); });
            },
            [](aap_ara_host_extension_t* e, AndroidAudioPluginHost* h, int64_t id, const aap_ara_audio_source_sample_range_t* r, aap_ara_audio_source_samples_buffer_t* b) {
                auto& s = *static_cast<ModelThreadTransport*>(e->aapxs_context);
                s.incoming([&] { s.services->read_audio_source_samples(s.services, h, id, r, b); });
            },
            [](aap_ara_host_extension_t* e, AndroidAudioPluginHost* h, const aap_ara_content_update_t* u) {
                auto& s = *static_cast<ModelThreadTransport*>(e->aapxs_context);
                std::lock_guard<std::mutex> lock(s.mutex);
                s.contentUpdates.push(*u);
            },
            [](aap_ara_host_extension_t* e, AndroidAudioPluginHost* h, void* data, uint32_t size) {
                auto& s = *static_cast<ModelThreadTransport*>(e->aapxs_context);
                return s.incoming([&] { return s.services->sdk_command(s.services, h, data, size); });
            }};
    }
};
template<auto Member> struct ModelThreadForward;
template<class R, class... Args, R (*aap_ara_extension_t::*Member)(aap_ara_extension_t*, Args...)>
struct ModelThreadForward<Member> {
    static R call(aap_ara_extension_t* e, Args... args) {
        auto& s = *static_cast<ModelThreadTransport*>(e->aapxs_context);
        return s.outbound([&] { return (s.remote->*Member)(s.remote, args...); });
    }
};
inline ModelThreadTransport::ModelThreadTransport(aap_ara_extension_t* remote) : remote(remote) {
    wire.aapxs_context = this;
    wire.get_factory_capability = ModelThreadForward<&aap_ara_extension_t::get_factory_capability>::call;
    wire.begin_model_update = ModelThreadForward<&aap_ara_extension_t::begin_model_update>::call;
    wire.end_model_update = ModelThreadForward<&aap_ara_extension_t::end_model_update>::call;
    wire.create_document = ModelThreadForward<&aap_ara_extension_t::create_document>::call;
    wire.update_document_properties = ModelThreadForward<&aap_ara_extension_t::update_document_properties>::call;
    wire.destroy_document = ModelThreadForward<&aap_ara_extension_t::destroy_document>::call;
    wire.create_musical_context = ModelThreadForward<&aap_ara_extension_t::create_musical_context>::call;
    wire.update_musical_context_properties = ModelThreadForward<&aap_ara_extension_t::update_musical_context_properties>::call;
    wire.destroy_musical_context = ModelThreadForward<&aap_ara_extension_t::destroy_musical_context>::call;
    wire.create_region_sequence = ModelThreadForward<&aap_ara_extension_t::create_region_sequence>::call;
    wire.update_region_sequence_properties = ModelThreadForward<&aap_ara_extension_t::update_region_sequence_properties>::call;
    wire.destroy_region_sequence = ModelThreadForward<&aap_ara_extension_t::destroy_region_sequence>::call;
    wire.create_audio_source = ModelThreadForward<&aap_ara_extension_t::create_audio_source>::call;
    wire.update_audio_source_properties = ModelThreadForward<&aap_ara_extension_t::update_audio_source_properties>::call;
    wire.enable_audio_source_samples_access = ModelThreadForward<&aap_ara_extension_t::enable_audio_source_samples_access>::call;
    wire.notify_audio_source_content_changed = ModelThreadForward<&aap_ara_extension_t::notify_audio_source_content_changed>::call;
    wire.destroy_audio_source = ModelThreadForward<&aap_ara_extension_t::destroy_audio_source>::call;
    wire.create_audio_modification = ModelThreadForward<&aap_ara_extension_t::create_audio_modification>::call;
    wire.update_audio_modification_properties = ModelThreadForward<&aap_ara_extension_t::update_audio_modification_properties>::call;
    wire.destroy_audio_modification = ModelThreadForward<&aap_ara_extension_t::destroy_audio_modification>::call;
    wire.create_playback_region = ModelThreadForward<&aap_ara_extension_t::create_playback_region>::call;
    wire.update_playback_region_properties = ModelThreadForward<&aap_ara_extension_t::update_playback_region_properties>::call;
    wire.destroy_playback_region = ModelThreadForward<&aap_ara_extension_t::destroy_playback_region>::call;
    wire.store_audio_modification_state = ModelThreadForward<&aap_ara_extension_t::store_audio_modification_state>::call;
    wire.restore_audio_modification_state = ModelThreadForward<&aap_ara_extension_t::restore_audio_modification_state>::call;
    wire.notify_selection = ModelThreadForward<&aap_ara_extension_t::notify_selection>::call;
    wire.sdk_command = ModelThreadForward<&aap_ara_extension_t::sdk_command>::call;
}
