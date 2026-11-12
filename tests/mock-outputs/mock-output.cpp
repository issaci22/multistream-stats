// SPDX-License-Identifier: GPL-2.0-or-later
#include "mock-output.hpp"

#include <util/platform.h>

#include <cstring>

namespace msdmock {

namespace {

constexpr int kConnectDelayMs = 150;

void joinConnectThread(MockStream *m)
{
	std::lock_guard<std::mutex> lock(m->threadMutex);
	if (m->connectThread.joinable())
		m->connectThread.join();
}

const char *mockName(void *)
{
	return "MSD Mock Stream (test only)";
}

const char *mockBasicName(void *)
{
	return "MSD Mock Stream Basic (test only)";
}

void *mockCreate(obs_data_t *, obs_output_t *output)
{
	auto *m = new MockStream();
	m->output = output;
	return m;
}

void mockDestroy(void *data)
{
	auto *m = static_cast<MockStream *>(data);
	joinConnectThread(m);
	delete m;
}

void connectWorker(MockStream *m)
{
	const uint64_t t0 = os_gettime_ns();
	os_sleep_ms(kConnectDelayMs);
	if (m->unreachable.load()) {
		// Like rtmp_output: failures while reconnecting are reported as
		// DISCONNECTED (so libobs keeps retrying); a first connect fails.
		obs_output_signal_stop(m->output, m->everConnected.load() ? OBS_OUTPUT_DISCONNECTED
									 : OBS_OUTPUT_CONNECT_FAILED);
		return;
	}
	m->connectMs.store((int)((os_gettime_ns() - t0) / 1'000'000ULL));
	if (!obs_output_can_begin_data_capture(m->output, 0) || !obs_output_initialize_encoders(m->output, 0)) {
		obs_output_signal_stop(m->output, OBS_OUTPUT_ERROR);
		return;
	}
	m->capturing.store(true);
	m->everConnected.store(true);
	obs_output_begin_data_capture(m->output, 0);
}

bool mockStart(void *data)
{
	auto *m = static_cast<MockStream *>(data);
	joinConnectThread(m);
	std::lock_guard<std::mutex> lock(m->threadMutex);
	m->bytes.store(0); // like rtmp_output: counters reset on every (re)connect
	m->dropped.store(0);
	m->connectThread = std::thread(connectWorker, m);
	return true;
}

void mockStop(void *data, uint64_t)
{
	auto *m = static_cast<MockStream *>(data);
	joinConnectThread(m);
	// libobs only calls stop() while data capture is active (or on forced
	// stop during destroy); end capture if this connection got that far.
	if (m->capturing.exchange(false))
		obs_output_end_data_capture(m->output);
}

void mockEncodedPacket(void *data, struct encoder_packet *packet)
{
	auto *m = static_cast<MockStream *>(data);
	if (!packet)
		return;
	if (packet->type == OBS_ENCODER_VIDEO && !packet->keyframe) {
		const double rate = m->dropRate.load();
		if (rate > 0.0) {
			m->rng ^= m->rng << 13;
			m->rng ^= m->rng >> 7;
			m->rng ^= m->rng << 17;
			if ((double)(m->rng % 10000) / 10000.0 < rate) {
				m->dropped.fetch_add(1);
				return;
			}
		}
	}
	m->bytes.fetch_add(packet->size);
}

uint64_t mockTotalBytes(void *data)
{
	return static_cast<MockStream *>(data)->bytes.load();
}

int mockDroppedFrames(void *data)
{
	return static_cast<MockStream *>(data)->dropped.load();
}

float mockCongestion(void *data)
{
	return static_cast<MockStream *>(data)->congestion.load();
}

int mockConnectTime(void *data)
{
	return static_cast<MockStream *>(data)->connectMs.load();
}

} // namespace

void registerMockOutputs()
{
	struct obs_output_info full = {};
	full.id = "msd_mock_stream";
	full.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE;
	full.protocols = "RTMP;RTMPS";
	full.encoded_video_codecs = "h264";
	full.encoded_audio_codecs = "aac";
	full.get_name = mockName;
	full.create = mockCreate;
	full.destroy = mockDestroy;
	full.start = mockStart;
	full.stop = mockStop;
	full.encoded_packet = mockEncodedPacket;
	full.get_total_bytes = mockTotalBytes;
	full.get_dropped_frames = mockDroppedFrames;
	full.get_congestion = mockCongestion;
	full.get_connect_time_ms = mockConnectTime;
	obs_register_output(&full);

	struct obs_output_info basic = full;
	basic.id = "msd_mock_stream_basic";
	basic.get_name = mockBasicName;
	basic.get_dropped_frames = nullptr;
	basic.get_congestion = nullptr;
	basic.get_connect_time_ms = nullptr;
	obs_register_output(&basic);
}

MockStream *mockOf(obs_output_t *output)
{
	if (!output)
		return nullptr;
	const char *id = obs_output_get_id(output);
	if (!id || (std::strcmp(id, "msd_mock_stream") != 0 && std::strcmp(id, "msd_mock_stream_basic") != 0))
		return nullptr;
	return static_cast<MockStream *>(obs_obj_get_data(output));
}

void prepareRelease(obs_output_t *output)
{
	if (MockStream *m = mockOf(output))
		joinConnectThread(m);
}

void simulateDisconnect(obs_output_t *output)
{
	MockStream *m = mockOf(output);
	if (!m || !m->capturing.exchange(false))
		return;
	obs_output_signal_stop(output, OBS_OUTPUT_DISCONNECTED);
}

} // namespace msdmock
