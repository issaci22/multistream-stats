// SPDX-License-Identifier: GPL-2.0-or-later
#include "output-probe.hpp"

#include <core/naming.hpp>

#include <obs.hpp>
#include <util/dstr.h>

#include <cstring>

// The core mirrors libobs stop codes; make sure they stay identical.
static_assert(msd::StopCode::Success == OBS_OUTPUT_SUCCESS);
static_assert(msd::StopCode::BadPath == OBS_OUTPUT_BAD_PATH);
static_assert(msd::StopCode::ConnectFailed == OBS_OUTPUT_CONNECT_FAILED);
static_assert(msd::StopCode::InvalidStream == OBS_OUTPUT_INVALID_STREAM);
static_assert(msd::StopCode::Error == OBS_OUTPUT_ERROR);
static_assert(msd::StopCode::Disconnected == OBS_OUTPUT_DISCONNECTED);
static_assert(msd::StopCode::Unsupported == OBS_OUTPUT_UNSUPPORTED);
static_assert(msd::StopCode::NoSpace == OBS_OUTPUT_NO_SPACE);
static_assert(msd::StopCode::EncodeError == OBS_OUTPUT_ENCODE_ERROR);

namespace msd {

namespace {

std::string safe(const char *s)
{
	return s ? std::string(s) : std::string();
}

// Configured bitrate of a rate-controlled encoder, or -1.
int configuredKbps(obs_encoder_t *encoder)
{
	OBSDataAutoRelease settings = obs_encoder_get_settings(encoder);
	if (!settings)
		return -1;
	const char *rc = obs_data_get_string(settings, "rate_control");
	if (rc && *rc) {
		// Quality-based modes have no bitrate target.
		if (astrcmpi(rc, "CQP") == 0 || astrcmpi(rc, "CRF") == 0 || astrcmpi(rc, "ICQ") == 0 ||
		    astrcmpi(rc, "lossless") == 0 || astrcmpi(rc, "CQVBR") == 0)
			return -1;
	}
	if (!obs_data_has_user_value(settings, "bitrate") && !obs_data_has_default_value(settings, "bitrate"))
		return -1;
	const long long kbps = obs_data_get_int(settings, "bitrate");
	return kbps > 0 ? (int)kbps : -1;
}

struct CanvasLookup {
	video_t *video = nullptr;
	std::string name;
	bool found = false;
};

bool findCanvasByVideo(void *param, obs_canvas_t *canvas)
{
	auto *lookup = static_cast<CanvasLookup *>(param);
	if (obs_canvas_get_video(canvas) == lookup->video) {
		lookup->name = safe(obs_canvas_get_name(canvas));
		lookup->found = true;
		return false;
	}
	return true;
}

EncoderInfo describeEncoders(obs_output_t *output)
{
	EncoderInfo info;
	obs_encoder_t *venc = obs_output_get_video_encoder(output);
	if (venc) {
		info.present = true;
		info.displayName = safe(obs_encoder_get_display_name(obs_encoder_get_id(venc)));
		info.codec = safe(obs_encoder_get_codec(venc));
		info.targetKbps = configuredKbps(venc);

		video_t *mainVideo = obs_get_video();
		video_t *encVideo = obs_encoder_video(venc);
		video_t *parentVideo = obs_encoder_parent_video(venc);

		if (encVideo) {
			const uint32_t divisor = obs_encoder_get_frame_rate_divisor(venc);
			const double rate = video_output_get_frame_rate(encVideo);
			info.fps = rate > 0.0 ? rate / (double)(divisor ? divisor : 1) : 0.0;
		}

		// A dedicated pipeline (GPU-rescaled mix or a separate canvas) has
		// its own skipped-frame counter that belongs to this encoder only.
		info.dedicatedPipeline = encVideo && encVideo != mainVideo;
		if (info.dedicatedPipeline) {
			info.pipelineSkipped = video_output_get_skipped_frames(encVideo);
			info.pipelineTotal = video_output_get_total_frames(encVideo);
		}

		video_t *canvasVideo = parentVideo ? parentVideo : encVideo;
		if (canvasVideo && canvasVideo == mainVideo) {
			info.canvasIsMain = true;
		} else if (canvasVideo) {
			CanvasLookup lookup;
			lookup.video = canvasVideo;
			obs_enum_canvases(findCanvasByVideo, &lookup);
			if (lookup.found)
				info.canvasName = lookup.name;
		}
	}

	obs_encoder_t *aenc = obs_output_get_audio_encoder(output, 0);
	if (aenc) {
		info.audioDisplayName = safe(obs_encoder_get_display_name(obs_encoder_get_id(aenc)));
		OBSDataAutoRelease settings = obs_encoder_get_settings(aenc);
		if (settings) {
			const long long kbps = obs_data_get_int(settings, "bitrate");
			info.audioKbps = kbps > 0 ? (int)kbps : -1;
		}
	}
	return info;
}

} // namespace

bool isStreamingOutput(obs_output_t *output)
{
	return (obs_output_get_flags(output) & OBS_OUTPUT_SERVICE) != 0;
}

OutputDescription describeOutput(obs_output_t *output, bool isPrimary)
{
	OutputDescription d;
	d.outputName = safe(obs_output_get_name(output));
	d.typeId = safe(obs_output_get_id(output));
	d.caps = capabilitiesForOutputType(d.typeId);
	d.kind = isPrimary ? ProviderKind::Primary : classifyByOutputName(d.outputName);
	if (d.kind == ProviderKind::Aitum)
		d.destination = aitumDestinationFromOutputName(d.outputName);

	if (obs_service_t *service = obs_output_get_service(output)) {
		// Only the server URL's host is used; the stream key is never read.
		d.ingestHost = hostFromUrl(safe(obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL)));
		if (std::strcmp(safe(obs_service_get_id(service)).c_str(), "rtmp_common") == 0) {
			OBSDataAutoRelease settings = obs_service_get_settings(service);
			if (settings)
				d.serviceName = safe(obs_data_get_string(settings, "service"));
		}
	}
	return d;
}

OutputSample sampleOutput(obs_output_t *output, uint64_t nowNs)
{
	OutputSample s;
	s.timestampNs = nowNs;
	s.active = obs_output_active(output);
	s.reconnecting = obs_output_reconnecting(output);
	s.totalBytes = obs_output_get_total_bytes(output);
	s.framesDropped = obs_output_get_frames_dropped(output);
	s.totalFrames = obs_output_get_total_frames(output);
	s.congestion = obs_output_get_congestion(output);
	s.connectTimeMs = obs_output_get_connect_time_ms(output);
	s.delaySec = obs_output_get_active_delay(output);
	if (obs_output_get_video_encoder(output)) {
		s.width = obs_output_get_width(output);
		s.height = obs_output_get_height(output);
	}
	s.encoder = describeEncoders(output);
	return s;
}

} // namespace msd
