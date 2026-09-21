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

[[nodiscard]] std::filesystem::path transactionSibling(const std::filesystem::path &destination,
                                                       const char *role, uint64_t nonce)
{
    return destination.parent_path() /
           (destination.stem().string() + "." + role + "." + std::to_string(nonce) + ".ktx2");
}

/// Installs a staged KTX2 while retaining the previous file until the CPU/GPU
/// commit succeeds. Destruction rolls the disk change back on exceptions.
class CookedFileTransaction final
{
  public:
    CookedFileTransaction(std::filesystem::path destination, std::filesystem::path staged)
        : destination_(std::move(destination)), staged_(std::move(staged))
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

    CookedFileTransaction(const CookedFileTransaction &) = delete;
    CookedFileTransaction &operator=(const CookedFileTransaction &) = delete;

    [[nodiscard]] const std::filesystem::path &stagedPath() const noexcept
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
            throw std::runtime_error("staged KTX2 file is absent: " + staged_.string());
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

  public:
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

  private:
    std::filesystem::path destination_;
    std::filesystem::path staged_;
    std::filesystem::path backup_;
    bool hadPrevious_ = false;
    bool installed_ = false;
    bool finished_ = false;
};

[[nodiscard]] importer::texture::KtxTextureCooker::Request makeCookRequest(
    const importer::texture::TextureImportRecord &record,
    const importer::texture::TextureImportSettings &settings,
    const std::filesystem::path &stagedPath)
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

struct App::TextureReimportTransaction final : render::ResourcePublicationTransaction
{
    asset::AssetManager &assets;
    importer::texture::TextureReimportRequest request;
    importer::texture::TextureImportRecord record;
    CookedFileTransaction file;
    asset::AssetManager::StagedTextureReplacement staged;
    render::ResourcePreparationTicket ticket;
    bool committed = false;

    TextureReimportTransaction(asset::AssetManager &manager, PreparedTextureReimport prepared)
        : assets(manager), request(std::move(prepared.request)), record(std::move(prepared.record)),
          file(record.cookedPath, prepared.stagedPath),
          staged(manager.stageTextureReplacement(request.texture,
                                                 std::move(prepared.replacementAsset)))
    {
    }
    void begin() override
    {
        assets.validateStagedTexture(staged);
        file.install();
    }
    void commit() noexcept override
    {
        assets.commitStagedTexture(staged);
        file.finish();
        committed = true;
    }
    void rollback() noexcept override
    {
        file.rollback();
    }
};

void App::processPendingTextureReimport()
{
    using namespace std::chrono_literals;

    // CPU cooking, GPU preparation and publication are separate nonblocking phases.
    if (textureReimportTransaction_)
    {
        auto &transaction = *textureReimportTransaction_;
        try
        {
            if (!transaction.ticket)
            {
                const auto result = resourcePreparation_.prepare(transaction.staged.snapshot());
                if (result.code == render::ResourcePreparationCode::QueueFull)
                    return;
                if (!result.accepted())
                    throw std::runtime_error(result.error);
                transaction.ticket = result.ticket;
                resourcePreparation_.setPublicationTransaction(result.ticket,
                                                               textureReimportTransaction_);
            }
            const auto status = resourcePreparation_.status(transaction.ticket);
            if (!render::resourcePreparationFinished(status.state))
                return;
            if (status.state != render::ResourcePreparationState::Ready || !transaction.committed)
                throw std::runtime_error(status.error.empty() ? "texture preparation cancelled"
                                                              : status.error);
            resourcePreparation_.release(transaction.ticket);
            transaction.ticket = {};
            textureImports.markSucceeded(transaction.request.texture, transaction.request.settings);
            // Preview descriptors refresh by the cache's publication serial on the next GUI draw.
            std::clog << "[Assets] Reimported texture " << transaction.record.sourcePath.string()
                      << " -> " << transaction.record.cookedPath.string() << '\n';
        }
        catch (const std::exception &error)
        {
            if (transaction.ticket)
            {
                try
                {
                    resourcePreparation_.cancel(transaction.ticket);
                    resourcePreparation_.release(transaction.ticket);
                }
                catch (...)
                {
                }
            }
            textureImports.markFailed(transaction.request.texture, error.what());
            std::cerr << "[Assets] Texture reimport preparation failed: " << error.what() << '\n';
        }
        textureReimportTransaction_.reset();
        activeTextureReimport_ = {};
        activeTextureReimportStagedPath_.clear();
    }
    if (textureReimportFuture_.valid())
    {
        if (textureReimportFuture_.wait_for(0ms) != std::future_status::ready)
            return;
        try
        {
            auto prepared = textureReimportFuture_.get();
            if (!prepared.error.empty())
                throw std::runtime_error(prepared.error);
            if (prepared.domain != assetManager.domain() ||
                !assetManager.isCurrent(prepared.baseVersion))
                throw std::runtime_error("texture changed while reimport was cooking");
            textureReimportTransaction_ =
                std::make_shared<TextureReimportTransaction>(assetManager, std::move(prepared));
            return; // The next pump admits the immutable candidate through the frontend port.
        }
        catch (const std::exception &error)
        {
            std::error_code ignored;
            std::filesystem::remove(activeTextureReimportStagedPath_, ignored);
            textureImports.markFailed(activeTextureReimport_, error.what());
            activeTextureReimport_ = {};
            activeTextureReimportStagedPath_.clear();
            std::cerr << "[Assets] Texture reimport worker failed: " << error.what() << '\n';
        }
    }

    if (pendingTextureReimports_.empty())
    {
        return;
    }

    importer::texture::TextureReimportRequest request = std::move(pendingTextureReimports_.front());
    pendingTextureReimports_.pop_front();
    const asset::TextureAssetHandle requestedTexture = request.texture;

    const importer::texture::TextureImportRecord *storedRecord =
        textureImports.find(request.texture);
    if (storedRecord == nullptr)
    {
        return;
    }
    importer::texture::TextureImportRecord record = *storedRecord;

    try
    {
        if (record.reimporting || !assetManager.contains(request.texture))
        {
            throw std::invalid_argument(
                "reimport target is busy or absent from the CPU asset registry");
        }

        importer::texture::KtxTextureImporter::CreateInfo importInfo{};
        importInfo.name = assetManager.texture(request.texture).name();
        importInfo.sampler = assetManager.texture(request.texture).sampler();
        importInfo.transcodeFormat = request.settings.transcodeFormat;
        importInfo.highQuality = request.settings.highQualityTranscode;

        const uint64_t nonce = nextTransactionNonce.fetch_add(1);
        const std::filesystem::path stagedPath =
            transactionSibling(record.cookedPath, "reimport", nonce);

        textureImports.markStarted(request.texture);
        activeTextureReimport_ = request.texture;
        activeTextureReimportStagedPath_ = stagedPath;
        const auto baseVersion = assetManager.version(request.texture);
        const auto sourceDomain = assetManager.domain();
        textureReimportFuture_ = std::async(std::launch::async, [request = std::move(request),
                                                                 record = std::move(record),
                                                                 importInfo = std::move(importInfo),
                                                                 stagedPath, baseVersion,
                                                                 sourceDomain]() mutable {
            PreparedTextureReimport result{};
            result.request = std::move(request);
            result.record = std::move(record);
            result.stagedPath = stagedPath;
            result.baseVersion = baseVersion;
            result.domain = sourceDomain;
            try
            {
                result.replacementAsset =
                    asset::TextureAsset(importer::texture::KtxTextureCooker{}.cookAndImport(
                        makeCookRequest(result.record, result.request.settings, result.stagedPath),
                        importInfo));
            }
            catch (const std::exception &error)
            {
                result.error = error.what();
            }
            catch (...)
            {
                result.error = "unknown exception while cooking or importing KTX2";
            }

            if (!result.error.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(result.stagedPath, ignored);
            }
            return result;
        });
    }
    catch (const std::exception &error)
    {
        activeTextureReimport_ = {};
        activeTextureReimportStagedPath_.clear();
        textureImports.markFailed(requestedTexture, error.what());
        std::cerr << "[Assets] Failed to start texture reimport: " << error.what() << '\n';
    }
}

void App::discardTextureReimport() noexcept
{
    pendingTextureReimports_.clear();
    if (textureReimportTransaction_)
    {
        auto &transaction = *textureReimportTransaction_;
        if (transaction.ticket)
        {
            try
            {
                resourcePreparation_.cancel(transaction.ticket);
                resourcePreparation_.release(transaction.ticket);
            }
            catch (...)
            {
            }
        }
        try
        {
            if (transaction.committed)
                textureImports.markSucceeded(transaction.request.texture,
                                             transaction.request.settings);
            else
                textureImports.markFailed(transaction.request.texture,
                                          "texture reimport cancelled");
        }
        catch (...)
        {
        }
        textureReimportTransaction_.reset();
    }
    else if (activeTextureReimport_)
    {
        try
        {
            textureImports.markFailed(activeTextureReimport_, "texture reimport cancelled");
        }
        catch (...)
        {
        }
    }
    if (textureReimportFuture_.valid())
    {
        try
        {
            PreparedTextureReimport prepared = textureReimportFuture_.get();
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
        std::filesystem::remove(activeTextureReimportStagedPath_, ignored);
    }
    activeTextureReimportStagedPath_.clear();
    activeTextureReimport_ = {};
}

} // namespace rubia::editor
