#include <drogon/drogon.h>
#include <json/json.h>
#include <vector>
#include <cstring> //for memcpy

// Avro-specific includes for decoding
#include <avro/Decoder.hh>
#include <avro/Specific.hh>
#include <avro/Stream.hh>
#include <avro/Exception.hh>
#include <avro/DataFile.hh>

#include "plugins/KafkaConsumerPlugin.h"
#include "json_utils.h"
#include "processing/MediaProcessor.h"
#include "MediaProcessingJob.hh"	//This is from the generated file from avro schema which encodes & decodes messages

using namespace cppkafka;
using namespace drogon;

//http://<schema-registry-url>/schemas/ids/<schema-id>

void KafkaConsumerPlugin::initAndStart(const Json::Value &config) override {
  //all the variables here are defined in the kafkaConsumerPlugin.h file as private members of the class
	brokers_ = config.get("brokers", "localhost:9092").asString();
	groupId_ = config.get("group_id", "media-consumer-group").asString();
	
	// the topic name should be same as in the utils/kafka/types.js file in the node.js server
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

		// The payload is a Buffer
		auto payload = msg.get_payload();
		xyz::virajdoshi::reelz::MediaProcessingJob decodedMsg = decodeAvroMessage(payload);
		auto key = msg.get_key() ? msg.get_key()->to_string() : "";
		auto topic = msg.get_topic();
		auto partition = msg.get_partition();
		auto offset = msg.get_offset();

		app().getThreadPool()->runTask([=] {
			if (!autoCommit_) {
				consumer_->commit();
			}
			processS3Files(topic, partition, offset, key, decodedMsg);
		});
	}
}

void KafkaConsumerPlugin::processS3Files(const std::string& topic, int partition, TopicPartition::Offset offset,
	const std::string& key, const xyz::virajdoshi::reelz::MediaProcessingJob& decodedMsg) {
	LOG_DEBUG << "Kafka msg: " << topic << "/" << partition << " offset=" << offset << " key=" << key << " decodedMsg=" << decodedMsg;

	if (decodedMsg.toProcessUrls.empty()) {
		LOG_WARN << "No urls to process";
		return;
	}
	std::string uploadType = decodedMsg.uploadType;	//media_processing
	std::string post_id = decodedMsg.post_id;
	long long timeStamp = decodedMsg.timeStamp;	//do I need it?

	//process each url
	for (const auto& url : decodedMsg.toProcessUrls) {

	}
}

/**
 * Decodes an Avro message from a Buffer.
 * @param payload The Buffer containing the Avro message
 * @return A MediaProcessingJob type containing the decoded message
 */
xyz::virajdoshi::reelz::MediaProcessingJob KafkaConsumerPlugin::decodeAvroMessage(const Buffer& payload) {
	// The first 5 bytes is the magic byte (0) and the schema ID
	if(payload.get_size() < 5) {
		LOG_WARN << "Avro message too short to contain a schema id";
		return MediaProcessingJob();
	}

	// The first byte is the magic byte, must be 0
	const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.get_data());
	if(data[0] != 0x0) {
		LOG_WARN << "Invalid Avro message: missing magic byte";
		return MediaProcessingJob();
	}

	// The next four bytes are the schema ID in big-endian format
	int32_t schemaIdBigEndian;
	memcpy(&schemaIdBigEndian, data + 1, 4);
	int32_t schemaId = ntohl(schemaIdBigEndian);

	// Get the Avro payload (data after the 5-byte header)
	const uint8_t* avroData = data + 5;
	size_t avroDataLen = payload.get_size() - 5;

	try {
		std::unique_ptr<avro::InputStream> inStream = avro::memoryInputStream(avroData, avroDataLen);
		avro::DecoderPtr decoder = avro::binaryDecoder();
		decoder->init(*inStream);

		// Create an instance of your generated Avro class & decode the message
		xyz::virajdoshi::reelz::MediaProcessingJob job;
		avro::decode(*decoder, job);

		LOG_INFO << "Avro message: " << job.toProcessUrls.size() << " urls";
		return job;
	} catch (const std::exception& e) {
		LOG_ERROR << "Error decoding Avro message: " << e.what();
		return MediaProcessingJob();
	}
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