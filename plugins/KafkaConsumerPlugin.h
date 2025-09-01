#pragma once
#include <drogon/drogon.h>
#include <cppkafka/cppkafka.h>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

using namespace drogon;
using namespace cppkafka;

class KafkaConsumerPlugin : public Plugin<KafkaConsumerPlugin> {
	public:
		// The initAndStart and shutdown methods are from the Plugin class which is a template class. These 2 methods
		// are called automatically when the drogon server is started and stopped.
		void initAndStart(const Json::Value &config) override;
		void shutdown() override;

	private:
		void consumeLoop();
		bool processS3Files(const std::string& topic,
			int partition,
			TopicPartition::Offset offset,
			const std::string& key,
			const xyz::virajdoshi::reelz::MediaProcessingJob& decodedMsg);
		xyz::virajdoshi::reelz::MediaProcessingJob decodeAvroMessage(const Buffer& payload);

		std::string brokers_;
		std::string groupId_;
		std::string topic_;
		uint32_t pollMs_{100};
		bool autoCommit_{false};
		std::string autoOffsetReset_{"earliest"};

		std::unique_ptr<Consumer> consumer_;
		std::thread consumerThread_;
		//running_ here is not a simple bool because it will be used by multiple threads and if one changes its value
		//while other reads it then you know what happens
		std::atomic<bool> running_{false};
};