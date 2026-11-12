// SPDX-License-Identifier: GPL-2.0-or-later
//
// The "Multi-Stream Statistics" dock widget.
//   Summary          overall health, outputs, Σ bitrate, Σ network drops
//   Output sections  one collapsible section per displayed destination
//   OBS Performance  CPU, memory, disk, FPS, render time, rendering/encoding lag
//
// 1.1.0 responsive layout, decided from the scroll viewport's size on every
// resize (core/layout-math.hpp, with scroll-bar-width hysteresis):
//   tall (height >= width)  everything stacked vertically, as in 1.0.x;
//   wide (width > height)   like OBS's native Stats dock: Summary and OBS
//                           Performance side by side when both fit, and the
//                           outputs as a table (one line per output with
//                           Status / Bitrate / Dropped / Data sent [/ Duration]
//                           columns) when the required columns fit.
// Label/value grids flow into several columns wherever they have the room.
#pragma once

#include "disclosure-arrow.hpp"
#include "elided-label.hpp"
#include "output-section.hpp"
#include "stat-grid.hpp"
#include "status-indicator.hpp"

#include <core/layout-math.hpp>
#include <obs/stats-collector.hpp>

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <array>
#include <map>
#include <set>

class QFrame;
class QGridLayout;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace msdui {

class StatsDock : public QWidget {
	Q_OBJECT
	// Read-only probes for the isolated UI tests.
	Q_PROPERTY(int msdLayoutMode READ layoutModeId)          // 0 stacked, 1 table
	Q_PROPERTY(int msdTableColumns READ tableColumns)        // cells per output line (table)
	Q_PROPERTY(int msdOverviewSideBySide READ sideBySideId)  // summary | performance
	Q_PROPERTY(int msdWide READ wideId)

public:
	StatsDock(msd::StatsCollector *collector, QWidget *parent = nullptr);
	~StatsDock() override;

	void saveState();

	// OBS sizes a newly added plugin dock from this hint (it adds the dock to
	// the right dock area and floats it, OBSStudioAPI::obs_frontend_add_dock_by_id),
	// so a first-time user gets a ~360 px wide dock. Saved layouts win.
	static constexpr int kDefaultWidth = 360; // logical px; Qt scales for DPI
	QSize sizeHint() const override;

protected:
	bool event(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	void onSnapshot(const msd::DockSnapshot &snapshot);
	void applySummary(const msd::DockSnapshot &snapshot);
	void applySystem(const msd::SystemSnapshot &system);
	void syncSections(const std::vector<msd::RowSnapshot> &rows);
	void setPerfExpanded(bool expanded);
	void setAllExpanded(bool expanded);
	void loadState();
	void scheduleSave();

	void updateResponsiveLayout();
	void placeBlocks();
	TableGeometry computeTableGeometry(int width, int margin);
	void applyTableGeometry(const TableGeometry &geometry);
	bool expandedFor(const QString &key) const;
	int contentHMargins() const;
	int scrollBarExtent() const;

	int layoutModeId() const { return table_.enabled ? 1 : 0; }
	int tableColumns() const { return table_.enabled ? table_.columns : 0; }
	int sideBySideId() const { return sideBySide_ ? 1 : 0; }
	int wideId() const { return wide_ ? 1 : 0; }

	QPointer<msd::StatsCollector> collector_;

	QScrollArea *scroll_ = nullptr;
	QGridLayout *contentLayout_ = nullptr;
	QWidget *summaryBlock_ = nullptr;
	QWidget *sectionsBlock_ = nullptr;
	QWidget *perfBlock_ = nullptr;
	QFrame *sepTop_ = nullptr;
	QFrame *sepBottom_ = nullptr;

	QVBoxLayout *sectionsLayout_ = nullptr;
	StatusIndicator *healthIndicator_ = nullptr;
	ElidedLabel *healthText_ = nullptr;
	StatGrid *summaryGrid_ = nullptr;
	ElidedLabel *outputsValue_ = nullptr;
	ElidedLabel *bitrateValue_ = nullptr;
	ElidedLabel *droppedValue_ = nullptr;
	ElidedLabel *lastFailureValue_ = nullptr;

	// Output table column headers (table layout only).
	QWidget *tableHeader_ = nullptr;
	QWidget *tableLead_ = nullptr;
	ElidedLabel *tableName_ = nullptr;
	std::array<ElidedLabel *, kTableColumns> tableCells_{};

	DisclosureArrow *perfArrow_ = nullptr;
	QWidget *perfBody_ = nullptr;
	StatGrid *perfGrid_ = nullptr;
	bool perfExpanded_ = true;
	ElidedLabel *cpu_ = nullptr;
	ElidedLabel *memory_ = nullptr;
	ElidedLabel *disk_ = nullptr;
	ElidedLabel *fps_ = nullptr;
	ElidedLabel *renderTime_ = nullptr;
	ElidedLabel *renderLag_ = nullptr;
	ElidedLabel *encodeLag_ = nullptr;

	QPushButton *resetButton_ = nullptr;

	std::map<QString, OutputSection *> sections_;
	std::vector<QString> order_;
	std::set<QString> collapsed_;     // stacked layout: sections the user collapsed
	std::set<QString> expandedTable_; // table layout: sections the user expanded
	QTimer saveTimer_;
	bool dirty_ = false;

	// Responsive layout state.
	bool wide_ = false;
	bool sideBySide_ = false;
	bool inLayoutUpdate_ = false;
	msd::layout::TablePlan tablePlan_;
	TableGeometry table_;
	mutable int contentHMargins_ = -1;
	mutable int scrollBarExtent_ = -1;
};

} // namespace msdui
