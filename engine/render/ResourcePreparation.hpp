#pragma once

#include "render/ResourcePreparationSource.hpp"
#include "render/ResourcePreparationTypes.hpp"

namespace rubia::asset
{
class AssetManager;
}

namespace rubia::render
{
// Main-thread port bound to one renderer/cache session. Owns no CPU registry.
// Submit outside command recording. Model composition stays on the CPU: use
// collectModelMeshes() and submit each mesh when adding a model.
class ResourcePreparation
{
  public:
    virtual ~ResourcePreparation() = default;

    // Captures the current immutable version, including dependencies, on the
    // asset owner's thread. Later CPU edits require another explicit request.
    [[nodiscard]] ResourcePreparationResult prepare(const asset::AssetManager& assets,
                                                    ResourceAssetHandle handle);
    [[nodiscard]] virtual ResourcePreparationResult prepare(ResourceAssetSnapshot source) = 0;

    // Ready means resident in the GPU cache. It does not insert scene nodes,
    // rebuild pipelines, or make a not-yet-ready mesh safe to draw.
    [[nodiscard]] virtual ResourcePreparationStatus status(
        ResourcePreparationTicket ticket) const = 0;
    // Cancels this subscriber only; other subscribers may still publish the resource.
    virtual void cancel(ResourcePreparationTicket ticket) = 0;
    // Terminal tickets must be released, including CacheHit and Cancelled tickets.
    // Releasing a ticket does not evict its resident resource.
    virtual void release(ResourcePreparationTicket ticket) = 0;
    virtual void cancelAll() noexcept = 0;

    // Application scheduler calls once per frame, even with no scene/loading task.
    virtual void advance() = 0;
};
} // namespace rubia::render
