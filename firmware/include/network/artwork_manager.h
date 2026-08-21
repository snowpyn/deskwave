#pragma once

#include <Arduino.h>

#include "app/messages.h"

namespace deskwave::network {

class ArtworkManager {
   public:
    ArtworkManager(QueueHandle_t requestQueue, QueueHandle_t resultQueue);
    [[nodiscard]] bool begin();

   private:
    static void taskEntry(void* context);
    void run();
    [[nodiscard]] app::ArtworkResult download(const app::ArtworkRequest& request);
    void clearOldArtwork(const char* keepPath);

    QueueHandle_t requestQueue_;
    QueueHandle_t resultQueue_;
    TaskHandle_t task_{nullptr};
};

}  // namespace deskwave::network
