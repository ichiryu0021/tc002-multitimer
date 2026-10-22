// Host tests for net/mini_json (self-contained JSON, no SDK headers).
#include "test_framework.h"

#include <string>

#include "net/mini_json.h"

namespace {

bool parseOk(const std::string& text, JsonValue& out) {
	std::string err;
	return JsonValue::parse(text, out, err);
}

}  // namespace

TF_TEST(json_parse_basic_types) {
	const std::string text =
			R"({"s":"hi","i":42,"n":-7,"b":true,"f":false,"z":null,"a":[1,2,3]})";
	JsonValue root;
	TF_CHECK(parseOk(text, root));
	TF_CHECK(root.isObject());
	TF_EQ(static_cast<int>(root.size()), 7);

	TF_EQ(root["s"].asString(), std::string("hi"));
	TF_CHECK(root["s"].isString());
	TF_EQ(root["i"].asInt(), 42);
	TF_EQ(root["n"].asInt(), -7);
	TF_CHECK(root["n"].isNumber());
	TF_EQ(root["b"].asBool(), true);
	TF_EQ(root["f"].asBool(), false);
	TF_CHECK(root["f"].isBool());
	TF_CHECK(root["z"].isNull());

	TF_CHECK(root["a"].isArray());
	TF_EQ(static_cast<int>(root["a"].size()), 3);
	TF_EQ(root["a"].at(0).asInt(), 1);
	TF_EQ(root["a"].at(1).asInt(), 2);
	TF_EQ(root["a"].at(2).asInt(), 3);
	TF_CHECK(root["a"].at(9).isNull());

	TF_CHECK(root.contains("s"));
	TF_CHECK(!root.contains("missing"));
	TF_CHECK(root["missing"].isNull());
}

TF_TEST(json_parse_escapes) {
	const std::string text =
			R"({"q":"a\"b","bs":"c\\d","nl":"e\nf","tab":"g\th","slash":"a\/b","uni":"\u4e2d","emoji":"\uD83D\uDE00","ctrl":"\u0001"})";
	JsonValue root;
	TF_CHECK(parseOk(text, root));

	TF_EQ(root["q"].asString(), std::string("a\"b"));
	TF_EQ(static_cast<int>(root["q"].asString().size()), 3);
	TF_EQ(root["bs"].asString(), std::string("c\\d"));
	TF_EQ(root["nl"].asString(), std::string("e\nf"));
	TF_EQ(static_cast<int>(root["nl"].asString().size()), 3);
	TF_EQ(root["tab"].asString(), std::string("g\th"));
	TF_EQ(root["slash"].asString(), std::string("a/b"));

	// \u4e2d must become the UTF-8 bytes E4 B8 AD.
	TF_EQ(root["uni"].asString(), std::string("\xE4\xB8\xAD"));
	// Surrogate pair \uD83D\uDE00 must become U+1F600 as F0 9F 98 80.
	TF_EQ(root["emoji"].asString(), std::string("\xF0\x9F\x98\x80"));
	TF_EQ(static_cast<int>(root["emoji"].asString().size()), 4);
	// \u0001 must become a raw 0x01 byte.
	TF_EQ(root["ctrl"].asString(), std::string("\x01"));
}

TF_TEST(json_parse_numbers_and_clamping) {
	JsonValue root;
	TF_CHECK(parseOk(R"([0,-1,2147483648,1e3,1.5,-2.25])", root));
	TF_CHECK(root.isArray());
	TF_EQ(static_cast<int>(root.size()), 6);

	TF_EQ(root.at(0).asInt(), 0);
	TF_EQ(root.at(1).asInt(), -1);
	TF_EQ(root.at(2).asInt64(), 2147483648LL);
	TF_CHECK(root.at(3).asDouble() == 1000.0);
	TF_CHECK(root.at(4).asDouble() == 1.5);
	TF_CHECK(root.at(5).asDouble() == -2.25);
	TF_EQ(root.at(2).dump(), std::string("2147483648"));
	TF_EQ(root.at(3).dump(), std::string("1000"));

	JsonValue clamped;
	TF_CHECK(parseOk(R"([-2147483649,2147483648])", clamped));
	TF_EQ(clamped.at(0).asInt(), -2147483647 - 1);
	TF_EQ(clamped.at(1).asInt(), 2147483647);

	JsonValue unsignedClamp;
	TF_CHECK(parseOk(R"([-1,4294967295,4294967296])", unsignedClamp));
	TF_EQ(unsignedClamp.at(0).asUint32(7), 0u);
	TF_EQ(unsignedClamp.at(1).asUint32(7), 4294967295u);
	TF_EQ(unsignedClamp.at(2).asUint32(7), 4294967295u);

	// Defaults are returned for every non-number type.
	JsonValue str;
	TF_CHECK(parseOk(R"("abc")", str));
	TF_EQ(str.asInt(9), 9);
	TF_EQ(str.asInt64(9LL), 9LL);
	TF_CHECK(str.asDouble(2.5) == 2.5);
	TF_EQ(str.asBool(true), true);
	TF_EQ(str.asUint32(3u), 3u);
	TF_EQ(str.asString(), std::string("abc"));

	// asString only accepts strings: numbers and null fall back to the default.
	JsonValue number(5);
	TF_EQ(number.asString(), std::string());
	TF_EQ(number.asString("def"), std::string("def"));
	TF_EQ(JsonValue().asString("def"), std::string("def"));

	JsonValue nullValue;
	TF_CHECK(parseOk("null", nullValue));
	TF_EQ(nullValue.asInt(99), 99);
	TF_EQ(nullValue.asBool(false), false);
	TF_EQ(nullValue.asString("def"), std::string("def"));
}

TF_TEST(json_parse_nested) {
	JsonValue root;
	TF_CHECK(parseOk(
			R"({"a":{"b":[{"c":1},{"c":2}]},"d":[[1],[2,[3]]]})", root));

	TF_EQ(static_cast<int>(root["a"]["b"].size()), 2);
	TF_EQ(root["a"]["b"].at(1)["c"].asInt(), 2);
	TF_EQ(root["d"].at(1).at(1).at(0).asInt(), 3);
	TF_CHECK(root["d"].at(1).at(1).at(5).isNull());
}

TF_TEST(json_dump_round_trip) {
	const std::string text =
			R"({"z":null,"b":true,"n":-5,"s":"a\"b\\c\nd","arr":[1,2,{"k":"v"}],"obj":{},"empty":[]})";
	JsonValue root;
	TF_CHECK(parseOk(text, root));

	const std::string compact = root.dump(-1);
	const std::string pretty = root.dump(2);
	TF_CHECK(!compact.empty());
	TF_CHECK(compact.find('\n') == std::string::npos);
	TF_CHECK(pretty.find('\n') != std::string::npos);
	TF_CHECK(pretty.size() > compact.size());

	JsonValue fromCompact;
	TF_CHECK(parseOk(compact, fromCompact));
	TF_EQ(fromCompact.dump(-1), compact);

	JsonValue fromPretty;
	TF_CHECK(parseOk(pretty, fromPretty));
	TF_EQ(fromPretty.dump(-1), compact);

	TF_EQ(fromCompact["s"].asString(), std::string("a\"b\\c\nd"));
	TF_EQ(fromCompact["arr"].at(2)["k"].asString(), std::string("v"));
}

TF_TEST(json_dump_escaping) {
	JsonValue obj = JsonValue::newObject();
	obj.set("a\"b", JsonValue(std::string("x\x01y\nz")));
	TF_EQ(obj.dump(-1), std::string("{\"a\\\"b\":\"x\\u0001y\\nz\"}"));

	JsonValue escapes = JsonValue(std::string("\b\f\n\r\t"));
	TF_EQ(escapes.dump(), std::string("\"\\b\\f\\n\\r\\t\""));

	JsonValue plain = JsonValue(std::string("caf\xE4\xB8\xAD"));
	TF_EQ(plain.dump(), std::string("\"caf\xE4\xB8\xAD\""));
}

TF_TEST(json_dump_empty_containers) {
	JsonValue obj = JsonValue::newObject();
	TF_EQ(obj.dump(), std::string("{}"));
	TF_EQ(obj.dump(2), std::string("{}"));

	JsonValue arr = JsonValue::newArray();
	TF_EQ(arr.dump(), std::string("[]"));
	TF_EQ(arr.dump(0), std::string("[]"));

	JsonValue nullValue;
	TF_EQ(nullValue.dump(), std::string("null"));
	TF_EQ(JsonValue(true).dump(), std::string("true"));
	TF_EQ(JsonValue(false).dump(), std::string("false"));
}

TF_TEST(json_invalid_inputs_rejected) {
	const char* bad[] = {
		"", " ", "{", "[", "{\"a\":1,}", "[1,]", "{'a':1}", "tru", "nul",
		"01", "+1", ".5", "1.", "\"abc", "1 2", "{\"a\" 1}", "{\"a\":}",
		"[,]", "NaN", "{\"a\":1}extra", "\"a\nb\"",
		"{\"a\":\"\\x\"}", "{\"a\":\"\\u12\"}", "{\"a\":\"\\uZZZZ\"}"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		JsonValue out;
		std::string err;
		TF_CHECK_MSG(!JsonValue::parse(bad[i], out, err),
				std::string("accepted invalid JSON: ") + bad[i]);
		TF_CHECK_MSG(!err.empty(),
				std::string("empty error message for: ") + bad[i]);
	}
}

TF_TEST(json_deep_nesting_does_not_crash) {
	std::string nested60;
	for (int i = 0; i < 60; ++i) {
		nested60 += '[';
	}
	nested60 += '1';
	for (int i = 0; i < 60; ++i) {
		nested60 += ']';
	}
	JsonValue out;
	std::string err;
	TF_CHECK(JsonValue::parse(nested60, out, err));
	TF_CHECK(out.isArray());

	std::string tooDeep;
	for (int i = 0; i < 1000; ++i) {
		tooDeep += '[';
	}
	tooDeep += '1';
	for (int i = 0; i < 1000; ++i) {
		tooDeep += ']';
	}
	TF_CHECK(!JsonValue::parse(tooDeep, out, err));
	TF_CHECK(!err.empty());

	std::string unbalanced;
	for (int i = 0; i < 1000; ++i) {
		unbalanced += '[';
	}
	TF_CHECK(!JsonValue::parse(unbalanced, out, err));
}

TF_TEST(json_type_and_access_helpers) {
	JsonValue obj = JsonValue::newObject();
	obj.set("a", JsonValue(1));
	TF_CHECK(obj.contains("a"));
	TF_CHECK(!obj.contains("b"));
	TF_EQ(obj["a"].asInt(), 1);
	// Read through a const reference: the non-const operator[] inserts a null
	// member for a missing key (see the note in tests/README.md).
	const JsonValue& constObj = obj;
	TF_CHECK(constObj["b"].isNull());
	TF_EQ(static_cast<int>(obj.size()), 1);

	// pushBack on an object is a no-op.
	obj.pushBack(JsonValue(2));
	TF_EQ(static_cast<int>(obj.size()), 1);

	// Copies are deep: mutating the copy must not touch the original.
	JsonValue copy = obj;
	copy.set("a", JsonValue(9));
	TF_EQ(obj["a"].asInt(), 1);
	TF_EQ(copy["a"].asInt(), 9);

	JsonValue arr = JsonValue::newArray();
	arr.pushBack(JsonValue(1));
	arr.pushBack(JsonValue(2));
	TF_EQ(static_cast<int>(arr.size()), 2);
	TF_EQ(arr.at(0).asInt(), 1);
	TF_CHECK(arr.at(2).isNull());

	// const operator[] on a non-object returns the null singleton.
	const JsonValue& constArr = arr;
	TF_CHECK(constArr["x"].isNull());

	// set() on a non-object is a no-op.
	JsonValue number(5);
	number.set("x", JsonValue(1));
	TF_EQ(static_cast<int>(number.size()), 0);
	TF_EQ(number.asInt(), 5);
	TF_EQ(number.asString("n"), std::string("n"));
}
