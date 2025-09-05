#include "AWSPlugin.h"

#include <drogon/drogon.h>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>

//https://docs.aws.amazon.com/code-library/latest/ug/cpp_1_s3_code_examples.html

void AWSPlugin::initAndStart(const Json::Value &config) {
	options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Debug;
	Aws::InitAPI(options_);

	// currently getting the keys from config.jsonc file becuase I am not setting these keys in the env variable of
	// my docker container (yes I know this is not the best approach to go with but for development this is easier)
	const std::string accessKey = config.get("access_key", "").asString();
	const std::string secretKey = config.get("secret_access_key", "").asString();
	Aws::Auth::AWSCredentials credentials(
		Aws::String(accessKey.c_str()),
		Aws::String(secretKey.c_str())
	);
	
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.region = Aws::Region::US_EAST_1;

	s3Client_ = Aws::MakeShared<Aws::S3::S3Client>("S3Client", credentials, clientConfig);

	LOG_INFO << "AWS SDK initialized and S3 client created.";
}

void AWSPlugin::shutdown() {
	s3Client_.reset();
	Aws::ShutdownAPI(options_);
	LOG_INFO << "AWS SDK shutdown.";
}