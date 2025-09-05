#pragma once

#include <drogon/drogon.h>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>

class AWSPlugin : public drogon::Plugin<AWSPlugin> {
	public:
		virtual void initAndStart(const Json::Value &config) override;
		virtual void shutdown() override;
		std::shared_ptr<Aws::S3::S3Client> getS3Client() { return s3Client_; }

	private:
		Aws::SDKOptions options_;
		std::shared_ptr<Aws::S3::S3Client> s3Client_;
};