// SPDX-License-Identifier: GPL-2.0-or-later
//
// One collapsible section per streaming destination.
//
// Stacked (tall/narrow docks, 1.0.x layout):
//   header: [▸] ■ Destination name  Provider ....... 6012 kb/s
//   body:   label / value grid with per-output statistics
//
// Table (wide docks, 1.1.0): the header is one row of an output table whose
// columns line up across sections (widths set by StatsDock):
//   header: [▸] ■ Destination name | Status | Bitrate | Dropped | Data | Duration
//   body:   the remaining details (congestion, video, encoders, ...), flowing
//           into columns
#pragma once

#include "disclosure-arrow.hpp"
#include "elided-label.hpp"
#include "stat-grid.hpp"
#include "status-indicator.hpp"

#include <core/snapshot.hpp>

#include <QWidget>

#include <array>

class QHBoxLayout;

namespace msdui {

enum class TableColumn { Status = 0, Bitrate, Dropped, Data, Duration };
constexpr int kTableColumns = 5;
// Columns that must fit for the table layout; the rest are added when they fit.
constexpr int kRequiredTableColumns = 4;

// Display name of an output (destination, host or output name) and the
// description of a libobs stop code; shared with the summary's last-failure line.
QString outputTitle(const msd::RowSnapshot &row);
QString stopCodeText(int code);

struct TableGeometry {
	bool enabled = false;
	int columns = 0;   // cells shown, in TableColumn order
	int nameWidth = 0; // fixed width of the name column
	std::array<int, kTableColumns> widths{};
	bool operator==(const TableGeometry &o) const
	{
		return enabled == o.enabled && columns == o.columns && nameWidth == o.nameWidth && widths == o.widths;
	}
	bool operator!=(const TableGeometry &o) const { return !(*this == o); }
	bool shows(TableColumn c) const { return enabled && (int)c < columns; }
};

class OutputSection : public QWidget {
	Q_OBJECT

public:
	explicit OutputSection(const QString &key, QWidget *parent = nullptr);

	const QString &key() const { return key_; }
	void apply(const msd::RowSnapshot &row);
	void setExpanded(bool expanded);
	bool isExpanded() const { return expanded_; }

	void setTableGeometry(const TableGeometry &geometry);
	const TableGeometry &tableGeometry() const { return table_; }

	// Measurements for the table layout (natural widths, current font).
	int leadWidth() const;      // arrow + status square, each followed by spacing
	int nameNaturalWidth() const; // title (+ provider word)
	int cellNaturalWidth(TableColumn column) const;
	int headerSpacing() const;

signals:
	void expandedChanged(const QString &key, bool expanded);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void updateHeaderFit();
	void updateRowVisibility();
	QString titleFor(const msd::RowSnapshot &row) const;
	QString providerFor(const msd::RowSnapshot &row) const;
	QString tooltipFor(const msd::RowSnapshot &row) const;
	QString stateText(const msd::RowSnapshot &row) const;
	ElidedLabel *cell(TableColumn c) const { return cells_[(size_t)c]; }

	QString key_;
	bool expanded_ = true;
	bool hasStats_ = true;
	bool hasServer_ = false;
	bool hasDelay_ = false;
	bool hasError_ = false;
	bool dedicatedPipeline_ = false;
	TableGeometry table_;
	mutable int headerSpacing_ = -1;

	QWidget *header_ = nullptr;
	QHBoxLayout *headerLayout_ = nullptr;
	DisclosureArrow *arrow_ = nullptr;
	StatusIndicator *indicator_ = nullptr;
	QWidget *nameBox_ = nullptr;
	ElidedLabel *title_ = nullptr;
	ElidedLabel *subtitle_ = nullptr;
	ElidedLabel *headline_ = nullptr;
	std::array<ElidedLabel *, kTableColumns> cells_{};
	QWidget *body_ = nullptr;
	StatGrid *grid_ = nullptr;

	ElidedLabel *status_ = nullptr;
	ElidedLabel *duration_ = nullptr;
	ElidedLabel *bitrate_ = nullptr;
	ElidedLabel *dropped_ = nullptr;
	ElidedLabel *congestion_ = nullptr;
	ElidedLabel *video_ = nullptr;
	ElidedLabel *encoders_ = nullptr;
	ElidedLabel *pipeline_ = nullptr;
	ElidedLabel *data_ = nullptr;
	ElidedLabel *connection_ = nullptr;
	ElidedLabel *server_ = nullptr;
	ElidedLabel *delay_ = nullptr;
	ElidedLabel *error_ = nullptr;
};

} // namespace msdui
