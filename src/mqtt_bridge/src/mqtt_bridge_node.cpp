#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>
#include <sstream>
#include <mqtt/async_client.h>
#include <ros/console.h>
#define RAPIDJSON_HAS_STDSTRING 1
class JsonAssertException : public std::exception
{
public:
	char const *what()
	{
		return "JSON assert exception";
	}
};
// custom assert(x) for rapidjson library so that it doesn't abort the program on errors
#define RAPIDJSON_ASSERT(x) (static_cast<bool>(x) ? void(0) : throw JsonAssertException())
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"
#include "mqtt_bridge/ROSTopicHandler.hpp"

// TODO:
// - rosdep (for paho)?

ros::Time unixMillisecondsToROSTimestamp(unsigned long int msec)
{
	ros::Time timestamp;
	timestamp.fromSec(msec / (double)1000.0);
	return timestamp;
}

void processMqttRoverControlMessage(const char *payloadMsg, std::shared_ptr<ROSTopicHandler> rth)
{
	rapidjson::Document d;
	rapidjson::ParseResult ok = d.Parse(payloadMsg);

	if (!ok)
	{
		ROS_WARN_STREAM("JSON parse error: " << rapidjson::GetParseError_En(ok.Code()) << " (" << ok.Offset() << "), discarding MQTT message.");
	}
	else
	{
		try
		{
			can_wrapper::RoverControl msg;

			msg.XVelAxis = d["XVelAxis"].GetDouble();
			msg.ZRotAxis = d["ZRotAxis"].GetDouble();

			msg.header.stamp = unixMillisecondsToROSTimestamp(d["Timestamp"].GetUint64());

			rth->publishMessage_RoverControl(msg);
		}
		catch (JsonAssertException e)
		{
			ROS_WARN("JSON assert exception, discarding MQTT message.");
		}
	}
}

void processMqttManipulatorControlMessage(const char *payloadMsg, std::shared_ptr<ROSTopicHandler> rth)
{
	rapidjson::Document d;
	rapidjson::ParseResult ok = d.Parse(payloadMsg);

	if (!ok)
	{
		ROS_WARN_STREAM("JSON parse error: " << rapidjson::GetParseError_En(ok.Code()) << " (" << ok.Offset() << "), discarding MQTT message.");
	}
	else
	{
		try
		{
			can_wrapper::ManipulatorControl msg;

			msg.Axis1 = d["Axis1"].GetDouble();
			msg.Axis2 = d["Axis2"].GetDouble();
			msg.Axis3 = d["Axis3"].GetDouble();
			msg.Axis4 = d["Axis4"].GetDouble();
			msg.Axis5 = d["Axis5"].GetDouble();
			msg.Gripper = d["Gripper"].GetDouble();

			msg.header.stamp = unixMillisecondsToROSTimestamp(d["Timestamp"].GetUint64());

			rth->publishMessage_ManipulatorControl(msg);
		}
		catch (JsonAssertException e)
		{
			ROS_WARN("JSON assert exception, discarding MQTT message.");
		}
	}
}

void processMqttMissionStatusMessage(const char *payloadMsg, std::shared_ptr<ROSTopicHandler> rth)
{
	rapidjson::Document d;
	rapidjson::ParseResult ok = d.Parse(payloadMsg);

	if (!ok)
	{
		ROS_WARN_STREAM("JSON parse error: " << rapidjson::GetParseError_En(ok.Code()) << " (" << ok.Offset() << "), discarding MQTT message.");
	}
	else
	{
		try
		{
			can_wrapper::MissionStatus msg;

			msg.MissionStatus = d["MissionStatus"].GetUint();

			msg.header.stamp = unixMillisecondsToROSTimestamp(d["Timestamp"].GetUint64());

			rth->publishMessage_MissionStatus(msg);
		}
		catch (JsonAssertException e)
		{
			ROS_WARN("JSON assert exception, discarding MQTT message.");
		}
	}
}

void processMqttRoverStatusMessage(const char *payloadMsg, std::shared_ptr<ROSTopicHandler> rth)
{
	rapidjson::Document d;
	rapidjson::ParseResult ok = d.Parse(payloadMsg);

	if (!ok)
	{
		ROS_WARN_STREAM("JSON parse error: " << rapidjson::GetParseError_En(ok.Code()) << " (" << ok.Offset() << "), discarding MQTT message.");
	}
	else
	{
		try
		{
			can_wrapper::RoverStatus msg;

			msg.CommunicationState = d["CommunicationState"].GetUint();
			msg.PadConnected = d["PadConnected"].GetBool();
			msg.ControlMode = d["ControlMode"].GetUint();

			msg.header.stamp = unixMillisecondsToROSTimestamp(d["Timestamp"].GetUint64());

			rth->publishMessage_RoverStatus(msg);
		}
		catch (JsonAssertException e)
		{
			ROS_WARN("JSON assert exception, discarding MQTT message.");
		}
	}
}

int main(int argc, char *argv[])
{
	ros::init(argc, argv, "mqtt_bridge_node");
	std::shared_ptr<ros::NodeHandle> n = std::make_shared<ros::NodeHandle>();

	// set ROS console logger level to DEBUG
	if (ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Debug))
	{
		ros::console::notifyLoggerLevelsChanged();
	}


	// ### MQTT configuration ###
	const std::string DEFAULT_SERVER_ADDRESS("mqtt://192.168.10.20:1883");
	const std::string CLIENT_ID("mqtt_bridge_node_ros");
	const int MQTT_VERSION = MQTTVERSION_5;
	const int SESSION_EXPIRY = 604800;
	const int PUBLISHER_QOS = 0;
	const int KEEP_ALIVE = 20;
	const std::chrono::seconds RECONNECT_MIN_RETRY_INTERVAL{1};
	const std::chrono::seconds RECONNECT_MAX_RETRY_INTERVAL{16};
	const bool CLEAN_START = false;

	auto SUBSCRIBED_TOPICS_NAMES = mqtt::string_collection::create(
		{"RappTORS/RoverControl", "RappTORS/ManipulatorControl", "RappTORS/MissionStatus", "RappTORS/RoverStatus"});
	const std::vector<int> SUBSCRIBED_TOPICS_QOS{0, 0, 0, 0};

	// get MQTT server address from the parameter server; if param not present, then default to DEFAULT_SERVER_ADDRESS
	std::string SERVER_ADDRESS;
  	n->param<std::string>("/MQTT/server_address", SERVER_ADDRESS, DEFAULT_SERVER_ADDRESS);

	std::shared_ptr<mqtt::async_client> cli = std::make_shared<mqtt::async_client>(SERVER_ADDRESS, CLIENT_ID,
													 mqtt::create_options(MQTT_VERSION));


	auto connOpts = mqtt::connect_options_builder()
						.properties({{mqtt::property::SESSION_EXPIRY_INTERVAL, SESSION_EXPIRY}})
						.clean_start(CLEAN_START)
						.keep_alive_interval(std::chrono::seconds(KEEP_ALIVE))
						.automatic_reconnect(RECONNECT_MIN_RETRY_INTERVAL, RECONNECT_MAX_RETRY_INTERVAL)
						.finalize();

	std::shared_ptr<ROSTopicHandler> rth = std::make_shared<ROSTopicHandler>(cli, n, PUBLISHER_QOS);

	// callback for connection lost to MQTT broker
	cli->set_connection_lost_handler([](const std::string &)
									 { ROS_ERROR("Connection to MQTT server lost. Trying to reconnect..."); });

	// callback for incoming MQTT messages
	cli->set_message_callback([rth](mqtt::const_message_ptr mqtt_msg) {
		auto messageTopic = mqtt_msg->get_topic();

		ROS_DEBUG("I received (MQTT): [%s] on topic: [%s]", mqtt_msg->get_payload_str().c_str(), messageTopic.c_str());

		if (messageTopic == "RappTORS/RoverControl") {
			processMqttRoverControlMessage(mqtt_msg->get_payload_str().c_str(), rth);
		} else if (messageTopic == "RappTORS/ManipulatorControl") {
			processMqttManipulatorControlMessage(mqtt_msg->get_payload_str().c_str(), rth);
		} else if (messageTopic == "RappTORS/MissionStatus") {
			processMqttMissionStatusMessage(mqtt_msg->get_payload_str().c_str(), rth);
		} else if (messageTopic == "RappTORS/RoverStatus") {
			processMqttRoverStatusMessage(mqtt_msg->get_payload_str().c_str(), rth);
		} else {
			ROS_WARN_STREAM("Unknown MQTT topic: " << messageTopic << ", discarding MQTT message.");
		} });

	// loop for connecting to MQTT broker (repeat on failure)
	while (true) {
		// Start the MQTT connection, subscribe to MQTT topics.
		try
		{
			ROS_INFO_STREAM("Trying to connect to MQTT server at " << SERVER_ADDRESS << " ...");
			auto tok = cli->connect(connOpts);
			tok->wait();
			ROS_INFO("Successfully connected to MQTT server.");

			cli->subscribe(SUBSCRIBED_TOPICS_NAMES, SUBSCRIBED_TOPICS_QOS);
			break;
		}
		catch (const mqtt::exception &exc)
		{
			ROS_ERROR_STREAM("Error connecting to MQTT server: " << exc.what() << ", retrying in 5 seconds...");
			ros::Duration(5).sleep();
			if (ros::isShuttingDown()) {
				ros::waitForShutdown();
				return 1;
			}
		}
	}


	ros::spin();

	// Disconnect from MQTT broker
	try
	{
		std::cout << "Disconnecting from the MQTT server..." << std::endl;
		cli->disconnect()->wait();
		std::cout << "Disconnected from the MQTT server." << std::endl;
	}
	catch (const mqtt::exception &exc)
	{
		std::cout << "Error disconnecting from MQTT server: " << exc.what() << std::endl;
		return 1;
	}

	return 0;
}
