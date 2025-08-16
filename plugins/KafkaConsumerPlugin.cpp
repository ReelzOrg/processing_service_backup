#include <drogon/drogon.h>
#include <json/json.h>

#include "plugins/KafkaConsumerPlugin.h"
#include "json_utils.h"

using namespace cppkafka;
using namespace drogon;

void KafkaConsumerPlugin::initAndStart(const Json::Value &config) override {
  //all the variables here are defined in the kafkaConsumerPlugin.h file as private members of the class
	brokers_ = config.get("brokers", "localhost:9092").asString();
	groupId_ = config.get("group_id", "media-consumer-group").asString();
	topic_ = config.get("topic", "media_processing").asString();
	pollMs_  = config.get("poll_ms", 100).asUInt();
	autoCommit_ = config.get("enable_auto_commit", false).asBool();
	autoOffsetReset_ = config.get("auto_offset_reset", "latest").asString();

  //The kafka topic here is hardcoded but when deplying I should probably create a config file which will be shared
  //between this c++ service and the node.js api gateway
	Configuration cfg = {
		{ "metadata.broker.list", brokers_ },
		{ "group.id", groupId_ },
		{ "enable.auto.commit", autoCommit_ ? "true" : "false" },
		{ "auto.offset.reset", autoOffsetReset_ } // earliest|latest
	};

	consumer_ = std::make_unique<Consumer>(cfg);
	consumer_->subscribe({ topic_ });

	running_.store(true);
	consumerThread_ = std::thread([this] { this->consumeLoop(); });
	LOG_INFO << "KafkaConsumerPlugin started: topic=" << topic_ << " group=" << groupId_;
}

void KafkaConsumerPlugin::consumeLoop() {
	while (running_.load()) {
		auto msg = consumer_->poll(std::chrono::milliseconds(pollMs_));
		if (!msg) continue;

		if (msg.get_error()) {
			if (!msg.is_eof()) {
				LOG_WARN << "Kafka error: " << msg.get_error();
			}
			continue;
		}

		auto payload = msg.get_payload().to_string();
		auto key = msg.get_key() ? msg.get_key()->to_string() : "";
		auto topic = msg.get_topic();
		auto partition = msg.get_partition();
		auto offset = msg.get_offset();

		app().getThreadPool()->runTask([=] {
			if (!autoCommit_ && processMessage(topic, partition, offset, key, payload)) {
				consumer_->commit();
			}
		});
	}
}

bool KafkaConsumerPlugin::processMessage(const std::string& topic, int partition, TopicPartition::Offset offset,
	const std::string& key, const std::string& payload) {
	LOG_DEBUG << "Kafka msg: " << topic << "/" << partition << " offset=" << offset << " key=" << key << " payload=" << payload;
	
	// TODO: Your processing logic here
	auto json_payload = utils::parseJsonPayload(payload);
	
	std::string uploadType = json_payload["uploadType"].asString();	//media_processing
	std::string post_id = json_payload["post_id"].asString();
	long long timeStamp = json_payload["timeStamp"].asInt64();	//do I need it?

	std::vector<std::string> urls;
	for (const auto& url : json_payload["toProcessUrls"]) {
		urls.push_back(url.asString());
	}

	return true;
}

void KafkaConsumerPlugin::shutdown() override {
	running_.store(false);
	try {
		if (consumer_) {
			consumer_->unsubscribe();
			consumer_->close(); // triggers EOF for pollers
		}
	} catch (const std::exception& e) {
		LOG_ERROR << "Error closing Kafka consumer: " << e.what();
	}
	if (consumerThread_.joinable()) consumerThread_.join();
	consumer_.reset();
	LOG_INFO << "KafkaConsumerPlugin stopped";
}