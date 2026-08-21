#include "network/artwork_manager.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "config/build_config.h"
#include "system/logging.h"

namespace deskwave::network {
namespace {

constexpr char kArtworkDirectory[] = "/art";
constexpr char kTemporaryPath[] = "/art/.download";

bool validRequest(const app::ArtworkRequest& request) {
    return request.port != 0 && request.host[0] != '\0' && request.token[0] != '\0' &&
           std::strlen(request.artworkId) == 64 &&
           std::strncmp(request.path, "/v1/artwork/", 12) == 0 &&
           std::strstr(request.path, "..") == nullptr;
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
        xQueueOverwrite(resultQueue_, &result);
    }
}

app::ArtworkResult ArtworkManager::download(const app::ArtworkRequest& request) {
    app::ArtworkResult result;
    app::copyText(result.artworkId, request.artworkId);
    if (!validRequest(request)) {
        app::copyText(result.error, "Artwork request is invalid");
        return result;
    }
    char finalPath[96];
    std::snprintf(finalPath, sizeof(finalPath), "/art/%s.jpg", request.artworkId);
    if (LittleFS.exists(finalPath)) {
        result.success = true;
        app::copyText(result.localPath, finalPath);
        return result;
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

    LittleFS.remove(kTemporaryPath);
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
    const auto deadline = millis() + 8'000;
    while (total < static_cast<std::size_t>(contentLength) &&
           static_cast<std::int32_t>(millis() - deadline) < 0) {
        const int available = stream->available();
        if (available <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const auto wanted = std::min<std::size_t>(sizeof(buffer),
                                                  static_cast<std::size_t>(contentLength) - total);
        const auto count = stream->readBytes(buffer, std::min<std::size_t>(wanted, available));
        if (count == 0 || output.write(buffer, count) != count) {
            break;
        }
        if (total == 0 && count >= 2) {
            first[0] = buffer[0];
            first[1] = buffer[1];
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
    if (total != static_cast<std::size_t>(contentLength) || first[0] != 0xFF || first[1] != 0xD8 ||
        last[0] != 0xFF || last[1] != 0xD9) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Artwork download was incomplete");
        return result;
    }
    clearOldArtwork(finalPath);
    LittleFS.remove(finalPath);
    if (!LittleFS.rename(kTemporaryPath, finalPath)) {
        LittleFS.remove(kTemporaryPath);
        app::copyText(result.error, "Artwork could not be committed");
        return result;
    }
    result.success = true;
    app::copyText(result.localPath, finalPath);
    DW_LOG_INFO("artwork", "Cached artwork %s (%u bytes)", request.artworkId,
                static_cast<unsigned>(total));
    return result;
}

void ArtworkManager::clearOldArtwork(const char* keepPath) {
    File directory = LittleFS.open(kArtworkDirectory);
    if (!directory || !directory.isDirectory()) {
        return;
    }
    File file = directory.openNextFile();
    while (file) {
        const String path = file.path();
        const bool remove = !file.isDirectory() && path.endsWith(".jpg") && path != keepPath;
        file.close();
        if (remove) {
            LittleFS.remove(path);
        }
        file = directory.openNextFile();
    }
    directory.close();
}

}  // namespace deskwave::network
