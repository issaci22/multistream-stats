// SPDX-License-Identifier: GPL-2.0-or-later
#include <doctest.h>

#include <core/format.hpp>
#include <core/naming.hpp>

using namespace msd;

TEST_CASE("Aitum output names map to destination names")
{
	CHECK(aitumDestinationFromOutputName("aitum_multi_output_YouTube Horizontal") == "YouTube Horizontal");
	CHECK(aitumDestinationFromOutputName("aitum_multi_output_") == "");
	CHECK(aitumDestinationFromOutputName("adv_stream") == "");
	CHECK(classifyByOutputName("aitum_multi_output_Kick Output") == ProviderKind::Aitum);
	CHECK(classifyByOutputName("aitum_multi_output_") == ProviderKind::Generic);
	CHECK(classifyByOutputName("multi-output") == ProviderKind::MultiRtmp);
	CHECK(classifyByOutputName("adv_stream") == ProviderKind::Generic);
	CHECK(classifyByOutputName("aitum_multi_video_encoder_X") == ProviderKind::Generic);
}

TEST_CASE("Capability table matches OBS 33.0.1 output implementations")
{
	auto rtmp = capabilitiesForOutputType("rtmp_output");
	CHECK(rtmp.droppedFrames);
	CHECK(rtmp.congestion);
	CHECK(rtmp.connectTime);

	auto srt = capabilitiesForOutputType("ffmpeg_mpegts_muxer");
	CHECK_FALSE(srt.droppedFrames);
	CHECK_FALSE(srt.congestion);
	CHECK_FALSE(srt.connectTime);

	auto whip = capabilitiesForOutputType("whip_output");
	CHECK_FALSE(whip.droppedFrames);
	CHECK_FALSE(whip.congestion);
	CHECK(whip.connectTime);

	auto unknown = capabilitiesForOutputType("some_plugin_output");
	CHECK_FALSE(unknown.droppedFrames);
}

TEST_CASE("hostFromUrl keeps only the host and never credentials, path or query")
{
	CHECK(hostFromUrl("rtmps://a.rtmps.youtube.com:443/live2") == "a.rtmps.youtube.com");
	CHECK(hostFromUrl("rtmp://user:secret@Live.Twitch.TV/app/streamkey") == "live.twitch.tv");
	CHECK(hostFromUrl("srt://127.0.0.1:9000?streamid=abc&passphrase=x") == "127.0.0.1");
	CHECK(hostFromUrl("https://whip.example.com/whip/endpoint?token=abc") == "whip.example.com");
	CHECK(hostFromUrl("rtmp://[::1]:1935/live") == "[::1]");
	CHECK(hostFromUrl("") == "");
	CHECK(hostFromUrl("rtmp://bad host/x") == "");
}

TEST_CASE("Row keys")
{
	OutputObservation o;
	o.kind = ProviderKind::Aitum;
	o.destination = "YouTube Horizontal";
	CHECK(baseRowKey(o) == "aitum:YouTube Horizontal");
	o.kind = ProviderKind::Primary;
	CHECK(baseRowKey(o) == "primary");
	o.kind = ProviderKind::MultiRtmp;
	o.ingestHost = "live.twitch.tv";
	CHECK(baseRowKey(o) == "multirtmp:live.twitch.tv");
	o.kind = ProviderKind::Generic;
	o.outputName = "source_record_stream";
	CHECK(baseRowKey(o) == "output:source_record_stream|live.twitch.tv");
}

TEST_CASE("Formatting follows native Stats dock conventions")
{
	CHECK(fmt::kbps(6012.4) == "6012 kb/s");
	CHECK(fmt::kbps(0.0) == "0 kb/s");
	CHECK(fmt::kbps(9999.4) == "9999 kb/s");
	CHECK(fmt::kbps(19840.0) == "19.84 Mb/s");
	CHECK(fmt::percent(2.94) == "2.9%");
	CHECK(fmt::percent(0.0) == "0.0%");
	CHECK(fmt::frames(720, 25142, 2.86) == "720 / 25142 (2.9%)");
	CHECK(fmt::duration(0) == "00:00:00");
	CHECK(fmt::duration(4364ULL * 1'000'000'000ULL) == "01:12:44");
	CHECK(fmt::duration(100ULL * 3600ULL * 1'000'000'000ULL) == "100:00:00");
	CHECK(fmt::bytes(512ULL * 1024 * 1024) == "512.0 MiB");
	CHECK(fmt::bytes(3ULL * 1024 * 1024 * 1024 + 220ULL * 1024 * 1024) == "3.21 GiB");
	CHECK(fmt::fps(59.94) == "59.94");
	CHECK(fmt::milliseconds(1.234) == "1.2 ms");
	CHECK(fmt::resolution(2560, 1440) == "2560x1440");
}
