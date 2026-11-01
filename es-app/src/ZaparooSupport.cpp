#include "ZaparooSupport.h"
#include "HttpReq.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/Randomizer.h"
#include <cstring>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

static const char* ZAPAROO_API = "http://127.0.0.1:7497/api/v0.1";

static std::string buildRequest(const char* method, const std::string& uuid, const std::string& text = "")
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("jsonrpc");
	writer.String("2.0");
	writer.Key("id");
	writer.String(uuid.c_str());
	writer.Key("method");
	writer.String(method);
	if (!text.empty())
	{
		writer.Key("params");
		writer.StartObject();
		writer.Key("text");
		writer.String(text.c_str());
		writer.EndObject();
	}
	writer.EndObject();
	return buffer.GetString();
}

static bool hasWriteCapability(const rapidjson::Value& reader)
{
	if (!reader.IsObject())
		return false;

	auto connected = reader.FindMember("connected");
	if (connected == reader.MemberEnd() || !connected->value.IsBool() || !connected->value.GetBool())
		return false;

	auto capabilities = reader.FindMember("capabilities");
	if (capabilities == reader.MemberEnd() || !capabilities->value.IsArray())
		return false;

	for (const auto& capability : capabilities->value.GetArray())
		if (capability.IsString() && strcmp(capability.GetString(), "write") == 0)
			return true;

	return false;
}

bool Zaparoo::isZaparooEnabled()
{
#ifndef _WIN32
	if (!Utils::FileSystem::exists("/tmp/zaparoo/core.pid", false))
		return false;
#endif

	HttpReqOptions options;
	options.connectTimeout = 100L;
	options.customHeaders = { "Content-Type: application/json" };
	options.dataToPost = buildRequest("readers", Randomizer::generateUUID());

	HttpReq req(ZAPAROO_API, &options);
	if (!req.wait())
		return false;

	rapidjson::Document doc;
	doc.Parse(req.getContent().c_str());
	if (doc.HasParseError() || !doc.IsObject())
		return false;

	auto result = doc.FindMember("result");
	if (result == doc.MemberEnd() || !result->value.IsObject())
		return false;

	auto readers = result->value.FindMember("readers");
	if (readers == result->value.MemberEnd() || !readers->value.IsArray())
		return false;

	for (const auto& reader : readers->value.GetArray())
		if (hasWriteCapability(reader))
			return true;

	return false;
}

bool Zaparoo::writeZaparooCard(const std::string& name)
{
	std::string uuid = Randomizer::generateUUID();
	LOG(LogInfo) << "Zaparoo::writeZaparooCard -> " << uuid;

	HttpReqOptions options;
	options.customHeaders = { "Content-Type: application/json" };
	options.dataToPost = buildRequest("readers.write", uuid, name);

	HttpReq req(ZAPAROO_API, &options);
	if (!req.wait())
		return false;

	// Write failures still come back as HTTP 200
	rapidjson::Document doc;
	doc.Parse(req.getContent().c_str());
	return !doc.HasParseError() && doc.IsObject() && !doc.HasMember("error");
}
