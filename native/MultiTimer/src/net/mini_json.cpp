#include "net/mini_json.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const int kMaxDepth = 64;       // 嵌套深度上限，防止异常输入打爆栈
const size_t kMaxInput = 1024 * 1024;

// 自有空白/十六进制定义，避免 cctype 在负 char 上的 UB
bool isWs(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isDigit1to9(char c) {
	return c >= '1' && c <= '9';
}

int hexVal(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

void appendUtf8(std::string& out, uint32_t cp) {
	if (cp <= 0x7F) {
		out.push_back(static_cast<char>(cp));
	} else if (cp <= 0x7FF) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0xFFFF) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0x10FFFF) {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

void appendEscaped(std::string& out, const std::string& s) {
	for (size_t i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					// 已为 UTF-8 字节序列，原样输出
					out.push_back(static_cast<char>(c));
				}
				break;
		}
	}
}

class Parser {
public:
	Parser(const std::string& text, std::string& error)
		: mText(text), mP(text.data()), mEnd(text.data() + text.size()),
		  mErr(error) {
		mErr.clear();
	}

	bool run(JsonValue& out) {
		if (mText.size() > kMaxInput) {
			return fail("input too large");
		}
		skipWs();
		if (!parseValue(out, 0)) {
			return false;
		}
		skipWs();
		if (mP != mEnd) {
			return fail("trailing characters after JSON value");
		}
		return true;
	}

private:
	const std::string& mText;
	const char* mP;
	const char* mEnd;
	std::string& mErr;

	bool fail(const char* msg) {
		mErr = msg;
		return false;
	}

	bool eof() const {
		return mP >= mEnd;
	}

	void skipWs() {
		while (mP < mEnd && isWs(*mP)) {
			++mP;
		}
	}

	bool consume(char c) {
		if (mP < mEnd && *mP == c) {
			++mP;
			return true;
		}
		return false;
	}

	bool parseLiteral(const char* lit) {
		size_t n = strlen(lit);
		if (static_cast<size_t>(mEnd - mP) < n || memcmp(mP, lit, n) != 0) {
			return false;
		}
		mP += n;
		return true;
	}

	bool parseValue(JsonValue& out, int depth) {
		if (depth > kMaxDepth) {
			return fail("nesting too deep");
		}
		skipWs();
		if (eof()) {
			return fail("unexpected end of input");
		}
		char c = *mP;
		switch (c) {
			case '{': return parseObject(out, depth);
			case '[': return parseArray(out, depth);
			case '"': {
				std::string s;
				if (!parseString(s)) {
					return false;
				}
				out = JsonValue(s);
				return true;
			}
			case 't':
				if (!parseLiteral("true")) {
					return fail("invalid literal");
				}
				out = JsonValue(true);
				return true;
			case 'f':
				if (!parseLiteral("false")) {
					return fail("invalid literal");
				}
				out = JsonValue(false);
				return true;
			case 'n':
				if (!parseLiteral("null")) {
					return fail("invalid literal");
				}
				out = JsonValue();
				return true;
			default:
				if (c == '-' || (c >= '0' && c <= '9')) {
					return parseNumber(out);
				}
				return fail("unexpected character");
		}
	}

	bool parseObject(JsonValue& out, int depth) {
		++mP;  // 消费 '{'
		out = JsonValue::newObject();
		skipWs();
		if (consume('}')) {
			return true;
		}
		while (true) {
			skipWs();
			if (eof() || *mP != '"') {
				return fail("expected string key in object");
			}
			std::string key;
			if (!parseString(key)) {
				return false;
			}
			skipWs();
			if (!consume(':')) {
				return fail("expected ':' in object");
			}
			JsonValue val;
			if (!parseValue(val, depth + 1)) {
				return false;
			}
			out.set(key, val);
			skipWs();
			if (consume(',')) {
				continue;
			}
			if (consume('}')) {
				return true;
			}
			return fail("expected ',' or '}' in object");
		}
	}

	bool parseArray(JsonValue& out, int depth) {
		++mP;  // 消费 '['
		out = JsonValue::newArray();
		skipWs();
		if (consume(']')) {
			return true;
		}
		while (true) {
			JsonValue val;
			if (!parseValue(val, depth + 1)) {
				return false;
			}
			out.pushBack(val);
			skipWs();
			if (consume(',')) {
				continue;
			}
			if (consume(']')) {
				return true;
			}
			return fail("expected ',' or ']' in array");
		}
	}

	bool parseString(std::string& out) {
		++mP;  // 消费开引号
		while (true) {
			if (eof()) {
				return fail("unterminated string");
			}
			char c = *mP++;
			if (c == '"') {
				return true;
			}
			if (static_cast<unsigned char>(c) < 0x20) {
				return fail("unescaped control character in string");
			}
			if (c != '\\') {
				out.push_back(c);
				continue;
			}
			if (eof()) {
				return fail("unterminated escape");
			}
			char e = *mP++;
			switch (e) {
				case '"':  out.push_back('"'); break;
				case '\\': out.push_back('\\'); break;
				case '/':  out.push_back('/'); break;
				case 'b':  out.push_back('\b'); break;
				case 'f':  out.push_back('\f'); break;
				case 'n':  out.push_back('\n'); break;
				case 'r':  out.push_back('\r'); break;
				case 't':  out.push_back('\t'); break;
				case 'u': {
					uint32_t cp = 0;
					if (!parseHex4(cp)) {
						return fail("invalid \\u escape");
					}
					// 代理对
					if (cp >= 0xD800 && cp <= 0xDBFF) {
						if (mEnd - mP >= 6 && mP[0] == '\\' && mP[1] == 'u') {
							const char* save = mP;
							mP += 2;
							uint32_t lo = 0;
							if (parseHex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							} else {
								mP = save;  // 不是低代理，回退按原码点容错
							}
						}
					}
					appendUtf8(out, cp);
					break;
				}
				default:
					return fail("invalid escape sequence");
			}
		}
	}

	bool parseHex4(uint32_t& cp) {
		if (mEnd - mP < 4) {
			return false;
		}
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i) {
			int h = hexVal(mP[i]);
			if (h < 0) {
				return false;
			}
			v = (v << 4) | static_cast<uint32_t>(h);
		}
		mP += 4;
		cp = v;
		return true;
	}

	bool parseNumber(JsonValue& out) {
		const char* start = mP;
		bool isInt = true;
		if (consume('-')) {
		}
		if (eof()) {
			return fail("invalid number");
		}
		if (*mP == '0') {
			++mP;
		} else if (isDigit1to9(*mP)) {
			while (mP < mEnd && *mP >= '0' && *mP <= '9') {
				++mP;
			}
		} else {
			return fail("invalid number");
		}
		if (mP < mEnd && *mP == '.') {
			isInt = false;
			++mP;
			if (mP >= mEnd || *mP < '0' || *mP > '9') {
				return fail("invalid number fraction");
			}
			while (mP < mEnd && *mP >= '0' && *mP <= '9') {
				++mP;
			}
		}
		if (mP < mEnd && (*mP == 'e' || *mP == 'E')) {
			isInt = false;
			++mP;
			if (mP < mEnd && (*mP == '+' || *mP == '-')) {
				++mP;
			}
			if (mP >= mEnd || *mP < '0' || *mP > '9') {
				return fail("invalid number exponent");
			}
			while (mP < mEnd && *mP >= '0' && *mP <= '9') {
				++mP;
			}
		}

		std::string token(start, static_cast<size_t>(mP - start));
		if (isInt) {
			errno = 0;
			char* endp = NULL;
			long long iv = strtoll(token.c_str(), &endp, 10);
			if (errno == 0 && endp != NULL &&
					static_cast<size_t>(endp - token.c_str()) == token.size()) {
				out = JsonValue(iv);
				return true;
			}
			// 超 long long 范围，按浮点解析
		}
		errno = 0;
		char* endp = NULL;
		double dv = strtod(token.c_str(), &endp);
		if (endp == NULL ||
				static_cast<size_t>(endp - token.c_str()) != token.size()) {
			return fail("invalid number");
		}
		out = JsonValue(dv);
		return true;
	}
};

}  // namespace

JsonValue::JsonValue()
	: mType(J_NULL), mBool(false), mIsInt(true), mInt(0), mNum(0.0) {
}

JsonValue::JsonValue(bool b)
	: mType(J_BOOL), mBool(b), mIsInt(true), mInt(0), mNum(0.0) {
}

JsonValue::JsonValue(int n)
	: mType(J_NUMBER), mBool(false), mIsInt(true),
	  mInt(static_cast<long long>(n)), mNum(static_cast<double>(n)) {
}

JsonValue::JsonValue(long long n)
	: mType(J_NUMBER), mBool(false), mIsInt(true), mInt(n),
	  mNum(static_cast<double>(n)) {
}

JsonValue::JsonValue(double n)
	: mType(J_NUMBER), mBool(false), mIsInt(false),
	  mInt(static_cast<long long>(n)), mNum(n) {
}

JsonValue::JsonValue(const char* s)
	: mType(J_STRING), mBool(false), mIsInt(true), mInt(0), mNum(0.0),
	  mStr(s ? s : "") {
}

JsonValue::JsonValue(const std::string& s)
	: mType(J_STRING), mBool(false), mIsInt(true), mInt(0), mNum(0.0),
	  mStr(s) {
}

JsonValue::~JsonValue() {
}

JsonValue::JsonValue(const JsonValue& other) {
	copyFrom(other);
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
	if (this != &other) {
		copyFrom(other);
	}
	return *this;
}

void JsonValue::copyFrom(const JsonValue& other) {
	mType = other.mType;
	mBool = other.mBool;
	mIsInt = other.mIsInt;
	mInt = other.mInt;
	mNum = other.mNum;
	mStr = other.mStr;
	mObj = other.mObj;
	mArr = other.mArr;
}

// static
JsonValue JsonValue::newObject() {
	JsonValue v;
	v.mType = J_OBJECT;
	return v;
}

// static
JsonValue JsonValue::newArray() {
	JsonValue v;
	v.mType = J_ARRAY;
	return v;
}

const JsonValue& JsonValue::nullValue() {
	static const JsonValue kNull;
	return kNull;
}

bool JsonValue::contains(const std::string& key) const {
	return mType == J_OBJECT && mObj.find(key) != mObj.end();
}

JsonValue& JsonValue::operator[](const std::string& key) {
	if (mType != J_OBJECT) {
		static JsonValue dummy;
		dummy = JsonValue();
		return dummy;
	}
	return mObj[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
	if (mType != J_OBJECT) {
		return nullValue();
	}
	Object::const_iterator it = mObj.find(key);
	if (it == mObj.end()) {
		return nullValue();
	}
	return it->second;
}

void JsonValue::set(const std::string& key, const JsonValue& value) {
	if (mType == J_OBJECT) {
		mObj[key] = value;
	}
}

void JsonValue::pushBack(const JsonValue& value) {
	if (mType == J_ARRAY) {
		mArr.push_back(value);
	}
}

size_t JsonValue::size() const {
	if (mType == J_ARRAY) {
		return mArr.size();
	}
	if (mType == J_OBJECT) {
		return mObj.size();
	}
	return 0;
}

const JsonValue& JsonValue::at(size_t index) const {
	if (mType != J_ARRAY || index >= mArr.size()) {
		return nullValue();
	}
	return mArr[index];
}

std::string JsonValue::asString(const std::string& def) const {
	return mType == J_STRING ? mStr : def;
}

long long JsonValue::asInt64(long long def) const {
	if (mType != J_NUMBER) {
		return def;
	}
	if (mIsInt) {
		return mInt;
	}
	return static_cast<long long>(mNum);
}

int JsonValue::asInt(int def) const {
	if (mType != J_NUMBER) {
		return def;
	}
	long long v = asInt64(0);
	if (v < -2147483647LL - 1) {
		return -2147483647 - 1;
	}
	if (v > 2147483647LL) {
		return 2147483647;
	}
	return static_cast<int>(v);
}

uint32_t JsonValue::asUint32(uint32_t def) const {
	if (mType != J_NUMBER) {
		return def;
	}
	long long v = asInt64(0);
	if (v < 0) {
		return 0;
	}
	if (v > 4294967295LL) {
		return 4294967295U;
	}
	return static_cast<uint32_t>(v);
}

double JsonValue::asDouble(double def) const {
	if (mType != J_NUMBER) {
		return def;
	}
	return mIsInt ? static_cast<double>(mInt) : mNum;
}

bool JsonValue::asBool(bool def) const {
	if (mType == J_BOOL) {
		return mBool;
	}
	if (mType == J_NUMBER) {
		return mIsInt ? mInt != 0 : mNum != 0.0;
	}
	return def;
}

// static
bool JsonValue::parse(const std::string& text, JsonValue& out, std::string& err) {
	Parser parser(text, err);
	return parser.run(out);
}

std::string JsonValue::dump(int indent) const {
	std::string out;
	dumpTo(out, indent, 0);
	return out;
}

void JsonValue::dumpTo(std::string& out, int indent, int level) const {
	char numBuf[40];
	switch (mType) {
		case J_NULL:
			out += "null";
			return;
		case J_BOOL:
			out += mBool ? "true" : "false";
			return;
		case J_NUMBER:
			if (mIsInt) {
				snprintf(numBuf, sizeof(numBuf), "%lld", mInt);
				out += numBuf;
			} else {
				double intPart = 0.0;
				if (mNum >= -9.2233720368547758e18 && mNum <= 9.2233720368547758e18 &&
						modf(mNum, &intPart) == 0.0) {
					snprintf(numBuf, sizeof(numBuf), "%lld",
							static_cast<long long>(mNum));
					out += numBuf;
				} else {
					snprintf(numBuf, sizeof(numBuf), "%.17g", mNum);
					out += numBuf;
				}
			}
			return;
		case J_STRING:
			out.push_back('"');
			appendEscaped(out, mStr);
			out.push_back('"');
			return;
		case J_ARRAY: {
			if (mArr.empty()) {
				out += "[]";
				return;
			}
			bool pretty = indent >= 0;
			out.push_back('[');
			for (size_t i = 0; i < mArr.size(); ++i) {
				if (pretty) {
					out.append(static_cast<size_t>((level + 1) * indent), ' ');
				}
				mArr[i].dumpTo(out, indent, level + 1);
				if (i + 1 < mArr.size()) {
					out.push_back(',');
				}
				if (pretty) {
					out.push_back('\n');
				}
			}
			if (pretty) {
				out.append(static_cast<size_t>(level * indent), ' ');
			}
			out.push_back(']');
			return;
		}
		case J_OBJECT: {
			if (mObj.empty()) {
				out += "{}";
				return;
			}
			bool pretty = indent >= 0;
			out.push_back('{');
			size_t i = 0;
			for (Object::const_iterator it = mObj.begin(); it != mObj.end();
					++it, ++i) {
				if (pretty) {
					out.append(static_cast<size_t>((level + 1) * indent), ' ');
				}
				out.push_back('"');
				appendEscaped(out, it->first);
				out += pretty ? "\": " : "\":";
				it->second.dumpTo(out, indent, level + 1);
				if (i + 1 < mObj.size()) {
					out.push_back(',');
				}
				if (pretty) {
					out.push_back('\n');
				}
			}
			if (pretty) {
				out.append(static_cast<size_t>(level * indent), ' ');
			}
			out.push_back('}');
			return;
		}
	}
}
