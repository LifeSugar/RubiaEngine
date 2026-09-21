#include "App.hpp"

#include "render/SceneResourcePreparation.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rubia::editor
{

void App::startContentLoading()
{
    if (contentLoadStatus_.state != ContentLoadState::Idle &&
        contentLoadStatus_.state != ContentLoadState::Failed)
    {
        return;
    }
    discardContentLoading();
    contentLoadStatus_ = {ContentLoadState::Preparing,
        "Reading model and decoding textures..."};
    std::clog << "[Content] " << contentLoadStatus_.message
        << " " << contentLoadConfig_.modelPath.string() << '\n';
    try
    {
        contentLoadCancelled_ = std::make_shared<std::atomic<bool>>(false);
        contentLoadFuture_ = std::async(std::launch::async,
            [config = contentLoadConfig_, cancelled = contentLoadCancelled_]()
            {
                const auto checkpoint = [&cancelled]
                {
                    if (cancelled->load(std::memory_order_relaxed))
                    {
                        throw std::runtime_error("Content loading cancelled");
                    }
                };
                auto prepared = std::make_unique<PreparedContent>();
                prepared->content = DemoContentLoader::load(
                    prepared->assets, prepared->scene, config,
                    &prepared->textureImports, checkpoint);
                checkpoint();
                return prepared;
            });
    }
    catch (const std::exception& error)
    {
        contentLoadStatus_ = {ContentLoadState::Failed, error.what()};
        std::cerr << "[Content] " << error.what() << '\n';
    }
}

void App::updateContentLoading()
{
    using namespace std::chrono_literals;
    try
    {
        if (contentLoadStatus_.state == ContentLoadState::Preparing)
        {
            if (contentLoadFuture_.wait_for(0ms) != std::future_status::ready)
            {
                return;
            }
            preparedContent_ = contentLoadFuture_.get();
            auto request = render::makeSceneResourceRequest(preparedContent_->assets,
                {preparedContent_->content.model}, preparedContent_->content.materialTemplate,
                preparedContent_->content.presentProgram);
            renderer.beginScenePreparation(renderAssets, std::move(request));
            const auto status = renderer.scenePreparationStatus();
            if (status.state == render::ScenePreparationState::Failed)
            {
                throw std::runtime_error(status.error);
            }
            contentLoadStatus_ = {ContentLoadState::Uploading,
                "Preparing scene resources...", status.completed, status.total};
            std::clog << "[Content] " << contentLoadStatus_.message
                << " 0/" << contentLoadStatus_.total << '\n';
            return;
        }
        if (contentLoadStatus_.state == ContentLoadState::Uploading ||
            contentLoadStatus_.state == ContentLoadState::Finalizing)
        {
            const auto status = renderer.scenePreparationStatus();
            if (status.state == render::ScenePreparationState::Failed)
            {
                throw std::runtime_error(status.error);
            }
            if (status.state == render::ScenePreparationState::Cancelled)
            {
                throw std::runtime_error("Scene resource preparation cancelled");
            }

            const std::size_t previousCompleted = contentLoadStatus_.completed;
            contentLoadStatus_.completed = status.completed;
            contentLoadStatus_.total = status.total;
            if (status.total != 0 && status.completed * 10 / status.total >
                previousCompleted * 10 / status.total)
            {
                std::clog << "[Content] Prepared " << status.completed
                    << '/' << status.total << " root resources ("
                    << status.completed * 100 / status.total << "%)\n";
            }
            if (status.state == render::ScenePreparationState::PreparingPipelines &&
                contentLoadStatus_.state != ContentLoadState::Finalizing)
            {
                contentLoadStatus_.state = ContentLoadState::Finalizing;
                contentLoadStatus_.message = "Preparing scene rendering...";
                std::clog << "[Content] " << contentLoadStatus_.message << '\n';
            }
            if (status.state != render::ScenePreparationState::Ready)
            {
                return;
            }

            // No partially imported assets are exposed to inspectors. Handles
            // remain valid because the complete registries move together.
            static_assert(std::is_nothrow_move_assignable_v<asset::AssetManager>);
            static_assert(std::is_nothrow_move_assignable_v<scene::Scene>);
            static_assert(std::is_nothrow_move_assignable_v<importer::texture::TextureImportRegistry>);
            renderer.activatePreparedScene();
            assetManager = std::move(preparedContent_->assets);
            scene = std::move(preparedContent_->scene);
            textureImports = std::move(preparedContent_->textureImports);
            demoContent = preparedContent_->content;
            discardContentLoading();
            contentLoadStatus_ = {ContentLoadState::Ready, "Scene ready"};
            std::clog << "[Content] Scene ready\n";
        }
    }
    catch (const std::exception& error)
    {
        discardContentLoading();
        contentLoadStatus_ = {ContentLoadState::Failed, error.what()};
        std::cerr << "[Content] " << error.what() << '\n';
    }
}

void App::discardContentLoading() noexcept
{
    if (contentLoadCancelled_)
    {
        contentLoadCancelled_->store(true, std::memory_order_relaxed);
    }
    // Join the worker before GUI log capture and application resources die.
    // Parsing/decoding a single source is allowed to finish before cancellation.
    if (contentLoadFuture_.valid())
    {
        try { static_cast<void>(contentLoadFuture_.get()); }
        catch (...) {}
    }
    renderer.cancelScenePreparation();
    preparedContent_.reset();
    contentLoadCancelled_.reset();
}

} // namespace rubia::editor
