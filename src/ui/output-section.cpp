// SPDX-License-Identifier: GPL-2.0-or-later
#include "output-section.hpp"
#include "theme.hpp"
#include "value-label.hpp"

#include <core/format.hpp>

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using msd::OutputState;
using msd::RowSnapshot;

namespace msdui {

namespace {

QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

QString kbpsText(double kbps)
{
	return QString::fromStdString(msd::fmt::kbps(kbps));
}

} // namespace

QString stopCodeText(int code)
{
	switch (code) {
	case msd::StopCode::BadPath:
		return T("StopCode.BadPath");
	case msd::StopCode::ConnectFailed:
		return T("StopCode.ConnectFailed");
	case msd::StopCode::InvalidStream:
		return T("StopCode.InvalidStream");
	case msd::StopCode::Disconnected:
		return T("StopCode.Disconnected");
	case msd::StopCode::Unsupported:
		return T("StopCode.Unsupported");
	case msd::StopCode::NoSpace:
		return T("StopCode.NoSpace");
	case msd::StopCode::EncodeError:
		return T("StopCode.EncodeError");
	case msd::StopCode::HdrDisabled:
		return T("StopCode.HdrDisabled");
	default:
		return T("StopCode.Error");
	}
}

QString outputTitle(const RowSnapshot &row)
{
	QString title;
	switch (row.kind) {
	case msd::ProviderKind::Primary:
		title = T("Output.Primary");
		if (!row.serviceName.empty())
			title += QStringLiteral(" · ") + qs(row.serviceName);
		break;
	case msd::ProviderKind::Aitum:
		title = qs(row.destination);
		break;
	case msd::ProviderKind::MultiRtmp:
		title = row.ingestHost.empty() ? qs(row.outputName) : qs(row.ingestHost);
		break;
	case msd::ProviderKind::Generic:
	default:
		title = qs(row.outputName);
		break;
	}
	if (row.ordinal > 1)
		title += QStringLiteral(" (%1)").arg(row.ordinal);
	return title;
}

namespace {

QString protocolLabel(const std::string &typeId)
{
	if (typeId == "rtmp_output")
		return QStringLiteral("RTMP");
	if (typeId == "ffmpeg_mpegts_muxer")
		return QStringLiteral("SRT/RIST");
	if (typeId == "whip_output")
		return QStringLiteral("WHIP");
	return qs(typeId);
}

// Detail grid columns: tall docks get up to 2 label/value pairs side by side
// when wide enough, the table layout's details up to 3.
constexpr int kStackedDetailColumns = 2;
constexpr int kTableDetailColumns = 3;

} // namespace

OutputSection::OutputSection(const QString &key, QWidget *parent) : QWidget(parent), key_(key)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	// Header
	header_ = new QWidget(this);
	header_->setCursor(Qt::PointingHandCursor);
	header_->installEventFilter(this);
	headerLayout_ = new QHBoxLayout(header_);
	headerLayout_->setContentsMargins(0, 0, 0, 0);

	arrow_ = new DisclosureArrow(header_);
	arrow_->setToolTip(T("Section.Toggle"));
	QSizePolicy keep = arrow_->sizePolicy();
	keep.setRetainSizeWhenHidden(true); // titles stay aligned when hidden
	arrow_->setSizePolicy(keep);
	connect(arrow_, &QAbstractButton::clicked, this, [this]() {
		setExpanded(!expanded_);
		emit expandedChanged(key_, expanded_);
	});

	indicator_ = new StatusIndicator(header_);

	// Narrow docks: the provider subtitle is dropped first (it is repeated in
	// the title's tooltip), then the destination name elides; the headline
	// (bitrate or state) and the table cells are values and are never cut off.
	nameBox_ = new QWidget(header_);
	auto *nameLayout = new QHBoxLayout(nameBox_);
	nameLayout->setContentsMargins(0, 0, 0, 0);
	title_ = new ElidedLabel(nameBox_, ElidedLabel::Role::Text);
	title_->setStyleSheet(QStringLiteral("font-weight: bold;"));
	subtitle_ = new ElidedLabel(nameBox_, ElidedLabel::Role::Label);
	setClasses(subtitle_, QStringLiteral("text-muted"));
	nameLayout->addWidget(title_, 0);
	nameLayout->addWidget(subtitle_, 1);

	headline_ = new ElidedLabel(header_, ElidedLabel::Role::Value);
	headline_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	headerLayout_->addWidget(arrow_);
	headerLayout_->addWidget(indicator_);
	headerLayout_->addWidget(nameBox_, 1);
	headerLayout_->addWidget(headline_, 0);

	// Table cells (hidden in the stacked layout).
	const char *cellTips[kTableColumns] = {"Tooltip.Status", "Tooltip.Bitrate", "Tooltip.Dropped", "Tooltip.DataSent",
					       "Tooltip.Duration"};
	for (int c = 0; c < kTableColumns; ++c) {
		auto *l = new ElidedLabel(header_, ElidedLabel::Role::Value);
		l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		l->setProperty(kTipProperty, T(cellTips[c]));
		l->setBaseToolTip(T(cellTips[c]));
		l->hide();
		headerLayout_->addWidget(l, 0);
		cells_[(size_t)c] = l;
	}
	headerLayout_->addStretch(0); // takes the spare width in the table layout
	layout->addWidget(header_);

	// Body, indented to line up with the title.
	body_ = new QWidget(this);
	auto *bodyLayout = new QVBoxLayout(body_);
	bodyLayout->setContentsMargins(arrow_->sizeHint().width() + indicator_->sizeHint().width(), 0, 0, 0);
	grid_ = new StatGrid(body_);
	grid_->setMaxColumns(kStackedDetailColumns);
	bodyLayout->addWidget(grid_);
	layout->addWidget(body_);

	// Measured values (Role::Value) are never cut off; descriptive text
	// (Role::Text) may shorten, with the full text in the tooltip.
	using R = ElidedLabel::Role;
	status_ = grid_->addRow(T("Stat.Status"), T("Tooltip.Status"), R::Value);
	duration_ = grid_->addRow(T("Stat.Duration"), T("Tooltip.Duration"), R::Value);
	bitrate_ = grid_->addRow(T("Stat.Bitrate"), T("Tooltip.Bitrate"), R::Value);
	dropped_ = grid_->addRow(T("Stat.Dropped"), T("Tooltip.Dropped"), R::Value);
	congestion_ = grid_->addRow(T("Stat.Congestion"), T("Tooltip.Congestion"), R::Value);
	video_ = grid_->addRow(T("Stat.Video"), T("Tooltip.Video"), R::Value);
	encoders_ = grid_->addRow(T("Stat.Encoders"), T("Tooltip.Encoders"), R::Text);
	pipeline_ = grid_->addRow(T("Stat.EncodingLag"), T("Tooltip.EncodingLag"), R::Value);
	data_ = grid_->addRow(T("Stat.DataSent"), T("Tooltip.DataSent"), R::Value);
	connection_ = grid_->addRow(T("Stat.Connection"), T("Tooltip.Connection"), R::Value);
	server_ = grid_->addRow(T("Stat.Server"), T("Tooltip.Server"), R::Text);
	delay_ = grid_->addRow(T("Stat.Delay"), T("Tooltip.Delay"), R::Value);
	error_ = grid_->addRow(T("Stat.LastError"), T("Tooltip.LastError"), R::Text);
}

int OutputSection::headerSpacing() const
{
	if (headerSpacing_ < 0) // cached: QBoxLayout::spacing() asks the (style-sheet) style each time
		headerSpacing_ = std::max(0, headerLayout_->spacing());
	return headerSpacing_;
}

int OutputSection::leadWidth() const
{
	return arrow_->sizeHint().width() + indicator_->sizeHint().width() + 2 * headerSpacing();
}

int OutputSection::nameNaturalWidth() const
{
	int w = title_->sizeHint().width();
	if (!subtitle_->fullText().isEmpty())
		w += headerSpacing() + subtitle_->sizeHint().width();
	return w;
}

int OutputSection::cellNaturalWidth(TableColumn column) const
{
	return cell(column)->sizeHint().width();
}

void OutputSection::updateHeaderFit()
{
	// Show the provider subtitle only if the whole name fits with it.
	if (subtitle_->fullText().isEmpty()) {
		subtitle_->setVisible(false);
		return;
	}
	const int sp = headerSpacing();
	int avail;
	if (table_.enabled) {
		avail = table_.nameWidth;
	} else {
		const QMargins m = headerLayout_->contentsMargins();
		avail = header_->width() - m.left() - m.right() - arrow_->sizeHint().width() -
			indicator_->sizeHint().width() - headline_->sizeHint().width() - 3 * sp;
	}
	const bool fits = avail >= title_->sizeHint().width() + sp + subtitle_->sizeHint().width();
	if (subtitle_->isHidden() == fits)
		subtitle_->setVisible(fits);
}

void OutputSection::setTableGeometry(const TableGeometry &g)
{
	if (g == table_)
		return;
	table_ = g;
	const int trailing = headerLayout_->count() - 1;
	if (g.enabled) {
		nameBox_->setFixedWidth(g.nameWidth);
		headerLayout_->setStretchFactor(nameBox_, 0);
		headerLayout_->setStretch(trailing, 1);
		headline_->hide();
		for (int c = 0; c < kTableColumns; ++c) {
			cells_[(size_t)c]->setFixedWidth(g.widths[(size_t)c]);
			cells_[(size_t)c]->setVisible(c < g.columns);
		}
		grid_->setMaxColumns(kTableDetailColumns);
	} else {
		nameBox_->setMinimumWidth(0);
		nameBox_->setMaximumWidth(QWIDGETSIZE_MAX);
		headerLayout_->setStretchFactor(nameBox_, 1);
		headerLayout_->setStretch(trailing, 0);
		headline_->show();
		for (ElidedLabel *c : cells_)
			c->hide();
		grid_->setMaxColumns(kStackedDetailColumns);
	}
	updateRowVisibility();
	updateHeaderFit();
}

void OutputSection::setExpanded(bool expanded)
{
	expanded_ = expanded;
	arrow_->setChecked(expanded);
	body_->setVisible(expanded && hasStats_);
}

void OutputSection::updateRowVisibility()
{
	// Rows whose value is a column of the table layout are not repeated in
	// the details; Bitrate stays because it adds the session average.
	using C = TableColumn;
	const bool s = hasStats_;
	grid_->setRowVisible(status_, !table_.shows(C::Status));
	grid_->setRowVisible(duration_, s && !table_.shows(C::Duration));
	grid_->setRowVisible(bitrate_, s);
	grid_->setRowVisible(dropped_, s && !table_.shows(C::Dropped));
	grid_->setRowVisible(congestion_, s);
	grid_->setRowVisible(video_, s);
	grid_->setRowVisible(encoders_, s);
	grid_->setRowVisible(pipeline_, s && dedicatedPipeline_);
	grid_->setRowVisible(data_, s && !table_.shows(C::Data));
	grid_->setRowVisible(connection_, s);
	grid_->setRowVisible(server_, hasServer_);
	grid_->setRowVisible(delay_, s && hasDelay_);
	grid_->setRowVisible(error_, hasError_);
	body_->setVisible(expanded_ && hasStats_);
}

bool OutputSection::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == header_ && (event->type() == QEvent::StyleChange || event->type() == QEvent::Polish))
		headerSpacing_ = -1;
	if (watched == header_ && event->type() == QEvent::Resize)
		updateHeaderFit();
	if (watched == header_ && event->type() == QEvent::MouseButtonRelease && hasStats_) {
		auto *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::LeftButton) {
			setExpanded(!expanded_);
			emit expandedChanged(key_, expanded_);
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

QString OutputSection::titleFor(const RowSnapshot &row) const
{
	return outputTitle(row);
}

// Muted text after the title naming the integration that owns the output.
QString OutputSection::providerFor(const RowSnapshot &row) const
{
	switch (row.kind) {
	case msd::ProviderKind::Primary:
		return T("Provider.Primary");
	case msd::ProviderKind::Aitum:
		return row.vertical ? T("Provider.Aitum") + QStringLiteral(" · ") + T("Output.Vertical") : T("Provider.Aitum");
	case msd::ProviderKind::MultiRtmp:
		return T("Output.MultiRtmp");
	case msd::ProviderKind::Generic:
	default:
		return row.typeId.empty() ? QString() : protocolLabel(row.typeId);
	}
}

QString OutputSection::tooltipFor(const RowSnapshot &row) const
{
	QString tip;
	switch (row.kind) {
	case msd::ProviderKind::Primary:
		tip = T("Output.Primary.Tooltip");
		break;
	case msd::ProviderKind::Aitum:
		tip = T("Output.Aitum.Tooltip");
		break;
	case msd::ProviderKind::MultiRtmp:
		tip = T("Output.MultiRtmp.Tooltip");
		break;
	case msd::ProviderKind::Generic:
	default:
		tip = T("Output.Generic.Tooltip");
		break;
	}
	if (!row.outputName.empty())
		tip += QStringLiteral("\n") + T("Output.Name").arg(qs(row.outputName), qs(row.typeId));
	return tip;
}

QString OutputSection::stateText(const RowSnapshot &row) const
{
	switch (row.state) {
	case OutputState::Idle:
		return row.kind == msd::ProviderKind::Primary ? T("State.NotStreaming") : T("State.Idle");
	case OutputState::Connecting:
		return T("State.Connecting");
	case OutputState::Live:
		return T("State.Live");
	case OutputState::Reconnecting:
		return T("State.Reconnecting");
	case OutputState::Stopping:
		return T("State.Stopping");
	case OutputState::Stopped:
		return T("State.Stopped");
	case OutputState::Disconnected:
		return T("State.Disconnected");
	case OutputState::Error:
		return T("State.Error");
	}
	return QString();
}

void OutputSection::apply(const RowSnapshot &row)
{
	const bool running = msd::isRunning(row.state);
	const bool flowing = row.state == OutputState::Live || row.state == OutputState::Reconnecting ||
			     row.state == OutputState::Stopping;
	// Values of a finished session are shown muted ("last session").
	const QString plain = running ? QString() : QStringLiteral("text-muted");
	auto colored = [&](msd::Health h) { return running ? healthClass(h) : plain; };

	// Header
	title_->setFullText(titleFor(row));
	title_->setBaseToolTip(tooltipFor(row));
	subtitle_->setFullText(providerFor(row));
	subtitle_->setBaseToolTip(tooltipFor(row));
	indicator_->setHealth(row.health);
	indicator_->setToolTip(stateText(row));
	if (flowing && row.kbps.has()) {
		headline_->setFullText(kbpsText(row.kbps.value()));
		setClasses(headline_, row.health == msd::Health::Good ? QString() : healthClass(row.health));
	} else {
		headline_->setFullText(stateText(row));
		setClasses(headline_, healthClass(row.health));
	}

	// A destination that has never run has no statistics: it is shown as a
	// single header line (its state is already in the header) instead of a
	// column of N/A values.
	const bool hasStats = row.hasSession;
	if (hasStats != hasStats_) {
		hasStats_ = hasStats;
		arrow_->setVisible(hasStats);
		header_->setCursor(hasStats ? Qt::PointingHandCursor : Qt::ArrowCursor);
	}
	hasServer_ = !row.ingestHost.empty();
	hasDelay_ = row.delaySec > 0;
	hasError_ = !row.lastError.empty();
	dedicatedPipeline_ = row.encoder.dedicatedPipeline;
	updateRowVisibility();
	updateHeaderFit();

	// Status
	QString status = stateText(row);
	if (row.state == OutputState::Reconnecting && row.reconnects > 0)
		status += QStringLiteral(" · ") + T("Stat.Reconnects").arg(row.reconnects);
	if (row.state == OutputState::Disconnected || row.state == OutputState::Error)
		status += QStringLiteral(" · ") + stopCodeText(row.stopCode);
	if (!running && row.hasSession && row.state != OutputState::Idle)
		status += QStringLiteral(" · ") + T("Stat.LastSession");
	setValue(status_, status, healthClass(msd::stateHealth(row.state)), QString(), stateText(row));

	// Duration
	if (row.durationNs.has()) {
		QString extra;
		if (row.startSource == msd::StartSource::Detected)
			extra = T("Tooltip.DurationDetected");
		setValue(duration_, QString::fromStdString(msd::fmt::duration(row.durationNs.value())), plain, extra);
	} else {
		setNA(duration_, row.durationNs.reason());
	}

	// Bitrate
	if (flowing && row.kbps.has()) {
		const QString current = kbpsText(row.kbps.value());
		QString text = current;
		if (row.avgKbps.has())
			text += QStringLiteral("  (") + T("Stat.Average").arg(kbpsText(row.avgKbps.value())) +
				QStringLiteral(")");
		setValue(bitrate_, text, plain, QString(), current);
	} else if (row.avgKbps.has()) {
		setValue(bitrate_, T("Stat.Average").arg(kbpsText(row.avgKbps.value())), plain, naTooltip(row.kbps.reason()));
	} else {
		setNA(bitrate_, row.kbps.reason());
	}

	// Network-dropped frames
	if (row.dropped.has() && row.totalFrames.has()) {
		QString text;
		QString compact;
		if (row.dropPct.has()) {
			text = QString::fromStdString(
				msd::fmt::frames(row.dropped.value(), row.totalFrames.value(), row.dropPct.value()));
			compact = QString::fromStdString(msd::fmt::framesCompact(row.dropped.value(), row.dropPct.value()));
		} else {
			text = QString::fromStdString(msd::fmt::count(row.dropped.value()) + " / " +
						      msd::fmt::count(row.totalFrames.value()));
			compact = QString::fromStdString(msd::fmt::count(row.dropped.value()));
		}
		QString extra;
		if (row.recentDropPct.has())
			extra = T("Tooltip.RecentDrops").arg(QString::fromStdString(msd::fmt::percent(row.recentDropPct.value())));
		if (row.countsSinceReset)
			extra += (extra.isEmpty() ? QString() : QStringLiteral("\n")) + T("Tooltip.SinceReset");
		setValue(dropped_, text, colored(row.dropHealth), extra, compact);
	} else {
		setNA(dropped_, row.dropped.reason());
	}

	// Congestion
	if (row.congestion.has())
		setValue(congestion_, QString::fromStdString(msd::fmt::percent(row.congestion.value() * 100.0, 0)),
			 colored(row.congestionHealth));
	else
		setNA(congestion_, row.congestion.reason());

	// Video
	if (row.width.has() && row.height.has()) {
		const QString resolution = QString::fromStdString(msd::fmt::resolution(row.width.value(), row.height.value()));
		QString text = resolution;
		QString compact = resolution; // "1280x720@30.00": resolution and frame rate only
		if (row.fps.has()) {
			const QString fps = QString::fromStdString(msd::fmt::fps(row.fps.value()));
			text += QStringLiteral(" · ") + fps + QStringLiteral(" fps");
			compact += QStringLiteral("@") + fps;
		}
		if (row.encoder.canvasIsMain)
			text += QStringLiteral(" · ") + T("Stat.MainCanvas");
		else if (!row.encoder.canvasName.empty())
			text += QStringLiteral(" · ") + T("Stat.Canvas").arg(qs(row.encoder.canvasName));
		setValue(video_, text, plain, QString(), compact);
	} else {
		setNA(video_, row.width.reason());
	}

	// Encoders (configured values, not measurements)
	if (row.encoder.present) {
		QString text = qs(row.encoder.displayName);
		if (row.encoder.targetKbps > 0)
			text += QStringLiteral(" ") + T("Stat.Target").arg(kbpsText(row.encoder.targetKbps));
		if (!row.encoder.audioDisplayName.empty()) {
			text += QStringLiteral(" · ") + qs(row.encoder.audioDisplayName);
			if (row.encoder.audioKbps > 0)
				text += QStringLiteral(" ") + kbpsText(row.encoder.audioKbps);
		}
		setValue(encoders_, text, plain,
			 row.encoder.dedicatedPipeline ? T("Tooltip.PipelineDedicated") : T("Tooltip.PipelineShared"),
			 qs(row.encoder.displayName));
	} else {
		setNA(encoders_, row.everSampled ? msd::Unavailable::Unknown : msd::Unavailable::NotActive);
	}

	// Encoding lag of a dedicated encoder pipeline only (never the global one).
	if (row.encoder.dedicatedPipeline) {
		if (row.pipelineSkipPct.has())
			setValue(pipeline_,
				 QString::fromStdString(msd::fmt::frames(row.encoder.pipelineSkipped,
									 row.encoder.pipelineTotal,
									 row.pipelineSkipPct.value())),
				 colored(row.pipelineHealth), QString(),
				 QString::fromStdString(
					 msd::fmt::framesCompact(row.encoder.pipelineSkipped, row.pipelineSkipPct.value())));
		else
			setNA(pipeline_, row.pipelineSkipPct.reason());
	}

	// Data sent
	if (row.bytes.has())
		setValue(data_, QString::fromStdString(msd::fmt::bytes(row.bytes.value())), plain);
	else
		setNA(data_, row.bytes.reason());

	// Connection
	QStringList parts;
	// Compact form keeps the key number: the handshake time, else the reconnect count.
	QString connectionCompact;
	if (row.connectTimeMs.has()) {
		parts << T("Stat.Handshake").arg(row.connectTimeMs.value());
		connectionCompact = T("Stat.HandshakeShort").arg(row.connectTimeMs.value());
	} else if (row.hasSession) {
		connectionCompact = T("Stat.Reconnects").arg(row.reconnects);
	}
	if (row.hasSession)
		parts << T("Stat.Reconnects").arg(row.reconnects);
	if (row.reconnects > 0 && row.lastConnectedAgoNs.has() && running)
		parts << T("Stat.LastConnected").arg(QString::fromStdString(msd::fmt::duration(row.lastConnectedAgoNs.value())));
	if (parts.isEmpty())
		setNA(connection_, row.connectTimeMs.reason());
	else
		setValue(connection_, parts.join(QStringLiteral(" · ")),
			 row.reconnects > 0 && running ? QStringLiteral("text-warning") : plain, QString(), connectionCompact);

	// Server (host only, never the stream key)
	if (hasServer_)
		setValue(server_, qs(row.ingestHost) + QStringLiteral(" · ") + protocolLabel(row.typeId), plain, QString(),
			 qs(row.ingestHost));

	if (hasDelay_)
		setValue(delay_, T("Stat.DelaySeconds").arg(row.delaySec), plain);

	if (hasError_)
		setValue(error_, qs(row.lastError), QStringLiteral("text-danger"));

	// Table cells: the same values as the details above. The state is always
	// shown; a destination without a session has no other values.
	using C = TableColumn;
	setValue(cell(C::Status), stateText(row), healthClass(msd::stateHealth(row.state)),
		 status == stateText(row) ? QString() : status);
	if (hasStats) {
		if (flowing && row.kbps.has())
			setValue(cell(C::Bitrate), kbpsText(row.kbps.value()));
		else
			mirrorValue(cell(C::Bitrate), bitrate_);
		mirrorValue(cell(C::Dropped), dropped_);
		mirrorValue(cell(C::Data), data_);
		mirrorValue(cell(C::Duration), duration_);
	} else {
		for (C c : {C::Bitrate, C::Dropped, C::Data, C::Duration})
			setValue(cell(c), QString());
	}
}

} // namespace msdui
