#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "hardware/wifi/proto_at.h"
#include "hardware/wifi/wifi_params.h"

using namespace wifi;

void setUp() {}
void tearDown() {}

void test_classify_ok_and_error() {
  TEST_ASSERT_TRUE(LineKind::Ok    == classifyLine("OK"));
  TEST_ASSERT_TRUE(LineKind::Error == classifyLine("ERROR"));
  TEST_ASSERT_TRUE(LineKind::Error == classifyLine("FAIL"));
}

void test_classify_urcs() {
  TEST_ASSERT_TRUE(LineKind::WifiConnected  == classifyLine("WIFI CONNECTED"));
  TEST_ASSERT_TRUE(LineKind::WifiGotIp      == classifyLine("WIFI GOT IP"));
  TEST_ASSERT_TRUE(LineKind::WifiDisconnect == classifyLine("WIFI DISCONNECT"));
}

void test_classify_replies() {
  TEST_ASSERT_TRUE(LineKind::CwjapReply == classifyLine("+CWJAP:\"Home\",\"aa:bb:cc:dd:ee:ff\",6,-52"));
  TEST_ASSERT_TRUE(LineKind::CwlapRow   == classifyLine("+CWLAP:(3,\"Home\",-52,\"aa:bb:cc:dd:ee:ff\",6)"));
  TEST_ASSERT_TRUE(LineKind::Cifsr      == classifyLine("+CIFSR:STAIP,\"192.168.0.42\""));
}

void test_classify_other_for_unrecognised_lines() {
  TEST_ASSERT_TRUE(LineKind::Other == classifyLine(""));
  TEST_ASSERT_TRUE(LineKind::Other == classifyLine("ready"));
  TEST_ASSERT_TRUE(LineKind::Other == classifyLine("+IPD,4:test"));
}

void test_parse_cwjap_reply_extracts_ssid_and_rssi() {
  char ssid[33]; int16_t rssi = 0;
  bool ok = parseCwjapReply("+CWJAP:\"Home\",\"aa:bb:cc:dd:ee:ff\",6,-52", ssid, sizeof(ssid), &rssi);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("Home", ssid);
  TEST_ASSERT_EQUAL_INT16(-52, rssi);
}

void test_parse_cwjap_reply_rejects_malformed_line() {
  char ssid[33]; int16_t rssi = 0;
  TEST_ASSERT_FALSE(parseCwjapReply("+CWJAP:garbage", ssid, sizeof(ssid), &rssi));
}

void test_parse_cwlap_row_extracts_ssid_and_rssi() {
  ScanResult r;
  bool ok = parseCwlapRow("+CWLAP:(3,\"Neighbour\",-81,\"11:22:33:44:55:66\",11)", &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("Neighbour", r.ssid);
  TEST_ASSERT_EQUAL_INT16(-81, r.rssi);
}

// The real parsing trap: an SSID may itself contain a comma. A naive
// split-on-comma would read "Bob's" as the whole SSID and desync every
// field after it.
void test_parse_cwlap_row_handles_comma_inside_ssid() {
  ScanResult r;
  bool ok = parseCwlapRow("+CWLAP:(4,\"Bob's, Actually\",-70,\"aa:bb:cc:00:11:22\",1)", &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("Bob's, Actually", r.ssid);
  TEST_ASSERT_EQUAL_INT16(-70, r.rssi);
}

void test_parse_cwlap_row_truncates_an_oversized_ssid_rather_than_overflow() {
  ScanResult r;
  char longSsid[40];
  memset(longSsid, 'x', sizeof(longSsid) - 1);
  longSsid[sizeof(longSsid) - 1] = '\0';
  char line[96];
  snprintf(line, sizeof(line), "+CWLAP:(3,\"%s\",-60,\"aa:bb:cc:dd:ee:ff\",1)", longSsid);
  bool ok = parseCwlapRow(line, &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT(32, strlen(r.ssid));   // sizeof(ScanResult::ssid) - 1
}

void test_parse_cifsr_packs_octets_big_endian() {
  uint32_t ip = 0;
  bool ok = parseCifsr("+CIFSR:STAIP,\"192.168.0.1\"", &ip);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(0xC0A80001u, ip);
}

void test_parse_cifsr_rejects_malformed_line() {
  uint32_t ip = 0;
  TEST_ASSERT_FALSE(parseCifsr("+CIFSR:STAIP,\"not-an-ip\"", &ip));
}

void test_wifi_desc_has_two_params_and_three_tlm_fields() {
  TEST_ASSERT_EQUAL_UINT(2, wifi::kDesc.paramCount);
  TEST_ASSERT_EQUAL_UINT(3, wifi::kDesc.tlmCount);
  TEST_ASSERT_EQUAL_STRING("wifi", wifi::kDesc.id);
}

void test_wifi_password_param_is_secret_ssid_is_not() {
  TEST_ASSERT_FALSE(wifi::kDesc.params[wifi::P_SSID].secret);
  TEST_ASSERT_TRUE(wifi::kDesc.params[wifi::P_PASSWORD].secret);
}

void test_wifi_ip_tlm_uses_the_ip_renderer() {
  TEST_ASSERT_EQUAL_STRING("ip", wifi::kDesc.tlm[wifi::T_IP].fmt);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_classify_ok_and_error);
  RUN_TEST(test_classify_urcs);
  RUN_TEST(test_classify_replies);
  RUN_TEST(test_classify_other_for_unrecognised_lines);
  RUN_TEST(test_parse_cwjap_reply_extracts_ssid_and_rssi);
  RUN_TEST(test_parse_cwjap_reply_rejects_malformed_line);
  RUN_TEST(test_parse_cwlap_row_extracts_ssid_and_rssi);
  RUN_TEST(test_parse_cwlap_row_handles_comma_inside_ssid);
  RUN_TEST(test_parse_cwlap_row_truncates_an_oversized_ssid_rather_than_overflow);
  RUN_TEST(test_parse_cifsr_packs_octets_big_endian);
  RUN_TEST(test_parse_cifsr_rejects_malformed_line);
  RUN_TEST(test_wifi_desc_has_two_params_and_three_tlm_fields);
  RUN_TEST(test_wifi_password_param_is_secret_ssid_is_not);
  RUN_TEST(test_wifi_ip_tlm_uses_the_ip_renderer);
  return UNITY_END();
}
