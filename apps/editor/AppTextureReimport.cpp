#include "App.hpp"

#include "texture/KtxTextureCooker.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace rubia::editor
{
namespace
{

std::atomic<uint64_t> nextTransactionNonce{1};

[[nodiscard]] std::filesystem::path transactionSibling(
    const std::filesystem::path& destination,
    const char* role,
    uint64_t nonce)
{
    return destination.parent_path() /
        (destination.stem().string() + "." + role + "." +
         std::to_string(nonce) + ".ktx2");
}

/// Installs a staged KTX2 while retaining the previous file until the CPU/GPU
/// commit succeeds. Destruction rolls the disk change back on exceptions.
class CookedFileTransaction final
{
public:
    CookedFileTransaction(
        std::filesystem::path destination,
        std::filesystem::path staged)
        : destination_(std::move(destination)),
          staged_(std::move(staged))
    {
        const uint64_t nonce = nextTransactionNonce.fetch_add(1);
        backup_ = transactionSibling(destination_, "backup", nonce);
    }

    ~CookedFileTransaction()
    {
        rollback();
        std::error_code ignored;
        std::filesystem::remove(staged_, ignored);
    }

    CookedFileTransaction(const CookedFileTransaction&) = delete;
    CookedFileTransaction& operator=(const CookedFileTransaction&) = delete;

    [[nodiscard]] const std::filesystem::path& stagedPath() const noexcept
    {
        return staged_;
    }

    void install()
    {
        if (installed_)
        {
            throw std::logic_error("cooked texture file is already installed");
        }
        if (!std::filesystem::is_regular_file(staged_))
        {
            throw std::runtime_error(
                "staged KTX2 file is absent: " + staged_.string());
        }

        if (!destination_.parent_path().empty())
        {
            std::filesystem::create_directories(destination_.parent_path());
        }
        hadPrevious_ = std::filesystem::exists(destination_);
        if (hadPrevious_)
        {
            std::filesystem::rename(destination_, backup_);
        }

        try
        {
            std::filesystem::rename(staged_, destination_);
            installed_ = true;
        }
        catch (...)
        {
            if (hadPrevious_)
            {
                std::error_code ignored;
                std::filesystem::rename(backup_, destination_, ignored);
            }
            throw;
        }
    }

    void finish() noexcept
    {
        if (hadPrevious_)
        {
            std::error_code ignored;
            std::filesystem::remove(backup_, ignored);
        }
        installed_ = false;
        finished_ = true;
    }

private:
    void rollback() noexcept
    {
        if (!installed_ || finished_)
        {
            return;
        }

        std::error_code ignored;
        std::filesystem::remove(destination_, ignored);
        if (hadPrevious_)
        {
            ignored.clear();
            std::filesystem::rename(backup_, destination_, ignored);
        }
        installed_ = false;
    }

    std::filesystem::path destination_;
    std::filesystem::path staged_;
    std::filesystem::path backup_;
    bool hadPrevious_ = false;
    bool installed_ = false;
    bool finished_ = false;
};

[[nodiscard]] importer::texture::KtxTextureCooker::Request makeCookRequest(
    const importer::texture::TextureImportRecord& record,
    const importer::texture::TextureImportSettings& settings,
    const std::filesystem::path& stagedPath)
{
    importer::texture::KtxTextureCooker::Request result{};
    result.inputPath = record.sourcePath;
    result.outputPath = stagedPath;
    result.colorSpace = settings.colorSpace;
    result.generateMipmaps = settings.generateMipmaps;
    result.mipFilter = settings.mipFilter;
    result.mipEdgeMode = settings.mipEdgeMode;
    result.basis = settings.basis;
    result.zstdLevel = settings.zstdLevel;
    return result;
}

} // namespace

void App::processPendingTextureReimport()
{
    using namespace std::chrono_literals;

    if (textureReimportFuture_.valid())
    {
        if (textureReimportFuture_.wait_for(0ms) !=
            std::future_status::ready)
        {
            return;
        }

        const asset::TextureAssetHandle completedTexture =
            activeTextureReimport_;
        const std::filesystem::path completedStagedPath =
            activeTextureReimportStagedPath_;
        activeTextureReimport_ = {};
        try
        {
            PreparedTextureReimport prepared =
                textureReimportFuture_.get();
            activeTextureReimportStagedPath_.clear();
            if (!prepared.error.empty())
            {
                textureImports.markFailed(
                    prepared.request.texture,
                    std::move(prepared.error));
                return;
            }

            try
            {
                if (!assetManager.contains(prepared.request.texture) ||
                    renderAssets.tryTexture(prepared.request.texture) == nullptr)
                {
                    throw std::invalid_argument(
                        "reimport target is absent from the CPU or GPU asset cache");
                }

                CookedFileTransaction cookedFile(
                    prepared.record.cookedPath,
                    prepared.stagedPath);

                // Only this short commit phase remains synchronous. CPU image
                // decode, Basis encoding, KTX2 IO, and Basis transcoding have
                // already completed on the worker thread.
                renderer.waitIdle();
                const rhi::vulkan::Device& device = vulkanContext.device();
                auto replacementAsset =
                    std::make_shared<asset::TextureAsset>(std::move(prepared.replacementAsset));
                rhi::vulkan::GpuTexture replacementGpu =
                    renderer.uploadTextureAndWait(replacementAsset);

                cookedFile.install();
                guiRenderBridge.invalidatePreview(
                    prepared.request.texture);

                rhi::vulkan::GpuTexture previousGpu =
                    renderAssets.commitTextureReplacement(
                        device,
                        assetManager,
                        prepared.request.texture,
                        std::move(replacementGpu));
                try
                {
                    asset::TextureAsset previousAsset = assetManager.replaceTexture(
                        prepared.request.texture, std::move(*replacementAsset));
                    static_cast<void>(previousAsset);
                }
                catch (...)
                {
                    rhi::vulkan::GpuTexture failedReplacement =
                        renderAssets.commitTextureReplacement(
                            device,
                            assetManager,
                            prepared.request.texture,
                            std::move(previousGpu));
                    static_cast<void>(failedReplacement);
                    throw;
                }

                textureImports.markSucceeded(
                    prepared.request.texture,
                    prepared.request.settings);
                cookedFile.finish();
                std::clog
                    << "[Assets] Reimported texture "
                    << prepared.record.sourcePath.string()
                    << " -> " << prepared.record.cookedPath.string()
                    << '\n';
            }
            catch (const std::exception& error)
            {
                textureImports.markFailed(
                    prepared.request.texture,
                    error.what());
                std::cerr
                    << "[Assets] Texture reimport commit failed: "
                    << error.what() << '\n';
            }
        }
        catch (const std::exception& error)
        {
            std::error_code ignored;
            std::filesystem::remove(completedStagedPath, ignored);
            activeTextureReimportStagedPath_.clear();
            textureImports.markFailed(completedTexture, error.what());
            std::cerr
                << "[Assets] Texture reimport worker failed: "
                << error.what() << '\n';
        }
    }

    if (pendingTextureReimports_.empty())
    {
        return;
    }

    importer::texture::TextureReimportRequest request =
        std::move(pendingTextureReimports_.front());
    pendingTextureReimports_.pop_front();
    const asset::TextureAssetHandle requestedTexture = request.texture;

    const importer::texture::TextureImportRecord* storedRecord =
        textureImports.find(request.texture);
    if (storedRecord == nullptr)
    {
        return;
    }
    importer::texture::TextureImportRecord record = *storedRecord;

    try
    {
        if (record.reimporting ||
            !assetManager.contains(request.texture) ||
            renderAssets.tryTexture(request.texture) == nullptr)
        {
            throw std::invalid_argument(
                "reimport target is busy or absent from the CPU/GPU asset cache");
        }

        importer::texture::KtxTextureImporter::CreateInfo importInfo{};
        importInfo.name = assetManager.texture(request.texture).name();
        importInfo.sampler = assetManager.texture(request.texture).sampler();
        importInfo.transcodeFormat = request.settings.transcodeFormat;
        importInfo.highQuality = request.settings.highQualityTranscode;

        const uint64_t nonce = nextTransactionNonce.fetch_add(1);
        const std::filesystem::path stagedPath = transactionSibling(
            record.cookedPath,
            "reimport",
            nonce);

        textureImports.markStarted(request.texture);
        activeTextureReimport_ = request.texture;
        activeTextureReimportStagedPath_ = stagedPath;
        textureReimportFuture_ = std::async(
            std::launch::async,
            [request = std::move(request),
             record = std::move(record),
             importInfo = std::move(importInfo),
             stagedPath]() mutable
            {
                PreparedTextureReimport result{};
                result.request = std::move(request);
                result.record = std::move(record);
                result.stagedPath = stagedPath;
                try
                {
                    result.replacementAsset = asset::TextureAsset(
                        importer::texture::KtxTextureCooker{}.cookAndImport(
                            makeCookRequest(
                                result.record,
                                result.request.settings,
                                result.stagedPath),
                            importInfo));
                }
                catch (const std::exception& error)
                {
                    result.error = error.what();
                }
                catch (...)
                {
                    result.error =
                        "unknown exception while cooking or importing KTX2";
                }

                if (!result.error.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(result.stagedPath, ignored);
                }
                return result;
            });
    }
    catch (const std::exception& error)
    {
        activeTextureReimport_ = {};
        activeTextureReimportStagedPath_.clear();
        textureImports.markFailed(requestedTexture, error.what());
        std::cerr
            << "[Assets] Failed to start texture reimport: "
            << error.what() << '\n';
    }
}

void App::discardTextureReimport() noexcept
{
    pendingTextureReimports_.clear();
    if (textureReimportFuture_.valid())
    {
        try
        {
            PreparedTextureReimport prepared =
                textureReimportFuture_.get();
            std::error_code ignored;
            std::filesystem::remove(prepared.stagedPath, ignored);
        }
        catch (...)
        {
        }
    }

    if (!activeTextureReimportStagedPath_.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(
            activeTextureReimportStagedPath_,
            ignored);
    }
    activeTextureReimportStagedPath_.clear();
    activeTextureReimport_ = {};
}

} // namespace rubia::editor
