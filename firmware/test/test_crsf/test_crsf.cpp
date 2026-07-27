#include <unity.h>
#include <string.h>
#include "hardware/crsf/crsf_params.h"

using namespace crsf;

// A valid RC_CHANNELS_PACKED frame. len 0x18 = 24 = type + 22 payload + crc.
// Channels: 172, 992, 1811, 1000, 500, 1500, 200, 1800, then four at 992 and
// four at 0 -- deliberately spanning both endpoints and the centre.
static const uint8_t kRcFrame[] = {
  0xC8, 0x18, 0x16,
  0xAC, 0x00, 0xDF, 0xC4, 0xD1, 0x47, 0x1F, 0xEE, 0x22, 0x03, 0xE1,
  0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x1B,
};
static const uint16_t kRcExpected[16] = {
  172, 992, 1811, 1000, 500, 1500, 200, 1800, 992, 992, 992, 992, 0, 0, 0, 0,
};

// A valid LINK_STATISTICS frame: rssi1 42, rssi2 99, lq 100, snr 12,
// antenna 0, rf profile 2, power 3, downlink 55/98/10.
static const uint8_t kLinkFrame[] = {
  0xC8, 0x0C, 0x14, 0x2A, 0x63, 0x64, 0x0C, 0x00, 0x02, 0x03, 0x37, 0x62,
  0x0A, 0xC1,
};

// The same frame with 0xC8 -- the sync byte -- sitting in the payload as the
// downlink RSSI. Feeding this proves an embedded sync cannot desynchronise
// the stream, which is the failure mode a naive "scan for 0xC8" parser has.
static const uint8_t kLinkFrameEmbeddedSync[] = {
  0xC8, 0x0C, 0x14, 0x2A, 0x63, 0x64, 0x0C, 0x00, 0x02, 0x03, 0xC8, 0x62,
  0x0A, 0x83,
};

static FrameParser::Result feedAll(FrameParser& p, const uint8_t* b, size_t n) {
  FrameParser::Result last = FrameParser::Result::None;
  for (size_t i = 0; i < n; ++i) last = p.feed(b[i]);
  return last;
}

// --- crc8 --------------------------------------------------------------------

void test_crc8_of_empty_is_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, crc8(nullptr, 0));
}

void test_crc8_matches_a_real_rc_frame() {
  // Covers type + payload: bytes 2..24 of the frame, and the answer is the
  // last byte the transmitter appended.
  TEST_ASSERT_EQUAL_UINT8(0x1B, crc8(kRcFrame + 2, 23));
}

void test_crc8_matches_a_real_link_frame() {
  TEST_ASSERT_EQUAL_UINT8(0xC1, crc8(kLinkFrame + 2, 11));
}

void test_crc8_detects_a_single_flipped_bit() {
  uint8_t corrupt[23];
  memcpy(corrupt, kRcFrame + 2, 23);
  corrupt[7] ^= 0x01;
  TEST_ASSERT_NOT_EQUAL(0x1B, crc8(corrupt, 23));
}

// --- ticksToUs ---------------------------------------------------------------

void test_ticks_to_us_hits_the_published_reference_points() {
  // These three are the spec's own reference values. They are the reason the
  // conversion floor-divides with a +4 bias: plain truncation gives 2011 at
  // the top and round-half-away gives 987 at the bottom.
  TEST_ASSERT_EQUAL_UINT16(988,  ticksToUs(172));
  TEST_ASSERT_EQUAL_UINT16(1500, ticksToUs(992));
  TEST_ASSERT_EQUAL_UINT16(2012, ticksToUs(1811));
}

void test_ticks_to_us_is_monotonic_across_the_whole_11_bit_range() {
  uint16_t prev = ticksToUs(0);
  for (uint16_t t = 1; t < 2048; ++t) {
    uint16_t v = ticksToUs(t);
    TEST_ASSERT_TRUE(v >= prev);
    prev = v;
  }
}

void test_ticks_to_us_does_not_clamp_out_of_range_input() {
  // A receiver may legally send outside 172..1811. Reporting the truth is the
  // point; the browser clamps its bar, the firmware does not clamp the value.
  TEST_ASSERT_EQUAL_UINT16(880,  ticksToUs(0));
  TEST_ASSERT_EQUAL_UINT16(2159, ticksToUs(2047));
}

// --- unpackChannels ----------------------------------------------------------

void test_unpack_channels_decodes_a_real_payload() {
  uint16_t ch[kWireChannels] = {};
  unpackChannels(kRcFrame + 3, ch);
  for (uint8_t i = 0; i < kWireChannels; ++i)
    TEST_ASSERT_EQUAL_UINT16(kRcExpected[i], ch[i]);
}

void test_unpack_channels_all_zero_payload() {
  uint8_t payload[kRcPayloadLen] = {};
  uint16_t ch[kWireChannels] = {};
  unpackChannels(payload, ch);
  for (uint8_t i = 0; i < kWireChannels; ++i) TEST_ASSERT_EQUAL_UINT16(0, ch[i]);
}

void test_unpack_channels_all_ones_payload_saturates_every_slot() {
  uint8_t payload[kRcPayloadLen];
  memset(payload, 0xFF, sizeof(payload));
  uint16_t ch[kWireChannels] = {};
  unpackChannels(payload, ch);
  // 11 bits set, and nothing bled into a 12th.
  for (uint8_t i = 0; i < kWireChannels; ++i) TEST_ASSERT_EQUAL_UINT16(2047, ch[i]);
}

// --- decodeLinkStats ---------------------------------------------------------

void test_decode_link_stats_extracts_fields_and_flips_the_rssi_sign() {
  LinkStats s = {};
  decodeLinkStats(kLinkFrame + 3, &s);
  TEST_ASSERT_EQUAL_UINT8(100, s.lq);
  TEST_ASSERT_EQUAL_INT8(12, s.snr);
  TEST_ASSERT_EQUAL_UINT8(0, s.antenna);
  // The wire carries a positive magnitude; dBm is negative.
  TEST_ASSERT_EQUAL_INT16(-42, s.rssiDbm);
}

void test_decode_link_stats_follows_the_active_antenna() {
  uint8_t payload[kLinkPayloadLen];
  memcpy(payload, kLinkFrame + 3, sizeof(payload));
  payload[4] = 1;                       // active antenna = 2nd
  LinkStats s = {};
  decodeLinkStats(payload, &s);
  TEST_ASSERT_EQUAL_UINT8(1, s.antenna);
  TEST_ASSERT_EQUAL_INT16(-99, s.rssiDbm);   // rssi_ant2, not ant1
}

// --- FrameParser -------------------------------------------------------------

void test_parser_accepts_a_valid_rc_frame() {
  FrameParser p;
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
  TEST_ASSERT_EQUAL_UINT8(kTypeRcChannels, p.type());
  TEST_ASSERT_EQUAL_UINT8(kRcPayloadLen, p.payloadLen());
  uint16_t ch[kWireChannels] = {};
  unpackChannels(p.payload(), ch);
  TEST_ASSERT_EQUAL_UINT16(1811, ch[2]);
}

void test_parser_returns_none_until_the_final_byte() {
  FrameParser p;
  for (size_t i = 0; i < sizeof(kRcFrame) - 1; ++i)
    TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::None, (int)p.feed(kRcFrame[i]));
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)p.feed(kRcFrame[sizeof(kRcFrame) - 1]));
}

void test_parser_rejects_a_bad_crc() {
  uint8_t bad[sizeof(kRcFrame)];
  memcpy(bad, kRcFrame, sizeof(bad));
  bad[sizeof(bad) - 1] ^= 0xFF;
  FrameParser p;
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Rejected,
                        (int)feedAll(p, bad, sizeof(bad)));
}

void test_parser_rejects_an_out_of_range_length_without_consuming_payload() {
  // len 0, 1 and 63+ are all impossible. Each must be refused on the length
  // byte itself, so the very next 0xC8 is treated as a fresh sync.
  const uint8_t kBadLens[] = {0x00, 0x01, 0x3F, 0xFF};
  for (uint8_t i = 0; i < sizeof(kBadLens); ++i) {
    FrameParser p;
    TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::None, (int)p.feed(kSync));
    TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Rejected, (int)p.feed(kBadLens[i]));
    TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                          (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
  }
}

void test_parser_skips_garbage_before_a_frame() {
  const uint8_t junk[] = {0x00, 0xFF, 0x13, 0x7E, 0xAA};
  FrameParser p;
  feedAll(p, junk, sizeof(junk));
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
}

void test_parser_recovers_within_two_frames_of_a_tear() {
  // A torn stream is what a UART buffer overflow during an 87ms display
  // refresh actually looks like: the parser is left mid-payload owing a byte
  // count the frame it was reading will never supply.
  //
  // Recovery is bounded but NOT immediate, and the bound is measured rather
  // than assumed. Across all 26 possible tear points in this frame, 25 swallow
  // exactly one following whole frame before the next decodes and one costs
  // nothing -- so the price is the torn frame plus at most one more, ~13ms at
  // 150 frames/s.
  //
  // Both halves are asserted deliberately. A parser that quietly needed three
  // frames would still satisfy a test that only checked the happy ending.
  FrameParser p;
  feedAll(p, kRcFrame, 10);                        // truncated mid-payload
  TEST_ASSERT_NOT_EQUAL((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
}

void test_parser_is_not_desynchronised_by_a_sync_byte_inside_a_payload() {
  FrameParser p;
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kLinkFrameEmbeddedSync,
                                     sizeof(kLinkFrameEmbeddedSync)));
  TEST_ASSERT_EQUAL_UINT8(kTypeLinkStats, p.type());
  // And the frame that follows it still decodes.
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
}

void test_parser_passes_through_an_unknown_frame_type() {
  // Framing is type-agnostic: an unrecognised type is a well-formed frame the
  // caller ignores, not a parse error. GPS and battery frames will arrive on
  // a real link and must not disturb the stream.
  FrameParser p;
  uint8_t body[] = {0x02, 0x11, 0x22, 0x33};       // type 0x02 (GPS), 3 bytes
  // len 0x05 = type + 3 payload + crc. It counts the CRC but not itself and
  // not the sync byte, so payloadLen() is len - 2.
  uint8_t frame[7] = {kSync, 0x05, body[0], body[1], body[2], body[3], 0};
  frame[6] = crc8(body, sizeof(body));
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_UINT8(0x02, p.type());
  TEST_ASSERT_EQUAL_UINT8(3, p.payloadLen());
  TEST_ASSERT_EQUAL_INT((int)FrameParser::Result::Frame,
                        (int)feedAll(p, kRcFrame, sizeof(kRcFrame)));
}

// --- LinkState ---------------------------------------------------------------

void test_link_starts_down_with_nothing_counted() {
  LinkState s;
  TEST_ASSERT_FALSE(s.up());
  TEST_ASSERT_EQUAL_UINT32(0, s.rate());
  TEST_ASSERT_EQUAL_UINT32(0, s.errors());
}

void test_link_stays_down_while_no_frame_has_ever_arrived() {
  // A board with nothing wired to the UART must read "down", not "timed out
  // from an imaginary link". Silence before the first frame is not an error.
  LinkState s;
  for (uint32_t t = 0; t < 10000; t += 100) s.tick(t, 1000);
  TEST_ASSERT_FALSE(s.up());
  TEST_ASSERT_EQUAL_UINT32(0, s.errors());
}

void test_link_comes_up_on_the_first_frame() {
  LinkState s;
  s.onFrame(5000);
  s.tick(5000, 1000);
  TEST_ASSERT_TRUE(s.up());
}

void test_link_survives_exactly_the_timeout_and_drops_past_it() {
  LinkState s;
  s.onFrame(5000);
  s.tick(6000, 1000);          // exactly at the timeout
  TEST_ASSERT_TRUE(s.up());
  s.tick(6001, 1000);          // one millisecond past
  TEST_ASSERT_FALSE(s.up());
}

void test_link_loss_zeroes_the_rate_immediately() {
  // Not at the end of the next window: "link 0, rate 150" would be a lie for
  // up to a second, and the rate is the field a human reads to confirm loss.
  LinkState s;
  for (uint32_t t = 0; t < 100; ++t) s.onFrame(t * 10);
  s.tick(1000, 1000);
  TEST_ASSERT_TRUE(s.rate() > 0);
  s.tick(3000, 1000);
  TEST_ASSERT_FALSE(s.up());
  TEST_ASSERT_EQUAL_UINT32(0, s.rate());
}

void test_link_recovers_when_frames_resume() {
  LinkState s;
  s.onFrame(1000);
  s.tick(3000, 1000);
  TEST_ASSERT_FALSE(s.up());
  s.onFrame(3100);
  s.tick(3100, 1000);
  TEST_ASSERT_TRUE(s.up());
}

void test_rate_reports_frames_in_the_last_whole_second() {
  LinkState s;
  // 50 frames spread across the first second, starting at t=0.
  for (uint32_t i = 0; i < 50; ++i) s.onFrame(i * 20);
  TEST_ASSERT_EQUAL_UINT32(0, s.rate());   // window has not closed yet
  s.tick(1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(50, s.rate());
}

void test_rate_resets_between_windows() {
  LinkState s;
  for (uint32_t i = 0; i < 50; ++i) s.onFrame(i * 20);
  s.tick(1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(50, s.rate());
  for (uint32_t i = 0; i < 10; ++i) s.onFrame(1000 + i * 20);
  s.tick(2000, 1000);
  TEST_ASSERT_EQUAL_UINT32(10, s.rate());   // not 60
}

void test_errors_accumulate_and_do_not_affect_link_state() {
  // A rejected frame is normal on a torn stream. It must be countable without
  // being mistaken for a lost link -- the timeout owns that decision alone.
  LinkState s;
  s.onFrame(1000);
  s.onReject();
  s.onReject();
  s.tick(1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(2, s.errors());
  TEST_ASSERT_TRUE(s.up());
}

void test_link_timeout_survives_the_millis_wraparound() {
  // millis() wraps at ~49.7 days. Unsigned subtraction handles it; an
  // "if (now < last)" guard would not, and this is the test that would catch
  // someone adding one.
  LinkState s;
  const uint32_t nearMax = 0xFFFFFF00u;
  s.onFrame(nearMax);
  s.tick(nearMax + 500, 1000);     // wraps through zero
  TEST_ASSERT_TRUE(s.up());
  s.tick(nearMax + 1500, 1000);
  TEST_ASSERT_FALSE(s.up());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_of_empty_is_zero);
  RUN_TEST(test_crc8_matches_a_real_rc_frame);
  RUN_TEST(test_crc8_matches_a_real_link_frame);
  RUN_TEST(test_crc8_detects_a_single_flipped_bit);
  RUN_TEST(test_ticks_to_us_hits_the_published_reference_points);
  RUN_TEST(test_ticks_to_us_is_monotonic_across_the_whole_11_bit_range);
  RUN_TEST(test_ticks_to_us_does_not_clamp_out_of_range_input);
  RUN_TEST(test_unpack_channels_decodes_a_real_payload);
  RUN_TEST(test_unpack_channels_all_zero_payload);
  RUN_TEST(test_unpack_channels_all_ones_payload_saturates_every_slot);
  RUN_TEST(test_decode_link_stats_extracts_fields_and_flips_the_rssi_sign);
  RUN_TEST(test_decode_link_stats_follows_the_active_antenna);
  RUN_TEST(test_parser_accepts_a_valid_rc_frame);
  RUN_TEST(test_parser_returns_none_until_the_final_byte);
  RUN_TEST(test_parser_rejects_a_bad_crc);
  RUN_TEST(test_parser_rejects_an_out_of_range_length_without_consuming_payload);
  RUN_TEST(test_parser_skips_garbage_before_a_frame);
  RUN_TEST(test_parser_recovers_within_two_frames_of_a_tear);
  RUN_TEST(test_parser_is_not_desynchronised_by_a_sync_byte_inside_a_payload);
  RUN_TEST(test_parser_passes_through_an_unknown_frame_type);
  RUN_TEST(test_link_starts_down_with_nothing_counted);
  RUN_TEST(test_link_stays_down_while_no_frame_has_ever_arrived);
  RUN_TEST(test_link_comes_up_on_the_first_frame);
  RUN_TEST(test_link_survives_exactly_the_timeout_and_drops_past_it);
  RUN_TEST(test_link_loss_zeroes_the_rate_immediately);
  RUN_TEST(test_link_recovers_when_frames_resume);
  RUN_TEST(test_rate_reports_frames_in_the_last_whole_second);
  RUN_TEST(test_rate_resets_between_windows);
  RUN_TEST(test_errors_accumulate_and_do_not_affect_link_state);
  RUN_TEST(test_link_timeout_survives_the_millis_wraparound);
  return UNITY_END();
}
