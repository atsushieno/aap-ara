#include <jni.h>
#include <aap/android-audio-plugin.h>
#include <aap/ext/plugin-info.h>
#include <array>
#include <algorithm>
#include <cstring>

// A host-injected MIDI port preceding audio must remain untouched by process().
extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparapluginsample_PluginTransportTest_verifyPortIsolation(JNIEnv* env, jobject) {
    aap_host_plugin_info_extension_t extension{};
    extension.get = [](aap_host_plugin_info_extension_t*, AndroidAudioPluginHost*, const char*) {
        aap_plugin_info_t info{};
        info.plugin_id = [](aap_plugin_info_t*) { return "transport-test"; };
        info.get_port_count = [](aap_plugin_info_t*) -> uint32_t { return 4; };
        info.get_port = [](aap_plugin_info_t*, uint32_t index) {
            aap_plugin_info_port_t port{};
            port.context = reinterpret_cast<void*>(static_cast<uintptr_t>(index));
            port.content_type = [](aap_plugin_info_port_t* p) {
                return reinterpret_cast<uintptr_t>(p->context) % 2 ? AAP_CONTENT_TYPE_AUDIO : AAP_CONTENT_TYPE_MIDI2;
            };
            port.direction = [](aap_plugin_info_port_t* p) {
                return reinterpret_cast<uintptr_t>(p->context) == 2 ? AAP_PORT_DIRECTION_INPUT : AAP_PORT_DIRECTION_OUTPUT;
            };
            return port;
        };
        return info;
    };
    AndroidAudioPluginHost host{};
    host.context = &extension;
    host.get_extension = [](AndroidAudioPluginHost* h, const char* uri) -> void* {
        return std::strcmp(uri, AAP_PLUGIN_INFO_EXTENSION_URI) == 0 ? h->context : nullptr;
    };
    alignas(float) std::array<std::array<uint8_t, 64>, 4> planes{};
    for (auto& plane : planes) plane.fill(0xA5);
    aap_buffer_t buffer{};
    buffer.impl = &planes;
    buffer.num_frames = [](aap_buffer_t*) -> int32_t { return 16; };
    buffer.num_ports = [](aap_buffer_t*) -> int32_t { return 4; };
    buffer.get_buffer = [](aap_buffer_t* b, int32_t index) -> void* {
        return (*static_cast<decltype(planes)*>(b->impl))[index].data();
    };
    buffer.get_buffer_size = [](aap_buffer_t*, int32_t) -> int32_t { return 64; };
    auto* factory = GetAndroidAudioPluginFactory();
    auto* plugin = factory->instantiate(factory, "transport-test", &host);
    plugin->prepare(plugin, 48000, &buffer);
    plugin->process(plugin, &buffer, 8, 0);
    factory->release(factory, plugin);
    for (int port = 0; port < 4; ++port) {
        uint8_t expected = port % 2 ? 0 : 0xA5;
        if (!std::all_of(planes[port].begin(), planes[port].end(), [=](uint8_t b) { return b == expected; }))
            return env->NewStringUTF("Audio output or MIDI transport buffer was incorrectly written");
    }
    return env->NewStringUTF("");
}
