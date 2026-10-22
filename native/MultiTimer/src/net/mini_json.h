// 自包含极简 JSON，不依赖任何 SDK / 平台头，纯 C++11，便于将来 PC 单测。
// 不使用异常：parse 失败返回 false 并填写 err。
#ifndef NET_MINI_JSON_H_
#define NET_MINI_JSON_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

class JsonValue {
public:
	enum Type {
		J_NULL,
		J_BOOL,
		J_NUMBER,
		J_STRING,
		J_ARRAY,
		J_OBJECT
	};

	JsonValue();
	JsonValue(bool b);
	JsonValue(int n);
	JsonValue(long long n);
	JsonValue(double n);
	JsonValue(const char* s);
	JsonValue(const std::string& s);
	~JsonValue();

	JsonValue(const JsonValue& other);
	JsonValue& operator=(const JsonValue& other);

	static JsonValue newObject();
	static JsonValue newArray();

	Type type() const { return mType; }
	bool isNull() const { return mType == J_NULL; }
	bool isBool() const { return mType == J_BOOL; }
	bool isNumber() const { return mType == J_NUMBER; }
	bool isString() const { return mType == J_STRING; }
	bool isArray() const { return mType == J_ARRAY; }
	bool isObject() const { return mType == J_OBJECT; }

	// object 访问：key 不存在时返回只读单例 null；非 object 同理
	bool contains(const std::string& key) const;
	JsonValue& operator[](const std::string& key);
	const JsonValue& operator[](const std::string& key) const;
	void set(const std::string& key, const JsonValue& value);

	// array 访问：非 array 时 pushBack 无效，at 越界返回只读单例 null
	void pushBack(const JsonValue& value);
	size_t size() const;  // array 元素数 / object 成员数，其余为 0
	const JsonValue& at(size_t index) const;

	// 带默认值的类型转换
	std::string asString(const std::string& def = std::string()) const;
	int asInt(int def = 0) const;
	long long asInt64(long long def = 0) const;
	uint32_t asUint32(uint32_t def = 0U) const;
	double asDouble(double def = 0.0) const;
	bool asBool(bool def = false) const;

	// 解析 JSON 文本；成功返回 true，失败返回 false 且不抛异常
	static bool parse(const std::string& text, JsonValue& out, std::string& err);

	// 序列化；indent<0 为紧凑输出，>=0 为带缩进美化输出（常用 2）
	std::string dump(int indent = -1) const;

private:
	typedef std::map<std::string, JsonValue> Object;
	typedef std::vector<JsonValue> Array;

	Type mType;
	bool mBool;
	bool mIsInt;       // number 是否为整数
	long long mInt;
	double mNum;
	std::string mStr;
	Object mObj;
	Array mArr;

	static const JsonValue& nullValue();
	void copyFrom(const JsonValue& other);

	// 序列化内部递归
	void dumpTo(std::string& out, int indent, int level) const;
};

#endif  // NET_MINI_JSON_H_
