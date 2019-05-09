/*
 * AUTOGENERATED - DO NOT EDIT
 *
 * This file is generated from server-decoration.xml
 * To regenerate, run the “refresh-wayland-wrapper” target.
 */

#ifndef MIR_FRONTEND_WAYLAND_SERVER_DECORATION_XML_WRAPPER
#define MIR_FRONTEND_WAYLAND_SERVER_DECORATION_XML_WRAPPER

#include <experimental/optional>

#include "mir/fd.h"
#include <wayland-server-core.h>

namespace mir
{
namespace wayland
{

class ServerDecorationManager
{
public:
    static char const constexpr* interface_name = "org_kde_kwin_server_decoration_manager";
    static int const interface_version = 1;

    static ServerDecorationManager* from(struct wl_resource*);

    ServerDecorationManager(struct wl_display* display, uint32_t max_version);
    virtual ~ServerDecorationManager();

    void send_default_mode_event(struct wl_resource* resource, uint32_t mode) const;

    void destroy_wayland_object(struct wl_resource* resource) const;

    struct wl_global* const global;
    uint32_t const max_version;

    struct Mode
    {
        static uint32_t const None = 0;
        static uint32_t const Client = 1;
        static uint32_t const Server = 2;
    };

    struct Opcode
    {
        static uint32_t const default_mode = 0;
    };

    struct Thunks;

private:
    virtual void bind(struct wl_client* client, struct wl_resource* resource) { (void)client; (void)resource; }

    virtual void create(struct wl_client* client, struct wl_resource* resource, struct wl_resource* id, struct wl_resource* surface) = 0;
};

class ServerDecoration
{
public:
    static char const constexpr* interface_name = "org_kde_kwin_server_decoration";
    static int const interface_version = 1;

    static ServerDecoration* from(struct wl_resource*);

    ServerDecoration(struct wl_resource* resource);
    virtual ~ServerDecoration() = default;

    void send_mode_event(uint32_t mode) const;

    void destroy_wayland_object() const;

    struct wl_client* const client;
    struct wl_resource* const resource;

    struct Mode
    {
        static uint32_t const None = 0;
        static uint32_t const Client = 1;
        static uint32_t const Server = 2;
    };

    struct Opcode
    {
        static uint32_t const mode = 0;
    };

    struct Thunks;

    static bool is_instance(wl_resource* resource);

private:
    virtual void release() = 0;
    virtual void request_mode(uint32_t mode) = 0;
};

}
}

#endif // MIR_FRONTEND_WAYLAND_SERVER_DECORATION_XML_WRAPPER