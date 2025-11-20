#include "AWSPlugin.h"

#include <drogon/drogon.h>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/core/auth/AWSCredentials.h>

//https://docs.aws.amazon.com/code-library/latest/ug/cpp_1_s3_code_examples.html
void AWSPlugin::initAndStart(const Json::Value &config) {
	options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Debug;
	Aws::InitAPI(options_);

	// currently getting the keys from config.jsonc file becuase I am not setting these keys in the env variable of
	// my docker container (yes I know this is not the best approach to go with but for development this is easier)
	// const std::string accessKey = config.get("access_key", "").asString();
	// const std::string secretKey = config.get("secret_access_key", "").asString();
	// Aws::Auth::AWSCredentials credentials(
	// 	Aws::String(accessKey.c_str()),
	// 	Aws::String(secretKey.c_str())
	// );
	// auto credentialsProvider = Aws::MakeShared<Aws::Auth::SimpleAWSCredentialsProvider>("CredentialsProvider", accessKey, secretKey);
	
	Aws::Client::ClientConfiguration clientConfig;
	const char* envRegion = std::getenv("AWS_DEFAULT_REGION");
	if (envRegion && strlen(envRegion) > 0) {
    clientConfig.region = envRegion;
	} else {
		//set the region in the config file and add the region from there here instead of hardcoding
		clientConfig.region = "us-east-1";
	}

	// Note: Create a s3 client like this: Aws::S3::S3Client(clientConfig); creates the client
	// on the stack and is destroyed when the function returns. Since we want to keep the client
	// alive for the entire duration of the application, we use Aws::MakeShared to create a shared
	// pointer to the client.
	s3Client_ = Aws::MakeShared<Aws::S3::S3Client>(
		"S3Client",
		// credentials, #Credentials are auto detected by aws sdk from the env variables
		clientConfig,
		Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
		false
	);

	LOG_INFO << "AWS SDK initialized and S3 client created.";
}

void AWSPlugin::shutdown() {
	s3Client_.reset();
	Aws::ShutdownAPI(options_);
	LOG_INFO << "AWS SDK shutdown.";
}