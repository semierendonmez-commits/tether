// The handful of JACK calls tether-audio needs, loaded at run time from libjack.so.0. No JACK
// headers at build time, so the binary cross-builds anywhere and runs on any norns image.
#pragma once

#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>
#include <type_traits>

namespace jackapi
{

using client_t = void;
using port_t = void;
using nframes_t = uint32_t;
using sample_t = float;

constexpr unsigned long PortIsInput = 0x1, PortIsOutput = 0x2;
constexpr int NoStartServer = 0x01;
constexpr const char* AudioType = "32 bit float mono audio";

struct Api
{
    void* lib = nullptr;
    client_t* (*client_open) (const char*, int, int*, ...) = nullptr;
    int (*client_close) (client_t*) = nullptr;
    port_t* (*port_register) (client_t*, const char*, const char*, unsigned long, unsigned long) = nullptr;
    int (*set_process_callback) (client_t*, int (*) (nframes_t, void*), void*) = nullptr;
    int (*set_xrun_callback) (client_t*, int (*) (void*), void*) = nullptr;
    void (*on_shutdown) (client_t*, void (*) (void*), void*) = nullptr;
    int (*activate) (client_t*) = nullptr;
    int (*deactivate) (client_t*) = nullptr;
    nframes_t (*get_sample_rate) (client_t*) = nullptr;
    nframes_t (*get_buffer_size) (client_t*) = nullptr;
    void* (*port_get_buffer) (port_t*, nframes_t) = nullptr;
    int (*connect) (client_t*, const char*, const char*) = nullptr;
    port_t* (*port_by_name) (client_t*, const char*) = nullptr;
    int (*port_connected) (const port_t*) = nullptr;
    int (*port_connected_to) (const port_t*, const char*) = nullptr;
    const char* (*port_name) (const port_t*) = nullptr;

    bool load()
    {
        for (const char* n : { "libjack.so.0", "libjack.so" })
            if ((lib = dlopen (n, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
        if (lib == nullptr) { std::fprintf (stderr, "tether-audio: no libjack (%s)\n", dlerror()); return false; }
        bool ok = true;
        auto get = [&] (auto& fn, const char* name)
        {
            fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (dlsym (lib, name));
            if (fn == nullptr) { std::fprintf (stderr, "tether-audio: libjack has no %s\n", name); ok = false; }
        };
        get (client_open, "jack_client_open");
        get (client_close, "jack_client_close");
        get (port_register, "jack_port_register");
        get (set_process_callback, "jack_set_process_callback");
        get (set_xrun_callback, "jack_set_xrun_callback");
        get (on_shutdown, "jack_on_shutdown");
        get (activate, "jack_activate");
        get (deactivate, "jack_deactivate");
        get (get_sample_rate, "jack_get_sample_rate");
        get (get_buffer_size, "jack_get_buffer_size");
        get (port_get_buffer, "jack_port_get_buffer");
        get (connect, "jack_connect");
        get (port_by_name, "jack_port_by_name");
        get (port_connected, "jack_port_connected");
        get (port_connected_to, "jack_port_connected_to");
        get (port_name, "jack_port_name");
        return ok;
    }
};

} // namespace jackapi
