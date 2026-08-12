#pragma once
#include "domain/interface/components/http/HttpController.h"
#include <mutex>

class BesqContext;

namespace web {

/// CRUD + metadata + actions for named profiles and their ench/equip/tag
/// registries. Replaces the old ApiProfiles resource with full REST semantics
/// (web-http-modernize design §6.1/§8.1).
///
/// Every handler that touches _ctx holds _gate FIRST — it guards BesqContext
/// business-resource access, mutually exclusive with the solve worker's
/// snapshot build / format (the solve itself runs lock-free on the snapshot).
class ProfilesController : public HttpController<ProfilesController> {
public:
    using Self = ProfilesController;

    explicit ProfilesController(BesqContext& ctx, std::mutex& gate) : _ctx(ctx), _gate(gate) {}

    static constexpr auto route_defs() {
        return std::array{
            BESQ_ROUTE(Get,    "/api/profiles",                                list),
            BESQ_ROUTE(Post,   "/api/profiles",                                create),
            BESQ_ROUTE(Get,    "/api/profiles/{key}",                          read),
            BESQ_ROUTE(Patch,  "/api/profiles/{key}",                          update),
            BESQ_ROUTE(Delete, "/api/profiles/{key}",                          remove),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/activate",                 activate),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/fork",                     fork),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/merge",                    merge),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/publish",                  publish),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/rename",                   rename),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/enchantments",             listEnch),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/enchantments",             addEnch),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/enchantments/{name}",      readEnch),
            BESQ_ROUTE(Patch,  "/api/profiles/{key}/enchantments/{name}",      updateEnch),
            BESQ_ROUTE(Delete, "/api/profiles/{key}/enchantments/{name}",      removeEnch),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/equipments",               listEquip),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/equipments",               addEquip),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/equipments/{name}",        readEquip),
            BESQ_ROUTE(Patch,  "/api/profiles/{key}/equipments/{name}",        updateEquip),
            BESQ_ROUTE(Delete, "/api/profiles/{key}/equipments/{name}",        removeEquip),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/tags",                     listTag),
            BESQ_ROUTE(Post,   "/api/profiles/{key}/tags",                     addTag),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/tags/{name}",              readTag),
            BESQ_ROUTE(Patch,  "/api/profiles/{key}/tags/{name}",              updateTag),
            BESQ_ROUTE(Delete, "/api/profiles/{key}/tags/{name}",              removeTag),
            BESQ_ROUTE(Get,    "/api/profiles/{key}/enchantables/{item}",      listEnchantables),
        };
    }

    // ── Profile collection / item ──
    Response list(const HttpRequest&);
    Response create(const HttpRequest&, const PathParams&, const Json&);
    Response read(const HttpRequest&, const PathParams&);
    Response update(const HttpRequest&, const PathParams&, const Json&);
    Response remove(const HttpRequest&, const PathParams&);
    Response activate(const HttpRequest&, const PathParams&);
    Response fork(const HttpRequest&, const PathParams&, const Json&);
    Response merge(const HttpRequest&, const PathParams&, const Json&);
    Response publish(const HttpRequest&, const PathParams&, const Json&);
    Response rename(const HttpRequest&, const PathParams&, const Json&);

    // ── Enchantment sub-resource ──
    Response listEnch(const HttpRequest&, const PathParams&);
    Response addEnch(const HttpRequest&, const PathParams&, const Json&);
    Response readEnch(const HttpRequest&, const PathParams&);
    Response updateEnch(const HttpRequest&, const PathParams&, const Json&);
    Response removeEnch(const HttpRequest&, const PathParams&);

    // ── Equipment sub-resource ──
    Response listEquip(const HttpRequest&, const PathParams&);
    Response addEquip(const HttpRequest&, const PathParams&, const Json&);
    Response readEquip(const HttpRequest&, const PathParams&);
    Response updateEquip(const HttpRequest&, const PathParams&, const Json&);
    Response removeEquip(const HttpRequest&, const PathParams&);

    // ── Tag sub-resource ──
    Response listTag(const HttpRequest&, const PathParams&);
    Response addTag(const HttpRequest&, const PathParams&, const Json&);
    Response readTag(const HttpRequest&, const PathParams&);
    Response updateTag(const HttpRequest&, const PathParams&, const Json&);
    Response removeTag(const HttpRequest&, const PathParams&);

    // ── Enchantables sub-resource ──
    /// Enchantments applicable to an item (effective-view tag resolution +
    /// platform gate, mirroring solve).
    Response listEnchantables(const HttpRequest&, const PathParams&);

private:
    BesqContext& _ctx;
    std::mutex& _gate;
};

} // namespace web
