#include "network/artwork_manager.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>

#include "config/build_config.h"
#include "system/logging.h"

namespace deskwave::network {
namespace {

constexpr char kArtworkDirectory[] = "/art";
constexpr char kTemporaryPath[] = "/art/.download";
constexpr std::size_t kArtworkFilesystemReserveBytes = 32 * 1'024;

bool validArtworkIdentifier(const char* value) {
    if (value == nullptr || std::strlen(value) != 64) {
        return false;
    }
    for (std::size_t index = 0; index < 64; ++index) {
        const char character = value[index];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

void normalizeArtworkIdentifier(char (&destination)[65], const char* value) {
    destination[0] = '\0';
    if (validArtworkIdentifier(value)) {
        app::copyText(destination, value);
    }
}

bool validArtworkPath(const char* path, const char* artworkId) {
    if (path == nullptr || !validArtworkIdentifier(artworkId)) {
        return false;
    }
    char expectedPath[96];
    std::snprintf(expectedPath, sizeof(expectedPath), "/v1/artwork/%s.jpg", artworkId);
    return std::strcmp(path, expectedPath) == 0;
}

bool validRequest(const app::ArtworkRequest& request) {
    return request.port != 0 && request.host[0] != '\0' && request.token[0] != '\0' &&
           validArtworkPath(request.path, request.artworkId);
}

bool digestMatchesIdentifier(const std::array<std::uint8_t, 32>& digest, const char* identifier) {
    if (!validArtworkIdentifier(identifier)) {
        return false;
    }
    constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t index = 0; index < digest.size(); ++index) {
        if (identifier[index * 2] != kHex[digest[index] >> 4U] ||
            identifier[index * 2 + 1] != kHex[digest[index] & 0x0FU]) {
            return false;
        }
    }
    return true;
}

bool validCachedArtwork(const char* path, const char* artworkId) {
    File file = LittleFS.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        file.close();
        return false;
    }
    const auto size = file.size();
    if (size <= 4 || size > config::kMaximumArtworkBytes) {
        file.close();
        return false;
    }

    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (mbedtls_sha256_starts_ret(&hash, 0) != 0) {
        mbedtls_sha256_free(&hash);
        file.close();
        return false;
    }
    std::array<std::uint8_t, 1'024> buffer{};
    std::array<std::uint8_t, 2> first{};
    std::array<std::uint8_t, 2> last{};
    std::size_t total = 0;
    bool hashValid = true;
    while (total < size) {
        const auto count =
            file.read(buffer.data(), std::min<std::size_t>(buffer.size(), size - total));
        if (count == 0 || mbedtls_sha256_update_ret(&hash, buffer.data(), count) != 0) {
            hashValid = false;
            break;
        }
        for (std::size_t index = 0; index < count && total + index < first.size(); ++index) {
            first[total + index] = buffer[index];
        }
        if (count >= 2) {
            last[0] = buffer[count - 2];
            last[1] = buffer[count - 1];
        } else {
            last[0] = last[1];
            last[1] = buffer[0];
        }
        total += count;
    }
    std::array<std::uint8_t, 32> digest{};
    hashValid = hashValid && total == size && mbedtls_sha256_finish_ret(&hash, digest.data()) == 0;
    mbedtls_sha256_free(&hash);
    file.close();
    return hashValid && first[0] == 0xFF && first[1] == 0xD8 && last[0] == 0xFF &&
           last[1] == 0xD9 && digestMatchesIdentifier(digest, artworkId);
}

}  // namespace

ArtworkManager::ArtworkManager(const QueueHandle_t requestQueue, const QueueHandle_t resultQueue)
    : requestQueue_(requestQueue), resultQueue_(resultQueue) {}

bool ArtworkManager::begin() {
    if (task_ != nullptr) {
        return true;
    }
    return xTaskCreatePinnedToCore(taskEntry, "deskwave-artwork", 6'144, this, 1, &task_, 0) ==
           pdPASS;
}

void ArtworkManager::setArtworkProtection(const char* activeArtworkId,
                                          const char* stagedArtworkId) {
    char normalizedActive[65]{};
    char normalizedStaged[65]{};
    normalizeArtworkIdentifier(normalizedActive, activeArtworkId);
    normalizeArtworkIdentifier(normalizedStaged, stagedArtworkId);
    portENTER_CRITICAL(&artworkProtectionMux_);
    app::copyText(protectedActiveArtworkId_, normalizedActive);
    app::copyText(protectedStagedArtworkId_, normalizedStaged);
    portEXIT_CRITICAL(&artworkProtectionMux_);
}

bool ArtworkManager::acknowledgeResult(const app::ArtworkResult& result,
                                       const char* activeArtworkId, const char* stagedArtworkId) {
    if (!result.success) {
        return false;
    }
    char normalizedActive[65]{};
    char normalizedStaged[65]{};
    normalizeArtworkIdentifier(normalizedActive, activeArtworkId);
    normalizeArtworkIdentifier(normalizedStaged, stagedArtworkId);
    bool acknowledged = false;
    portENTER_CRITICAL(&artworkProtectionMux_);
    if (publishedResultPending_ &&
        app::artworkIdentityMatches(publishedArtworkId_, publishedArtworkGeneration_,
                                    result.artworkId, result.artworkGeneration)) {
        app::copyText(protectedActiveArtworkId_, normalizedActive);
        app::copyText(protectedStagedArtworkId_, normalizedStaged);
        publishedArtworkId_[0] = '\0';
        publishedArtworkGeneration_ = 0;
        publishedResultPending_ = false;
        acknowledged = true;
    }
    portEXIT_CRITICAL(&artworkProtectionMux_);
    if (acknowledged && task_ != nullptr) {
        xTaskNotifyGive(task_);
    }
    return acknowledged;
}

void ArtworkManager::taskEntry(void* context) {
    static_cast<ArtworkManager*>(context)->run();
    vTaskDelete(nullptr);
}

void ArtworkManager::run() {
    if (!LittleFS.begin(false)) {
        DW_LOG_WARN("artwork", "Artwork filesystem is corrupt; rebuilding its cache");
        if (!LittleFS.format() || !LittleFS.begin(false)) {
            DW_LOG_ERROR("artwork", "Artwork filesystem could not be mounted");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1'000));
            }
        }
    }
    LittleFS.mkdir(kArtworkDirectory);
    while (true) {
        app::ArtworkRequest request;
        if (xQueueReceive(requestQueue_, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        auto result = download(request);
        std::memset(request.token, 0, sizeof(request.token));
        if (!result.success) {
            xQueueOverwrite(resultQueue_, &result);
            continue;
        }

        // Publish protection before exposing the successful result. The task
        // cannot dequeue another request until the main loop has accepted or
        // rejected this exact ID+generation and transferred protection to the
        // UI's active/staged covers.
        portENTER_CRITICAL(&artworkProtectionMux_);
        app::copyText(publishedArtworkId_, result.artworkId);
        publishedArtworkGeneration_ = result.artworkGeneration;
        publishedResultPending_ = true;
        portEXIT_CRITICAL(&artworkProtectionMux_);
        if (xQueueOverwrite(resultQueue_, &result) != pdTRUE) {
            portENTER_CRITICAL(&artworkProtectionMux_);
            publishedArtworkId_[0] = '\0';
            publishedArtworkGeneration_ = 0;
            publishedResultPending_ = false;
            portEXIT_CRITICAL(&artworkProtectionMux_);
            DW_LOG_ERROR("artwork", "Could not publish validated artwork result");
            continue;
        }
        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            portENTER_CRITICAL(&artworkProtectionMux_);
            const bool awaitingAcknowledgment = publishedResultPending_;
            portEXIT_CRITICAL(&artworkProtectionMux_);
            if (!awaitingAcknowledgment) {
                break;
            }
        }
    }
}

app::ArtworkResult ArtworkManager::download(const app::ArtworkRequest& request) {
    app::ArtworkResult result;
    app::copyText(result.artworkId, request.artworkId);
    result.artworkGeneration = request.artworkGeneration;
    result.theme = request.theme;
    result.hasTheme = request.hasTheme;
    if (!validRequest(request)) {
        app::copyText(result.error, "Artwork request is invalid");
        return result;
    }
    char finalPath[96];
    std::snprintf(finalPath, sizeof(finalPath), "/art/%s.jpg", request.artworkId);
    const bool destinationExists = LittleFS.exists(finalPath);
    if (destinationExists && validCachedArtwork(finalPath, request.artworkId)) {
        pruneArtworkCache(finalPath);
        result.success = true;
        app::copyText(result.localPath, finalPath);
        return result;
    }
    if (destinationExists) {
        DW_LOG_WARN("artwork", "Cached artwork %.12s failed integrity validation",
                    request.artworkId);
    }
    if (WiFi.status() != WL_CONNECTED) {
        app::copyText(result.error, "Wi-Fi is offline");
        return result;
    }

    WiFiClient client;
    client.setTimeout(1'000);
    HTTPClient http;
    http.setConnectTimeout(config::kHttpTimeoutMs);
    http.setTimeout(config::kHttpTimeoutMs);
    const String url = "http://" + String(request.host) + ":" + String(request.port) + request.path;
    if (!http.begin(client, url)) {
        app::copyText(result.error, "Artwork connection failed");
        return result;
    }
    static const char* kResponseHeaders[] = {"Content-Type"};
    http.collectHeaders(kResponseHeaders, 1);
    http.addHeader("Authorization", "Bearer " + String(request.token));
    http.addHeader("Accept", "image/jpeg");
    const int code = http.GET();
    const int contentLength = http.getSize();
    if (code != HTTP_CODE_OK || contentLength <= 4 ||
        contentLength > static_cast<int>(config::kMaximumArtworkBytes) ||
        !http.header("Content-Type").startsWith("image/jpeg")) {
        http.end();
        app::copyText(result.error, "Artwork response was rejected");
        return result;
    }

    // A corrupt target that is not active or staged has no remaining reader.
    // Remove it before allocating the temp file; protected destinations stay
    // in place until their validated replacement is ready to rename.
    if (destinationExists && !isProtectedArtwork(request.artworkId)) {
        if (!LittleFS.remove(finalPath)) {
            http.end();
            app::copyText(result.error, "Corrupt artwork could not be removed");
            return result;
        }
        DW_LOG_DEBUG("artwork", "Removed corrupt target %.12s before download", request.artworkId);
    }

    // Keep only the acknowledged active/staged covers and this target before
    // allocating the temporary file.
    pruneArtworkCache(finalPath);
    LittleFS.remove(kTemporaryPath);
    const auto filesystemBytes = LittleFS.totalBytes();
    const auto usedBytes = LittleFS.usedBytes();
    const auto freeBytes = filesystemBytes > usedBytes ? filesystemBytes - usedBytes : 0;
    const auto requiredBytes =
        static_cast<std::size_t>(contentLength) + kArtworkFilesystemReserveBytes;
    if (freeBytes < requiredBytes) {
        http.end();
        app::copyText(result.error, "Artwork cache has insufficient space");
        DW_LOG_WARN("artwork", "Cache has %u bytes free; %u required",
                    static_cast<unsigned>(freeBytes), static_cast<unsigned>(requiredBytes));
        return result;
    }
    File output = LittleFS.open(kTemporaryPath, FILE_WRITE);
    if (!output) {
        http.end();
        app::copyText(result.error, "Artwork cache is unavailable");
        return result;
    }
    WiFiClient* stream = http.getStreamPtr();
    std::uint8_t buffer[1'024];
    std::size_t total = 0;
    std::uint8_t first[2]{};
    std::uint8_t last[2]{};
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    if (mbedtls_sha256_starts_ret(&hash, 0) != 0) {
        output.close();
        http.end();
        LittleFS.remove(kTemporaryPath);
        mbedtls_sha256_free(&hash);
        app::copyText(result.error, "Artwork integrity check failed");
        return result;
    }
    bool hashValid = true;
    const auto deadline = millis() + 8'000;
    while (total < static_cast<std::size_t>(contentLength) &&
           static_cast<std::int32_t>(millis() - deadline) < 0) {
        const int available = stream->available();
        if (available <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const auto wanted =
            std::min<std::size_t>(sizeof(buffer), static_cast<std::size_t>(contentLength) - total);
        const auto count = stream->readBytes(buffer, std::min<std::size_t>(wanted, available));
        if (count == 0 || output.write(buffer, count) != count ||
            mbedtls_sha256_update_ret(&hash, buffer, count) != 0) {
            hashValid = false;
            break;
        }
        for (std::size_t index = 0; index < count && total + index < 2; ++index) {
            first[total + index] = buffer[index];
        }
        if (count >= 2) {
            last[0] = buffer[count - 2];
            last[1] = buffer[count - 1];
        } else {
            last[0] = last[1];
            last[1] = buffer[0];
        }
        total += count;
    }
    output.flush();
    output.close();
    http.end();
    std::array<std::uint8_t, 32> digest{};
    hashValid = hashValid && mbedtls_sha256_finish_ret(&hash, digest.data()) == 0;
    mbedtls_sha256_free(&hash);
    if (!hashValid || total != static_cast<std::size_t>(contentLength) || first[0] != 0xFF ||
        first[1] != 0xD8 || last[0] != 0xFF || last[1] != 0xD9) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Artwork download was incomplete");
        return result;
    }
    if (!digestMatchesIdentifier(digest, request.artworkId)) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Artwork integrity check failed");
        DW_LOG_WARN("artwork", "Rejected artwork %.12s with mismatched SHA-256", request.artworkId);
        return result;
    }
    // A successful write call does not prove the filesystem persisted every
    // byte (for example after ENOSPC). Re-open and hash the closed file before
    // replacing any destination.
    if (!validCachedArtwork(kTemporaryPath, request.artworkId)) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Artwork cache verification failed");
        DW_LOG_WARN("artwork", "Rejected unverified cached artwork %.12s", request.artworkId);
        return result;
    }
    // Any destination still present is a protected active/staged cover that
    // failed integrity validation. Replace it only after the temp file is
    // complete, independently re-read, and ready for an immediate rename.
    if (LittleFS.exists(finalPath) && !LittleFS.remove(finalPath)) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Corrupt artwork could not be replaced");
        return result;
    }
    if (!LittleFS.rename(kTemporaryPath, finalPath)) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Artwork could not be committed");
        return result;
    }
    pruneArtworkCache(finalPath);
    result.success = true;
    app::copyText(result.localPath, finalPath);
    DW_LOG_INFO("artwork", "Cached artwork %s (%u bytes)", request.artworkId,
                static_cast<unsigned>(total));
    return result;
}

void ArtworkManager::pruneArtworkCache(const char* keepPath) {
    char activeArtworkId[65]{};
    char stagedArtworkId[65]{};
    char publishedArtworkId[65]{};
    portENTER_CRITICAL(&artworkProtectionMux_);
    app::copyText(activeArtworkId, protectedActiveArtworkId_);
    app::copyText(stagedArtworkId, protectedStagedArtworkId_);
    if (publishedResultPending_) {
        app::copyText(publishedArtworkId, publishedArtworkId_);
    }
    portEXIT_CRITICAL(&artworkProtectionMux_);
    String activePath;
    if (validArtworkIdentifier(activeArtworkId)) {
        activePath = String(kArtworkDirectory) + "/" + activeArtworkId + ".jpg";
    }
    String stagedPath;
    if (validArtworkIdentifier(stagedArtworkId)) {
        stagedPath = String(kArtworkDirectory) + "/" + stagedArtworkId + ".jpg";
    }
    String publishedPath;
    if (validArtworkIdentifier(publishedArtworkId)) {
        publishedPath = String(kArtworkDirectory) + "/" + publishedArtworkId + ".jpg";
    }

    File directory = LittleFS.open(kArtworkDirectory);
    if (!directory || !directory.isDirectory()) {
        return;
    }
    File file = directory.openNextFile();
    while (file) {
        const String path = file.path();
        const bool remove = !file.isDirectory() && path.endsWith(".jpg") && path != keepPath &&
                            path != activePath && path != stagedPath && path != publishedPath;
        file.close();
        if (remove && LittleFS.remove(path)) {
            DW_LOG_DEBUG("artwork", "Pruned cached cover %s", path.c_str());
        }
        file = directory.openNextFile();
    }
    directory.close();
}

bool ArtworkManager::isProtectedArtwork(const char* artworkId) {
    if (!validArtworkIdentifier(artworkId)) {
        return false;
    }
    portENTER_CRITICAL(&artworkProtectionMux_);
    const bool protectedArtwork =
        std::strcmp(protectedActiveArtworkId_, artworkId) == 0 ||
        std::strcmp(protectedStagedArtworkId_, artworkId) == 0 ||
        (publishedResultPending_ && std::strcmp(publishedArtworkId_, artworkId) == 0);
    portEXIT_CRITICAL(&artworkProtectionMux_);
    return protectedArtwork;
}

}  // namespace deskwave::network
