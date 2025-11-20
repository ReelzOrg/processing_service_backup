#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <condition_variable>
#include <atomic>

// AWS SDK includes
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/CompletedPart.h>
#include <aws/core/Aws.h>
#include <aws/core/utils/stream/ResponseStream.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/core/utils/threading/Executor.h>
#include <aws/core/utils/logging/LogMacros.h>

// Forward declaration to avoid including Drogon headers
namespace drogon {
    class AppFramework;
}

class MediaProcessor {
public:
    struct VideoConfig {
        int width;
        int videoBitrate;
        int audioBitrate;
    };

    struct ProcessingConfig {
        std::string outputFormat;
        // int videoBitrate = 2000;  // in kbps
        // int audioBitrate = 128;   // in kbps
        //9:16 for reels and stories
        //width, videoBitrate, audioBitrate - Calculate height by maintaining aspect ratio
        std::vector<VideoConfig> res;
        int threads = 4;          // FFmpeg thread count
    };

    explicit MediaProcessor(const std::shared_ptr<Aws::S3::S3Client>& s3Client, 
                          const ProcessingConfig& config);
    
    ~MediaProcessor();

    // Process a single media file
    bool processFile(const std::string& sourceUrl, const std::string& destinationUrl, const std::string& bucketName);
    
    /**
     * @brief Process multiple media files in parallel
     * @param urlPairs Vector of source and destination URL pairs
     * @return Vector of futures representing the processing status of each file
     */
    std::vector<std::future<bool>> processBatch(
        const std::vector<std::pair<std::string, std::string>>& urlPairs, const std::string& bucketName);

private:
    class FFmpegPipe {
    public:
        FFmpegPipe();
        ~FFmpegPipe();
        
        bool open(const std::string& ffmpegCommand);
        bool isOpen() const;
        bool readChunk(char* buffer, size_t size, size_t& bytesRead);
        int getExitCode();  // Added to check FFmpeg exit status
        void close();
        
    private:
        FILE* pipe_;
        bool isOpen_;
    };

    // Main processing function using streaming
    bool processWithStreaming(const std::string& sourceUrl,
                            const std::string& destinationUrl,
                            const std::string& bucketName);
    
    // S3 multipart upload helpers
    std::string uploadPart(const std::string& bucket,
                        const std::string& key,
                        const std::string& uploadId,
                        const char* data,
                        size_t size,
                        int partNumber);
    
    bool completeMultipartUpload(const std::string& bucket,
                                const std::string& key,
                                const std::string& uploadId,
                                const std::vector<Aws::S3::Model::CompletedPart>& parts);
    
    void abortMultipartUpload(const std::string& bucket,
                            const std::string& key,
                            const std::string& uploadId);
    
    // Deprecated - kept for backward compatibility
    bool downloadAndProcessChunk(const std::string& sourceUrl, 
                                const std::string& destinationUrl);
    bool uploadChunk(const std::string& uploadId, 
                    const std::string& destinationUrl,
                    const char* data, 
                    size_t size,
                    int partNumber);
    
    std::shared_ptr<Aws::S3::S3Client> s3Client_;
    ProcessingConfig config_;
    std::shared_ptr<Aws::Utils::Threading::Executor> executor_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<int> activeTasks_;
    const unsigned int processorCount = std::thread::hardware_concurrency(); //returns the number of logical cores, not physical
    const int maxConcurrentTasks_ = processorCount > 0 ? 2*processorCount : 4;
};