#include <drogon/drogon.h>
#include <json/json.h>
#include <vector>
#include <cstring>
#include <chrono>
#include <sstream>

// Avro-specific includes for decoding
#include <avro/Decoder.hh>
#include <avro/Specific.hh>
#include <avro/Stream.hh>
#include <avro/Exception.hh>
#include <avro/DataFile.hh>

#include "plugins/KafkaConsumerPlugin.h"
#include "plugins/AWSPlugin.h"
#include "json_utils.h"
#include "processing/MediaProcessor.h"
#include "MediaProcessingJob.hh"
#include <aws/s3/S3Client.h>
#include <aws/core/Aws.h>
#include <functional>  // for std::hash

using namespace cppkafka;
using namespace drogon;
using namespace std::chrono_literals;

// Constants
constexpr auto CONSUMER_POLL_TIMEOUT = 100ms;
constexpr auto CONSUMER_ERROR_BACKOFF = 5s;
constexpr auto COMMIT_INTERVAL = 5s;

/**
 * @brief Initialize and start the Kafka consumer plugin.
 * 
 * This function is called when the plugin is initialized and started.
 * It loads the configuration, initializes the Kafka consumer, and starts
 * the consumer thread.
 */
void KafkaConsumerPlugin::initAndStart(const Json::Value &config) {
  try {
    // Load configuration with validation
    config_.brokers = config.get("brokers", "localhost:9092").asString();
    config_.groupId = config.get("group_id", "media-consumer-group").asString();
    config_.topic = config.get("topic", "media_processing").asString();
    config_.pollMs = config.get("poll_ms", 100).asUInt();
    config_.autoCommit = config.get("enable_auto_commit", false).asBool();
    config_.autoOffsetReset = config.get("auto_offset_reset", "latest").asString();
    config_.sessionTimeoutMs = config.get("session_timeout_ms", 30000).asUInt();
    config_.heartbeatIntervalMs = config.get("heartbeat_interval_ms", 10000).asUInt();
    config_.maxPollIntervalMs = config.get("max_poll_interval_ms", 300000).asUInt();
    config_.maxBatchSize = config.get("max_batch_size", 1000).asUInt();

    if (!initializeConsumer()) {
      LOG_ERROR << "Failed to initialize Kafka consumer";
      throw std::runtime_error("Failed to initialize Kafka consumer");
    }

	// The running_ variable here is used as a flag. If it is set to true,
	// it means that the consumer thread is running.
    running_.store(true);

	// Creates and start a new thread
    consumerThread_ = std::thread(&KafkaConsumerPlugin::consumeLoop, this);
    
    LOG_INFO << "KafkaConsumerPlugin started: topic=" << config_.topic 
            << " group=" << config_.groupId
            << " brokers=" << config_.brokers;
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to initialize KafkaConsumerPlugin: " << e.what();
    throw;
  }
}

bool KafkaConsumerPlugin::initializeConsumer() {
    try {
			// This is is required by cppkakfka
        Configuration config = {
            {"metadata.broker.list", config_.brokers},
            {"group.id", config_.groupId},
            {"enable.auto.commit", config_.autoCommit ? "true" : "false"},
            {"auto.offset.reset", config_.autoOffsetReset},
            {"session.timeout.ms", std::to_string(config_.sessionTimeoutMs)},
            {"heartbeat.interval.ms", std::to_string(config_.heartbeatIntervalMs)},
            {"max.poll.interval.ms", std::to_string(config_.maxPollIntervalMs)},
            {"enable.partition.eof", "false"},
            {"enable.auto.offset.store", "false"}
        };

        consumer_ = std::make_unique<Consumer>(config);
        consumer_->subscribe({config_.topic});
        
        LOG_DEBUG << "Kafka consumer initialized successfully";
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to create Kafka consumer: " << e.what();
        return false;
    }
}

void KafkaConsumerPlugin::consumeLoop() {
    LOG_INFO << "Starting Kafka consumer loop for topic: " << config_.topic;
    
    std::vector<Message> messages;
    auto lastCommitTime = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        try {
            // Check for shutdown signal
            if (isShuttingDown_) {
                LOG_INFO << "Shutdown signal received, stopping consumer loop";
                break;
            }
            
            // Poll for messages
            Message msg = consumer_->poll(std::chrono::milliseconds(config_.pollMs));
            
            if (msg) {
                if (msg.get_error()) {
                    if (msg.is_eof()) {
                        LOG_DEBUG << "Reached end of partition";
                        continue;
                    }
                    LOG_ERROR << "Kafka error: " << msg.get_error().to_string();
                    continue;
                }
                
                // Add message to batch
                messages.emplace_back(std::move(msg));
                
                // Process batch if we've reached the max size
                if (messages.size() >= config_.maxBatchSize) {
                    processMessageBatch(messages);
                    messages.clear();
                    lastCommitTime = std::chrono::steady_clock::now();
                }
            } else {
                // Process any remaining messages in the batch
                if (!messages.empty()) {
                    processMessageBatch(messages);
                    messages.clear();
                    lastCommitTime = std::chrono::steady_clock::now();
                }
            }
            
            // Commit offsets periodically
            auto now = std::chrono::steady_clock::now();
            if (now - lastCommitTime > COMMIT_INTERVAL) {
                commitOffsets();
                lastCommitTime = now;
            }
            
        } catch (const std::exception& e) {
            handleConsumerError(e);
            std::this_thread::sleep_for(CONSUMER_ERROR_BACKOFF);
        }
    }
    
    // Process any remaining messages before shutting down
    if (!messages.empty()) {
        processMessageBatch(messages);
    }
    
    // Final commit before exiting
    commitOffsets();
    
    LOG_INFO << "Kafka consumer loop stopped";
}

void KafkaConsumerPlugin::processMessageBatch(const std::vector<Message>& messages) {
    if (messages.empty()) return;
    LOG_DEBUG << "Processing batch of " << messages.size() << " messages";
    
    for (const auto& msg : messages) {
        try {
            // Decode the Avro message
            auto decodedMsg = decodeAvroMessage(msg.get_payload());
            
            //print toProcessUrls
            for (const auto& item : decodedMsg.toProcessUrls) {
                std::cout << "---------------------------------------\n";
                std::cout << "URL: " << item.url << "\n";
                std::cout << "Media Type: " << item.mediaType << "\n";

                if(!item.dimensions.is_null()) {
                    xyz::virajdoshi::reelz::Dimensions dims = item.dimensions.get_Dimensions();
                    std::cout << "Dimensions: " << dims.width << " x " << dims.height << "\n";
                } else {
                    std::cout << "Dimensions: n/a\n";
                }
                std::cout << "---------------------------------------\n";
            }
            
            // Convert key buffer to string to avoid copying deleted Buffer copy-ctor
            std::string key;
            if (msg.get_key()) {
                const auto &buf = msg.get_key();
                key.assign(reinterpret_cast<const char*>(buf.get_data()), buf.get_size());
            }
            
            // Process the message
            // Topic and partition are simple and commonly used types so the kafka
            // library converts them to respective types internally hence no conversion
            // is required
            if (!processS3Files(
                msg.get_topic(),
                msg.get_partition(),
                static_cast<TopicPartition::Offset>(msg.get_offset()),
                key,
                decodedMsg)) {
                LOG_ERROR << "Failed to process message from " << msg.get_topic() 
                         << " partition " << msg.get_partition() 
                         << " offset " << msg.get_offset();
                continue;
            }
            
            // Store offset for commit
            std::lock_guard<std::mutex> lock(commitMutex_);
            pendingCommits_.emplace_back(msg.get_topic(), msg.get_partition(), msg.get_offset() + 1);
            
            // Commit if we've reached the threshold
            if (pendingCommits_.size() >= MAX_PENDING_COMMITS) {
                commitOffsets();
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR << "Error processing message: " << e.what();
        }
    }
}

xyz::virajdoshi::reelz::MediaProcessingJob KafkaConsumerPlugin::decodeAvroMessage(const Buffer& payload) {
    if (payload.get_size() < 5) {
        LOG_WARN << "Avro message too short to contain a schema id";
        throw std::runtime_error("Invalid Avro message: too short");
    }

    //here static cast is used because get_data() return binary data
    //earlier we used reinterpret_cast since we wanted to convert binary data
    //to pointer of uint8_t
    const uint8_t* data = static_cast<const uint8_t*>(payload.get_data());
    
    // Validate magic byte
    if (data[0] != 0x0) {
        std::ostringstream oss;
        oss << "Invalid magic byte in Avro message: 0x" << std::hex << static_cast<int>(data[0]);
        LOG_ERROR << oss.str();
        throw std::runtime_error(oss.str());
    }
    
    // Extract schema ID (big-endian)
    uint32_t schemaId = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
    
    LOG_DEBUG << "Decoding Avro message with schema ID: " << schemaId;
    
    try {
        // Create a stream for the Avro data (skip the first 5 bytes: magic byte + schema ID)
        auto inStream = avro::memoryInputStream(data + 5, payload.get_size() - 5);
        auto decoder = avro::binaryDecoder();
        decoder->init(*inStream);

        // Decode the message
        xyz::virajdoshi::reelz::MediaProcessingJob job;
        avro::decode(*decoder, job);
        
        // LOG_DEBUG << "Successfully decoded message with " << job.toProcessUrls << " URLs";
        return job;
        
    } catch (const avro::Exception& e) {
        LOG_ERROR << "Avro decoding error: " << e.what();
        throw std::runtime_error(std::string("Avro decoding failed: ") + e.what());
    } catch (const std::exception& e) {
        LOG_ERROR << "Error decoding Avro message: " << e.what();
        throw;
    } catch (...) {
        LOG_ERROR << "Unknown error during Avro message decoding";
        throw std::runtime_error("Unknown error during Avro message decoding");
    }
}

void KafkaConsumerPlugin::shutdown() {
    LOG_INFO << "Shutting down KafkaConsumerPlugin...";
    
    // Signal the consumer thread to stop
    {
        std::lock_guard<std::mutex> lock(consumerMutex_);
        isShuttingDown_ = true;
        running_.store(false);
    }
    shutdownCV_.notify_all();
    
    // Wait for consumer thread to finish
    if (consumerThread_.joinable()) {
        if (std::this_thread::get_id() != consumerThread_.get_id()) {
            consumerThread_.join();
        } else {
            consumerThread_.detach();
        }
    }
    
    // Close and clean up the consumer
    safeShutdown();
    
    LOG_INFO << "KafkaConsumerPlugin stopped";
}

void KafkaConsumerPlugin::safeShutdown() {
    try {
        if (consumer_) {
            // Commit any pending offsets before shutting down
            commitOffsets();
            
            // Gracefully release the consumer
            // Some cppkafka builds don't expose close()/stop(); resetting the unique_ptr is enough
            consumer_.reset();
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "Error during consumer shutdown: " << e.what();
    }
}

void KafkaConsumerPlugin::commitOffsets() {
    std::lock_guard<std::mutex> lock(commitMutex_);
    if (pendingCommits_.empty() || !consumer_) {
        return;
    }
    
    try {
        consumer_->commit(pendingCommits_);
        pendingCommits_.clear();
    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to commit offsets: " << e.what();
    }
}

void KafkaConsumerPlugin::handleConsumerError(const std::exception& e) {
    LOG_ERROR << "Kafka consumer error: " << e.what();
    consecutiveErrors_++;
    
    if (consecutiveErrors_ >= MAX_CONSECUTIVE_ERRORS) {
        LOG_ERROR << "Too many consecutive errors, attempting to reinitialize consumer";
        safeShutdown();
        std::this_thread::sleep_for(CONSUMER_ERROR_BACKOFF);
        
        if (!initializeConsumer()) {
            LOG_ERROR << "Failed to reinitialize Kafka consumer after errors";
            throw std::runtime_error("Failed to reinitialize Kafka consumer");
        }
        consecutiveErrors_ = 0;
    }
}

// Add these helper methods to KafkaConsumerPlugin class:
void KafkaConsumerPlugin::updateProcessedMedia(const std::string& postId,
                                              const std::string& originalUrl,
                                              const std::string& processedUrl,
                                              const std::string& mediaType) {
    // TODO: Implement database update or API call
    // Example: Send HTTP request to your backend API to update the post's media URLs
    
    Json::Value updateRequest;
    updateRequest["post_id"] = postId;
    updateRequest["original_url"] = originalUrl;
    updateRequest["processed_url"] = processedUrl;
    updateRequest["media_type"] = mediaType;
    updateRequest["processed_at"] = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Send to your backend API or write to database
    // Example using Drogon HTTP client:
    /*
    auto client = drogon::HttpClient::newHttpClient("http://your-api-endpoint");
    auto req = drogon::HttpRequest::newHttpJsonRequest(updateRequest);
    req->setPath("/api/posts/update-media");
    req->setMethod(drogon::Post);
    
    client->sendRequest(req, [postId](drogon::ReqResult result, 
                                     const drogon::HttpResponsePtr& response) {
        if (result == drogon::ReqResult::Ok && response->getStatusCode() == drogon::k200OK) {
            LOG_INFO << "Successfully updated media URLs for post " << postId;
        } else {
            LOG_ERROR << "Failed to update media URLs for post " << postId;
        }
    });
    */
}

//=================================================================
// Fixed processS3Files method
bool KafkaConsumerPlugin::processS3Files(const std::string& topic, int partition, TopicPartition::Offset offset,
    const std::string& key, const xyz::virajdoshi::reelz::MediaProcessingJob& decodedMsg) {
    
    LOG_DEBUG << "Processing message - Topic: " << topic 
              << " Partition: " << partition 
              << " Offset: " << offset 
              << " Key: " << key;
            //   << " Trace ID: " << decodedMsg.traceId;

    if (decodedMsg.toProcessUrls.empty()) {      //There is no member named 'empty'
        LOG_WARN << "No URLs to process in message";
        return false;
    }

    const std::string& uploadType = decodedMsg.uploadType;
    const std::string& postId = decodedMsg.post_id;
    const int64_t timestamp = decodedMsg.timeStamp;
    
    LOG_INFO << "Processing " << decodedMsg.toProcessUrls.size() 
             << " files for post " << postId 
             << " (type: " << uploadType << ")"
    //          << " Trace ID: " << decodedMsg.traceId;

    try {
        // Get AWSPlugin instance and S3 client
        auto awsPlugin = drogon::app().getPlugin<AWSPlugin>();
        if (!awsPlugin) {
            LOG_ERROR << "Failed to get AWSPlugin instance";
            return false;
        }

        auto s3Client = awsPlugin->getS3Client();
        if (!s3Client) {
            LOG_ERROR << "Failed to get S3 client from AWSPlugin";
            return false;
        }

        // Configure media processing based on upload type
        MediaProcessor::ProcessingConfig procConfig;
        
        //for stories the uploadType can be either reel or image
        if (uploadType == "reel") {
            procConfig.outputFormat = "mp4";
            procConfig.res = {{1440, 7250, 192}, {1080, 3750, 160}, {720, 1850, 128}, {360, 900, 128}};
            // procConfig.videoBitrate = 2000;
            // procConfig.audioBitrate = 128;
            // procConfig.width = 1280;
            // procConfig.height = 1920; //9:16 for reels and stories
            procConfig.threads = 4;
        } else if (uploadType == "image") {
            procConfig.outputFormat = "jpg"; // For JPEG output
            // procConfig.videoBitrate = 0; // No video for images
            // procConfig.width = 1080;
            // procConfig.height = 1350; // Instagram aspect ratio (4:5)
            procConfig.threads = 2;
        } else {
            LOG_WARN << "Unknown upload type: " << uploadType << ", using default config";
        }
        
        MediaProcessor processor(s3Client, procConfig);
        std::vector<std::pair<std::string, std::string>> urlPairs;
        // Extract bucket and key from S3 URL
        std::string bucket = "reelzapp"; // Default bucket
        
        //creates pairs of input and output urls and adds it to the urlPairs vector
        //commented out so that the code compiles. Check the decodedMsg value
        // for (const auto& item : decodedMsg.toProcessUrls) {
        //     const std::string& url = item.url;
        //     const std::string& mediaType = item.mediaType;

        //     if (url.empty()) {
        //         LOG_WARN << "Empty URL in message, skipping";
        //         continue;
        //     }
            
        //     std::string path;
            
        //     //https://reelzapp.s3.us-east-1.amazonaws.com/userPosts/44938b73-afca-4cf9-a298-bd6c5a7bda46/97f5cd4d-e515-4775-b7b6-baf649f6c5c5/post_1000000041-1
        //     if (url.find("https://reelzapp.s3.us-east-1.amazonaws.com/") == 0) {
        //         path = url.substr(44);
        //     } else {
        //         LOG_WARN << "Invalid URL format: " << url;
        //         continue;
        //     }
            
        //     // Generate unique output filename
        //     std::string extension = (mediaType == "image") ? ".jpg" : ".mp4";
        //     std::string outputFilename = std::to_string(std::hash<std::string>{}(url + std::to_string(timestamp))) + extension;
            
        //     // Generate output path: processed/<filename>
        //     std::string outputKey = "processed/" + outputFilename;
        //     std::string outputUrl = "https://reelzapp.s3.us-east-1.amazonaws.com/"+ path.substr(0, path.find_last_of('/')) + "/" + outputKey;
            
        //     urlPairs.emplace_back(url, outputUrl);
            
        //     LOG_DEBUG << "Queued for processing: " << url << " -> " << outputUrl;
        // }
        
        if (urlPairs.empty()) {
            LOG_WARN << "No valid URLs to process after validation";
            return false;
        }
        
        // Process all files in parallel
        auto futures = processor.processBatch(urlPairs, bucket);
        
        // Wait for all processing to complete and collect results
        size_t successCount = 0;
        size_t failureCount = 0;
        
        for (size_t i = 0; i < futures.size(); ++i) {
            try {
                bool success = futures[i].get(); // Wait for completion
                if (success) {
                    successCount++;
                    LOG_INFO << "Successfully processed: " << urlPairs[i].first;
                    
                    // TODO: Update database with processed URL
                    // You might want to batch these updates or send them to another queue
                    updateProcessedMedia(postId, urlPairs[i].first, urlPairs[i].second, uploadType);
                } else {
                    failureCount++;
                    LOG_ERROR << "Failed to process: " << urlPairs[i].first;
                    
                    // TODO: Send to dead letter queue or implement retry logic
                    // handleProcessingFailure(postId, urlPairs[i].second, decodedMsg.traceId);
                }
            } catch (const std::exception& e) {
                failureCount++;
                LOG_ERROR << "Exception while processing " << urlPairs[i].first 
                         << ": " << e.what();
            }
        }
        
        // LOG_INFO << "Batch processing complete for post " << postId 
        //         << " - Success: " << successCount 
        //         << ", Failures: " << failureCount
        //         << ", Trace ID: " << decodedMsg.traceId;
        
        // Return true if at least one file was processed successfully
        return successCount > 0;
        
    } catch (const std::exception& e) {
        // LOG_ERROR << "Error in processS3Files: " << e.what() 
        //          << " (Trace ID: " << decodedMsg.traceId << ")";
        return false;
    }
}

void KafkaConsumerPlugin::handleProcessingFailure(const std::string& postId,
                                                 const std::string& failedUrl,
                                                 const std::string& traceId) {
    // TODO: Implement dead letter queue or retry mechanism
    
    LOG_ERROR << "Processing failed for URL: " << failedUrl 
              << " (Post: " << postId << ", Trace: " << traceId << ")";
    
    // Option 1: Send to a dead letter Kafka topic
    /*
    Json::Value failureMessage;
    failureMessage["post_id"] = postId;
    failureMessage["failed_url"] = failedUrl;
    failureMessage["trace_id"] = traceId;
    failureMessage["failure_time"] = std::chrono::system_clock::now().time_since_epoch().count();
    failureMessage["retry_count"] = 0; // Track retry attempts
    
    // Send to dead letter topic
    if (producer_) {
        std::string messageStr = failureMessage.toStyledString();
        cppkafka::MessageBuilder builder("media_processing_dlq");
        builder.partition(0);
        builder.payload(messageStr);
        producer_->produce(builder);
    }
    */
    
    // Option 2: Store in a database for manual review
    // Option 3: Schedule for retry with exponential backoff
}