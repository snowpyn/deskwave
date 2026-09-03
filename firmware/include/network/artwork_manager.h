#pragma once

#include <Arduino.h>

#include "app/messages.h"

namespace deskwave::network {

class ArtworkManager {
   public:
    ArtworkManager(QueueHandle_t requestQueue, QueueHandle_t resultQueue);
    [[nodiscard]] bool begin();
    void setArtworkProtection(const char* activeArtworkId, const char* stagedArtworkId);
    [[nodiscard]] bool acknowledgeResult(const app::ArtworkResult& result,
                                         const char* activeArtworkId, const char* stagedArtworkId);

   private:
    static void taskEntry(void* context);
    void run();
    [[nodiscard]] app::ArtworkResult download(const app::ArtworkRequest& request);
    void pruneArtworkCache(const char* keepPath);
    [[nodiscard]] bool isProtectedArtwork(const char* artworkId);

    QueueHandle_t requestQueue_;
    QueueHandle_t resultQueue_;
    TaskHandle_t task_{nullptr};
    portMUX_TYPE artworkProtectionMux_ = portMUX_INITIALIZER_UNLOCKED;
    char protectedActiveArtworkId_[65]{};
    char protectedStagedArtworkId_[65]{};
    char publishedArtworkId_[65]{};
    std::uint32_t publishedArtworkGeneration_{0};
    bool publishedResultPending_{false};
};

}  // namespace deskwave::network
