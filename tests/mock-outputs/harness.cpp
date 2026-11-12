// SPDX-License-Identifier: GPL-2.0-or-later
#include "harness.hpp"
#include "mock-output.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <obs-websocket-api.h>

#include <QAbstractButton>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QGridLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace msdmock {

namespace {

void hlog(const QString &msg)
{
	blog(LOG_INFO, "[msd-mock] %s", msg.toUtf8().constData());
}

QMainWindow *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

QDockWidget *findDock(const QString &name)
{
	QMainWindow *w = mainWindow();
	return w ? w->findChild<QDockWidget *>(name) : nullptr;
}

// Our labels are identified by class name and read through their msd*
// properties only, so this test module never links the plugin's code.
bool isMsdLabel(const QWidget *w)
{
	return qstrcmp(w->metaObject()->className(), "msdui::ElidedLabel") == 0;
}

QString kv(const QString &s)
{
	QString t = s;
	t.replace(QLatin1Char('"'), QLatin1Char('\''));
	return QStringLiteral("\"") + t + QStringLiteral("\"");
}

// "12 / 3456 (0.3%)" -> (12, 3456)
bool parseFrames(const QString &text, qint64 &part, qint64 &total)
{
	static const QRegularExpression re(QStringLiteral("^(\\d+) / (\\d+) \\("));
	const QRegularExpressionMatch m = re.match(text.trimmed());
	if (!m.hasMatch())
		return false;
	part = m.captured(1).toLongLong();
	total = m.captured(2).toLongLong();
	return true;
}

// Value label next to a caption in the native Stats dock's grid.
QLabel *nativeValue(QWidget *stats, const QString &caption)
{
	for (QLabel *l : stats->findChildren<QLabel *>()) {
		if (l->text() != caption)
			continue;
		auto *grid = qobject_cast<QGridLayout *>(l->parentWidget() ? l->parentWidget()->layout() : nullptr);
		QList<QGridLayout *> grids = l->parentWidget()->findChildren<QGridLayout *>();
		if (grid)
			grids.prepend(grid);
		for (QGridLayout *g : grids) {
			const int idx = g->indexOf(l);
			if (idx < 0)
				continue;
			int r, c, rs, cs;
			g->getItemPosition(idx, &r, &c, &rs, &cs);
			if (QLayoutItem *it = g->itemAtPosition(r, c + 1))
				return qobject_cast<QLabel *>(it->widget());
		}
	}
	return nullptr;
}

// Our value label for a caption: by object name (1.0.1+), else by the
// caption's accessible name and its grid neighbour (1.0.0 used QGridLayout).
QWidget *ourValue(QWidget *root, const QString &objectName, const QString &caption)
{
	if (QWidget *w = root->findChild<QWidget *>(objectName))
		return w;
	for (QLabel *l : root->findChildren<QLabel *>()) {
		if (l->accessibleName() != caption || !l->parentWidget())
			continue;
		for (QGridLayout *g : l->parentWidget()->findChildren<QGridLayout *>()) {
			const int idx = g->indexOf(l);
			if (idx < 0)
				continue;
			int r, c, rs, cs;
			g->getItemPosition(idx, &r, &c, &rs, &cs);
			if (QLayoutItem *it = g->itemAtPosition(r, c + 1))
				return it->widget();
		}
		if (auto *grid = qobject_cast<QGridLayout *>(l->parentWidget()->layout())) {
			const int idx = grid->indexOf(l);
			if (idx >= 0) {
				int r, c, rs, cs;
				grid->getItemPosition(idx, &r, &c, &rs, &cs);
				if (QLayoutItem *it = grid->itemAtPosition(r, c + 1))
					return it->widget();
			}
		}
	}
	return nullptr;
}

qint64 median(std::vector<qint64> v)
{
	if (v.empty())
		return -1;
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

} // namespace

Harness::Harness(QObject *parent) : QObject(parent) {}

Harness::~Harness()
{
	releaseAll();
}

bool Harness::load(const QString &scenarioPath, const QString &outDir)
{
	QFile f(scenarioPath);
	if (!f.open(QIODevice::ReadOnly)) {
		hlog(QStringLiteral("cannot open scenario %1").arg(scenarioPath));
		return false;
	}
	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	steps_ = doc.object().value(QStringLiteral("steps")).toArray();
	outDir_ = outDir;
	if (!outDir_.isEmpty())
		QDir().mkpath(outDir_);
	hlog(QStringLiteral("scenario loaded: %1 steps from %2").arg(steps_.size()).arg(scenarioPath));
	return true;
}

void Harness::begin()
{
	// Prime obs-websocket-api.h's (unsynchronised) proc-handler cache on the
	// UI thread before any worker thread uses it.
	obs_websocket_get_api_version();
	for (const QJsonValue &v : steps_) {
		const QJsonObject step = v.toObject();
		const int ms = (int)(step.value(QStringLiteral("at")).toDouble() * 1000.0);
		// PreciseTimer: the default coarse timer may fire long delays up to
		// 5% early/late, which reorders steps scheduled close together.
		QTimer::singleShot(ms, Qt::PreciseTimer, this, [this, step]() { runStep(step); });
	}
	hlog(QStringLiteral("timeline started"));
}

void Harness::runStep(const QJsonObject &step)
{
	if (released_)
		return;
	const QString action = step.value(QStringLiteral("action")).toString();
	const QString id = step.value(QStringLiteral("id")).toString();
	hlog(QStringLiteral("step %1 %2").arg(action, id));

	if (action == QLatin1String("create")) {
		createMock(step);
	} else if (action == QLatin1String("start")) {
		auto it = mocks_.find(id);
		if (it != mocks_.end())
			hlog(QStringLiteral("start %1 -> %2").arg(id).arg(obs_output_start(it->second.output) ? "ok" : "failed"));
	} else if (action == QLatin1String("stop")) {
		auto it = mocks_.find(id);
		if (it != mocks_.end())
			obs_output_stop(it->second.output);
	} else if (action == QLatin1String("release")) {
		releaseMock(id);
	} else if (action == QLatin1String("set")) {
		auto it = mocks_.find(id);
		if (it != mocks_.end()) {
			if (MockStream *m = mockOf(it->second.output)) {
				if (step.contains(QStringLiteral("drop_rate")))
					m->dropRate.store(step.value(QStringLiteral("drop_rate")).toDouble());
				if (step.contains(QStringLiteral("congestion")))
					m->congestion.store((float)step.value(QStringLiteral("congestion")).toDouble());
				if (step.contains(QStringLiteral("unreachable")))
					m->unreachable.store(step.value(QStringLiteral("unreachable")).toBool());
			}
		}
	} else if (action == QLatin1String("disconnect")) {
		auto it = mocks_.find(id);
		if (it != mocks_.end())
			simulateDisconnect(it->second.output);
	} else if (action == QLatin1String("churn")) {
		startChurn(step);
	} else if (action == QLatin1String("primary_start")) {
		obs_frontend_streaming_start();
	} else if (action == QLatin1String("primary_stop")) {
		obs_frontend_streaming_stop();
	} else if (action == QLatin1String("aitum_start")) {
		callAitum("start_output", step.value(QStringLiteral("name")).toString());
	} else if (action == QLatin1String("aitum_stop")) {
		callAitum("stop_output", step.value(QStringLiteral("name")).toString());
	} else if (action == QLatin1String("show_dock")) {
		showDock(step);
	} else if (action == QLatin1String("screenshot")) {
		screenshot(step);
	} else if (action == QLatin1String("probe_layout")) {
		probeLayout(step);
	} else if (action == QLatin1String("probe_dock")) {
		probeDock(step);
	} else if (action == QLatin1String("toggle_sections")) {
		toggleSections(step);
	} else if (action == QLatin1String("probe_lag")) {
		probeLag(step);
	} else if (action == QLatin1String("click_reset")) {
		// This dock's Reset button (statistics reset by the user).
		QDockWidget *dock = findDock(QStringLiteral("multistream-stats-dock"));
		int clicked = 0;
		if (dock && dock->widget()) {
			for (QAbstractButton *b : dock->widget()->findChildren<QAbstractButton *>()) {
				if (b->objectName() != QLatin1String("msd-disclosure") && b->isVisible()) {
					b->click();
					clicked++;
				}
			}
		}
		hlog(QStringLiteral("click_reset: %1 button(s)").arg(clicked));
	} else if (action == QLatin1String("reset_video")) {
		// Same parameters again: exercises the libobs "video_reset" signal.
		struct obs_video_info ovi = {};
		if (obs_get_video_info(&ovi)) {
			const int r = obs_reset_video(&ovi);
			hlog(QStringLiteral("reset_video -> %1").arg(r));
		}
	} else if (action == QLatin1String("checkpoint")) {
		hlog(QStringLiteral("CHECKPOINT %1").arg(step.value(QStringLiteral("name")).toString()));
	} else if (action == QLatin1String("quit")) {
		hlog(QStringLiteral("quit requested"));
		if (QMainWindow *w = mainWindow())
			QMetaObject::invokeMethod(w, "close", Qt::QueuedConnection);
	} else {
		hlog(QStringLiteral("unknown action '%1'").arg(action));
	}
}

void Harness::createMock(const QJsonObject &step)
{
	const QString id = step.value(QStringLiteral("id")).toString();
	if (mocks_.count(id))
		releaseMock(id);

	const QString type = step.value(QStringLiteral("type")).toString(QStringLiteral("full")) == QLatin1String("basic")
				     ? QStringLiteral("msd_mock_stream_basic")
				     : QStringLiteral("msd_mock_stream");
	const QString name = step.value(QStringLiteral("name")).toString(QStringLiteral("msd_mock_") + id);
	const int width = step.value(QStringLiteral("width")).toInt(640);
	const int height = step.value(QStringLiteral("height")).toInt(360);
	const int bitrate = step.value(QStringLiteral("bitrate")).toInt(1500);
	const bool gpuScale = step.value(QStringLiteral("gpu_scale")).toBool(true);

	Mock m;
	OBSDataAutoRelease svc = obs_data_create();
	obs_data_set_string(svc, "server", step.value(QStringLiteral("server")).toString(QStringLiteral("rtmp://127.0.0.1/mock")).toUtf8().constData());
	obs_data_set_string(svc, "key", "mock-not-a-real-key");
	m.service = obs_service_create("rtmp_custom", ("msd_mock_service_" + id).toUtf8().constData(), svc, nullptr);

	OBSDataAutoRelease vs = obs_data_create();
	obs_data_set_int(vs, "bitrate", bitrate);
	obs_data_set_string(vs, "rate_control", "CBR");
	obs_data_set_string(vs, "preset", "veryfast");
	obs_data_set_int(vs, "keyint_sec", 2);
	m.venc = obs_video_encoder_create("obs_x264", ("msd_mock_venc_" + id).toUtf8().constData(), vs, nullptr);
	obs_encoder_set_video(m.venc, obs_get_video());
	if (gpuScale) {
		obs_encoder_set_gpu_scale_type(m.venc, OBS_SCALE_BICUBIC);
		obs_encoder_set_scaled_size(m.venc, (uint32_t)width, (uint32_t)height);
	}

	OBSDataAutoRelease as = obs_data_create();
	obs_data_set_int(as, "bitrate", 128);
	m.aenc = obs_audio_encoder_create("ffmpeg_aac", ("msd_mock_aenc_" + id).toUtf8().constData(), as, 0, nullptr);
	obs_encoder_set_audio(m.aenc, obs_get_audio());

	m.output = obs_output_create(type.toUtf8().constData(), name.toUtf8().constData(), nullptr, nullptr);
	obs_output_set_service(m.output, m.service);
	obs_output_set_video_encoder(m.output, m.venc);
	obs_output_set_audio_encoder(m.output, m.aenc, 0);
	obs_output_set_reconnect_settings(m.output, step.value(QStringLiteral("retries")).toInt(3),
					  step.value(QStringLiteral("retry_delay")).toInt(1));
	mocks_[id] = m;
	hlog(QStringLiteral("created %1 type=%2 name=%3").arg(id, type, name));
}

void Harness::releaseMock(const QString &id)
{
	auto it = mocks_.find(id);
	if (it == mocks_.end())
		return;
	Mock &m = it->second;
	// Wait for an in-flight simulated connect so it cannot begin data
	// capture on an output that is being destroyed (mock-only race).
	prepareRelease(m.output);
	obs_output_release(m.output); // like Aitum: release the output first
	obs_encoder_release(m.venc);
	obs_encoder_release(m.aenc);
	obs_service_release(m.service);
	mocks_.erase(it);
	hlog(QStringLiteral("released %1").arg(id));
}

void Harness::startChurn(const QJsonObject &step)
{
	churnRemaining_ = step.value(QStringLiteral("count")).toInt(50);
	const int interval = step.value(QStringLiteral("interval_ms")).toInt(120);
	auto *timer = new QTimer(this);
	timer->setInterval(interval);
	connect(timer, &QTimer::timeout, this, [this, timer]() {
		const QString prevId = QStringLiteral("churn%1").arg(churnIndex_ - 1);
		if (mocks_.count(prevId)) {
			obs_output_stop(mocks_[prevId].output);
			releaseMock(prevId);
		}
		if (churnRemaining_-- <= 0 || released_) {
			timer->stop();
			timer->deleteLater();
			hlog(QStringLiteral("churn finished"));
			return;
		}
		QJsonObject c;
		c.insert(QStringLiteral("id"), QStringLiteral("churn%1").arg(churnIndex_));
		c.insert(QStringLiteral("type"), QStringLiteral("basic"));
		c.insert(QStringLiteral("width"), 320);
		c.insert(QStringLiteral("height"), 180);
		c.insert(QStringLiteral("bitrate"), 300);
		createMock(c);
		obs_output_start(mocks_[QStringLiteral("churn%1").arg(churnIndex_)].output);
		churnIndex_++;
	});
	timer->start();
}

void Harness::callAitum(const char *request, const QString &name)
{
	// Aitum's start_output/stop_output handlers marshal to the UI thread with
	// Qt::BlockingQueuedConnection (they expect to be called from a websocket
	// thread), so they must not be called from the UI thread. Issue them from
	// a worker thread exactly like a remote obs-websocket client would.
	const std::string req = request;
	const std::string dest = name.toStdString();
	workers_.emplace_back([req, dest]() {
		OBSDataAutoRelease r = obs_data_create();
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_string(data, "name", dest.c_str());
		obs_data_set_string(r, "vendorName", "aitum-multistream");
		obs_data_set_string(r, "requestType", req.c_str());
		obs_data_set_obj(r, "requestData", data);
		obs_websocket_request_response *resp = obs_websocket_call_request("CallVendorRequest", r);
		if (!resp) {
			blog(LOG_INFO, "[msd-mock] aitum %s '%s': obs-websocket unavailable", req.c_str(), dest.c_str());
			return;
		}
		blog(LOG_INFO, "[msd-mock] aitum %s '%s': status %u %s", req.c_str(), dest.c_str(), resp->status_code,
		     resp->response_data ? resp->response_data : "");
		obs_websocket_request_response_free(resp);
	});
}

void Harness::showDock(const QJsonObject &step)
{
	const QString name = step.value(QStringLiteral("dock")).toString(QStringLiteral("multistream-stats-dock"));
	QDockWidget *dock = findDock(name);
	if (!dock) {
		hlog(QStringLiteral("dock '%1' not found").arg(name));
		return;
	}
	dock->setVisible(true);
	const bool floating = step.value(QStringLiteral("floating")).toBool(true);
	const int w = step.value(QStringLiteral("width")).toInt(0);
	const int h = step.value(QStringLiteral("height")).toInt(0);
	if (floating) {
		dock->setFloating(true);
		if (w > 0 && h > 0)
			dock->resize(w, h);
	} else {
		dock->setFloating(false);
		if (QMainWindow *mw = mainWindow()) {
			// "area": "bottom" docks it below the preview, alone in its row
			// (wide and short); otherwise the right area as before.
			const QString area = step.value(QStringLiteral("area")).toString();
			if (area == QLatin1String("bottom")) {
				mw->addDockWidget(Qt::BottomDockWidgetArea, dock, Qt::Vertical);
				if (h > 0)
					mw->resizeDocks({dock}, {h}, Qt::Vertical);
			} else if (area == QLatin1String("right")) {
				// Explicit: the test copy saves its dock layout on exit, so
				// an earlier scenario may have left the dock elsewhere.
				mw->addDockWidget(Qt::RightDockWidgetArea, dock);
				if (w > 0)
					mw->resizeDocks({dock}, {w}, Qt::Horizontal);
			} else {
				if (mw->dockWidgetArea(dock) == Qt::NoDockWidgetArea)
					mw->addDockWidget(Qt::RightDockWidgetArea, dock);
				if (w > 0)
					mw->resizeDocks({dock}, {w}, Qt::Horizontal);
			}
		}
	}
	if (step.contains(QStringLiteral("x")))
		dock->move(step.value(QStringLiteral("x")).toInt(), step.value(QStringLiteral("y")).toInt());
	dock->raise();
	hlog(QStringLiteral("dock '%1' shown %2x%3 floating=%4")
		     .arg(name)
		     .arg(dock->width())
		     .arg(dock->height())
		     .arg(dock->isFloating()));
}

void Harness::screenshot(const QJsonObject &step)
{
	if (outDir_.isEmpty())
		return;
	const QString name = step.value(QStringLiteral("dock")).toString(QStringLiteral("multistream-stats-dock"));
	QWidget *target = name == QLatin1String("main") ? static_cast<QWidget *>(mainWindow()) : findDock(name);
	if (!target) {
		hlog(QStringLiteral("screenshot: '%1' not found").arg(name));
		return;
	}
	const QString file = QDir(outDir_).filePath(step.value(QStringLiteral("file")).toString(QStringLiteral("shot.png")));
	const bool ok = target->grab().save(file, "PNG");
	hlog(QStringLiteral("screenshot %1 -> %2 (%3)").arg(name, file, ok ? "ok" : "failed"));
}

void Harness::probeDock(const QJsonObject &step)
{
	const QString name = step.value(QStringLiteral("name")).toString();
	QDockWidget *dock = findDock(step.value(QStringLiteral("dock")).toString(QStringLiteral("multistream-stats-dock")));
	if (!dock) {
		hlog(QStringLiteral("PROBE dock name=%1 found=0").arg(name));
		return;
	}
	hlog(QStringLiteral("PROBE dock name=%1 found=1 width=%2 height=%3 floating=%4 visible=%5 content_width=%6")
		     .arg(name)
		     .arg(dock->width())
		     .arg(dock->height())
		     .arg(dock->isFloating() ? 1 : 0)
		     .arg(dock->isVisible() ? 1 : 0)
		     .arg(dock->widget() ? dock->widget()->width() : -1));
}

void Harness::toggleSections(const QJsonObject &step)
{
	const bool expand = step.value(QStringLiteral("expanded")).toBool(true);
	QDockWidget *dock = findDock(QStringLiteral("multistream-stats-dock"));
	if (!dock || !dock->widget())
		return;
	int toggled = 0;
	for (QAbstractButton *b : dock->widget()->findChildren<QAbstractButton *>(QStringLiteral("msd-disclosure"))) {
		if (b->isVisible() && b->isChecked() != expand) {
			b->click();
			toggled++;
		}
	}
	hlog(QStringLiteral("sections %1: %2 toggled").arg(expand ? "expanded" : "collapsed").arg(toggled));
}

void Harness::probeLayout(const QJsonObject &step)
{
	const QString name = step.value(QStringLiteral("name")).toString();
	QDockWidget *dock = findDock(QStringLiteral("multistream-stats-dock"));
	QWidget *root = dock ? dock->widget() : nullptr;
	if (!root || !root->isVisible()) {
		hlog(QStringLiteral("PROBE layout name=%1 found=0").arg(name));
		return;
	}
	auto *scroll = root->findChild<QScrollArea *>();
	QWidget *viewport = scroll ? scroll->viewport() : root;
	const bool hscroll = scroll && scroll->horizontalScrollBar()->isVisible();

	int values = 0, valuesClipped = 0, valuesElided = 0, valuesOffscreen = 0, valuesCompact = 0;
	int labels = 0, labelsShortened = 0, texts = 0, textsShortened = 0, tipsMissing = 0;
	QStringList problems;
	struct Placed {
		QRect rect;
		QString text;
	};
	std::vector<Placed> placed; // for the overlap check (1.1.0)
	for (QWidget *w : root->findChildren<QWidget *>()) {
		if (!isMsdLabel(w) || !w->isVisible())
			continue;
		const QString full = w->property("msdFullText").toString();
		if (full.isEmpty())
			continue;
		placed.push_back({QRect(w->mapTo(root, QPoint(0, 0)), w->size()), full});
		const int role = w->property("msdRole").toInt();
		const int mode = w->property("msdMode").toInt();
		const QString shown = w->property("msdShownText").toString();
		const bool fits = w->fontMetrics().horizontalAdvance(shown) <= w->contentsRect().width();
		const QRect vr(w->mapTo(viewport, QPoint(0, 0)), w->size());
		const bool inside = vr.left() >= 0 && vr.left() + vr.width() <= viewport->width();
		// Anything shown shorter than its full text must carry the full text in its tooltip.
		const bool tipOk = mode == 0 || w->toolTip().contains(full);
		if (!tipOk)
			tipsMissing++;
		if (role == 1) {
			values++;
			valuesCompact += mode == 1;
			valuesElided += mode == 2;
			valuesClipped += !fits;
			valuesOffscreen += !inside;
			if (mode == 2 || !fits || !inside)
				problems << QStringLiteral("value full=%1 shown=%2 mode=%3 fits=%4 inside=%5")
						    .arg(kv(full), kv(shown))
						    .arg(mode)
						    .arg(fits ? 1 : 0)
						    .arg(inside ? 1 : 0);
		} else if (role == 0) {
			labels++;
			labelsShortened += mode != 0;
		} else {
			texts++;
			textsShortened += mode != 0;
		}
		if (!tipOk)
			problems << QStringLiteral("tooltip role=%1 full=%2").arg(role).arg(kv(full));
	}
	// Two visible labels with text must never share pixels.
	int overlaps = 0;
	for (size_t i = 0; i < placed.size(); ++i) {
		for (size_t j = i + 1; j < placed.size(); ++j) {
			if (!placed[i].rect.intersects(placed[j].rect))
				continue;
			overlaps++;
			problems << QStringLiteral("overlap a=%1 b=%2").arg(kv(placed[i].text), kv(placed[j].text));
		}
	}
	int sections = 0, expanded = 0;
	for (QAbstractButton *b : root->findChildren<QAbstractButton *>(QStringLiteral("msd-disclosure"))) {
		if (!b->isVisible())
			continue;
		sections++;
		expanded += b->isChecked();
	}
	auto prop = [root](const char *name) {
		const QVariant v = root->property(name);
		return v.isValid() ? QString::number(v.toInt()) : QStringLiteral("na");
	};
	// 1.1.0 fields: layout mode (0 stacked, 1 table), table columns, overview
	// side by side, wide shape, overlapping labels, vertical scroll bar.
	const QString extra = QStringLiteral(" dock_h=%1 viewport_h=%2 mode=%3 table_columns=%4 side_by_side=%5 wide=%6 "
					     "overlaps=%7 vscroll=%8")
				      .arg(dock->height())
				      .arg(viewport->height())
				      .arg(prop("msdLayoutMode"), prop("msdTableColumns"), prop("msdOverviewSideBySide"),
					   prop("msdWide"))
				      .arg(overlaps)
				      .arg(scroll && scroll->verticalScrollBar()->isVisible() ? 1 : 0);
	hlog(QStringLiteral("PROBE layout name=%1 found=1 dock_w=%2 viewport_w=%3 hscroll=%4 values=%5 values_clipped=%6 "
			    "values_elided=%7 values_offscreen=%8 values_compact=%9 labels=%10 labels_shortened=%11 "
			    "texts=%12 texts_shortened=%13 tips_missing=%14 sections=%15 sections_expanded=%16")
		     .arg(name)
		     .arg(dock->width())
		     .arg(viewport->width())
		     .arg(hscroll ? 1 : 0)
		     .arg(values)
		     .arg(valuesClipped)
		     .arg(valuesElided)
		     .arg(valuesOffscreen)
		     .arg(valuesCompact)
		     .arg(labels)
		     .arg(labelsShortened)
		     .arg(texts)
		     .arg(textsShortened)
		     .arg(tipsMissing)
		     .arg(sections)
		     .arg(expanded) +
	     extra);
	for (const QString &p : problems)
		hlog(QStringLiteral("PROBE layout-problem name=%1 %2").arg(name, p));
}

void Harness::probeLag(const QJsonObject &step)
{
	// Compares the counting windows of the native Stats dock and ours using
	// only what each dock DISPLAYS: the moment a dock's "a / b (p%)" text
	// changes (it just updated), libobs's counters are read; the window start
	// it implies is counter_now - displayed_total (error <= ~1 frame).
	const QString name = step.value(QStringLiteral("name")).toString();
	const int durationMs = (int)(step.value(QStringLiteral("duration")).toDouble(8.0) * 1000.0);
	const QString missedCaption =
		step.value(QStringLiteral("native_missed_caption")).toString(QStringLiteral("Frames missed due to rendering lag"));
	const QString skippedCaption =
		step.value(QStringLiteral("native_skipped_caption")).toString(QStringLiteral("Skipped frames due to encoding lag"));

	QDockWidget *statsDock = findDock(QStringLiteral("statsDock"));
	QDockWidget *ours = findDock(QStringLiteral("multistream-stats-dock"));
	QWidget *native = statsDock ? statsDock->widget() : nullptr;
	QPointer<QLabel> nMissed = native ? nativeValue(native, missedCaption) : nullptr;
	QPointer<QLabel> nSkipped = native ? nativeValue(native, skippedCaption) : nullptr;
	// Our captions are the same English strings as the native dock's.
	QPointer<QWidget> oMissed = ours && ours->widget() ? ourValue(ours->widget(), QStringLiteral("msd-perf-render-lag"), missedCaption) : nullptr;
	QPointer<QWidget> oSkipped = ours && ours->widget() ? ourValue(ours->widget(), QStringLiteral("msd-perf-encode-lag"), skippedCaption) : nullptr;
	if (!nMissed || !nSkipped || !oMissed || !oSkipped) {
		hlog(QStringLiteral("PROBE lag name=%1 ok=0 native_missed=%2 native_skipped=%3 ours_missed=%4 ours_skipped=%5")
			     .arg(name)
			     .arg(!nMissed.isNull())
			     .arg(!nSkipped.isNull())
			     .arg(!oMissed.isNull())
			     .arg(!oSkipped.isNull()));
		return;
	}

	struct Side {
		QString last;
		bool primed = false;
		std::vector<qint64> firstTotal, firstAffected;
		QString text;
	};
	struct State {
		Side nRender, oRender, nEncode, oEncode;
		qint64 elapsedStart = 0;
	};
	auto st = std::make_shared<State>();
	auto observe = [](Side &s, const QString &text, uint32_t total, uint32_t affected) {
		if (text == s.last)
			return;
		s.last = text;
		if (!s.primed) { // the first value seen may be stale; start at the next update
			s.primed = true;
			return;
		}
		qint64 a = 0, t = 0;
		if (!parseFrames(text, a, t))
			return;
		s.firstTotal.push_back((qint64)total - t);
		s.firstAffected.push_back((qint64)affected - a);
		s.text = text;
	};

	auto *timer = new QTimer(this);
	timer->setTimerType(Qt::PreciseTimer);
	timer->setInterval(4);
	auto *clock = new QElapsedTimer();
	clock->start();
	connect(timer, &QTimer::timeout, this, [=]() {
		if (released_ || !nMissed || !nSkipped || !oMissed || !oSkipped) {
			timer->stop();
			timer->deleteLater();
			delete clock;
			return;
		}
		const uint32_t total = obs_get_total_frames();
		const uint32_t lagged = obs_get_lagged_frames();
		video_t *video = obs_get_video();
		const uint32_t encoded = video ? video_output_get_total_frames(video) : 0;
		const uint32_t skipped = video ? video_output_get_skipped_frames(video) : 0;
		observe(st->nRender, nMissed->text(), total, lagged);
		// accessibleName carries the full value text in 1.0.0 and 1.0.1.
		observe(st->oRender, oMissed->accessibleName(), total, lagged);
		observe(st->nEncode, nSkipped->text(), encoded, skipped);
		observe(st->oEncode, oSkipped->accessibleName(), encoded, skipped);
		if (clock->elapsed() < durationMs)
			return;
		timer->stop();
		timer->deleteLater();
		delete clock;

		auto med = [](const std::vector<qint64> &v) { return median(v); };
		auto diff = [&](const Side &a, const Side &b, bool affected) -> QString {
			if (a.firstTotal.empty() || b.firstTotal.empty())
				return QStringLiteral("na");
			const qint64 x = affected ? med(a.firstAffected) : med(a.firstTotal);
			const qint64 y = affected ? med(b.firstAffected) : med(b.firstTotal);
			return QString::number(std::llabs(x - y));
		};
		auto first = [&](const Side &s, bool affected) -> QString {
			if (s.firstTotal.empty())
				return QStringLiteral("na");
			return QString::number(affected ? med(s.firstAffected) : med(s.firstTotal));
		};
		hlog(QStringLiteral("PROBE lag name=%1 ok=1 native_samples=%2 ours_samples=%3 native_first_total=%4 "
				    "ours_first_total=%5 diff_total=%6 native_first_lagged=%7 ours_first_lagged=%8 diff_lagged=%9 "
				    "enc_native_samples=%10 enc_ours_samples=%11 enc_diff_total=%12 enc_diff_skipped=%13 "
				    "lagged_now=%14 native_text=%15 ours_text=%16 enc_native_text=%17 enc_ours_text=%18")
			     .arg(name)
			     .arg(st->nRender.firstTotal.size())
			     .arg(st->oRender.firstTotal.size())
			     .arg(first(st->nRender, false), first(st->oRender, false), diff(st->nRender, st->oRender, false),
				  first(st->nRender, true), first(st->oRender, true), diff(st->nRender, st->oRender, true))
			     .arg(st->nEncode.firstTotal.size())
			     .arg(st->oEncode.firstTotal.size())
			     .arg(diff(st->nEncode, st->oEncode, false), diff(st->nEncode, st->oEncode, true))
			     .arg(lagged)
			     .arg(kv(st->nRender.text), kv(st->oRender.text), kv(st->nEncode.text), kv(st->oEncode.text)));
	});
	timer->start();
	hlog(QStringLiteral("lag probe '%1' running for %2 ms").arg(name).arg(durationMs));
}

void Harness::releaseAll()
{
	if (released_)
		return;
	released_ = true;
	for (std::thread &t : workers_)
		if (t.joinable())
			t.join();
	workers_.clear();
	std::vector<QString> ids;
	for (auto &[id, m] : mocks_)
		ids.push_back(id);
	for (const QString &id : ids)
		releaseMock(id);
}

} // namespace msdmock
