// #pragma once
// #include <drogon/drogon.h>
// #include <cppkafka/cppkafka.h>
// #include <vector>
// #include <memory>
// #include <thread>
// #include <atomic>
// #include <mutex>
// #include <condition_variable>

// using namespace drogon;
// using namespace cppkafka;

// class KafkaConsumerPlugin : public Plugin<KafkaConsumerPlugin> {
// public:
//     void initAndStart(const Json::Value &config) override;
//     void shutdown() override;

// private:
//     struct ConsumerConfig {
//         std::string brokers;
//         std::string groupId;
//         std::string topic;
//         uint32_t pollMs{100};
//         bool autoCommit{false};
//         std::string autoOffsetReset{"latest"};
//         uint32_t sessionTimeoutMs{30000};
//         uint32_t heartbeatIntervalMs{10000};
//         uint32_t maxPollIntervalMs{300000};
//         size_t maxBatchSize{1000};
//     };

//     void consumeLoop();
//     void processMessageBatch(const std::vector<Message>& messages);
//     bool processS3Files(const std::string& topic,
//                       int partition,
//                       TopicPartition::Offset offset,
//                       const std::string& key,
//                       const xyz::virajdoshi::reelz::MediaProcessingJob& decodedMsg);
    
//     xyz::virajdoshi::reelz::MediaProcessingJob decodeAvroMessage(const Buffer& payload);
//     void commitOffsets();
//     bool initializeConsumer();
//     void handleConsumerError(const std::exception& e);
//     void safeShutdown();

//     // Configuration
//     ConsumerConfig config_;
    
//     // Threading
//     std::unique_ptr<Consumer> consumer_;
//     std::thread consumerThread_;
//     std::atomic<bool> running_{false};
//     std::mutex consumerMutex_;
//     std::condition_variable shutdownCV_;
//     bool isShuttingDown_{false};
    
//     // Message processing
//     std::mutex commitMutex_;
//     std::vector<TopicPartition> pendingCommits_;
//     static constexpr size_t MAX_PENDING_COMMITS = 1000;
    
//     // Error handling
//     std::atomic<uint32_t> consecutiveErrors_{0};
//     static constexpr uint32_t MAX_CONSECUTIVE_ERRORS = 5;
// };

//=================================================================

#pragma once
#include <drogon/drogon.h>
#include <cppkafka/cppkafka.h>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "MediaProcessingJob.hh"

using namespace drogon;
using namespace cppkafka;

// Right now this is defined only to handle media processing requests
// If there is a need to handle other types of requests, then this might
// have to be generalized to handle different types of requests
class KafkaConsumerPlugin : public Plugin<KafkaConsumerPlugin> {
public:
    void initAndStart(const Json::Value &config) override;
    void shutdown() override;

private:
    struct ConsumerConfig {
        std::string brokers;
        std::string groupId;
        std::string topic;
        uint32_t pollMs{100};
        bool autoCommit{false};
        std::string autoOffsetReset{"latest"};
        uint32_t sessionTimeoutMs{30000};
        uint32_t heartbeatIntervalMs{10000};
        uint32_t maxPollIntervalMs{300000};
        size_t maxBatchSize{1000};
    };

    // Core processing methods
    void consumeLoop();
    void processMessageBatch(const std::vector<Message>& messages);
    bool processS3Files(const std::string& topic,
                      int partition,
                      TopicPartition::Offset offset,
                      const std::string& key,
                      const xyz::virajdoshi::reelz::MediaProcessingJob& decodedMsg);
    
    // Avro decoding
    xyz::virajdoshi::reelz::MediaProcessingJob decodeAvroMessage(const Buffer& payload);
    
    // Kafka operations
    void commitOffsets();
    bool initializeConsumer();
    void handleConsumerError(const std::exception& e);
    void safeShutdown();
    
    // Post-processing helpers
    void updateProcessedMedia(const std::string& postId,
                             const std::string& originalUrl,
                             const std::string& processedUrl,
                             const std::string& mediaType);
    
    void handleProcessingFailure(const std::string& postId,
                                const std::string& failedUrl,
                                const std::string& traceId);

    // Configuration
    ConsumerConfig config_;
    
    // Threading
    std::unique_ptr<Consumer> consumer_;
    std::thread consumerThread_;
    std::atomic<bool> running_{false}; //make single operations on 1 variable thread safe
    std::mutex consumerMutex_; //similar to atomic but for complex/multiple operations
    std::condition_variable shutdownCV_;
    bool isShuttingDown_{false};
    
    // Message processing
    std::mutex commitMutex_;
    std::vector<TopicPartition> pendingCommits_;
    static constexpr size_t MAX_PENDING_COMMITS = 1000;
    
    // Error handling
    std::atomic<uint32_t> consecutiveErrors_{0};
    static constexpr uint32_t MAX_CONSECUTIVE_ERRORS = 5;
    
    // Optional: Producer for dead letter queue
    // std::unique_ptr<Producer> producer_;
};