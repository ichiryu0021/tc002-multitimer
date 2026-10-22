// Host tests for config/WifiStore (INI + JSON credential parsing).
#include "test_framework.h"

#include <string>
#include <vector>

#include "config/WifiStore.h"

TF_TEST(wifi_ini_main_section) {
	WifiCredential cred;
	const std::string ini =
			"[main]\n"
			"wifiSsid=MyNet\n"
			"wifiPwd=MyPass\n";
	TF_CHECK(WifiStore::parseIni(ini, cred));
	TF_CHECK(cred.valid());
	TF_EQ(cred.ssid, std::string("MyNet"));
	TF_EQ(cred.password, std::string("MyPass"));
}

TF_TEST(wifi_ini_wifi_section) {
	WifiCredential cred;
	const std::string ini =
			"[wifi]\n"
			"ssid=Net2\n"
			"password=S3cret!\n";
	TF_CHECK(WifiStore::parseIni(ini, cred));
	TF_EQ(cred.ssid, std::string("Net2"));
	TF_EQ(cred.password, std::string("S3cret!"));
}

TF_TEST(wifi_ini_quotes_bom_crlf) {
	WifiCredential cred;
	std::string ini;
	ini += "\xEF\xBB\xBF";  // UTF-8 BOM
	ini += "[main]\r\n";
	ini += "wifiSsid = \"Quoted Net\"\r\n";
	ini += "wifiPwd = 'p a s s'\r\n";

	TF_CHECK(WifiStore::parseIni(ini, cred));
	TF_EQ(cred.ssid, std::string("Quoted Net"));
	TF_EQ(cred.password, std::string("p a s s"));
}

TF_TEST(wifi_ini_comments_and_blank_lines) {
	WifiCredential cred;
	const std::string ini =
			"\n"
			"# comment line\n"
			"; another comment line\n"
			"[main]\n"
			"\n"
			"# wifiSsid=CommentedOut\n"
			"wifiSsid=RealNet\n"
			"wifiPwd=RealPass\n";
	TF_CHECK(WifiStore::parseIni(ini, cred));
	TF_EQ(cred.ssid, std::string("RealNet"));
	TF_EQ(cred.password, std::string("RealPass"));
}

TF_TEST(wifi_ini_unknown_section_ignored) {
	WifiCredential cred;
	const std::string ini =
			"[other]\n"
			"ssid=Ignored\n"
			"password=Ignored\n"
			"[main]\n"
			"wifiSsid=Kept\n";
	TF_CHECK(WifiStore::parseIni(ini, cred));
	TF_EQ(cred.ssid, std::string("Kept"));
	TF_EQ(cred.password, std::string(""));

	WifiCredential onlyUnknown;
	const std::string iniOnlyUnknown =
			"[other]\n"
			"ssid=Ignored\n"
			"password=Ignored\n";
	TF_CHECK(!WifiStore::parseIni(iniOnlyUnknown, onlyUnknown));
	TF_CHECK(onlyUnknown.ssid.empty());
}

TF_TEST(wifi_ini_missing_returns_false) {
	WifiCredential cred;
	cred.ssid = "stale";
	cred.password = "stale";

	TF_CHECK(!WifiStore::parseIni(std::string(), cred));
	TF_CHECK(cred.ssid.empty());
	TF_CHECK(cred.password.empty());

	TF_CHECK(!WifiStore::parseIni("garbage line without separator\n", cred));
	TF_CHECK(!WifiStore::parseIni("[main]\nnotAKey=value\n", cred));
	TF_CHECK(!WifiStore::parseIni("password=OnlyPassword\n", cred));
	TF_CHECK(!cred.valid());
}

TF_TEST(wifi_ini_key_aliases_and_separators) {
	{
		// Colon separator, flat keys before any section header.
		WifiCredential cred;
		const std::string ini = "wifiSsid: ColonNet\nwifiPwd: ColonPass\n";
		TF_CHECK(WifiStore::parseIni(ini, cred));
		TF_EQ(cred.ssid, std::string("ColonNet"));
		TF_EQ(cred.password, std::string("ColonPass"));
	}
	{
		// Upper-case section and underscore keys.
		WifiCredential cred;
		const std::string ini =
				"[WIFI]\n"
				"wifi_ssid=Under\n"
				"WIFI_PWD=Score\n";
		TF_CHECK(WifiStore::parseIni(ini, cred));
		TF_EQ(cred.ssid, std::string("Under"));
		TF_EQ(cred.password, std::string("Score"));
	}
	{
		// Aliases: networkName for SSID, psk for password.
		WifiCredential cred;
		const std::string ini =
				"[main]\n"
				"networkName=NetName\n"
				"psk=AliasPsk\n";
		TF_CHECK(WifiStore::parseIni(ini, cred));
		TF_EQ(cred.ssid, std::string("NetName"));
		TF_EQ(cred.password, std::string("AliasPsk"));
	}
	{
		// Values keep inner spaces but lose surrounding whitespace.
		WifiCredential cred;
		const std::string ini = "wifiSsid =   My Net  \nwifiPwd =  pass word \n";
		TF_CHECK(WifiStore::parseIni(ini, cred));
		TF_EQ(cred.ssid, std::string("My Net"));
		TF_EQ(cred.password, std::string("pass word"));
	}
}

TF_TEST(wifi_json_direct_keys) {
	WifiCredential cred;
	TF_CHECK(WifiStore::parseJson(
			R"({"ssid":"Direct","password":"DirectPass"})", cred));
	TF_CHECK(cred.valid());
	TF_EQ(cred.ssid, std::string("Direct"));
	TF_EQ(cred.password, std::string("DirectPass"));
}

TF_TEST(wifi_json_alt_keys) {
	WifiCredential cred;
	TF_CHECK(WifiStore::parseJson(
			R"({"wifiSsid":"Alt","wifiPwd":"AltPass"})", cred));
	TF_EQ(cred.ssid, std::string("Alt"));
	TF_EQ(cred.password, std::string("AltPass"));
}

TF_TEST(wifi_json_nested_object) {
	WifiCredential cred;
	TF_CHECK(WifiStore::parseJson(
			R"({"wifi":{"ssid":"Nested","password":"NestedPass"}})", cred));
	TF_EQ(cred.ssid, std::string("Nested"));
	TF_EQ(cred.password, std::string("NestedPass"));

	WifiCredential nestedPwd;
	TF_CHECK(WifiStore::parseJson(
			R"({"wifi":{"ssid":"Nested2","pwd":"Nested2Pass"}})", nestedPwd));
	TF_EQ(nestedPwd.ssid, std::string("Nested2"));
	TF_EQ(nestedPwd.password, std::string("Nested2Pass"));

	// The nested "wifi" object is a fallback: it is only consulted when the
	// top-level object did not provide an ssid.
	WifiCredential topLevel;
	TF_CHECK(WifiStore::parseJson(
			R"({"ssid":"Top","wifi":{"ssid":"Inner","password":"P"}})",
			topLevel));
	TF_EQ(topLevel.ssid, std::string("Top"));
}

TF_TEST(wifi_json_pwd_alias) {
	WifiCredential cred;
	TF_CHECK(WifiStore::parseJson(R"({"ssid":"G","pwd":"H"})", cred));
	TF_EQ(cred.ssid, std::string("G"));
	TF_EQ(cred.password, std::string("H"));

	WifiCredential named;
	TF_CHECK(WifiStore::parseJson(R"({"name":"N","psk":"K"})", named));
	TF_EQ(named.ssid, std::string("N"));
	TF_EQ(named.password, std::string("K"));
}

TF_TEST(wifi_json_invalid_returns_false) {
	WifiCredential cred;
	TF_CHECK(!WifiStore::parseJson("not json", cred));
	TF_CHECK(!WifiStore::parseJson(R"({"password":"x"})", cred));
	TF_CHECK(!WifiStore::parseJson(R"({"ssid":123,"password":"x"})", cred));
	TF_CHECK(!WifiStore::parseJson(R"({"ssid":""})", cred));
	TF_CHECK(!WifiStore::parseJson(R"([1,2])", cred));
	TF_CHECK(!WifiStore::parseJson(R"("plain")", cred));
	TF_CHECK(!WifiStore::parseJson(std::string(), cred));
	TF_CHECK(cred.ssid.empty());
	TF_CHECK(!cred.valid());
}

TF_TEST(wifi_valid_requires_ssid) {
	WifiCredential cred;
	TF_CHECK(!cred.valid());

	cred.password = "onlypass";
	TF_CHECK(!cred.valid());

	cred.ssid = "net";
	TF_CHECK(cred.valid());
}

TF_TEST(wifi_json_networks_v2) {
	std::vector<WifiNetwork> networks;
	const std::string text =
			R"({"version":2,"networks":[)"
			R"({"ssid":"Home","password":"homepass","enabled":true},)"
			R"({"ssid":"Office","pwd":"officepass","enabled":false},)"
			R"({"ssid":"Open"}]})";
	TF_CHECK(WifiStore::parseJsonNetworks(text, networks));
	TF_EQ(networks.size(), static_cast<size_t>(3));
	TF_EQ(networks[0].ssid, std::string("Home"));
	TF_EQ(networks[0].password, std::string("homepass"));
	TF_EQ(networks[0].enabled, true);
	TF_EQ(networks[1].ssid, std::string("Office"));
	TF_EQ(networks[1].password, std::string("officepass"));
	TF_EQ(networks[1].enabled, false);
	TF_EQ(networks[2].ssid, std::string("Open"));
	TF_EQ(networks[2].password, std::string(""));
	TF_EQ(networks[2].enabled, true);
}

TF_TEST(wifi_json_networks_legacy_single) {
	std::vector<WifiNetwork> networks;
	TF_CHECK(WifiStore::parseJsonNetworks(
			R"({"ssid":"Legacy","password":"oldpass"})", networks));
	TF_EQ(networks.size(), static_cast<size_t>(1));
	TF_EQ(networks[0].ssid, std::string("Legacy"));
	TF_EQ(networks[0].password, std::string("oldpass"));
	TF_EQ(networks[0].enabled, true);

	// 兼容 {"wifi":{...}} 嵌套单对象
	std::vector<WifiNetwork> nested;
	TF_CHECK(WifiStore::parseJsonNetworks(
			R"({"wifi":{"ssid":"Nested","password":"nestedpass"}})", nested));
	TF_EQ(nested.size(), static_cast<size_t>(1));
	TF_EQ(nested[0].ssid, std::string("Nested"));
}

TF_TEST(wifi_json_networks_array_root_and_dedupe) {
	std::vector<WifiNetwork> networks;
	const std::string text =
			R"([{"ssid":"A","password":"1"},)"
			R"({"ssid":"B","password":"2"},)"
			R"({"ssid":"A","password":"duplicate"},)"
			R"({"password":"no-ssid"},)"
			R"({"ssid":""}])";
	TF_CHECK(WifiStore::parseJsonNetworks(text, networks));
	TF_EQ(networks.size(), static_cast<size_t>(2));
	TF_EQ(networks[0].ssid, std::string("A"));
	TF_EQ(networks[0].password, std::string("1"));
	TF_EQ(networks[1].ssid, std::string("B"));

	// 空列表是合法的“显式清空”，不是解析失败
	std::vector<WifiNetwork> empty;
	TF_CHECK(WifiStore::parseJsonNetworks(
			R"({"version":2,"networks":[]})", empty));
	TF_CHECK(empty.empty());
}

TF_TEST(wifi_json_networks_invalid_or_too_many) {
	std::vector<WifiNetwork> networks;
	TF_CHECK(!WifiStore::parseJsonNetworks("not json", networks));
	TF_CHECK(!WifiStore::parseJsonNetworks(R"({"networks":"nope"})", networks));
	TF_CHECK(!WifiStore::parseJsonNetworks(R"({"ssid":123})", networks));
	TF_CHECK(!WifiStore::parseJsonNetworks(R"("plain")", networks));

	std::string tooMany = R"({"version":2,"networks":[)";
	for (int i = 0; i < 9; ++i) {
		if (i != 0) {
			tooMany += ",";
		}
		tooMany += "{\"ssid\":\"N" + std::to_string(i) + "\"}";
	}
	tooMany += "]}";
	TF_CHECK(!WifiStore::parseJsonNetworks(tooMany, networks));
}

TF_TEST(wifi_json_networks_bad_enabled_item_skipped) {
	std::vector<WifiNetwork> networks;
	const std::string text =
			R"({"version":2,"networks":[)"
			R"({"ssid":"Good","password":"1","enabled":true},)"
			R"({"ssid":"Bad","enabled":"yes"},)"
			R"({"ssid":"Good2","enabled":false}]})";
	TF_CHECK(WifiStore::parseJsonNetworks(text, networks));
	TF_EQ(networks.size(), static_cast<size_t>(2));
	TF_EQ(networks[0].ssid, std::string("Good"));
	TF_EQ(networks[1].ssid, std::string("Good2"));
	TF_EQ(networks[1].enabled, false);
}

TF_TEST(wifi_json_networks_eight_allowed) {
	std::string eight = R"({"version":2,"networks":[)";
	for (int i = 0; i < WifiStore::kMaxNetworks; ++i) {
		if (i != 0) {
			eight += ",";
		}
		eight += "{\"ssid\":\"Net" + std::to_string(i) + "\"}";
	}
	eight += "]}";
	std::vector<WifiNetwork> networks;
	TF_CHECK(WifiStore::parseJsonNetworks(eight, networks));
	TF_EQ(networks.size(), static_cast<size_t>(WifiStore::kMaxNetworks));
}

