#pragma once

#include <iostream>
#include <fstream>
#include <zstd.h>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
// #include <aws/core/utils/Outcome.h>

class AWSUtils {
  public:
    Aws::S3::S3Client s3Client;
    Aws::Client::ClientConfiguration clientConfig;
    Aws::SDKOptions options;

    AWSUtils() {
      Aws::InitAPI(options);

      // Aws::Auth::AWSCredentials credentials("AKIAIOSFODNN7EXAMPLE", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");
      Aws::Auth::AWSCredentials credentials();

      clientConfig.region = Aws::Region::US_EAST_1;
      s3Client = Aws::S3::S3Client(credentials, clientConfig);
    }

    //Destructor
    ~AWSUtils() {
      Aws::ShutdownAPI(options);
    }

    void downloadFileFromS3(const std::string& bucketName, const std::string& objectKey) override;
};

// Aws::SDKOptions options;
// Aws::InitAPI(options);

// 1. Create S3 client and request object from S3 url
// Aws::S3::S3Client s3Client;

// std::string bucketName = "reelz";
// std::string objectKey = "path/to/file.mp4"; // get the key from the s3 urls

// Aws::S3::Model::GetObjectRequest getObjectRequest;
// getObjectRequest.SetBucket(bucketName);
// getObjectRequest.SetKey(objectKey);

// 2. Download the file and stream it for compression
// auto getObjectOutcome = s3Client.GetObject(getObjectRequest);

// if (getObjectOutcome.IsSuccess()) {
//   auto& stream = getObjectOutcome.GetResult().GetBody();
//   // Here you can pipe the input stream to your compression algorithm.
//   // For example, using a placeholder compression function:
//   compressAndSave(stream, "local_compressed_file");
//   LOG_INFO << "Successfully processed and compressed file from " << s3Url;
// } else {
//   LOG_ERROR << "Failed to download file from S3: " << getObjectOutcome.GetError().GetMessage();
// }

// Aws::ShutdownAPI(options);