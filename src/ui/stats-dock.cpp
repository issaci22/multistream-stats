// SPDX-License-Identifier: GPL-2.0-or-later
#include "stats-dock.hpp"
#include "theme.hpp"
#include "value-label.hpp"

#include <core/format.hpp>

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>

#include <QContextMenuEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using msd::Health;
using msd::Unavailable;

namespace msdui {

namespace {

constexpr const char *kStateFile = "dock-state.json";
// Performance grid: two label/value pairs side by side when there is room,
// like the native Stats dock's top grid; three next to the summary in wide,
// short docks so the output table below stays in view.
constexpr int kPerfColumns = 2;
constexpr int kPerfColumnsSideBySide = 3;

QFrame *makeSeparator(QWidget *parent)
{
	auto *line = new QFrame(parent);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

QString str(const std::string &s)
{
	return QString::fromStdString(s);
}

ElidedLabel *makeHeading(QWidget *parent, const QString &text, const QString &tip = QString())
{
	auto *l = new ElidedLabel(parent, ElidedLabel::Role::Label);
	l->setFullText(text);
	l->setStyleSheet(QStringLiteral("font-weight: bold;"));
	if (!tip.isEmpty())
		l->setBaseToolTip(tip);
	return l;
}

} // namespace

StatsDock::StatsDock(msd::StatsCollector *collector, QWidget *parent) : QWidget(parent), collector_(collector)
{
	setObjectName(QStringLiteral("MultiStreamStatsDock"));
	setMinimumWidth(120);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	scroll_ = new QScrollArea(this);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setWidgetResizable(true);
	// Labels shrink first and values switch to compact forms, so a horizontal
	// scroll bar only appears below the content's real minimum width (far
	// narrower than any normal dock) instead of values being cut off.
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scroll_->viewport()->installEventFilter(this);
	outer->addWidget(scroll_, 1);

	auto *content = new QWidget(scroll_);
	contentLayout_ = new QGridLayout(content);
	scroll_->setWidget(content);

	// --- Summary -------------------------------------------------------
	summaryBlock_ = new QWidget(content);
	auto *summaryLayout = new QVBoxLayout(summaryBlock_);
	summaryLayout->setContentsMargins(0, 0, 0, 0);
	auto *healthRow = new QWidget(summaryBlock_);
	auto *healthLayout = new QHBoxLayout(healthRow);
	healthLayout->setContentsMargins(0, 0, 0, 0);
	healthIndicator_ = new StatusIndicator(healthRow);
	healthText_ = new ElidedLabel(healthRow, ElidedLabel::Role::Text);
	healthText_->setStyleSheet(QStringLiteral("font-weight: bold;"));
	healthText_->setProperty(kTipProperty, T("Tooltip.Health"));
	healthLayout->addWidget(healthIndicator_);
	healthLayout->addWidget(healthText_, 1);
	summaryLayout->addWidget(healthRow);

	summaryGrid_ = new StatGrid(summaryBlock_);
	outputsValue_ = summaryGrid_->addRow(T("Summary.Outputs"), T("Summary.Outputs.Tooltip"));
	bitrateValue_ = summaryGrid_->addRow(T("Summary.TotalBitrate"), T("Summary.TotalBitrate.Tooltip"));
	droppedValue_ = summaryGrid_->addRow(T("Summary.Dropped"), T("Summary.Dropped.Tooltip"));
	// Reason of the most recent failure of an output that left the list
	// (descriptive text: may shorten, full text in the tooltip).
	lastFailureValue_ = summaryGrid_->addRow(T("Summary.LastFailure"), T("Summary.LastFailure.Tooltip"),
						 ElidedLabel::Role::Text);
	summaryGrid_->setRowVisible(lastFailureValue_, false);
	summaryLayout->addWidget(summaryGrid_);
	summaryLayout->addStretch(1);

	sepTop_ = makeSeparator(content);

	// --- Output sections -----------------------------------------------
	sectionsBlock_ = new QWidget(content);
	sectionsLayout_ = new QVBoxLayout(sectionsBlock_);
	sectionsLayout_->setContentsMargins(0, 0, 0, 0);

	// Column headers of the output table (bold, like the native Stats dock).
	tableHeader_ = new QWidget(sectionsBlock_);
	auto *th = new QHBoxLayout(tableHeader_);
	th->setContentsMargins(0, 0, 0, 0);
	tableLead_ = new QWidget(tableHeader_);
	tableLead_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	tableName_ = makeHeading(tableHeader_, T("Table.Output"));
	th->addWidget(tableLead_);
	th->addWidget(tableName_);
	const char *heads[kTableColumns] = {"Stat.Status", "Stat.Bitrate", "Stat.Dropped", "Stat.DataSent", "Stat.Duration"};
	const char *tips[kTableColumns] = {"Tooltip.Status", "Tooltip.Bitrate", "Tooltip.Dropped", "Tooltip.DataSent",
					   "Tooltip.Duration"};
	for (int c = 0; c < kTableColumns; ++c) {
		tableCells_[(size_t)c] = makeHeading(tableHeader_, T(heads[c]), T(tips[c]));
		th->addWidget(tableCells_[(size_t)c]);
	}
	th->addStretch(1);
	tableHeader_->hide();
	sectionsLayout_->addWidget(tableHeader_);

	sepBottom_ = makeSeparator(content);

	// --- OBS Performance -------------------------------------------------
	perfBlock_ = new QWidget(content);
	auto *perfLayout = new QVBoxLayout(perfBlock_);
	perfLayout->setContentsMargins(0, 0, 0, 0);
	auto *perfHeader = new QWidget(perfBlock_);
	auto *perfHeaderLayout = new QHBoxLayout(perfHeader);
	perfHeaderLayout->setContentsMargins(0, 0, 0, 0);
	perfArrow_ = new DisclosureArrow(perfHeader);
	perfArrow_->setToolTip(T("Section.Toggle"));
	auto *perfTitle = new ElidedLabel(perfHeader);
	perfTitle->setFullText(T("Perf.Title"));
	perfTitle->setStyleSheet(QStringLiteral("font-weight: bold;"));
	perfTitle->setBaseToolTip(T("Perf.Tooltip"));
	perfHeaderLayout->addWidget(perfArrow_);
	perfHeaderLayout->addWidget(perfTitle, 1);
	perfLayout->addWidget(perfHeader);
	connect(perfArrow_, &QAbstractButton::clicked, this, [this]() {
		setPerfExpanded(!perfExpanded_);
		scheduleSave();
	});

	perfBody_ = new QWidget(perfBlock_);
	auto *perfBodyLayout = new QVBoxLayout(perfBody_);
	perfBodyLayout->setContentsMargins(perfArrow_->sizeHint().width(), 0, 0, 0);
	perfGrid_ = new StatGrid(perfBody_);
	perfGrid_->setMaxColumns(kPerfColumns);
	cpu_ = perfGrid_->addRow(T("Perf.CPU"), T("Perf.CPU.Tooltip"));
	memory_ = perfGrid_->addRow(T("Perf.Memory"), T("Perf.Memory.Tooltip"));
	disk_ = perfGrid_->addRow(T("Perf.Disk"), T("Perf.Disk.Tooltip"));
	fps_ = perfGrid_->addRow(T("Perf.FPS"), T("Perf.FPS.Tooltip"));
	renderTime_ = perfGrid_->addRow(T("Perf.RenderTime"), T("Perf.RenderTime.Tooltip"));
	renderLag_ = perfGrid_->addRow(T("Perf.RenderLag"), T("Perf.RenderLag.Tooltip"));
	encodeLag_ = perfGrid_->addRow(T("Perf.EncodeLag"), T("Perf.EncodeLag.Tooltip"));
	renderLag_->setObjectName(QStringLiteral("msd-perf-render-lag"));
	encodeLag_->setObjectName(QStringLiteral("msd-perf-encode-lag"));
	perfBodyLayout->addWidget(perfGrid_);
	perfLayout->addWidget(perfBody_);
	perfLayout->addStretch(1);

	placeBlocks();

	// --- Bottom bar (native Stats dock style) --------------------------
	auto *buttons = new QHBoxLayout();
	buttons->setContentsMargins(4, 4, 4, 4);
	buttons->addStretch(1);
	resetButton_ = new QPushButton(T("Button.Reset"), this);
	resetButton_->setToolTip(T("Button.Reset.Tooltip"));
	connect(resetButton_, &QPushButton::clicked, this, [this]() {
		if (collector_)
			collector_->resetStatistics();
	});
	buttons->addWidget(resetButton_);
	outer->addLayout(buttons);

	// Placeholder values until the first snapshot arrives.
	for (ElidedLabel *l : {outputsValue_, bitrateValue_, droppedValue_, cpu_, memory_, disk_, fps_, renderTime_,
			       renderLag_, encodeLag_})
		setNA(l, Unavailable::NotMeasured);
	healthIndicator_->setHealth(Health::Inactive);
	setValue(healthText_, T("Health.Idle"), QStringLiteral("text-muted"));

	saveTimer_.setSingleShot(true);
	saveTimer_.setInterval(2000);
	connect(&saveTimer_, &QTimer::timeout, this, &StatsDock::saveState);

	loadState();
	setPerfExpanded(perfExpanded_);

	if (collector_) {
		connect(collector_, &msd::StatsCollector::snapshotReady, this, &StatsDock::onSnapshot);
		if (collector_->lastSnapshot().timestampNs != 0)
			onSnapshot(collector_->lastSnapshot());
	}
}

StatsDock::~StatsDock()
{
	if (dirty_)
		saveState();
}

QSize StatsDock::sizeHint() const
{
	const QSize h = QWidget::sizeHint();
	return {std::max(h.width(), kDefaultWidth), h.height()};
}

// ---------------------------------------------------------------------------
bool StatsDock::event(QEvent *event)
{
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::Polish) {
		contentHMargins_ = -1; // theme changed: re-read style metrics
		scrollBarExtent_ = -1;
	}
	return QWidget::event(event);
}

bool StatsDock::eventFilter(QObject *watched, QEvent *event)
{
	if (scroll_ && watched == scroll_->viewport() && event->type() == QEvent::Resize)
		updateResponsiveLayout();
	return QWidget::eventFilter(watched, event);
}

void StatsDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (collector_)
		collector_->setDisplayActive(true);
	updateResponsiveLayout();
}

void StatsDock::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	if (collector_)
		collector_->setDisplayActive(false);
}

void StatsDock::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);
	menu.addAction(T("Menu.ExpandAll"), this, [this]() { setAllExpanded(true); });
	menu.addAction(T("Menu.CollapseAll"), this, [this]() { setAllExpanded(false); });
	menu.addSeparator();
	QAction *reset = menu.addAction(T("Menu.Reset"), this, [this]() {
		if (collector_)
			collector_->resetStatistics();
	});
	QAction *clear = menu.addAction(T("Menu.ClearEnded"), this, [this]() {
		if (collector_)
			collector_->clearEnded();
	});
	reset->setEnabled(!collector_.isNull());
	clear->setEnabled(!collector_.isNull());
	menu.exec(event->globalPos());
}

// ---------------------------------------------------------------------------
// Responsive layout
// ---------------------------------------------------------------------------
int StatsDock::contentHMargins() const
{
	if (contentHMargins_ < 0) { // cached: default margins come from the (style-sheet) style
		const QMargins m = contentLayout_->contentsMargins();
		contentHMargins_ = std::max(0, m.left() + m.right());
	}
	return contentHMargins_;
}

int StatsDock::scrollBarExtent() const
{
	if (scrollBarExtent_ < 0)
		scrollBarExtent_ = std::max(8, style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, scroll_));
	return scrollBarExtent_;
}

void StatsDock::placeBlocks()
{
	for (QWidget *w : std::initializer_list<QWidget *>{summaryBlock_, perfBlock_, sepTop_, sectionsBlock_, sepBottom_})
		contentLayout_->removeWidget(w);
	if (sideBySide_) {
		// Native Stats dock arrangement: overview on top, outputs below.
		contentLayout_->addWidget(summaryBlock_, 0, 0);
		contentLayout_->addWidget(perfBlock_, 0, 1);
		contentLayout_->addWidget(sepTop_, 1, 0, 1, 2);
		contentLayout_->addWidget(sectionsBlock_, 2, 0, 1, 2);
		contentLayout_->addWidget(sepBottom_, 3, 0, 1, 2);
		sepBottom_->hide();
		// The summary keeps its natural width; performance takes the rest.
		contentLayout_->setColumnStretch(0, 0);
		contentLayout_->setColumnStretch(1, 1);
		perfGrid_->setMaxColumns(kPerfColumnsSideBySide);
	} else {
		contentLayout_->addWidget(summaryBlock_, 0, 0, 1, 2);
		contentLayout_->addWidget(sepTop_, 1, 0, 1, 2);
		contentLayout_->addWidget(sectionsBlock_, 2, 0, 1, 2);
		contentLayout_->addWidget(sepBottom_, 3, 0, 1, 2);
		contentLayout_->addWidget(perfBlock_, 4, 0, 1, 2);
		sepBottom_->show();
		contentLayout_->setColumnStretch(0, 1);
		contentLayout_->setColumnStretch(1, 1);
		perfGrid_->setMaxColumns(kPerfColumns);
	}
	for (int r = 0; r < 5; ++r)
		contentLayout_->setRowStretch(r, 0);
	contentLayout_->setRowStretch(5, 1);
}

void StatsDock::updateResponsiveLayout()
{
	if (inLayoutUpdate_ || !scroll_ || !isVisible())
		return;
	inLayoutUpdate_ = true;

	const QSize vp = scroll_->viewport()->size();
	const int margin = scrollBarExtent();
	const int width = vp.width() - contentHMargins();
	wide_ = msd::layout::isWide(vp.width(), vp.height(), wide_, margin);

	// Overview: Summary | OBS Performance side by side when both fit at
	// their natural width (the performance grid then gets its 2 columns).
	const int gap = fontMetrics().height() * 2;
	const int need = summaryBlock_->sizeHint().width() + gap + perfBlock_->sizeHint().width();
	const bool sideBySide = wide_ && msd::layout::threshold(sideBySide_, width, need, margin);
	if (sideBySide != sideBySide_) {
		sideBySide_ = sideBySide;
		placeBlocks();
	}

	applyTableGeometry(computeTableGeometry(width, margin));
	inLayoutUpdate_ = false;
}

TableGeometry StatsDock::computeTableGeometry(int width, int margin)
{
	TableGeometry g;
	if (!wide_ || sections_.empty()) {
		tablePlan_ = {};
		return g;
	}
	const OutputSection *any = sections_.begin()->second;
	const QFontMetrics fm = fontMetrics();

	msd::layout::TableInput in;
	in.width = width;
	in.lead = any->leadWidth();
	in.spacing = any->headerSpacing();
	in.nameNatural = tableName_->sizeHint().width();
	in.cells.assign(kTableColumns, 0);
	for (int c = 0; c < kTableColumns; ++c)
		in.cells[(size_t)c] = tableCells_[(size_t)c]->sizeHint().width();
	for (const auto &[key, section] : sections_) {
		in.nameNatural = std::max(in.nameNatural, section->nameNaturalWidth());
		for (int c = 0; c < kTableColumns; ++c)
			in.cells[(size_t)c] = std::max(in.cells[(size_t)c], section->cellNaturalWidth((TableColumn)c));
	}
	in.nameMin = fm.averageCharWidth() * 10 + fm.horizontalAdvance(QStringLiteral("…"));
	in.requiredCells = kRequiredTableColumns;
	in.maxPad = fm.height() * 2;

	tablePlan_ = msd::layout::planTable(in, tablePlan_, margin);
	if (!tablePlan_.table)
		return g;
	g.enabled = true;
	g.columns = tablePlan_.columns;
	g.nameWidth = tablePlan_.nameWidth;
	for (int c = 0; c < kTableColumns; ++c)
		g.widths[(size_t)c] = in.cells[(size_t)c] + tablePlan_.pad;
	return g;
}

void StatsDock::applyTableGeometry(const TableGeometry &g)
{
	if (g == table_)
		return;
	const bool modeChanged = g.enabled != table_.enabled;
	table_ = g;
	for (auto &[key, section] : sections_) {
		section->setTableGeometry(g);
		if (modeChanged)
			section->setExpanded(expandedFor(key));
	}
	if (g.enabled && !sections_.empty()) {
		const OutputSection *any = sections_.begin()->second;
		tableLead_->setFixedWidth(std::max(0, any->leadWidth() - any->headerSpacing()));
		tableName_->setFixedWidth(g.nameWidth);
		for (int c = 0; c < kTableColumns; ++c) {
			tableCells_[(size_t)c]->setFixedWidth(g.widths[(size_t)c]);
			tableCells_[(size_t)c]->setVisible(c < g.columns);
		}
	}
	tableHeader_->setVisible(g.enabled);
}

bool StatsDock::expandedFor(const QString &key) const
{
	// Stacked: expanded unless collapsed by the user (1.0.x behaviour).
	// Table: one line per output unless expanded by the user.
	return table_.enabled ? expandedTable_.count(key) > 0 : collapsed_.count(key) == 0;
}

// ---------------------------------------------------------------------------
void StatsDock::onSnapshot(const msd::DockSnapshot &snapshot)
{
	if (!isVisible())
		return;
	// Label changes only schedule repaints of the labels themselves; Qt
	// coalesces them into one paint pass, so no full-dock repaint is forced.
	applySummary(snapshot);
	syncSections(snapshot.rows);
	applySystem(snapshot.system);
	// Outputs appeared/disappeared or a column's widest value grew.
	updateResponsiveLayout();
}

void StatsDock::applySummary(const msd::DockSnapshot &snapshot)
{
	const msd::SummarySnapshot &s = snapshot.summary;

	// Overall health headline.
	QString headline;
	const int flagged = s.warnings + s.problems;
	if (s.overall == Health::Bad && s.problems > 0)
		headline = T("Health.Bad").arg(s.problems);
	else if (s.overall == Health::Bad || (s.overall == Health::Warning && flagged == 0))
		headline = T("Health.System");
	else if (s.overall == Health::Warning)
		headline = T("Health.Warning").arg(s.warnings);
	else if (s.running > 0)
		headline = T("Health.Good");
	else
		headline = T("Health.Idle");
	healthIndicator_->setHealth(s.overall);
	setValue(healthText_, headline, healthClass(s.overall));

	// Outputs line: only displayed outputs exist for the dock (1.1.0), so
	// there is no "(N total)" of configured destinations any more.
	// Compact form for narrow docks: the same counts with short words.
	QStringList parts;
	QStringList compactParts;
	auto add = [&](int n, const char *full, const char *shortKey) {
		if (n <= 0)
			return;
		parts << T(full).arg(n);
		compactParts << T(shortKey).arg(n);
	};
	add(s.live, "Outputs.Live", "Outputs.Live.Short");
	add(s.connecting, "Outputs.Connecting", "Outputs.Connecting.Short");
	add(s.reconnecting, "Outputs.Reconnecting", "Outputs.Reconnecting.Short");
	add(s.failed, "Outputs.Failed", "Outputs.Failed.Short");
	if (parts.isEmpty()) {
		parts << T("Outputs.None");
		compactParts << T("Outputs.None");
	}
	const QString states = parts.join(QStringLiteral(" · "));
	const Health outputsHealth = (s.reconnecting + s.failed) > 0 ? Health::Bad
				     : s.connecting > 0                 ? Health::Warning
									: Health::Inactive;
	setValue(outputsValue_, states, outputsHealth == Health::Inactive ? QString() : healthClass(outputsHealth),
		 QString(), compactParts.join(QStringLiteral(" · ")));

	// Last failure of an output that is no longer listed.
	if (snapshot.hasLastFailure) {
		const msd::RowSnapshot &f = snapshot.lastFailure;
		const QString state = f.state == msd::OutputState::Disconnected ? T("State.Disconnected") : T("State.Error");
		const uint64_t ago = snapshot.timestampNs > f.endNs ? snapshot.timestampNs - f.endNs : 0;
		const QString title = outputTitle(f);
		const QString text = T("Summary.LastFailure.Value")
					     .arg(title, state, stopCodeText(f.stopCode), str(msd::fmt::duration(ago)));
		QString extra;
		if (!f.lastError.empty())
			extra = T("Stat.LastError") + QStringLiteral(": ") + str(f.lastError);
		setValue(lastFailureValue_, text, QStringLiteral("text-warning"), extra,
			 title + QStringLiteral(" · ") + state);
	}
	summaryGrid_->setRowVisible(lastFailureValue_, snapshot.hasLastFailure);

	// Σ bitrate.
	if (s.sumKbps.has()) {
		QString extra;
		if (s.unmeasured > 0)
			extra = T("Summary.TotalBitrate.Unmeasured").arg(s.unmeasured);
		setValue(bitrateValue_, QStringLiteral("Σ ") + str(msd::fmt::kbps(s.sumKbps.value())), QString(), extra);
	} else {
		setNA(bitrateValue_, s.sumKbps.reason());
	}

	// Σ network drops.
	if (s.droppedSum.has() && s.framesSum.has()) {
		QString text = s.dropPct.has()
				       ? str(msd::fmt::frames(s.droppedSum.value(), s.framesSum.value(), s.dropPct.value()))
				       : str(msd::fmt::count(s.droppedSum.value()) + " / " + msd::fmt::count(s.framesSum.value()));
		const QString compact = s.dropPct.has() ? str(msd::fmt::framesCompact(s.droppedSum.value(), s.dropPct.value()))
							: str(msd::fmt::count(s.droppedSum.value()));
		QString extra;
		if (s.dropUnsupported > 0)
			extra = T("Summary.Dropped.Unsupported").arg(s.dropUnsupported);
		setValue(droppedValue_, text, healthClass(msd::percentHealth(s.dropPct)), extra, compact);
	} else {
		setNA(droppedValue_, s.droppedSum.reason());
	}
}

void StatsDock::syncSections(const std::vector<msd::RowSnapshot> &rows)
{
	std::vector<QString> order;
	order.reserve(rows.size());
	for (const msd::RowSnapshot &row : rows) {
		const QString key = str(row.key);
		order.push_back(key);
		auto it = sections_.find(key);
		if (it == sections_.end()) {
			auto *section = new OutputSection(key, sectionsBlock_);
			section->setTableGeometry(table_);
			section->setExpanded(expandedFor(key));
			connect(section, &OutputSection::expandedChanged, this, [this](const QString &k, bool expanded) {
				if (table_.enabled) {
					if (expanded)
						expandedTable_.insert(k);
					else
						expandedTable_.erase(k);
				} else {
					if (expanded)
						collapsed_.erase(k);
					else
						collapsed_.insert(k);
				}
				scheduleSave();
			});
			it = sections_.emplace(key, section).first;
		}
		it->second->apply(row);
	}

	// Remove sections whose row is no longer displayed.
	for (auto it = sections_.begin(); it != sections_.end();) {
		if (std::find(order.begin(), order.end(), it->first) == order.end()) {
			sectionsLayout_->removeWidget(it->second);
			it->second->deleteLater();
			it = sections_.erase(it);
		} else {
			++it;
		}
	}

	// Re-lay out only when the order changed (the table header stays first).
	if (order != order_) {
		for (const QString &key : order)
			sectionsLayout_->removeWidget(sections_[key]);
		for (const QString &key : order) {
			sectionsLayout_->addWidget(sections_[key]);
			sections_[key]->show();
		}
		order_ = std::move(order);
	}
}

void StatsDock::applySystem(const msd::SystemSnapshot &sys)
{
	if (sys.cpuPercent.has())
		setValue(cpu_, str(msd::fmt::percent(sys.cpuPercent.value())));
	else
		setNA(cpu_, sys.cpuPercent.reason());

	if (sys.memoryBytes.has())
		setValue(memory_, str(msd::fmt::bytes(sys.memoryBytes.value())));
	else
		setNA(memory_, sys.memoryBytes.reason());

	if (sys.diskFreeBytes.has())
		setValue(disk_, str(msd::fmt::bytes(sys.diskFreeBytes.value())));
	else
		setNA(disk_, sys.diskFreeBytes.reason());

	if (sys.fps.has()) {
		const QString current = str(msd::fmt::fps(sys.fps.value()));
		QString text = current;
		if (sys.targetFps > 0.0)
			text += QStringLiteral(" / ") + str(msd::fmt::fps(sys.targetFps));
		setValue(fps_, text, sys.fpsHealth == Health::Good ? QString() : healthClass(sys.fpsHealth), QString(),
			 current);
	} else {
		setNA(fps_, sys.fps.reason());
	}

	if (sys.renderMs.has())
		setValue(renderTime_, str(msd::fmt::milliseconds(sys.renderMs.value())),
			 sys.renderHealth == Health::Good ? QString() : healthClass(sys.renderHealth));
	else
		setNA(renderTime_, sys.renderMs.reason());

	const QString since = sys.lagWindow == msd::LagWindow::SinceUserReset    ? T("Tooltip.LagSinceUserReset")
			      : sys.lagWindow == msd::LagWindow::SinceVideoReset ? T("Tooltip.LagSinceVideoReset")
										 : T("Tooltip.LagSinceLoaded");
	if (sys.renderLagged.has() && sys.renderTotal.has() && sys.renderLagPct.has())
		setValue(renderLag_,
			 str(msd::fmt::frames(sys.renderLagged.value(), sys.renderTotal.value(), sys.renderLagPct.value())),
			 sys.renderLagHealth == Health::Good ? QString() : healthClass(sys.renderLagHealth), since,
			 str(msd::fmt::framesCompact(sys.renderLagged.value(), sys.renderLagPct.value())));
	else
		setNA(renderLag_, sys.renderLagPct.reason());

	if (sys.encodeSkipped.has() && sys.encodeTotal.has() && sys.encodeLagPct.has())
		setValue(encodeLag_,
			 str(msd::fmt::frames(sys.encodeSkipped.value(), sys.encodeTotal.value(), sys.encodeLagPct.value())),
			 sys.encodeLagHealth == Health::Good ? QString() : healthClass(sys.encodeLagHealth), since,
			 str(msd::fmt::framesCompact(sys.encodeSkipped.value(), sys.encodeLagPct.value())));
	else
		setNA(encodeLag_, sys.encodeLagPct.reason());
}

// ---------------------------------------------------------------------------
void StatsDock::setPerfExpanded(bool expanded)
{
	perfExpanded_ = expanded;
	perfArrow_->setChecked(expanded);
	perfBody_->setVisible(expanded);
	updateResponsiveLayout();
}

void StatsDock::setAllExpanded(bool expanded)
{
	for (auto &[key, section] : sections_) {
		section->setExpanded(expanded);
		if (table_.enabled) {
			if (expanded)
				expandedTable_.insert(key);
			else
				expandedTable_.erase(key);
		} else {
			if (expanded)
				collapsed_.erase(key);
			else
				collapsed_.insert(key);
		}
	}
	setPerfExpanded(expanded);
	scheduleSave();
}

void StatsDock::scheduleSave()
{
	dirty_ = true;
	saveTimer_.start();
}

void StatsDock::loadState()
{
	char *path = obs_module_config_path(kStateFile);
	if (!path)
		return;
	OBSDataAutoRelease state = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!state)
		return;
	perfExpanded_ = !obs_data_get_bool(state, "perf_collapsed");
	auto readKeys = [&](const char *name, std::set<QString> &out) {
		OBSDataArrayAutoRelease arr = obs_data_get_array(state, name);
		const size_t n = arr ? obs_data_array_count(arr) : 0;
		for (size_t i = 0; i < n; ++i) {
			OBSDataAutoRelease item = obs_data_array_item(arr, i);
			const char *key = obs_data_get_string(item, "key");
			if (key && *key)
				out.insert(QString::fromUtf8(key));
		}
	};
	readKeys("collapsed", collapsed_);
	readKeys("expanded_table", expandedTable_); // 1.1.0+; absent in older files
}

void StatsDock::saveState()
{
	dirty_ = false;
	char *dir = obs_module_config_path("");
	if (!dir)
		return;
	os_mkdirs(dir);
	bfree(dir);

	OBSDataAutoRelease state = obs_data_create();
	obs_data_set_bool(state, "perf_collapsed", !perfExpanded_);
	auto writeKeys = [&](const char *name, const std::set<QString> &keys) {
		OBSDataArrayAutoRelease arr = obs_data_array_create();
		for (const QString &key : keys) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "key", key.toUtf8().constData());
			obs_data_array_push_back(arr, item);
		}
		obs_data_set_array(state, name, arr);
	};
	writeKeys("collapsed", collapsed_);
	writeKeys("expanded_table", expandedTable_);

	char *path = obs_module_config_path(kStateFile);
	if (path) {
		obs_data_save_json_safe(state, path, "tmp", "bak");
		bfree(path);
	}
}

} // namespace msdui
