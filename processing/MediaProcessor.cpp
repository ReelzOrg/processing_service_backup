#include <iostream>
#include <utility>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <aws/core/utils/stream/SimpleStreamBuf.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/AbortMultipartUploadRequest.h>
#include <aws/core/utils/threading/Executor.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include "processing/MediaProcessor.h"
#include "plugins/AWSPlugin.h"

using namespace Aws::S3::Model;
using namespace Aws::Utils;

namespace {
    constexpr size_t CHUNK_SIZE = 5 * 1024 * 1024; // 5MB chunks for S3 multipart upload
    constexpr int MAX_RETRIES = 3;
    constexpr int RETRY_DELAY_MS = 1000;
    constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024; // 64KB buffer for streaming

    std::string parseS3Url(const std::string& url) {
        static const std::string PREFIX = "https://reelzapp.s3.us-east-1.amazonaws.com/";
        if (url.find(PREFIX) != 0) return "";
        return url.substr(PREFIX.length());
    }

    std::string generatePresignedUrl(const std::shared_ptr<Aws::S3::S3Client>& s3Client,
                                    const std::string& bucket,
                                    const std::string& key,
                                    uint64_t expirationSeconds = 3600) {
        // Generate a pre-signed URL for the S3 object
        return s3Client->GeneratePresignedUrl(bucket, key, Aws::Http::HttpMethod::HTTP_GET,
            expirationSeconds);
    }
}

// FFmpegPipe implementation
MediaProcessor::FFmpegPipe::FFmpegPipe() : pipe_(nullptr), isOpen_(false) {}

MediaProcessor::FFmpegPipe::~FFmpegPipe() {
    close();
}

bool MediaProcessor::FFmpegPipe::open(const std::string& ffmpegCommand) {
    close();
    pipe_ = popen(ffmpegCommand.c_str(), "r");
    isOpen_ = (pipe_ != nullptr);
    return isOpen_;
}

bool MediaProcessor::FFmpegPipe::isOpen() const {
    return isOpen_ && pipe_ != nullptr;
}

bool MediaProcessor::FFmpegPipe::readChunk(char* buffer, size_t size, size_t& bytesRead) {
    if (!isOpen()) return false;
    bytesRead = fread(buffer, 1, size, pipe_);
    return bytesRead > 0 || !ferror(pipe_);
}

int MediaProcessor::FFmpegPipe::getExitCode() {
    if (pipe_) {
        int status = pclose(pipe_);
        pipe_ = nullptr;
        isOpen_ = false;
        return WEXITSTATUS(status);
    }
    return -1;
}

void MediaProcessor::FFmpegPipe::close() {
    if (pipe_) {
        pclose(pipe_);
        pipe_ = nullptr;
    }
    isOpen_ = false;
}

// MediaProcessor implementation
MediaProcessor::MediaProcessor(const std::shared_ptr<Aws::S3::S3Client>& s3Client, 
                             const ProcessingConfig& config)
    : s3Client_(s3Client), 
      config_(config),
      executor_(Aws::MakeShared<Aws::Utils::Threading::DefaultExecutor>("MediaProcessor")),
      activeTasks_(0) {
    if (!s3Client_) {
        throw std::invalid_argument("S3 client cannot be null");
    }
    AWS_LOGSTREAM_INFO("MediaProcessor", "Initialized with " 
        << config_.threads << " threads, max concurrent tasks: " << maxConcurrentTasks_);
}

MediaProcessor::~MediaProcessor() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return activeTasks_ == 0; });
    AWS_LOGSTREAM_INFO("MediaProcessor", "Shutdown complete");
}

bool MediaProcessor::processFile(const std::string& sourceUrl, const std::string& destinationUrl, const std::string& bucketName) {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return activeTasks_ < maxConcurrentTasks_; });
    activeTasks_++;
    lock.unlock();

    bool result = false;
    try {
        result = processWithStreaming(sourceUrl, destinationUrl, bucketName);
    } catch (const std::exception& e) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", 
            "Error processing " << destinationUrl << ": " << e.what());
    }

    lock.lock();
    activeTasks_--;
    cv_.notify_all();
    
    return result;
}

std::vector<std::future<bool>> MediaProcessor::processBatch(
    const std::vector<std::pair<std::string, std::string>>& urlPairs, const std::string& bucketName) {
    
    std::vector<std::future<bool>> futures;
    
    for (const auto& urlPair : urlPairs) {
        // std::launch::async forces asynchronous execution on a new thread.
        // You can omit the launch policy, but behavior is then implementation-defined (may defer execution).
        auto future = std::async(std::launch::async, [this, urlPair, bucketName]() -> bool {
            return processFile(urlPair.first, urlPair.second, bucketName);
        });
        
        futures.push_back(std::move(future));
        AWS_LOGSTREAM_DEBUG("MediaProcessor", 
            "Submitted task " << urlPair.first << " -> " << urlPair.second);
    }
    
    AWS_LOGSTREAM_INFO("MediaProcessor", 
        "Submitted " << urlPairs.size() << " files for processing");
    return futures;
}

std::pair<int, int> getVideoDimensions(const std::string& presignedUrl) {
    std::ostringstream cmd_stream;
    cmd_stream << "ffprobe -v error -select_streams v:0 -show_entries stream=width,height "
               << "-of csv=s=x:p=0 \"" << presignedUrl << "\"";

    std::string command = cmd_stream.str();
    std::string dimensions_output;

    FILE* pipe;
    // Execute ffprobe command
    // For windows switch to _popen and _pclose
    #if defined(_WIN32) || defined(_WIN64)
    pipe = _popen(command.c_str(), "r");
    #elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__MACH__) || defined(__unix__) || defined(__unix)
    pipe = popen(command.c_str(), "r");
    #else
    #error "Unsupported platform"
    #endif

    if (!pipe) {
        std::cerr << "Error: popen failed to run ffprobe." << std::endl;
        return std::make_pair(0, 0);
    }

    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        dimensions_output += buffer;
    }

    #if defined(_WIN32) || defined(_WIN64)
    _pclose(pipe);
    #elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__MACH__) || defined(__unix__) || defined(__unix)
    pclose(pipe);
    #else
    #error "Unsupported platform"
    #endif
    
    // Trim any trailing whitespace/newline
    if (!dimensions_output.empty() && dimensions_output.back() == '\n') {
        dimensions_output.pop_back();
    }

    size_t x_pos = dimensions_output.find('x');
    if (x_pos == std::string::npos) {
        return std::make_pair(0, 0);
    }

    int width = std::stoi(dimensions_output.substr(0, x_pos));
    int height = std::stoi(dimensions_output.substr(x_pos + 1));

    return std::make_pair(width, height);
}

bool MediaProcessor::processWithStreaming(const std::string& sourceUrl, const std::string& destinationUrl, const std::string& bucketName) {
    auto sourceKey = parseS3Url(sourceUrl);
    if (sourceKey.empty()) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", "Invalid source URL: " << sourceUrl);
        return false;
    }

    auto destKey = parseS3Url(destinationUrl);
    if (destKey.empty()) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", "Invalid destination URL: " << destinationUrl);
        return false;
    }

    // Get file metadata
    HeadObjectRequest headRequest;
    headRequest.WithBucket(bucketName).WithKey(sourceKey);
    
    HeadObjectOutcome headOutcome = s3Client_->HeadObject(headRequest);
    if (!headOutcome.IsSuccess()) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", 
            "Failed to get file info: " << headOutcome.GetError().GetMessage());
        return false;
    }
    
    const auto& contentType = headOutcome.GetResult().GetContentType();
    //The users directly upload the video file to s3 hence node.js apigateway can't process and convert the file format to mp4
    //hence we need to check if the format is .mov or .avi or anything else
    bool isVideo = contentType.find("video") != std::string::npos ||
                   sourceKey.find(".mp4") != std::string::npos ||
                   sourceKey.find(".mov") != std::string::npos ||
                   sourceKey.find(".avi") != std::string::npos;
    
    // Generate pre-signed URL for FFmpeg to access
    std::string presignedUrl = generatePresignedUrl(s3Client_, bucketName, sourceKey);
    auto dimensions = getVideoDimensions(presignedUrl);
    if (dimensions.first == 0 || dimensions.second == 0) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", "Failed to get video dimensions");
        return false;
    }

    float sourceAspectRatio = static_cast<float>(dimensions.first) / dimensions.second;
    float targetAspectRatio = 9.0f / 16.0f;
    std::ostringstream vf;

    // if(std::abs(sourceAspectRatio - targetAspectRatio) < 0.01f) {
    //     vf << "scale=" << config_.width << ":" << config_.height;
    // } else {
        
    // }
    
    // Build FFmpeg command
    std::ostringstream cmd;
    cmd << "ffmpeg -i \"" << presignedUrl << "\" ";
    
    if (isVideo) {
        // cmd << "-c:v libx264 -b:v " << config_.videoBitrate << "k ";
        // cmd << "-c:a aac -b:a " << config_.audioBitrate << "k ";
        // cmd << "-vf scale=" << config_.width << ":" << config_.height << " ";
        // cmd << "-preset fast -movflags frag_keyframe+empty_moov ";
        // cmd << "-threads " << config_.threads << " ";
        // cmd << "-f " << config_.outputFormat << " ";
    } else {
        // Image processing
        // cmd << "-vf scale=" << config_.width << ":" << config_.height << " ";
        // cmd << "-q:v 2 "; // JPEG quality
        // cmd << "-f image2 ";
    }
    
    cmd << "pipe:1 2>/dev/null"; // Output to stdout, suppress stderr
    
    // Start multipart upload
    CreateMultipartUploadRequest createRequest;
    createRequest.SetBucket(bucketName);
    createRequest.SetKey(destKey);
    createRequest.SetContentType(isVideo ? "video/mp4" : "image/jpeg");
    
    auto createOutcome = s3Client_->CreateMultipartUpload(createRequest);
    if (!createOutcome.IsSuccess()) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", 
            "Failed to create multipart upload: " << createOutcome.GetError().GetMessage());
        return false;
    }
    
    std::string uploadId = createOutcome.GetResult().GetUploadId();
    std::vector<CompletedPart> completedParts;
    bool success = true;
    
    // Process with FFmpeg and upload in chunks
    FFmpegPipe pipe;
    if (!pipe.open(cmd.str())) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", "Failed to start FFmpeg process");
        abortMultipartUpload(bucketName, destKey, uploadId);
        return false;
    }
    
    AWS_LOGSTREAM_INFO("MediaProcessor", 
        "Started processing " << sourceUrl << " -> " << destinationUrl);
    
    // Buffer for accumulating data to meet minimum part size
    std::vector<char> accumulatedData;
    accumulatedData.reserve(CHUNK_SIZE);
    
    char readBuffer[STREAM_BUFFER_SIZE];
    size_t bytesRead = 0;
    int partNumber = 1;
    size_t totalBytesProcessed = 0;
    
    while (pipe.readChunk(readBuffer, STREAM_BUFFER_SIZE, bytesRead) && bytesRead > 0) {
        // Accumulate data
        accumulatedData.insert(accumulatedData.end(), readBuffer, readBuffer + bytesRead);
        totalBytesProcessed += bytesRead;
        
        // Upload when we have enough data (5MB minimum for multipart, except last part)
        if (accumulatedData.size() >= CHUNK_SIZE) {
            auto etag = uploadPart(bucketName, destKey, uploadId, 
                                 accumulatedData.data(), accumulatedData.size(), 
                                 partNumber);
            
            if (etag.empty()) {
                success = false;
                break;
            }
            
            CompletedPart part;
            part.SetPartNumber(partNumber);
            part.SetETag(etag);
            completedParts.push_back(part);
            
            partNumber++;
            accumulatedData.clear();
            
            AWS_LOGSTREAM_DEBUG("MediaProcessor", 
                "Uploaded part " << (partNumber - 1) << " (" 
                << totalBytesProcessed << " bytes total)");
        }
    }
    
    // Upload any remaining data as the last part
    if (success && !accumulatedData.empty()) {
        auto etag = uploadPart(bucketName, destKey, uploadId, 
                             accumulatedData.data(), accumulatedData.size(), 
                             partNumber);
        
        if (!etag.empty()) {
            CompletedPart part;
            part.SetPartNumber(partNumber);
            part.SetETag(etag);
            completedParts.push_back(part);
        } else {
            success = false;
        }
    }
    
    // Check FFmpeg exit code
    int exitCode = pipe.getExitCode();
    if (exitCode != 0) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", 
            "FFmpeg exited with code " << exitCode);
        success = false;
    }
    
    // Complete or abort the multipart upload
    if (success && !completedParts.empty()) {
        success = completeMultipartUpload(bucketName, destKey, uploadId, completedParts);
        if (success) {
            AWS_LOGSTREAM_INFO("MediaProcessor", 
                "Successfully processed " << sourceUrl << " -> " << destinationUrl 
                << " (" << totalBytesProcessed << " bytes)");
        }
    } else {
        abortMultipartUpload(bucketName, destKey, uploadId);
        AWS_LOGSTREAM_ERROR("MediaProcessor", 
            "Failed to process " << sourceUrl);
    }
    
    return success;
}

std::string MediaProcessor::uploadPart(const std::string& bucket,
                                      const std::string& key,
                                      const std::string& uploadId,
                                      const char* data,
                                      size_t size,
                                      int partNumber) {
    // for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    //     try {
    //         // Create a proper input stream from the binary data
    //         auto stream = Aws::MakeShared<Aws::StringStream>("UploadStream");
    //         stream->write(data, size);
            
    //         UploadPartRequest uploadRequest;
    //         uploadRequest.SetBucket(bucket);
    //         uploadRequest.SetKey(key);
    //         uploadRequest.SetUploadId(uploadId);
    //         uploadRequest.SetPartNumber(partNumber);
    //         uploadRequest.SetContentLength(size);
    //         uploadRequest.SetBody(stream);
            
    //         auto outcome = s3Client_->UploadPart(uploadRequest);
    //         if (outcome.IsSuccess()) {
    //             return outcome.GetResult().GetETag();
    //         }
            
    //         AWS_LOGSTREAM_WARN("MediaProcessor", 
    //             "Upload part failed (attempt " << (attempt + 1) << "/" 
    //             << MAX_RETRIES << "): " << outcome.GetError().GetMessage());
            
    //     } catch (const std::exception& e) {
    //         AWS_LOGSTREAM_WARN("MediaProcessor", 
    //             "Exception during upload (attempt " << (attempt + 1) << "/" 
    //             << MAX_RETRIES << "): " << e.what());
    //     }
        
    //     if (attempt < MAX_RETRIES - 1) {
    //         std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS * (attempt + 1)));
    //     }
    // }
    
    return ""; // Empty string indicates failure
}

bool MediaProcessor::completeMultipartUpload(const std::string& bucket,
                                            const std::string& key,
                                            const std::string& uploadId,
                                            const std::vector<CompletedPart>& parts) {
    CompleteMultipartUploadRequest completeRequest;
    completeRequest.SetBucket(bucket);
    completeRequest.SetKey(key);
    completeRequest.SetUploadId(uploadId);
    
    CompletedMultipartUpload completedUpload;
    completedUpload.SetParts(parts);
    completeRequest.WithMultipartUpload(completedUpload);
    
    auto completeOutcome = s3Client_->CompleteMultipartUpload(completeRequest);
    if (!completeOutcome.IsSuccess()) {
        AWS_LOGSTREAM_ERROR("MediaProcessor", 
            "Failed to complete multipart upload: " 
            << completeOutcome.GetError().GetMessage());
        return false;
    }
    
    return true;
}

void MediaProcessor::abortMultipartUpload(const std::string& bucket,
                                         const std::string& key,
                                         const std::string& uploadId) {
    AbortMultipartUploadRequest abortRequest;
    abortRequest.SetBucket(bucket);
    abortRequest.SetKey(key);
    abortRequest.SetUploadId(uploadId);
    
    auto outcome = s3Client_->AbortMultipartUpload(abortRequest);
    if (!outcome.IsSuccess()) {
        AWS_LOGSTREAM_WARN("MediaProcessor", 
            "Failed to abort multipart upload: " << outcome.GetError().GetMessage());
    }
}